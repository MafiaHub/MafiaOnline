#include "static_collision.h"

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace Mafia1Online::Features::MapObject {
    namespace {
        namespace Native = SDK::Collision;

        // LS3DF MRG_ZERO, the epsilon LoadCollision rejects faces with.
        constexpr float kZero = 1e-8f;
        // OptimizeFaceTags' COLLISION_FACE_TAG_EPSILON.
        constexpr double kTagEpsilon = 0.0001;

        Native::Vector ToNative(const glm::vec3 &value) {
            return {value.x, value.y, value.z};
        }

        glm::vec3 ToGlm(const Native::Vector &value) {
            return {value.x, value.y, value.z};
        }

        // A glm column-vector matrix and an LS3D row-vector matrix share one
        // memory layout: column c of the one is row c of the other.
        void StoreMatrix(float (&out)[16], const glm::mat4 &matrix) {
            std::memcpy(out, glm::value_ptr(matrix), sizeof(out));
        }

        Native::Header MakeHeader(uint8_t type, uint8_t material, uint8_t flags, uint8_t sortInfo = 0) {
            return {type, sortInfo, flags, material};
        }

        // tCollTreeLevel::OptimizeFaceTags (0x5c2980): the dominant normal
        // axis picks how LineInGridCell solves the barycentric weights.
        uint8_t FaceTag(const glm::vec3 &normal, const glm::vec3 &a, const glm::vec3 &b) {
            const float x = std::fabs(normal.x);
            const float y = std::fabs(normal.y);
            const float z = std::fabs(normal.z);
            char axis;
            if (x > y) {
                axis = z > x ? 'z' : 'x';
            }
            else {
                axis = z > y ? 'z' : 'y';
            }
            switch (axis) {
            case 'x': return std::fabs(static_cast<double>(b.y - a.y)) < kTagEpsilon ? Native::kFaceXConstantY : Native::kFaceX;
            case 'y':
                if (std::fabs(static_cast<double>(b.x - a.x)) < kTagEpsilon) {
                    return Native::kFaceYConstantX;
                }
                return std::fabs(static_cast<double>(b.z - a.z)) < kTagEpsilon ? Native::kFaceYConstantZ : Native::kFaceY;
            default: return std::fabs(static_cast<double>(b.x - a.x)) < kTagEpsilon ? Native::kFaceZConstantX : Native::kFaceZ;
            }
        }

        // LoadCollision's degenerate face score: 2 per collapsed edge, 1 per
        // flat axis, disabled above 1; also no usable normal.
        bool Degenerate(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
            int score = 0;
            const auto distance = [](const glm::vec3 &p, const glm::vec3 &q) { return glm::dot(p - q, p - q); };
            score += distance(c, b) < kZero ? 2 : 0;
            score += distance(b, a) < kZero ? 2 : 0;
            score += distance(a, c) < kZero ? 2 : 0;
            const glm::vec3 minimum = glm::min(a, glm::min(b, c));
            const glm::vec3 maximum = glm::max(a, glm::max(b, c));
            score += std::fabs(maximum.x - minimum.x) < kZero ? 1 : 0;
            score += std::fabs(maximum.y - minimum.y) < kZero ? 1 : 0;
            score += std::fabs(maximum.z - minimum.z) < kZero ? 1 : 0;
            const glm::vec3 cross = glm::cross(b - a, c - a);
            return score > 1 || !(glm::dot(cross, cross) > 1e-12f);
        }

        // One stored face: vertices sorted by x (the cell loops skip a face
        // whose vertex 2 lies before the query and stop at one whose vertex 0
        // lies past it), the normal pointing into the solid, and sortInfo
        // telling SphereInFace which way the sorted vertices wind.
        void WriteFace(Native::Face &face, Native::Vector *vertices, std::array<glm::vec3, 3> points, const glm::vec3 &normal, uint8_t material, uint8_t flags) {
            std::stable_sort(points.begin(), points.end(), [](const glm::vec3 &left, const glm::vec3 &right) { return left.x < right.x; });
            for (size_t i = 0; i < 3; ++i) {
                vertices[i]         = ToNative(points[i]);
                face.vertices[i]    = &vertices[i];
            }
            const float winding = glm::dot(normal, glm::cross(points[1] - points[0], points[2] - points[0]));
            face.header         = MakeHeader(FaceTag(normal, points[0], points[1]), material, flags, winding > 0.0f ? Native::kFaceSortPositiveWinding : 0);
            face.normal         = ToNative(normal);
            face.d              = -glm::dot(normal, points[0]);
        }

        glm::vec3 Corner(const glm::mat4 &transform, const glm::vec3 &half, int index) {
            const glm::vec3 local((index & 1) ? half.x : -half.x, (index & 2) ? half.y : -half.y, (index & 4) ? half.z : -half.z);
            return glm::vec3(transform * glm::vec4(local, 1.0f));
        }

        void Bounds(const glm::mat4 &transform, const glm::vec3 &half, glm::vec3 &minimum, glm::vec3 &maximum) {
            minimum = maximum = Corner(transform, half, 0);
            for (int index = 1; index < 8; ++index) {
                const glm::vec3 corner = Corner(transform, half, index);
                minimum                = glm::min(minimum, corner);
                maximum                = glm::max(maximum, corner);
            }
        }

        size_t UsableTriangles(const CollisionShape &shape) {
            return static_cast<size_t>(std::count_if(shape.triangles.begin(), shape.triangles.end(), [](const CollisionShape::Triangle &triangle) {
                return !Degenerate(triangle.a, triangle.b, triangle.c);
            }));
        }
    } // namespace

    TreeCheck CheckTree(const Native::TreeLevel &tree) {
        TreeCheck check;
        for (uint32_t i = 0; i < tree.faceCount; ++i) {
            const auto &face = tree.faces[i];
            if (face.header.type & Native::kNonFaceTypeMask) {
                continue;
            }
            ++check.faces;
            const glm::vec3 a = ToGlm(*face.vertices[0]), b = ToGlm(*face.vertices[1]), c = ToGlm(*face.vertices[2]);
            if (!(a.x <= b.x && b.x <= c.x)) {
                ++check.unsortedVertices;
            }
            const bool positive = glm::dot(ToGlm(face.normal), glm::cross(b - a, c - a)) > 0.0f;
            if (positive != ((face.header.sortInfo & Native::kFaceSortPositiveWinding) != 0)) {
                ++check.windingMismatches;
            }
        }
        for (int32_t cell = 0; cell < tree.width * tree.length; ++cell) {
            const auto &grid = *tree.cells[cell];
            bool seenFace = false;
            float last    = -std::numeric_limits<float>::infinity();
            for (uint32_t i = 0; i < grid.count; ++i) {
                const auto *volume = grid.volumes[i];
                if (volume->type & 0x80) {
                    check.volumesAfterFaces += seenFace ? 1 : 0;
                    continue;
                }
                seenFace = true;
                if (volume->type & Native::kNonFaceTypeMask) {
                    continue;
                }
                ++check.cellFaces;
                const float key = reinterpret_cast<const Native::Face *>(volume)->vertices[0]->x;
                check.facesOutOfOrder += key < last ? 1 : 0;
                last = key;
            }
        }
        return check;
    }

    StaticCollision::~StaticCollision() {
        Clear();
    }

    size_t StaticCollision::Storage::Bytes() const {
        return faceCount * (sizeof(Native::Face) + 3 * sizeof(Native::Vector)) + boxCount * sizeof(Native::OrientedBox) + plateCount * sizeof(Native::Plate) +
               ballCount * sizeof(Native::Sphere) + postCount * sizeof(Native::Cylinder);
    }

    void StaticCollision::Storage::Disable() {
        for (size_t i = 0; i < faceCount; ++i) faces[i].header.type = Native::kDisabled;
        for (size_t i = 0; i < boxCount; ++i) boxes[i].header.type = Native::kDisabled;
        for (size_t i = 0; i < plateCount; ++i) plates[i].header.type = Native::kDisabled;
        for (size_t i = 0; i < ballCount; ++i) balls[i].header.type = Native::kDisabled;
        for (size_t i = 0; i < postCount; ++i) posts[i].header.type = Native::kDisabled;
    }

    StaticCollision::Storage StaticCollision::Allocate(const CollisionShape &shape) {
        Storage storage;
        // Both sides of every triangle: model meshes are one-sided render
        // geometry, and an object must stop what comes from behind too.
        storage.faceCount  = 2 * UsableTriangles(shape);
        storage.boxCount   = shape.boxes.size();
        storage.plateCount = shape.plates.size();
        storage.ballCount  = shape.balls.size();
        storage.postCount  = shape.posts.size();
        storage.faces      = std::make_unique<Native::Face[]>(storage.faceCount);
        storage.vertices   = std::make_unique<Native::Vector[]>(storage.faceCount * 3);
        storage.boxes      = std::make_unique<Native::OrientedBox[]>(storage.boxCount);
        storage.plates     = std::make_unique<Native::Plate[]>(storage.plateCount);
        storage.balls      = std::make_unique<Native::Sphere[]>(storage.ballCount);
        storage.posts      = std::make_unique<Native::Cylinder[]>(storage.postCount);
        return storage;
    }

    bool StaticCollision::SameCounts(const Storage &storage, const CollisionShape &shape) {
        return storage.faceCount == 2 * UsableTriangles(shape) && storage.boxCount == shape.boxes.size() && storage.plateCount == shape.plates.size() &&
               storage.ballCount == shape.balls.size() && storage.postCount == shape.posts.size();
    }

    void StaticCollision::Write(Storage &storage, const CollisionShape &shape, void *frame, std::vector<Body::Extent> &extents) {
        extents.clear();
        size_t face = 0;
        for (const auto &triangle : shape.triangles) {
            if (Degenerate(triangle.a, triangle.b, triangle.c)) {
                continue;
            }
            const glm::vec3 normal = glm::normalize(glm::cross(triangle.b - triangle.a, triangle.c - triangle.a));
            for (const glm::vec3 &side : {normal, -normal}) {
                auto &stored = storage.faces[face];
                WriteFace(stored, &storage.vertices[face * 3], {triangle.a, triangle.b, triangle.c}, side, triangle.material, triangle.flags);
                const auto &a = *stored.vertices[0];
                const auto &b = *stored.vertices[1];
                const auto &c = *stored.vertices[2];
                extents.push_back({&stored.header, a.x, std::min({a.z, b.z, c.z}), c.x, std::max({a.z, b.z, c.z}), std::max({a.y, b.y, c.y}), a.x, true});
                ++face;
            }
        }
        for (size_t i = 0; i < shape.boxes.size(); ++i) {
            const auto &box = shape.boxes[i];
            auto &stored    = storage.boxes[i];
            glm::vec3 minimum, maximum;
            Bounds(box.transform, box.halfExtent, minimum, maximum);
            stored.header    = MakeHeader(Native::kOrientedBox, box.material, box.flags);
            stored.frame     = frame;
            stored.min       = ToNative(minimum);
            stored.max       = ToNative(maximum);
            stored.minExtent = ToNative(-box.halfExtent);
            stored.maxExtent = ToNative(box.halfExtent);
            StoreMatrix(stored.transform, box.transform);
            StoreMatrix(stored.inverse, glm::inverse(box.transform));
            extents.push_back({&stored.header, minimum.x, minimum.z, maximum.x, maximum.z, maximum.y, 0.0f, false});
        }
        for (size_t i = 0; i < shape.plates.size(); ++i) {
            const auto &plate = shape.plates[i];
            auto &stored      = storage.plates[i];
            glm::vec3 minimum, maximum;
            Bounds(plate.transform, plate.halfExtent, minimum, maximum);
            stored.header    = MakeHeader(Native::kPlate, plate.material, plate.flags);
            stored.frame     = frame;
            stored.minExtent = ToNative(-plate.halfExtent);
            stored.maxExtent = ToNative(plate.halfExtent);
            StoreMatrix(stored.transform, plate.transform);
            StoreMatrix(stored.inverse, glm::inverse(plate.transform));
            extents.push_back({&stored.header, minimum.x, minimum.z, maximum.x, maximum.z, maximum.y, 0.0f, false});
        }
        for (size_t i = 0; i < shape.balls.size(); ++i) {
            const auto &ball = shape.balls[i];
            auto &stored     = storage.balls[i];
            stored.header    = MakeHeader(Native::kSphere, ball.material, ball.flags);
            stored.frame     = frame;
            stored.center    = ToNative(ball.center);
            stored.radius    = ball.radius;
            extents.push_back({&stored.header, ball.center.x - ball.radius, ball.center.z - ball.radius, ball.center.x + ball.radius, ball.center.z + ball.radius,
                               ball.center.y + ball.radius, 0.0f, false});
        }
        for (size_t i = 0; i < shape.posts.size(); ++i) {
            const auto &post = shape.posts[i];
            auto &stored     = storage.posts[i];
            stored.header    = MakeHeader(Native::kCylinder, post.material, post.flags);
            stored.frame     = frame;
            stored.x         = post.centerXZ.x;
            stored.z         = post.centerXZ.y;
            stored.radius    = post.radius;
            extents.push_back({&stored.header, post.centerXZ.x - post.radius, post.centerXZ.y - post.radius, post.centerXZ.x + post.radius, post.centerXZ.y + post.radius,
                               post.topY, 0.0f, false});
        }
    }

    void StaticCollision::Link(uint32_t id, Body &body, std::vector<int32_t> &touched) {
        body.refs.clear();
        if (!_tree) {
            return;
        }
        const auto &tree = *_tree;
        for (const auto &extent : body.extents) {
            if (!(extent.maxX >= tree.worldMinX && extent.minX <= tree.worldMaxX && extent.maxZ >= tree.worldMinZ && extent.minZ <= tree.worldMaxZ)) {
                continue;
            }
            // Clamped as a float: an extent far past the grid overflows the
            // int conversion.
            const auto cellOf = [](float value, float origin, float size, int32_t count) {
                return static_cast<int32_t>(std::clamp(std::floor((value - origin) / size), 0.0f, static_cast<float>(count - 1)));
            };
            const int32_t x0 = cellOf(extent.minX, tree.worldMinX, tree.cellSizeX, tree.width);
            const int32_t x1 = cellOf(extent.maxX, tree.worldMinX, tree.cellSizeX, tree.width);
            const int32_t z0 = cellOf(extent.minZ, tree.worldMinZ, tree.cellSizeZ, tree.length);
            const int32_t z1 = cellOf(extent.maxZ, tree.worldMinZ, tree.cellSizeZ, tree.length);
            for (int32_t z = z0; z <= z1; ++z) {
                for (int32_t x = x0; x <= x1; ++x) {
                    const uint8_t neighbours = (z > z0 ? 0x1 : 0) | (x > x0 ? 0x2 : 0) | (z < z1 ? 0x4 : 0) | (x < x1 ? 0x8 : 0);
                    const int32_t cell       = x + z * tree.width;
                    body.refs.push_back({cell, extent.volume, extent.key, extent.maxY, neighbours, extent.face});
                    auto &patch = _cells[cell];
                    if (!patch.cell) {
                        patch.cell       = tree.cells[cell];
                        patch.count      = patch.cell->count;
                        patch.volumes    = patch.cell->volumes;
                        patch.neighbours = patch.cell->neighbours;
                        patch.maxY       = patch.cell->maxY;
                    }
                    if (patch.bodies.empty() || patch.bodies.back() != id) {
                        if (std::find(patch.bodies.begin(), patch.bodies.end(), id) == patch.bodies.end()) {
                            patch.bodies.insert(std::upper_bound(patch.bodies.begin(), patch.bodies.end(), id), id);
                        }
                    }
                    touched.push_back(cell);
                }
            }
        }
        // Rebuild reads one cell's references at a time; a large or moving
        // body would otherwise be walked whole for every cell it touches.
        std::stable_sort(body.refs.begin(), body.refs.end(), [](const CellRef &left, const CellRef &right) { return left.cell < right.cell; });
    }

    void StaticCollision::Unlink(uint32_t id, Body &body, std::vector<int32_t> &touched) {
        for (const auto &ref : body.refs) {
            auto it = _cells.find(ref.cell);
            if (it == _cells.end()) {
                continue;
            }
            auto &bodies = it->second.bodies;
            bodies.erase(std::remove(bodies.begin(), bodies.end(), id), bodies.end());
            touched.push_back(ref.cell);
        }
        body.refs.clear();
    }

    void StaticCollision::Rebuild(int32_t cell) {
        auto it = _cells.find(cell);
        if (it == _cells.end()) {
            return;
        }
        auto &patch  = it->second;
        auto *native = patch.cell;
        if (patch.bodies.empty()) {
            native->count      = patch.count;
            native->volumes    = patch.volumes;
            native->neighbours = patch.neighbours;
            native->maxY       = patch.maxY;
            _cells.erase(it);
            return;
        }
        std::vector<const CellRef *> volumes;
        std::vector<const CellRef *> faces;
        float maxY = patch.maxY;
        for (uint32_t id : patch.bodies) {
            const auto body = _bodies.find(id);
            if (body == _bodies.end()) {
                continue;
            }
            const auto &refs = body->second.refs;
            const auto first = std::lower_bound(refs.begin(), refs.end(), cell, [](const CellRef &ref, int32_t value) { return ref.cell < value; });
            for (auto ref = first; ref != refs.end() && ref->cell == cell; ++ref) {
                (ref->face ? faces : volumes).push_back(&*ref);
                maxY = std::max(maxY, ref->maxY);
            }
        }
        std::stable_sort(faces.begin(), faces.end(), [](const CellRef *left, const CellRef *right) { return left->key < right->key; });

        auto &merged     = patch.mergedVolumes;
        auto &neighbours = patch.mergedNeighbours;
        merged.clear();
        neighbours.clear();
        merged.reserve(patch.count + volumes.size() + faces.size());
        neighbours.reserve(merged.capacity());
        // The cell's own volumes lead (types 0x80-0x84); its faces follow,
        // with a disabled face (0x20, vertex links unresolved) wherever the
        // file put it.
        uint32_t index = 0;
        for (; index < patch.count && (patch.volumes[index]->type & 0x80); ++index) {
            merged.push_back(patch.volumes[index]);
            neighbours.push_back(patch.neighbours[index]);
        }
        for (const auto *ref : volumes) {
            merged.push_back(ref->volume);
            neighbours.push_back(ref->neighbours);
        }
        size_t next = 0;
        float key   = -std::numeric_limits<float>::infinity();
        for (; index < patch.count; ++index) {
            auto *volume = patch.volumes[index];
            if (!(volume->type & Native::kNonFaceTypeMask)) {
                key = reinterpret_cast<const Native::Face *>(volume)->vertices[0]->x;
            }
            for (; next < faces.size() && faces[next]->key < key; ++next) {
                merged.push_back(faces[next]->volume);
                neighbours.push_back(faces[next]->neighbours);
            }
            merged.push_back(volume);
            neighbours.push_back(patch.neighbours[index]);
        }
        for (; next < faces.size(); ++next) {
            merged.push_back(faces[next]->volume);
            neighbours.push_back(faces[next]->neighbours);
        }
        native->volumes    = merged.data();
        native->neighbours = neighbours.data();
        native->count      = static_cast<uint32_t>(merged.size());
        native->maxY       = maxY;
    }

    void StaticCollision::Retire(Storage storage) {
        storage.Disable();
        _retiredBytes += storage.Bytes();
        _retired.push_back(std::move(storage));
    }

    void StaticCollision::Attach(Native::TreeLevel *tree) {
        if (tree == _tree) {
            return;
        }
        Detach();
        // Whatever touched the retired primitives went with the old tree.
        _retired.clear();
        _retiredBytes = 0;
        _tree         = tree;
        if (!_tree) {
            return;
        }
        std::vector<int32_t> touched;
        for (auto &[id, body] : _bodies) {
            Link(id, body, touched);
        }
        std::sort(touched.begin(), touched.end());
        touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
        for (int32_t cell : touched) {
            Rebuild(cell);
        }
    }

    void StaticCollision::Detach() {
        for (auto &[cell, patch] : _cells) {
            patch.cell->count      = patch.count;
            patch.cell->volumes    = patch.volumes;
            patch.cell->neighbours = patch.neighbours;
            patch.cell->maxY       = patch.maxY;
        }
        _cells.clear();
        for (auto &[id, body] : _bodies) {
            body.refs.clear();
        }
        _tree = nullptr;
    }

    uint32_t StaticCollision::Add(const CollisionShape &shape, void *frame) {
        if (shape.Empty()) {
            return 0;
        }
        const uint32_t id = _nextBody++;
        auto &body        = _bodies[id];
        body.frame        = frame;
        body.storage      = Allocate(shape);
        Write(body.storage, shape, frame, body.extents);
        std::vector<int32_t> touched;
        Link(id, body, touched);
        std::sort(touched.begin(), touched.end());
        touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
        for (int32_t cell : touched) {
            Rebuild(cell);
        }
        return id;
    }

    void StaticCollision::Update(uint32_t id, const CollisionShape &shape) {
        auto it = _bodies.find(id);
        if (it == _bodies.end()) {
            return;
        }
        auto &body = it->second;
        std::vector<int32_t> touched;
        Unlink(id, body, touched);
        if (!SameCounts(body.storage, shape)) {
            Retire(std::move(body.storage));
            body.storage = Allocate(shape);
        }
        Write(body.storage, shape, body.frame, body.extents);
        Link(id, body, touched);
        std::sort(touched.begin(), touched.end());
        touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
        for (int32_t cell : touched) {
            Rebuild(cell);
        }
    }

    void StaticCollision::Remove(uint32_t id) {
        auto it = _bodies.find(id);
        if (it == _bodies.end()) {
            return;
        }
        std::vector<int32_t> touched;
        Unlink(id, it->second, touched);
        Storage storage = std::move(it->second.storage);
        _bodies.erase(it);
        std::sort(touched.begin(), touched.end());
        touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
        for (int32_t cell : touched) {
            Rebuild(cell);
        }
        Retire(std::move(storage));
    }

    void StaticCollision::Clear() {
        Detach();
        _bodies.clear();
        _retired.clear();
        _retiredBytes = 0;
    }

    size_t StaticCollision::LinkedPrimitives(uint32_t id) const {
        const auto it = _bodies.find(id);
        if (it == _bodies.end()) {
            return 0;
        }
        std::vector<const Native::Header *> volumes;
        for (const auto &ref : it->second.refs) {
            volumes.push_back(ref.volume);
        }
        std::sort(volumes.begin(), volumes.end());
        return static_cast<size_t>(std::unique(volumes.begin(), volumes.end()) - volumes.begin());
    }
} // namespace Mafia1Online::Features::MapObject
