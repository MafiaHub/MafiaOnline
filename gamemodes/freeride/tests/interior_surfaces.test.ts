// The rooms built from stock models (src/server/lib/house_rooms.ts,
// hospital_room.ts, bar_room.ts): every steel door stands inside its wall's
// thickness, filling the gap left for it; nothing reaches into a wall and
// whatever hangs on one keeps just clear of it; and no two faces anyone can
// see lie in one plane, where they would flicker (z-fighting).
//
// Each piece is placed as the client places it: scaled about its origin,
// turned by glm::quat(euler) (client MapObjectService cancels LS3D's
// conjugate), moved to its position. Its box is the model's LOD0 box, from
// the rooms' BOXES tables and the decorations' (MODEL_BOXES adds the rest).
// SURFACES says which faces of a box are real faces of the model: the walls,
// the doors, the flat planes and the rugs, whose boxes are their meshes.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import * as bar from '../src/server/lib/bar_room';
import * as decor from '../src/server/lib/house_decor';
import * as hospital from '../src/server/lib/hospital_room';
import * as homes from '../src/server/lib/house_rooms';
import { HANG, type Piece } from '../src/server/lib/map_objects';
import type { Point } from '../src/server/lib/util';

type Box = readonly [number, number, number, number, number, number];
type Vec = [number, number, number];

// Two faces facing the same way closer than this flicker; anything hung on a
// wall keeps between HANG and HANG_MAX off it.
const MIN_GAP = 0.005;
const HANG_MAX = 0.02;
const EPS = 1e-6;

// Measured from the LOD0 meshes (stock a2.dta) like the rooms' own tables.
// prettier-ignore
const MODEL_BOXES: Readonly<Record<string, Box>> = {
    'plotcihly1': [-6, 6, -1.35, 1.35, 0, 0],
    'zavesik': [-0.074, 0.074, -2.145, 2.145, -0.98, 0.98],
    'xstul07': [-0.63, 0.63, -0.84, 0.042, -0.63, 0.63],
    'xzidle 04': [-0.251, 0.251, -0.214, 0.442, -0.268, 0.295],
    'Sklenice': [-0.03, 0.03, 0, 0.064, -0.03, 0.03],
    'xskrin 01': [-0.765, 0.708, -1.215, 1.135, -0.312, 0.363],
    '2flask': [-0.082, 0.068, 0, 0.532, -0.075, 0.075],
};

const BOXES: Readonly<Record<string, Box>> = {
    ...Object.fromEntries(decor.ITEMS.map((item) => [item.model, item.box])),
    ...homes.BOXES,
    ...hospital.BOXES,
    ...MODEL_BOXES,
};

// The faces of a box that are faces of its model, as [axis, sign]: an xzed
// wall has no underside, a plane both sides, a rug only its top.
const ALL: readonly (readonly [number, number])[] = [
    [0, 1],
    [0, -1],
    [1, 1],
    [1, -1],
    [2, 1],
    [2, -1],
];
const PLANE: readonly (readonly [number, number])[] = [
    [2, 1],
    [2, -1],
];
const RUG: readonly (readonly [number, number])[] = [[1, 1]];
const SURFACES: Readonly<Record<string, readonly (readonly [number, number])[]>> = {
    xzed: ALL.filter(([axis, sign]) => !(axis === 1 && sign < 0)),
    plotdrev7: ALL,
    plotdrev: PLANE,
    plotcihly1: PLANE,
    plotcihly3: PLANE,
    plotcihly4: PLANE,
    '9kober1': RUG,
    '9kober3': RUG,
    '9kober4': RUG,
    xkoberec: RUG,
};
// The stock houses a home's room is, and the faces of the walls round the
// room in each, from their LOD0 meshes: piica's south wall is 0.35 m thick
// (its outer face, at z -4.0, is where the floor ends); myhouse's east wall
// is the curtains over its window bays, its south wall set back further
// round the door and a window.
const STOCK_ROOMS: Readonly<Record<string, { x0: number; x1: number; z0: number; z1: number }>> = {
    piica: { x0: -0.9, x1: 6.04, z0: -3.65, z1: 3.85 },
    myhouse: { x0: 0.2, x1: 6.47, z0: -3.55, z1: 4.14 },
};
// What hangs on a wall.
const HUNG = new Set([
    'xhodiny 2',
    'xobraz01',
    'xobraz02',
    'xobraz03',
    'xobraz05',
    'xobraz09',
    'xobraz10',
    'xobraz13',
    '9tabul',
    '9lekarna',
    '9obr3',
    '9obr5',
    '9obr8',
    '9zrcadlo',
    'okno3',
]);

/** An axis-aligned box in the room's frame. */
interface Aabb {
    min: Vec;
    max: Vec;
}

interface Placed {
    name: string;
    model: string;
    box: Aabb;
    /** The model's real faces (SURFACES), for a surface. */
    faces: Face[];
}

interface Face {
    piece: number;
    axis: number;
    sign: number;
    at: number;
    rect: Aabb;
}

interface Structure {
    name: string;
    pieces: Piece[];
    /** The rooms people are in; a face is seen only from inside one. */
    rooms: Aabb[];
    /** A stock room's walls, which nothing may reach past. */
    walls?: Aabb;
}

const modelOf = (piece: Piece): string => piece.model.replace(/\.i3d$/i, '');

// glm::quat(eulerAngles): x first, then y, then z.
function quaternion({ x, y, z }: Point): [number, number, number, number] {
    const [cx, cy, cz] = [x, y, z].map((a) => Math.cos(a / 2));
    const [sx, sy, sz] = [x, y, z].map((a) => Math.sin(a / 2));

    return [
        cx * cy * cz + sx * sy * sz,
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
    ];
}

function rotate([w, qx, qy, qz]: [number, number, number, number], [x, y, z]: Vec): Vec {
    const tx = 2 * (qy * z - qz * y);
    const ty = 2 * (qz * x - qx * z);
    const tz = 2 * (qx * y - qy * x);

    return [
        x + w * tx + (qy * tz - qz * ty),
        y + w * ty + (qz * tx - qx * tz),
        z + w * tz + (qx * ty - qy * tx),
    ];
}

function bounds(points: readonly Vec[]): Aabb {
    return {
        min: [0, 1, 2].map((k) => Math.min(...points.map((p) => p[k]))) as Vec,
        max: [0, 1, 2].map((k) => Math.max(...points.map((p) => p[k]))) as Vec,
    };
}

function place(piece: Piece, index: number): Placed {
    const model = modelOf(piece);
    const box = BOXES[model];

    assert.ok(box, `${model} has no measured box`);

    const q = quaternion(piece.rotation);
    const scale = piece.scale ?? { x: 1, y: 1, z: 1 };
    const world = ([x, y, z]: Vec): Vec => {
        const [rx, ry, rz] = rotate(q, [x * scale.x, y * scale.y, z * scale.z]);

        return [piece.position.x + rx, piece.position.y + ry, piece.position.z + rz];
    };
    const lo: Vec = [box[0], box[2], box[4]];
    const hi: Vec = [box[1], box[3], box[5]];
    const corners = (fixed?: [number, number]): Vec[] => {
        const list: Vec[] = [];

        for (const x of [lo[0], hi[0]]) {
            for (const y of [lo[1], hi[1]]) {
                for (const z of [lo[2], hi[2]]) {
                    const point: Vec = [x, y, z];

                    if (fixed) {
                        point[fixed[0]] = fixed[1];
                    }

                    list.push(world(point));
                }
            }
        }

        return list;
    };
    const faces = (SURFACES[model] ?? []).map(([axis, sign]) => {
        const normal: Vec = [0, 0, 0];

        normal[axis] = sign;

        const turned = rotate(q, normal);
        const worldAxis = [0, 1, 2].reduce((a, b) =>
            Math.abs(turned[b]) > Math.abs(turned[a]) ? b : a,
        );

        assert.ok(Math.abs(turned[worldAxis]) > 0.999, `${model} is not square to the room`);

        const rect = bounds(corners([axis, sign > 0 ? hi[axis] : lo[axis]]));

        return {
            piece: index,
            axis: worldAxis,
            sign: Math.sign(turned[worldAxis]),
            at: rect.min[worldAxis],
            rect,
        };
    });
    const at = piece.position;

    return {
        name: `${model} at ${at.x.toFixed(3)}, ${at.y.toFixed(3)}, ${at.z.toFixed(3)}`,
        model,
        box: bounds(corners()),
        faces,
    };
}

function inside(box: Aabb, point: Vec): boolean {
    return [0, 1, 2].every((k) => point[k] >= box.min[k] - EPS && point[k] <= box.max[k] + EPS);
}

// How much two ranges overlap (negative: how far apart they are).
function overlap(a0: number, a1: number, b0: number, b1: number): number {
    return Math.min(a1, b1) - Math.max(a0, b0);
}

// Whether a face can be seen: some point just in front of it is in a room.
function seen(face: Face, rooms: readonly Aabb[]): boolean {
    const [u, v] = [0, 1, 2].filter((k) => k !== face.axis);

    for (let i = 0; i < 5; i++) {
        for (let j = 0; j < 5; j++) {
            const point: Vec = [0, 0, 0];

            point[face.axis] = face.at + face.sign * 0.01;
            point[u] = face.rect.min[u] + ((face.rect.max[u] - face.rect.min[u]) * (i + 0.5)) / 5;
            point[v] = face.rect.min[v] + ((face.rect.max[v] - face.rect.min[v]) * (j + 0.5)) / 5;

            if (rooms.some((room) => inside(room, point))) {
                return true;
            }
        }
    }

    return false;
}

function roomBox(x0: number, x1: number, y0: number, y1: number, z0: number, z1: number): Aabb {
    return { min: [x0, y0, z0], max: [x1, y1, z1] };
}

const STRUCTURES: Structure[] = [
    {
        name: 'the hospital',
        pieces: hospital.pieces(),
        rooms: Object.values(hospital.ROOMS).map((room) =>
            roomBox(room.x0, room.x1, 0, room.height, room.z0, room.z1),
        ),
    },
    {
        name: 'the back-room bar',
        pieces: bar.pieces(),
        rooms: [roomBox(-bar.ROOM.x, bar.ROOM.x, 0, bar.ROOM.height, -bar.ROOM.z, bar.ROOM.z)],
    },
    ...(Object.keys(homes.ROOMS) as homes.RoomKey[]).map((key) => {
        const { walls, bounds: inner } = homes.ROOMS[key];
        const box = roomBox(walls.x0, walls.x1, inner.floor, inner.ceiling, walls.z0, walls.z1);

        return {
            name: `the home's ${key}`,
            pieces: homes.ROOMS[key].pieces(),
            rooms: [box],
            walls: box,
        };
    }),
];

const placed = new Map(STRUCTURES.map((s) => [s, s.pieces.map((piece, i) => place(piece, i))]));

// The upright layers a wall is built of: wall boxes and the facings on them.
function layers(list: readonly Placed[]): { placed: Placed; across: number }[] {
    return list.flatMap((item) => {
        if (!SURFACES[item.model] || item.model === 'plotdrev7') {
            return [];
        }

        const size = [0, 1, 2].map((k) => item.box.max[k] - item.box.min[k]);
        const across = size[0] < size[2] ? 0 : 2;

        // Floors, ceilings, rugs and the planes under lintels lie flat.
        return size[1] > 0.3 && size[across] < 0.3 ? [{ placed: item, across }] : [];
    });
}

test("a stock room's walls are where its model's walls are", () => {
    for (const key of Object.keys(homes.ROOMS) as homes.RoomKey[]) {
        const { pieces, walls, bounds: inner } = homes.ROOMS[key];
        const stock = pieces()
            .map((piece) => STOCK_ROOMS[modelOf(piece)])
            .find(Boolean);

        if (!stock) {
            continue;
        }

        assert.deepEqual(walls, stock, `the ${key}'s walls are not its model's`);
        assert.ok(
            inner.x0 > walls.x0 &&
                inner.x1 < walls.x1 &&
                inner.z0 > walls.z0 &&
                inner.z1 < walls.z1,
            `the ${key}'s decorations may move into its walls`,
        );
    }
});

test('every piece has a measured box and stands square to its room', () => {
    for (const [structure, list] of placed) {
        assert.equal(list.length, structure.pieces.length);
    }
});

test('no two faces anyone can see lie in one plane', () => {
    for (const [structure, list] of placed) {
        const faces = list.flatMap((item) => item.faces).filter((f) => seen(f, structure.rooms));

        for (let i = 0; i < faces.length; i++) {
            for (let j = i + 1; j < faces.length; j++) {
                const a = faces[i];
                const b = faces[j];

                if (
                    a.piece === b.piece ||
                    a.axis !== b.axis ||
                    a.sign !== b.sign ||
                    Math.abs(a.at - b.at) >= MIN_GAP - EPS
                ) {
                    continue;
                }

                const [u, v] = [0, 1, 2].filter((k) => k !== a.axis);
                const area =
                    Math.max(
                        0,
                        overlap(a.rect.min[u], a.rect.max[u], b.rect.min[u], b.rect.max[u]),
                    ) *
                    Math.max(
                        0,
                        overlap(a.rect.min[v], a.rect.max[v], b.rect.min[v], b.rect.max[v]),
                    );

                assert.ok(
                    area < 1e-6,
                    `${structure.name}: ${list[a.piece].name} and ${list[b.piece].name} share a plane`,
                );
            }
        }
    }
});

test('every door stands in its wall and fills the gap left for it', () => {
    let doors = 0;

    for (const [structure, list] of placed) {
        const walls = layers(list);

        for (const door of list.filter((item) => item.model === 'plotdrev7')) {
            doors++;

            const { min, max } = door.box;
            const across = max[0] - min[0] < max[2] - min[2] ? 0 : 2;
            const along = 2 - across;
            const middle: Vec = [
                (min[0] + max[0]) / 2,
                (min[1] + max[1]) / 2,
                (min[2] + max[2]) / 2,
            ];
            // The room is on the side of the door a step away from it.
            const step = (side: number): Vec => {
                const point: Vec = [...middle];

                point[across] += side * 0.5;

                return point;
            };
            const side = structure.rooms.some((room) => inside(room, step(1))) ? 1 : -1;
            const front = side > 0 ? max[across] : min[across];
            const wall = walls
                .map(({ placed: layer }) => layer)
                .filter(
                    (layer) =>
                        overlap(
                            layer.box.min[along],
                            layer.box.max[along],
                            min[along] - 0.05,
                            max[along] + 0.05,
                        ) > 0 &&
                        overlap(layer.box.min[1], layer.box.max[1], min[1], max[1]) > 0 &&
                        overlap(
                            layer.box.min[across],
                            layer.box.max[across],
                            min[across] - 0.4,
                            max[across] + 0.4,
                        ) >= 0 &&
                        layer.box.max[across] - layer.box.min[across] < 0.3,
                );

            assert.ok(wall.length, `${structure.name}: ${door.name} stands in no wall`);

            const back = Math.min(...wall.map((layer) => layer.box.min[across]));
            const face = Math.max(...wall.map((layer) => layer.box.max[across]));

            assert.ok(
                min[across] >= back - EPS && max[across] <= face + EPS,
                `${structure.name}: ${door.name} stands out of its wall (${back.toFixed(3)}..${face.toFixed(3)})`,
            );

            // What stands between the door and the room must leave its gap.
            const before = wall.filter((layer) =>
                side > 0 ? layer.box.max[across] > front : layer.box.min[across] < front,
            );

            assert.ok(before.length, `${structure.name}: ${door.name} stands in front of its wall`);

            for (const layer of before) {
                assert.ok(
                    overlap(
                        layer.box.min[along],
                        layer.box.max[along],
                        min[along] + 0.02,
                        max[along] - 0.02,
                    ) <= 0 ||
                        overlap(layer.box.min[1], layer.box.max[1], min[1] + 0.02, max[1] - 0.02) <=
                            0,
                    `${structure.name}: ${layer.name} hides ${door.name}`,
                );
            }

            // ...and lap over its sides and top, no gap showing round it,
            // unless the other leaf of a pair closes on that side.
            const leaves = list.filter((item) => item.model === 'plotdrev7' && item !== door);
            const covers = (boxes: readonly Placed[], a: number, y: number): boolean =>
                boxes.some(
                    ({ box }) =>
                        a >= box.min[along] - EPS &&
                        a <= box.max[along] + EPS &&
                        y >= box.min[1] - EPS &&
                        y <= box.max[1] + EPS,
                );
            // A point 5 mm inside each edge, and one 1 mm beyond it.
            const edges: [number, number, number, number][] = [
                [min[along] + 0.005, middle[1], min[along] - 0.001, middle[1]],
                [max[along] - 0.005, middle[1], max[along] + 0.001, middle[1]],
                [middle[along], max[1] - 0.005, middle[along], max[1] + 0.001],
            ];

            for (const [a, y, beyondA, beyondY] of edges) {
                assert.ok(
                    covers(before, a, y) || covers(leaves, beyondA, beyondY),
                    `${structure.name}: a gap shows beside ${door.name} at ${a.toFixed(3)}, ${y.toFixed(3)}`,
                );
            }
        }
    }

    assert.ok(doors >= 5, `only ${doors} doors`);
});

// How far a piece reaches into the wall layers (or past a stock room's
// walls); 0 when it keeps out of them.
function intrusion(
    item: Placed,
    walls: { placed: Placed; across: number }[],
    stock?: Aabb,
): number {
    let worst = 0;

    for (const { placed: layer, across } of walls) {
        const along = 2 - across;
        const depth = Math.min(
            overlap(
                item.box.min[across],
                item.box.max[across],
                layer.box.min[across],
                layer.box.max[across],
            ),
            // A plane is reached into when it passes through the piece.
            layer.box.max[across] - layer.box.min[across] < EPS
                ? Math.min(
                      layer.box.min[across] - item.box.min[across],
                      item.box.max[across] - layer.box.min[across],
                  )
                : Infinity,
        );

        if (
            overlap(
                item.box.min[along],
                item.box.max[along],
                layer.box.min[along],
                layer.box.max[along],
            ) > EPS &&
            overlap(item.box.min[1], item.box.max[1], layer.box.min[1], layer.box.max[1]) > EPS
        ) {
            worst = Math.max(worst, depth);
        }
    }

    if (stock) {
        for (const k of [0, 2]) {
            worst = Math.max(worst, stock.min[k] - item.box.min[k], item.box.max[k] - stock.max[k]);
        }
    }

    return worst;
}

test('nothing reaches into a wall', () => {
    for (const [structure, list] of placed) {
        const walls = layers(list);

        for (const item of list) {
            if (SURFACES[item.model] || STOCK_ROOMS[item.model]) {
                continue;
            }

            const depth = intrusion(item, walls, structure.walls);

            assert.ok(
                depth <= EPS,
                `${structure.name}: ${item.name} reaches ${(depth * 100).toFixed(1)} cm into a wall`,
            );
        }
    }
});

test('what hangs on a wall hangs just clear of it', () => {
    let hung = 0;

    for (const [structure, list] of placed) {
        const walls = layers(list);

        for (const item of list.filter(({ model }) => HUNG.has(model))) {
            hung++;

            const { min, max } = item.box;
            const across = max[0] - min[0] < max[2] - min[2] ? 0 : 2;
            const along = 2 - across;
            const gaps = walls
                .filter(
                    ({ placed: layer, across: other }) =>
                        other === across &&
                        overlap(
                            layer.box.min[along],
                            layer.box.max[along],
                            min[along],
                            max[along],
                        ) > 0 &&
                        overlap(layer.box.min[1], layer.box.max[1], min[1], max[1]) > 0,
                )
                .map(({ placed: layer }) =>
                    Math.max(
                        layer.box.min[across] - max[across],
                        min[across] - layer.box.max[across],
                    ),
                );

            if (structure.walls) {
                gaps.push(
                    min[across] - structure.walls.min[across],
                    structure.walls.max[across] - max[across],
                );
            }

            const gap = Math.min(...gaps.filter((value) => value > -EPS));

            assert.ok(
                gap >= HANG - EPS && gap <= HANG_MAX + EPS,
                `${structure.name}: ${item.name} hangs ${(gap * 100).toFixed(1)} cm off its wall`,
            );
        }
    }

    assert.ok(hung >= 20, `only ${hung} pieces hang on walls`);
});
