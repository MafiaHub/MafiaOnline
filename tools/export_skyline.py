#!/usr/bin/env python3
"""Export a mission's skyline: the highest static geometry over each map cell.

A scripted camera flying over the city must never pass through a building.
This collects every piece of static geometry the game places in a mission,
transforms it to world space and writes, for each square cell of the map,
the highest world y (Mafia is y-up) any triangle touching that cell reaches.
It reads game data and writes only the module (and an optional PNG preview);
it never writes game assets.

Geometry sources, composed the way the retail game does it:

* scene.4ds of the mission. The 4DS reader follows reM LS3DF
  I3D_loader.cpp LoadImageA (0x10060cb0) and LoadMeshImage (0x10061120), the
  material readers I3D_material::LoadImageA (0x1002bd50) and
  I3D_anim_material::LoadImageA (0x1002c000), I3D_frame::LoadImageA
  (0x1001d1c0) and every frame type's own LoadImageA: I3D_object,
  I3D_singlemesh (0x10056860), I3D_singlemorph, I3D_morph, morphBase::
  LoadImageB (0x10034bd0), I3D_billboard, I3D_lensflare, I3D_projector,
  I3D_mirror, I3D_sector (+ I3D_portal), I3D_dummy, I3D_target, I3D_joint,
  I3D_occluder, I3D_light and I3D_emitor. The scene is loaded under the
  scene's "Primary sector" (I3D_scene::Open -> LoadHierarchy).
* scene2.bin, after reM Game C_mission::OpenBin (0x00541030) and
  LoadMissionObject: objects (0x4010) either create a frame (0x4011 type
  before the 0x0010 name) or edit an existing one by name (no type); 0x0020
  position, 0x0022 rotation, 0x002d scale are LOCAL to the 0x4020 parent,
  which is looked up by exact name among the frames indexed so far (scene.4ds
  frames, the three scene sectors, earlier objects and the "object.frame"
  names of earlier loaded models). A missing parent falls back to the 0x002c
  position. Model frames (type 9) load models/<0x2012 name>.4ds.
* cache.bin, after C_cache_base_block_ex::Open (0x004013f0) and
  ApplySegmentFrames: each placement (chunk 2000) puts a model at a WORLD
  position/rotation/scale and overrides the scale of every visual frame of
  that model with the placement's visual scale; "0rek23" loads as "0rek17".

Frame transforms follow I3D_frame::UpdateWMatrixProc and S_matrix::SetRot3:
row vectors, local = rotation(quaternion w, x, y, z) with its rows scaled by
the frame scale and translated by the position, world = local * parent world.

Mission files come from a1.dta, or for a mod from its loose mission directory
(falling back to the package zip). Models resolve in the order the game
searches: the mod package zip, then a8.dta, then a2.dta (g_apszDtaArchives).
Mod 4DS files re-saved by mod tools may end one byte short or long of the
stock layout; the game's reader ignores that and so does this one. Children of the
"Backdrop sector" are skipped: the engine draws that sector with the camera's
rotation only (I3D_driver_render), so its sky and distant scenery have no
world position. Everything else, hidden frames and collision meshes included,
counts, which keeps the result conservative.
"""

import argparse
import base64
import importlib.util
import json
import math
import pathlib
import struct
import zipfile
import zlib

CELL = 32
FOURDS_VERSION = 29
MAT_HAS_ENVTEXTURE = 0x00080000
MAT_ANIM = 0x04000000
MAT_ALPHABLEND = 0x40000000
MAT_LOAD_NO_ALPHA_MASK = 0x80000000 | 0x20000000 | 0x01000000
FRAME_VISUAL, FRAME_LIGHT, FRAME_SECTOR, FRAME_DUMMY, FRAME_TARGET = 1, 2, 5, 6, 7
FRAME_MODEL, FRAME_JOINT, FRAME_OCCLUDER, FRAME_EMITOR = 9, 10, 12, 17
VISUAL_NAMES = ["object", "lit object", "singlemesh", "singlemorph", "billboard", "morph", "lens flare", "projector", "mirror"]
QUAT_IDENTITY_THRESHOLD = 0.99999999
CHUNK_NAME, CHUNK_POSITION, CHUNK_ROTATION, CHUNK_FALLBACK, CHUNK_SCALE = 0x0010, 0x0020, 0x0022, 0x002C, 0x002D
CHUNK_ROOT, CHUNK_OBJECTS, CHUNK_OBJECT = 0x4C53, 0x4000, 0x4010
CHUNK_FRAME_TYPE, CHUNK_MODEL_FILE, CHUNK_PARENT, CHUNK_FRAME_OFF = 0x4011, 0x2012, 0x4020, 0x4033
CACHE_ROOT, CACHE_SEGMENT, CACHE_FRAME = 500, 1000, 2000
SECTOR_NAMES = ("Primary sector", "Backdrop sector", "Omni sector")
MAPS = {
    "freeride": {"mission": "FREERIDE", "mod": False},
    "freeride_extended": {"mission": "freeride_extended", "mod": True},
    "freekrajina": {"mission": "FREEKRAJINA", "mod": False},
}


class FormatError(Exception):
    pass


# --------------------------------------------------------------------------
# Asset access


def load_dta_reader():
    path = pathlib.Path(__file__).with_name("inspect_stock_dta.py")
    spec = importlib.util.spec_from_file_location("inspect_stock_dta", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Archive:
    def __init__(self, dta, path):
        first, second = dta.KEYS[path.stem.lower()]
        self.dta, self.path = dta, path
        self.key = struct.pack("<II", first ^ 0x39475694, second ^ 0x34985762)
        with path.open("rb") as handle:
            self.entries = {entry[0].lower(): entry for entry in dta.archive_files(handle, self.key)}

    def read(self, name):
        entry = self.entries.get(name.lower())
        if entry is None:
            return None
        with self.path.open("rb") as handle:
            return bytes(self.dta.asset_bytes(handle, self.key, entry))


class Assets:
    """Game files in the order the game finds them: mod first, then archives."""

    def __init__(self, game, mod_zip=None):
        dta = load_dta_reader()
        self.archives = [Archive(dta, game / name) for name in ("A8.dta", "A2.dta", "A1.dta")]
        self.zip = zipfile.ZipFile(mod_zip) if mod_zip else None
        self.zip_names = {name.lower().replace("/", "\\"): name for name in self.zip.namelist()} if self.zip else {}
        self.cache = {}

    def find(self, name):
        """(bytes, origin) of a game path such as models\\a.4ds, or (None, None)."""
        key = name.lower().replace("/", "\\")
        if key in self.cache:
            return self.cache[key]
        result = (None, None)
        if self.zip and key in self.zip_names:
            result = (self.zip.read(self.zip_names[key]), "mod")
        if result[0] is None:
            for archive in self.archives:
                data = archive.read(key)
                if data is not None:
                    result = (data, archive.path.stem.upper())
                    break
        self.cache[key] = result
        return result


# --------------------------------------------------------------------------
# Binary reading


class Reader:
    def __init__(self, data, offset=0):
        self.data, self.offset = data, offset

    def take(self, size):
        if self.offset + size > len(self.data):
            raise FormatError(f"read of {size} bytes at {self.offset} runs past the end ({len(self.data)})")
        start = self.offset
        self.offset += size
        return start

    def skip(self, size):
        self.take(size)

    def u8(self):
        return self.data[self.take(1)]

    def u16(self):
        return struct.unpack_from("<H", self.data, self.take(2))[0]

    def u32(self):
        return struct.unpack_from("<I", self.data, self.take(4))[0]

    def floats(self, count):
        return struct.unpack_from(f"<{count}f", self.data, self.take(4 * count))

    def text(self, size):
        return self.data[self.take(size):self.offset].decode("latin1")


# --------------------------------------------------------------------------
# 4DS


class Frame:
    __slots__ = ("type", "visual", "parent", "position", "scale", "rotation", "name", "geometry", "lods", "billboard")

    def __init__(self):
        self.geometry = None  # (flat x,y,z vertex list, flat triangle index list) in frame space
        self.lods = 0
        self.billboard = None  # (rotation axis 0..2, mode) of I3D_billboard


def read_material(reader):
    flags = reader.u32()
    if flags & MAT_HAS_ENVTEXTURE:
        reader.skip(44)
        reader.skip(reader.u8())
    else:
        reader.skip(40)
    texture = reader.u8()
    if texture:
        reader.skip(texture)
        if flags & MAT_ALPHABLEND and not flags & MAT_LOAD_NO_ALPHA_MASK:
            reader.skip(reader.u8())
    if flags & MAT_ANIM:
        reader.skip(1 + 1 + 16)


def read_mesh(reader, frames, frame):
    """LoadMeshImage: LOD0 positions and triangles; later LODs are skipped."""
    shared = reader.u16()
    if shared:
        if shared > len(frames):
            raise FormatError(f"mesh instance id {shared} out of range")
        source = frames[shared - 1]
        frame.geometry, frame.lods = source.geometry, source.lods
        return
    lods = reader.u8()
    frame.lods = lods
    for lod in range(lods):
        reader.skip(4)  # LOD distance
        count = reader.u16()
        vertices = None
        if count == 0xFFFF:
            if lod == 0:
                raise FormatError("LOD0 reuses vertices")
            reader.skip(8 * frame_vertex_count(frame))
        elif count:
            start = reader.take(32 * count)
            if lod == 0:
                raw = struct.unpack_from(f"<{8 * count}f", reader.data, start)
                vertices = [value for index in range(count) for value in raw[8 * index:8 * index + 3]]
        triangles = []
        for _ in range(reader.u8()):
            faces = reader.u16()
            if faces:
                start = reader.take(6 * faces)
                if lod == 0:
                    triangles.extend(struct.unpack_from(f"<{3 * faces}H", reader.data, start))
            reader.skip(2)  # material id
        if lod == 0:
            frame.geometry = (vertices or [], triangles)
            if triangles and max(triangles) * 3 >= len(frame.geometry[0]):
                raise FormatError("triangle index past the vertex count")


def frame_vertex_count(frame):
    return len(frame.geometry[0]) // 3 if frame.geometry else 0


def read_morph(reader):
    levels = reader.u8()
    if not levels:
        return
    regions = reader.u8()
    definitions = reader.u8()
    for _ in range(definitions * regions):
        count = reader.u16()
        if count:
            reader.skip(24 * levels * count)
            if reader.u8():
                reader.skip(2 * count)
    reader.skip(40)


def read_skin(reader, frame):
    if not frame.lods:  # no mesh, so I3D_singlemesh::LoadImageA reads no skin
        return
    for _ in range(frame.lods):
        joints = reader.u8()
        if not joints:
            continue
        reader.skip(4 + 24)
        for _ in range(joints):
            start = reader.take(100)
            weighted, = struct.unpack_from("<i", reader.data, start + 0x44)
            reader.skip(4 * weighted)


def hull(reader, vertex_count, face_count):
    vertices = list(reader.floats(3 * vertex_count))
    start = reader.take(6 * face_count)
    triangles = list(struct.unpack_from(f"<{3 * face_count}H", reader.data, start))
    return (vertices, triangles)


def parse_4ds(data):
    """Every frame of a 4DS image, in file order, with its LOD0 geometry."""
    reader = Reader(data)
    if reader.text(4) != "4DS\0":
        raise FormatError("not a 4DS file")
    version = reader.u16()
    if version != FOURDS_VERSION:
        raise FormatError(f"4DS version {version}")
    reader.skip(8)
    for _ in range(reader.u16()):
        read_material(reader)
    frames = []
    for index in range(reader.u16()):
        frame = Frame()
        frame.type = reader.u8()
        frame.visual = None
        if frame.type == FRAME_VISUAL:
            frame.visual = reader.u8()
            if frame.visual >= len(VISUAL_NAMES):
                raise FormatError(f"visual type {frame.visual}")
            reader.skip(2)
        frame.parent = reader.u16()
        if frame.parent > index:
            raise FormatError(f"parent id {frame.parent} of frame {index + 1}")
        frame.position = reader.floats(3)
        frame.scale = reader.floats(3)
        frame.rotation = reader.floats(4)
        reader.skip(1)
        frame.name = reader.text(reader.u8())
        reader.skip(reader.u8())
        read_frame_payload(reader, frames, frame)
        frames.append(frame)
    # The animated flag byte ends a stock file. Files re-saved by mod tools may
    # lack it or carry one byte more; the game's dtaRead shrugs off either.
    if len(data) - reader.offset not in (0, 1, 2):
        raise FormatError(f"{len(data) - reader.offset} unread bytes")
    return frames


def read_frame_payload(reader, frames, frame):
    kind = frame.type
    if kind == FRAME_VISUAL:
        visual = frame.visual
        if visual in (0, 1, 2, 3, 4, 5):
            read_mesh(reader, frames, frame)
            if visual in (2, 3):
                read_skin(reader, frame)
            if visual in (3, 5):
                read_morph(reader)
            if visual == 4:
                frame.billboard = struct.unpack_from("<IB", reader.data, reader.take(5))
        elif visual == 6:
            count = reader.u8()
            reader.skip(6 * count)
        elif visual == 7:
            reader.skip(4)
        elif visual == 8:
            reader.skip(40)
            start = reader.take(88)
            vertices, faces = struct.unpack_from("<ii", reader.data, start + 80)
            frame.geometry = hull(reader, vertices, faces)
    elif kind == FRAME_SECTOR:
        _, _, vertices, faces = struct.unpack_from("<IIii", reader.data, reader.take(16))
        frame.geometry = hull(reader, vertices, faces)
        reader.skip(24)
        for _ in range(reader.u8()):
            reader.skip(28 + 12 * reader.u8())
    elif kind == FRAME_DUMMY:
        reader.skip(24)
    elif kind == FRAME_TARGET:
        reader.skip(2)
        reader.skip(2 * reader.u8())
    elif kind == FRAME_JOINT:
        reader.skip(68)
    elif kind == FRAME_OCCLUDER:
        vertices, faces = struct.unpack_from("<ii", reader.data, reader.take(8))
        frame.geometry = hull(reader, vertices, faces)
    elif kind == FRAME_LIGHT:
        reader.skip(0x28)
    elif kind == FRAME_EMITOR:
        read_mesh(reader, frames, frame)
    elif kind in (3, 8, 9, 11):  # camera, user, model, volume: the base frame only
        pass
    else:
        raise FormatError(f"frame type {kind} has no known payload")


# --------------------------------------------------------------------------
# Transforms


def local_matrix(position, rotation, scale):
    w, x, y, z = rotation
    if abs(w) < QUAT_IDENTITY_THRESHOLD:
        x2, y2, z2 = x + x, y + y, z + z
        xx, xy, xz, yy, yz, zz = x2 * x, y2 * x, z2 * x, y2 * y, z2 * y, z2 * z
        wx, wy, wz = x2 * w, y2 * w, z2 * w
        rows = [1 - (zz + yy), xy - wz, wy + xz, wz + xy, 1 - (zz + xx), yz - wx, xz - wy, wx + yz, 1 - (yy + xx)]
    else:
        rows = [1, 0, 0, 0, 1, 0, 0, 0, 1]
    sx, sy, sz = scale
    return [rows[0] * sx, rows[1] * sx, rows[2] * sx, rows[3] * sy, rows[4] * sy, rows[5] * sy,
            rows[6] * sz, rows[7] * sz, rows[8] * sz, position[0], position[1], position[2]]


def multiply(a, b):
    """Row-vector product a * b of two 4x3 matrices."""
    out = []
    for row in range(4):
        u, v, w = a[3 * row], a[3 * row + 1], a[3 * row + 2]
        for column in range(3):
            out.append(u * b[column] + v * b[3 + column] + w * b[6 + column])
    for column in range(3):
        out[9 + column] += b[9 + column]
    return out


IDENTITY = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]


class Node:
    """A frame of the assembled mission scene."""

    __slots__ = ("name", "parent", "position", "rotation", "scale", "geometry", "label", "kind", "_world")

    def __init__(self, name, parent, position, rotation, scale, geometry=None, label=None, kind=None):
        self.name, self.parent = name, parent
        self.position, self.rotation, self.scale = position, rotation, scale
        self.geometry, self.label, self.kind = geometry, label, kind
        self._world = None

    def world(self):
        if self._world is None:
            local = local_matrix(self.position, self.rotation, self.scale)
            self._world = multiply(local, self.parent.world()) if self.parent else local
        return self._world

    def root(self):
        node = self
        while node.parent:
            node = node.parent
        return node


# --------------------------------------------------------------------------
# Scene assembly


class Scene:
    def __init__(self, assets, map_id):
        self.assets, self.map_id = assets, map_id
        self.nodes = []
        self.index = {}
        self.models = {}
        self.stats = {"model_files": {}, "model_failures": {}, "scene2_missing_models": {}, "scene2_objects": 0,
                      "scene2_models": 0, "scene2_edits": 0, "scene2_hidden": 0, "scene2_ignored_duplicates": 0,
                      "scene2_parent_fallbacks": 0, "scene2_parent_missing": 0, "cache_segments": 0,
                      "cache_missing_segments": [], "cache_placements": 0, "cache_missing_models": {}}
        self.sectors = {name: Node(name, None, (0, 0, 0), (1, 0, 0, 0), (1, 1, 1), kind="sector") for name in SECTOR_NAMES}
        self.placements = []  # (model, position) of cache.bin, for validation

    def model(self, path):
        key = path.lower()
        if key not in self.models:
            data, origin = self.assets.find(path)
            if data is None:
                self.models[key] = None
            else:
                try:
                    self.models[key] = parse_4ds(data)
                    self.stats["model_files"][key] = origin
                except (FormatError, struct.error, IndexError) as error:
                    self.models[key] = None
                    self.stats["model_failures"][key] = str(error)
        return self.models[key]

    def instantiate(self, frames, parent, label, prefix=None, visual_scale=None, index=True):
        created = []
        for frame in frames:
            owner = created[frame.parent - 1] if frame.parent else parent
            scale = visual_scale if visual_scale is not None and frame.type == FRAME_VISUAL else frame.scale
            name = f"{prefix}.{frame.name}" if prefix else frame.name
            node = Node(name, owner, frame.position, frame.rotation, scale, frame.geometry, label, frame_kind(frame))
            created.append(node)
            self.nodes.append(node)
            if index:
                self.index[name] = node
        return created

    def load_scene(self, data, label):
        frames = parse_4ds(data)
        self.scene_frames = len(frames)
        self.instantiate(frames, self.sectors["Primary sector"], label)
        for name, node in self.sectors.items():
            self.index[name] = node

    def load_scene2(self, data):
        reader = Reader(data)
        kind, end = chunk(reader)
        if kind != CHUNK_ROOT:
            raise FormatError(f"scene2.bin root chunk {kind:#x}")
        while reader.offset < end:
            kind, chunk_end = chunk(reader)
            if kind == CHUNK_OBJECTS:
                while reader.offset < chunk_end:
                    kind, object_end = chunk(reader)
                    if kind == CHUNK_OBJECT:
                        self.scene2_object(reader, object_end)
                    reader.offset = object_end
            reader.offset = chunk_end

    def scene2_object(self, reader, end):
        """LoadMissionObject: create or edit one frame."""
        frame_type, node, parent = 0, None, None
        position = rotation = scale = fallback = None
        created = hidden = has_parent = False
        model_name = None
        while reader.offset < end:
            kind, chunk_end = chunk(reader)
            if kind == CHUNK_FRAME_TYPE:
                frame_type, = struct.unpack_from("<i", reader.data, reader.offset)
            elif kind == CHUNK_NAME:
                name = cstring(reader.data, reader.offset, chunk_end)
                existing = self.index.get(name)
                if frame_type == 0:
                    node = existing
                    if node is not None:
                        self.stats["scene2_edits"] += 1
                elif existing is None:
                    node = Node(name, None, (0, 0, 0), (1, 0, 0, 0), (1, 1, 1), label=f"scene2.bin {name}", kind=f"scene2 type {frame_type}")
                    self.nodes.append(node)
                    self.index[name] = node
                    created = True
                    self.stats["scene2_objects"] += 1
                else:
                    self.stats["scene2_ignored_duplicates"] += 1
            elif kind == CHUNK_POSITION:
                position = struct.unpack_from("<3f", reader.data, reader.offset)
            elif kind == CHUNK_FALLBACK:
                fallback = struct.unpack_from("<3f", reader.data, reader.offset)
            elif kind == CHUNK_ROTATION:
                rotation = struct.unpack_from("<4f", reader.data, reader.offset)
            elif kind == CHUNK_SCALE:
                scale = struct.unpack_from("<3f", reader.data, reader.offset)
            elif kind == CHUNK_PARENT and node is not None:
                child, child_end = chunk(reader)
                if child == CHUNK_NAME:
                    parent_name = cstring(reader.data, reader.offset, child_end)
                    parent = self.index.get(parent_name)
                    if parent is not None:
                        has_parent = True
                    elif fallback is not None:
                        position = fallback
                        self.stats["scene2_parent_fallbacks"] += 1
                    else:
                        self.stats["scene2_parent_missing"] += 1
            elif kind == CHUNK_MODEL_FILE and node is not None and (frame_type == FRAME_MODEL or node.kind == "scene2 type 9"):
                model_name = cstring(reader.data, reader.offset, chunk_end)
                if model_name:
                    self.scene2_model(node, model_name)
            elif kind == CHUNK_FRAME_OFF:
                hidden = True
            reader.offset = chunk_end
        if node is None:
            return
        if has_parent:
            node.parent = parent
        if position is not None:
            node.position = position
        if rotation is not None:
            node.rotation = rotation
        if scale is not None:
            node.scale = scale
        if hidden:
            self.stats["scene2_hidden"] += 1

    def scene2_model(self, node, model_name):
        stem = model_name.rsplit(".", 1)[0] if "." in model_name else model_name
        path = f"models\\{stem}.4ds"
        frames = self.model(path)
        if frames is None:
            self.stats["scene2_missing_models"][path.lower()] = self.stats["scene2_missing_models"].get(path.lower(), 0) + 1
            return
        self.stats["scene2_models"] += 1
        label = f"scene2.bin {node.name} ({path.lower()})"
        node.label = label
        self.instantiate(frames, node, label, prefix=node.name)

    def load_cache(self, data):
        reader = Reader(data)
        kind, end = chunk(reader)
        if kind != CACHE_ROOT:
            raise FormatError(f"cache.bin root chunk {kind}")
        if reader.u32() != 1:
            raise FormatError("cache.bin version")
        while reader.offset < end:
            kind, segment_end = chunk(reader)
            if kind == CACHE_SEGMENT:
                name = reader.text(reader.u32())
                self.stats["cache_segments"] += 1
                if name not in self.index:
                    self.stats["cache_missing_segments"].append(name)
                reader.skip(8 + 4 + 4 * 16)
                while reader.offset < segment_end:
                    kind, frame_end = chunk(reader)
                    if kind == CACHE_FRAME:
                        self.cache_placement(reader)
                    reader.offset = frame_end
            reader.offset = segment_end

    def cache_placement(self, reader):
        name = reader.text(reader.u32())
        if name.lower() == "0rek23.i3d":
            name = "0rek17.i3d"
        position = reader.floats(3)
        rotation = reader.floats(4)
        scale = reader.floats(3)
        reader.skip(4)
        visual_scale = reader.floats(3)
        stem = name.rsplit(".", 1)[0]
        path = f"models\\{stem}.4ds".lower()
        self.stats["cache_placements"] += 1
        self.placements.append((path, position))
        frames = self.model(path)
        if frames is None:
            self.stats["cache_missing_models"][path] = self.stats["cache_missing_models"].get(path, 0) + 1
            return
        label = f"cache.bin {path}"
        root = Node(path, None, position, rotation, scale, label=label, kind="cache model")
        self.nodes.append(root)
        self.instantiate(frames, root, label, visual_scale=visual_scale, index=False)


def frame_kind(frame):
    if frame.type == FRAME_VISUAL:
        return VISUAL_NAMES[frame.visual]
    return {FRAME_SECTOR: "sector hull", FRAME_OCCLUDER: "occluder", FRAME_EMITOR: "emitor"}.get(frame.type, f"frame type {frame.type}")


def chunk(reader):
    kind = reader.u16()
    size = reader.u32()
    end = reader.offset + size - 6
    if size < 6 or end > len(reader.data):
        raise FormatError(f"chunk {kind:#x} of size {size} at {reader.offset - 6}")
    return kind, end


def cstring(data, start, end):
    raw = data[start:end]
    return raw.split(b"\0", 1)[0].decode("latin1")


# --------------------------------------------------------------------------
# Rasterising


class Grid:
    """Highest world y per cell; cells are keyed by their integer column/row."""

    def __init__(self, cell):
        self.cell = cell
        self.inverse = 1.0 / cell
        self.cells = {}
        self.instances = {}
        self.bounds = [math.inf, math.inf, -math.inf, -math.inf]
        self.triangles = 0

    def add(self, node, skipped):
        vertices, triangles = node.geometry
        if not triangles:
            return
        m = node.world()
        inverse = self.inverse
        xs, ys, cxs, czs = [], [], [], []
        for index in range(0, len(vertices), 3):
            x, y, z = vertices[index], vertices[index + 1], vertices[index + 2]
            wx = x * m[0] + y * m[3] + z * m[6] + m[9]
            wy = x * m[1] + y * m[4] + z * m[7] + m[10]
            wz = x * m[2] + y * m[5] + z * m[8] + m[11]
            xs.append((wx, wz))
            ys.append(wy)
            cxs.append(math.floor(wx * inverse))
            czs.append(math.floor(wz * inverse))
        used = set(triangles)
        top = max(used, key=ys.__getitem__)
        peak = ys[top]
        instance = instance_key(node)
        best = self.instances.get(instance)
        if best is None or peak > best[0]:
            self.instances[instance] = (peak, xs[top][0], xs[top][1], node.label, node.name, node.kind, skipped)
        if skipped:
            return
        bounds = self.bounds
        for index in used:
            x, z = xs[index]
            if x < bounds[0]: bounds[0] = x
            if z < bounds[1]: bounds[1] = z
            if x > bounds[2]: bounds[2] = x
            if z > bounds[3]: bounds[3] = z
        cells = self.cells
        self.triangles += len(triangles) // 3
        if node.kind == "billboard":
            # I3D_billboard::ProductBillMatrix turns the mesh about its frame
            # origin to face the camera, so any vertex may end up anywhere on
            # the sphere of its distance around that pivot.
            px, py, pz = m[9], m[10], m[11]
            radius = math.sqrt(max((xs[i][0] - px) ** 2 + (ys[i] - py) ** 2 + (xs[i][1] - pz) ** 2 for i in used))
            peak = py + radius
            if peak > best_peak(self.instances.get(instance)):
                self.instances[instance] = (peak, px, pz, node.label, node.name, node.kind, skipped)
            for column in range(math.floor((px - radius) * inverse), math.floor((px + radius) * inverse) + 1):
                for row in range(math.floor((pz - radius) * inverse), math.floor((pz + radius) * inverse) + 1):
                    if peak > cells.get((column, row), -math.inf):
                        cells[(column, row)] = peak
            for x, z in ((px - radius, pz - radius), (px + radius, pz + radius)):
                bounds[0], bounds[1] = min(bounds[0], x), min(bounds[1], z)
                bounds[2], bounds[3] = max(bounds[2], x), max(bounds[3], z)
            return
        lo_x, hi_x = min(cxs[i] for i in used), max(cxs[i] for i in used)
        lo_z, hi_z = min(czs[i] for i in used), max(czs[i] for i in used)
        if lo_x == hi_x and lo_z == hi_z:
            key = (lo_x, lo_z)
            if peak > cells.get(key, -math.inf):
                cells[key] = peak
            return
        for index in range(0, len(triangles), 3):
            a, b, c = triangles[index], triangles[index + 1], triangles[index + 2]
            y = ys[a]
            if ys[b] > y: y = ys[b]
            if ys[c] > y: y = ys[c]
            ax, bx, cx = cxs[a], cxs[b], cxs[c]
            az, bz, cz = czs[a], czs[b], czs[c]
            if ax == bx == cx and az == bz == cz:
                key = (ax, az)
                if y > cells.get(key, -math.inf):
                    cells[key] = y
                continue
            for column in range(min(ax, bx, cx), max(ax, bx, cx) + 1):
                for row in range(min(az, bz, cz), max(az, bz, cz) + 1):
                    key = (column, row)
                    if y > cells.get(key, -math.inf):
                        cells[key] = y

    def export(self):
        columns = [key[0] for key in self.cells]
        rows = [key[1] for key in self.cells]
        first_column, first_row = min(columns), min(rows)
        cols, nrows = max(columns) - first_column + 1, max(rows) - first_row + 1
        heights = [None] * (cols * nrows)
        for (column, row), value in self.cells.items():
            heights[(row - first_row) * cols + column - first_column] = math.ceil(value)
        return {"cell": self.cell, "originX": first_column * self.cell, "originZ": first_row * self.cell,
                "cols": cols, "rows": nrows, "heights": heights}


def best_peak(entry):
    return -math.inf if entry is None else entry[0]


def instance_key(node):
    """The placement a node belongs to: a cache.bin model, a scene2 model, or itself."""
    walker = node
    while walker.parent is not None:
        if walker.kind in ("cache model", "scene2 type 9"):
            return id(walker)
        walker = walker.parent
    return id(node)


def under_backdrop(node, backdrop):
    walker = node
    while walker is not None:
        if walker is backdrop:
            return True
        walker = walker.parent
    return False


# --------------------------------------------------------------------------
# Output

EMPTY = -32768


def encode(grid):
    """zlib-deflated row-major little-endian int16 heights, base64; EMPTY marks a cell with no geometry."""
    values = [EMPTY if value is None else value for value in grid["heights"]]
    return base64.b64encode(zlib.compress(struct.pack(f"<{len(values)}h", *values), 9)).decode("ascii")


def write_module(path, grid, sources, skipped):
    body = dict(grid)
    body["heights"] = encode(grid)
    lines = [
        "// Generated by tools/export_skyline.py: the highest static geometry over each map cell, so a",
        "// scripted camera can stay clear of every building.",
        f"// Sources: {sources}.",
        "// Geometry: every LOD0 mesh, sector hull, occluder and mirror of scene.4ds, of the scene2.bin",
        "// model objects and of the cache.bin model placements, each through its full frame hierarchy;",
        "// billboards count as the sphere they can sweep facing the camera about their pivot.",
        f"// Skipped: {skipped}.",
        "// Cell (col, row) covers x in [originX + col*cell, +cell) and z in [originZ + row*cell, +cell);",
        "// its value is the max world y (metres, y-up) of every triangle whose xz bounding box touches",
        "// it, rounded up to an integer. heights is base64 of a zlib stream holding cols*rows",
        f"// little-endian int16 values in row-major order (index = row*cols + col); {EMPTY} marks a cell",
        "// with no geometry at all. Decode once on the server:",
        "//   const raw = require(\"zlib\").inflateSync(Buffer.from(skyline.heights, \"base64\"));",
        f"//   const top = (col, row) => {{ const v = raw.readInt16LE(2 * (row * skyline.cols + col)); return v === {EMPTY} ? null : v; }};",
        f"module.exports = {json.dumps(body, separators=(',', ':'))};",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


# --------------------------------------------------------------------------
# PNG preview

FONT = {
    "0": "01110100011001110101110011000101110", "1": "00100011000010000100001000010001110",
    "2": "01110100010000100010001000100011111", "3": "11111000100010000010000011000101110",
    "4": "00010001100101010010111110001000010", "5": "11111100001111000001000011000101110",
    "6": "00110010001000011110100011000101110", "7": "11111000010001000100010000100001000",
    "8": "01110100011000101110100011000101110", "9": "01110100011000101111000010001001100",
    "-": "00000000000000011111000000000000000", "x": "00000000001000101010001000101010001",
    "z": "00000000001111100010001000100011111", "y": "00000100011000101111000010001001100",
    "m": "00000000001101010101101011000110001", " ": "0" * 35, ">": "01000001000001000001000100010001000",
    "^": "00100011101010100100001000010000100", ".": "00000000000000000000000000110001100",
    "r": "00000000001011011001100001000010000", "o": "00000000000111010001100011000101110",
    "a": "00000000000111000001011111000101111", "d": "00001000010110110011100011000101111",
    "s": "00000000000111110000011100000111110", "n": "00000000001011011001100011000110001",
    "e": "00000000000111010001111111000001110", "t": "01000010001110001000010000100100110",
    "c": "00000000000111010000100001000101110", "i": "00100000000110000100001000010001110",
    "g": "00000011111000110001011110000101110", "p": "00000111101000111110100001000010000",
    "l": "01100001000010000100001000010001110", "w": "00000000001000110001101011010101010",
    "u": "00000000001000110001100011001101101", "h": "10000100001011011001100011000110001",
    "k": "10000100001001010100110001010010010", "b": "10000100001011011001100011000111110",
    "f": "00110010010100011100010000100001000", "v": "00000000001000110001100010101000100",
    "j": "00010000000011000010000101001001100", "(": "00010001000100001000010000010000010",
    ")": "01000001000001000010000100010001000", ",": "00000000000000000000001100010001000",
    "<": "00010001000100010000010000010000010", "=": "00000000001111100000111110000000000",
    "_": "00000000000000000000000000000011111",
}

# Sampled from the "turbo" ramp: dark blue low, through green and yellow, to dark red high.
STOPS = [(0.0, (48, 18, 59)), (0.15, (70, 107, 227)), (0.3, (40, 188, 235)), (0.45, (50, 241, 151)),
         (0.6, (164, 252, 60)), (0.75, (238, 207, 58)), (0.88, (251, 128, 34)), (1.0, (122, 4, 3))]
NO_GEOMETRY = (225, 225, 225)
ROAD = (220, 0, 120)
WHITE, BLACK = (255, 255, 255), (0, 0, 0)


def colour(t):
    t = min(1.0, max(0.0, t))
    for (a, ca), (b, cb) in zip(STOPS, STOPS[1:]):
        if t <= b:
            f = (t - a) / (b - a)
            return tuple(int(ca[i] + (cb[i] - ca[i]) * f) for i in range(3))
    return STOPS[-1][1]


class Image:
    def __init__(self, width, height):
        self.width, self.height = width, height
        self.pixels = bytearray(bytes(WHITE) * (width * height))

    def rect(self, x0, y0, x1, y1, rgb):
        x0, x1 = max(0, int(x0)), min(self.width, int(x1))
        if x1 <= x0:
            return
        row = bytes(rgb) * (x1 - x0)
        for y in range(max(0, int(y0)), min(self.height, int(y1))):
            offset = 3 * (y * self.width + x0)
            self.pixels[offset:offset + len(row)] = row

    def line(self, x0, y0, x1, y1, rgb, width=1):
        steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        half = width // 2
        for step in range(steps + 1):
            t = step / steps
            x, y = round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t)
            self.rect(x - half, y - half, x - half + width, y - half + width, rgb)

    def text(self, x, y, string, rgb=BLACK, size=2):
        for character in string:
            glyph = FONT.get(character, FONT[" "])
            for index, bit in enumerate(glyph):
                if bit == "1":
                    column, row = index % 5, index // 5
                    self.rect(x + column * size, y + row * size, x + (column + 1) * size, y + (row + 1) * size, rgb)
            x += 6 * size

    def save(self, path):
        stride = 3 * self.width
        raw = b"".join(b"\0" + bytes(self.pixels[y * stride:(y + 1) * stride]) for y in range(self.height))

        def block(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 2, 0, 0, 0)
        path.write_bytes(b"\x89PNG\r\n\x1a\n" + block(b"IHDR", header) + block(b"IDAT", zlib.compress(raw, 9)) + block(b"IEND", b""))


def nice_step(span, target):
    raw = span / target
    magnitude = 10 ** math.floor(math.log10(raw))
    for factor in (1, 2, 2.5, 5, 10):
        if factor * magnitude >= raw:
            return factor * magnitude
    return 10 * magnitude


def render_png(path, grid, roads, title, low, high, view=None, width_target=1400):
    """Heatmap of the grid inside view = (x0, z0, x1, z1), +x right and +z up, with the roads on top."""
    cols, rows, cell = grid["cols"], grid["rows"], grid["cell"]
    origin_x, origin_z = grid["originX"], grid["originZ"]
    if view is None:
        view = (origin_x, origin_z, origin_x + cols * cell, origin_z + rows * cell)
    first_col = max(0, math.floor((view[0] - origin_x) / cell))
    first_row = max(0, math.floor((view[1] - origin_z) / cell))
    last_col = min(cols, math.ceil((view[2] - origin_x) / cell))
    last_row = min(rows, math.ceil((view[3] - origin_z) / cell))
    shown_cols, shown_rows = last_col - first_col, last_row - first_row
    scale = max(1, width_target // max(shown_cols, shown_rows * 3 // 4))
    left, top = 100, 56
    right, bottom = left + shown_cols * scale, top + shown_rows * scale
    image = Image(right + 170, bottom + 80)
    heights = grid["heights"]
    for row in range(first_row, last_row):
        y0 = bottom - (row - first_row + 1) * scale
        for column in range(first_col, last_col):
            value = heights[row * cols + column]
            rgb = NO_GEOMETRY if value is None else colour((value - low) / (high - low))
            x0 = left + (column - first_col) * scale
            image.rect(x0, y0, x0 + scale, y0 + scale, rgb)
    world_x0, world_z0 = origin_x + first_col * cell, origin_z + first_row * cell
    world_x1, world_z1 = origin_x + last_col * cell, origin_z + last_row * cell

    def to_pixel(x, z):
        return left + (x - world_x0) / cell * scale, bottom - (z - world_z0) / cell * scale

    major = nice_step(max(world_x1 - world_x0, world_z1 - world_z0), 12)
    minor = major / 4
    for step, rgb in ((minor, (150, 150, 150)), (major, (40, 40, 40))):
        tick = math.ceil(world_x0 / step) * step
        while tick <= world_x1:
            px = int(to_pixel(tick, 0)[0])
            image.rect(px, top, px + 1, bottom, rgb)
            tick += step
        tick = math.ceil(world_z0 / step) * step
        while tick <= world_z1:
            py = int(to_pixel(0, tick)[1])
            image.rect(left, py, right, py + 1, rgb)
            tick += step
    if roads:
        crossings = roads["crossings"]
        for start, end, _, points in roads["edges"]:
            polyline = [crossings[start]] + points + [crossings[end]]
            for a, b in zip(polyline, polyline[1:]):
                (xa, ya), (xb, yb) = to_pixel(a[0], a[2]), to_pixel(b[0], b[2])
                image.line(xa, ya, xb, yb, WHITE, 3)
                image.line(xa, ya, xb, yb, BLACK, 1)
        for x, _, z in crossings:
            px, py = to_pixel(x, z)
            image.rect(px - 3, py - 3, px + 4, py + 4, WHITE)
            image.rect(px - 2, py - 2, px + 3, py + 3, ROAD)
    # Clear the margins the roads may have spilled into, then frame and label the map.
    image.rect(0, 0, image.width, top, WHITE)
    image.rect(0, bottom, image.width, image.height, WHITE)
    image.rect(0, 0, left, image.height, WHITE)
    image.rect(right, 0, image.width, image.height, WHITE)
    for x0, y0, x1, y1 in ((left - 1, top - 1, right + 1, top), (left - 1, bottom, right + 1, bottom + 1),
                           (left - 1, top, left, bottom), (right, top, right + 1, bottom)):
        image.rect(x0, y0, x1, y1, BLACK)
    tick = math.ceil(world_x0 / major) * major
    while tick <= world_x1:
        px = int(to_pixel(tick, 0)[0])
        image.rect(px, bottom, px + 1, bottom + 6, BLACK)
        label = f"{tick:g}"
        image.text(px - len(label) * 6, bottom + 10, label)
        tick += major
    tick = math.ceil(world_z0 / major) * major
    while tick <= world_z1:
        py = int(to_pixel(0, tick)[1])
        image.rect(left - 6, py, left, py + 1, BLACK)
        label = f"{tick:g}"
        image.text(left - 10 - len(label) * 12, py - 7, label)
        tick += major
    image.text(left, 10, title)
    image.text(left, 32, f"dark lines every {major:g} m, light lines every {minor:g} m", rgb=(80, 80, 80))
    image.text(right - 170, bottom + 44, "x (m) >")
    image.text(8, 34, "z (m) ^")
    bar_left, bar_top, bar_height = right + 30, top + 30, min(560, bottom - top - 140)
    image.text(bar_left - 10, top, "max y m")
    for offset in range(bar_height):
        image.rect(bar_left, bar_top + offset, bar_left + 24, bar_top + offset + 1, colour(1 - offset / bar_height))
    legend_step = nice_step(high - low, 8)
    labels = [low] + [value for value in range(math.ceil(low / legend_step) * int(legend_step), high + 1, int(legend_step))
                      if value - low > legend_step / 3]
    for value in labels:
        py = bar_top + int((1 - (value - low) / (high - low)) * bar_height)
        image.rect(bar_left + 24, py, bar_left + 30, py + 1, BLACK)
        image.text(bar_left + 34, py - 7, f"<={value:g}" if value == low else f"{value:g}")
    image.rect(bar_left, bar_top + bar_height + 24, bar_left + 24, bar_top + bar_height + 48, NO_GEOMETRY)
    image.text(bar_left + 30, bar_top + bar_height + 30, "empty")
    if roads:
        image.rect(bar_left + 6, bar_top + bar_height + 64, bar_left + 18, bar_top + bar_height + 76, ROAD)
        image.text(bar_left + 30, bar_top + bar_height + 64, "crossing")
        image.rect(bar_left, bar_top + bar_height + 96, bar_left + 24, bar_top + bar_height + 99, BLACK)
        image.text(bar_left + 30, bar_top + bar_height + 90, "road")
    image.save(path)
    return image.width, image.height


def load_roads(path):
    text = path.read_text(encoding="utf-8")
    start = text.index("module.exports = ") + len("module.exports = ")
    return json.loads(text[start:text.rindex(";")])


def road_view(roads, margin=300):
    points = list(roads["crossings"]) + [point for edge in roads["edges"] for point in edge[3]]
    xs, zs = [point[0] for point in points], [point[2] for point in points]
    return (min(xs) - margin, min(zs) - margin, max(xs) + margin, max(zs) + margin)


# --------------------------------------------------------------------------


def build(map_id, game, mod_dir, mod_zip, cell):
    settings = MAPS[map_id]
    mission = settings["mission"]
    assets = Assets(game, mod_zip if settings["mod"] else None)
    sources = []

    def mission_file(name):
        if settings["mod"]:
            if mod_dir and (mod_dir / name).is_file():
                sources.append(f"{mod_dir.name}/{name} of the mod")
                return (mod_dir / name).read_bytes()
            data, _ = assets.find(f"missions\\{mission}\\{name}")
            if data is not None:
                sources.append(f"{mod_zip.name}:missions/{mission}/{name}")
            return data
        data = assets.archives[2].read(f"MISSIONS\\{mission}\\{name}")
        if data is not None:
            sources.append(f"stock A1.dta MISSIONS/{mission}/{name}")
        return data

    scene = Scene(assets, map_id)
    scene.load_scene(mission_file("scene.4ds"), "scene.4ds")
    scene2 = mission_file("scene2.bin")
    if scene2 is not None:
        scene.load_scene2(scene2)
    cache = mission_file("cache.bin")
    if cache is not None:
        scene.load_cache(cache)
    origins = {"mod": mod_zip.name if mod_zip else "the mod", "A8": "A8.dta", "A2": "A2.dta", "A1": "A1.dta"}
    used = sorted(set(scene.stats["model_files"].values()), key=["mod", "A8", "A2", "A1"].index)
    if used:
        sources.append("models/*.4ds from " + ", then ".join(origins[origin] for origin in used))
    grid = Grid(cell)
    backdrop = scene.sectors["Backdrop sector"]
    counts = {"meshes": 0, "backdrop_meshes": 0, "empty_meshes": 0, "kinds": {}}
    for node in scene.nodes:
        if not node.geometry:
            continue
        skipped = under_backdrop(node, backdrop)
        if not node.geometry[1]:
            counts["empty_meshes"] += 1
            continue
        counts["backdrop_meshes" if skipped else "meshes"] += 1
        if not skipped:
            counts["kinds"][node.kind] = counts["kinds"].get(node.kind, 0) + 1
        grid.add(node, skipped)
    return scene, grid, counts, sources


def skipped_summary(grid, counts):
    backdrop = sorted({label for _, _, _, label, _, _, skipped in grid.instances.values() if skipped})
    summary = f"{counts['backdrop_meshes']} meshes under the camera-relative Backdrop sector"
    if backdrop:
        summary += " (" + ", ".join(backdrop) + ")"
    return summary + "; dummy boxes, lights, sounds and other frames that carry no triangles"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("map", choices=sorted(MAPS), help="Mission to export")
    parser.add_argument("output", type=pathlib.Path, help="JavaScript module to write")
    parser.add_argument("--game", type=pathlib.Path, required=True, help="Stock Mafia directory with A1/A2/A8.dta")
    parser.add_argument("--mod-dir", type=pathlib.Path, help="Loose mod mission directory (scene.4ds, scene2.bin, cache.bin)")
    parser.add_argument("--mod-zip", type=pathlib.Path, help="Mod package zip with models/ and missions/<mission>/")
    parser.add_argument("--cell", type=int, default=CELL, help="Cell size in metres (default 32)")
    parser.add_argument("--roads", type=pathlib.Path, help="roads_<map>.js to check against and draw on the previews")
    parser.add_argument("--png", type=pathlib.Path, help="Draw the whole grid here")
    parser.add_argument("--png-roads", type=pathlib.Path, help="Draw the area around the road graph here (needs --roads)")
    parser.add_argument("--report", action="store_true", help="Print parse coverage and validation details")
    args = parser.parse_args()
    if MAPS[args.map]["mod"] and not (args.mod_dir or args.mod_zip):
        parser.error("this map needs --mod-dir and/or --mod-zip")
    if args.png_roads and not args.roads:
        parser.error("--png-roads needs --roads")
    scene, grid, counts, sources = build(args.map, args.game, args.mod_dir, args.mod_zip, args.cell)
    exported = grid.export()
    write_module(args.output, exported, "; ".join(sources), skipped_summary(grid, counts))
    print(f"{args.map}: {exported['cols']}x{exported['rows']} cells of {args.cell} m from ({exported['originX']}, {exported['originZ']}), "
          f"{grid.triangles} triangles in {counts['meshes']} meshes -> {args.output} ({args.output.stat().st_size} bytes)")
    roads = load_roads(args.roads) if args.roads else None
    if args.report:
        report(scene, grid, exported, counts, roads)
    values = [value for value in exported["heights"] if value is not None]
    low = max(math.floor(min(values) / 10) * 10, -20)
    high = math.ceil(max(values) / 10) * 10
    title = f"{args.map}: max y per {args.cell} m cell"
    for path, view in ((args.png, None), (args.png_roads, road_view(roads) if args.png_roads else None)):
        if path:
            size = render_png(path, exported, roads, title, low, high, view)
            print(f"preview {size[0]}x{size[1]}, colours {low}..{high} m -> {path}")


def cell_value(exported, x, z):
    column = math.floor((x - exported["originX"]) / exported["cell"])
    row = math.floor((z - exported["originZ"]) / exported["cell"])
    if 0 <= column < exported["cols"] and 0 <= row < exported["rows"]:
        return exported["heights"][row * exported["cols"] + column]
    return None


def report(scene, grid, exported, counts, roads):
    stats = scene.stats
    bounds = grid.bounds
    cell = exported["cell"]
    print(f"geometry extents x {bounds[0]:.1f}..{bounds[2]:.1f}, z {bounds[1]:.1f}..{bounds[3]:.1f}; grid x {exported['originX']}.."
          f"{exported['originX'] + exported['cols'] * cell}, z {exported['originZ']}..{exported['originZ'] + exported['rows'] * cell}")
    print(f"scene.4ds frames {scene.scene_frames}; scene2.bin frames created {stats['scene2_objects']} (models {stats['scene2_models']}), "
          f"edits of existing frames {stats['scene2_edits']}, hidden {stats['scene2_hidden']}, ignored duplicate names "
          f"{stats['scene2_ignored_duplicates']}, parent fallbacks {stats['scene2_parent_fallbacks']}, parents not found {stats['scene2_parent_missing']}")
    print(f"cache.bin segments {stats['cache_segments']} (not found {stats['cache_missing_segments']}), placements {stats['cache_placements']}")
    print(f"model files parsed {len(stats['model_files'])}, unparseable {len(stats['model_failures'])}: {stats['model_failures']}")
    print(f"scene2 models not found or unparseable: {stats['scene2_missing_models']}")
    print(f"cache models not found or unparseable: {stats['cache_missing_models']}")
    print(f"meshes rasterised {counts['meshes']} {counts['kinds']}; backdrop meshes skipped {counts['backdrop_meshes']}; "
          f"meshes without triangles {counts['empty_meshes']}")
    for model in ("models\\vezeni.4ds", "models\\9medic.4ds"):
        for path, position in scene.placements:
            if path == model:
                print(f"cache.bin places {path} at ({position[0]:.2f}, {position[1]:.2f}, {position[2]:.2f})")
    values = [value for value in exported["heights"] if value is not None]
    print(f"cells with geometry {len(values)} of {len(exported['heights'])}; max {max(values)}, min {min(values)}")
    if roads:
        above = []
        for index, (x, y, z) in enumerate(roads["crossings"]):
            value = cell_value(exported, x, z)
            if value is None or y > value:
                above.append((index, x, y, z, value))
        print(f"road crossings above their cell max: {len(above)} of {len(roads['crossings'])} {above[:10]}")
        points = [point for edge in roads["edges"] for point in edge[3]]
        high = [(x, y, z) for x, y, z in points if cell_value(exported, x, z) is None or y > cell_value(exported, x, z)]
        print(f"road polyline points above their cell max: {len(high)} of {len(points)} {high[:10]}")
    ranked = sorted(grid.instances.values(), key=lambda item: -item[0])
    print("tallest placements (y, x, z, source, frame, kind):")
    for peak, x, z, label, name, kind, skipped in [item for item in ranked if not item[6]][:25]:
        print(f"  {peak:8.2f}  ({x:9.2f}, {z:9.2f})  {label}  {name}  [{kind}]")
    print("skipped backdrop placements:")
    for peak, x, z, label, name, kind, skipped in [item for item in ranked if item[6]]:
        print(f"  {peak:8.2f}  ({x:9.2f}, {z:9.2f})  {label}  {name}  [{kind}]")


if __name__ == "__main__":
    main()
