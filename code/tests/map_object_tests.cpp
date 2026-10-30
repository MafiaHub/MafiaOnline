// Map object motion, model names, and static collision patching checked
// against ports of the retail per-cell queries, and the traffic sign break
// reports that name a static tree primitive (reM g_collision.cpp:
// LineInGridCell 0x5c66a0, LineInVolume 0x5c60e0, the contact half of
// SphereInFace 0x5cc470).
#include "features/map_object/collision_template.h"
#include "features/map_object/static_collision.h"
#include "shared/features/map_object/map_object_motion.h"
#include "shared/features/world/world_props.h"

#include <mafia1/sdk/world/native_city_cache.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <vector>

// The networking headers bring in windows.h and its far and near macros.
#undef far
#undef near

namespace {
    namespace Native = Mafia1Online::SDK::Collision;
    namespace Motion = Mafia1Online::Shared::MapObject;
    using Mafia1Online::Features::MapObject::CollisionShape;
    using Mafia1Online::Features::MapObject::StaticCollision;

    int gFailures = 0;
#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);        \
            ++gFailures;                                                              \
        }                                                                             \
    } while (0)

    bool Near(float a, float b, float epsilon = 1e-3f) {
        return std::fabs(a - b) <= epsilon;
    }

    glm::vec3 V(const Native::Vector &value) {
        return {value.x, value.y, value.z};
    }

    // --- Retail ports -------------------------------------------------------
    struct Line {
        glm::vec3 start, direction, end;
        float minX, minZ, maxX, maxZ;
        float fraction = 1.0f;
        glm::vec3 normal {0.0f};
        const Native::Header *hit = nullptr;
    };

    Line MakeLine(glm::vec3 start, glm::vec3 direction) {
        Line line;
        line.start     = start;
        line.direction = direction;
        line.end       = start + direction;
        line.minX      = std::min(start.x, line.end.x);
        line.maxX      = std::max(start.x, line.end.x);
        line.minZ      = std::min(start.z, line.end.z);
        line.maxZ      = std::max(start.z, line.end.z);
        return line;
    }

    glm::vec3 RowTransform(const glm::vec3 &point, const float *matrix) {
        return glm::vec3(glm::make_mat4(matrix) * glm::vec4(point, 1.0f));
    }

    // LineVsAABB (0x5c5b20), slab form; enough to tell a hit and its distance.
    bool LineVsBox(const glm::vec3 &minimum, const glm::vec3 &maximum, Line &line) {
        float enter = 0.0f, leave = line.fraction;
        for (int axis = 0; axis < 3; ++axis) {
            const float origin = line.start[axis], delta = line.direction[axis];
            if (std::fabs(delta) < 1e-12f) {
                if (origin < minimum[axis] || origin > maximum[axis]) return false;
                continue;
            }
            float t0 = (minimum[axis] - origin) / delta, t1 = (maximum[axis] - origin) / delta;
            if (t0 > t1) std::swap(t0, t1);
            enter = std::max(enter, t0);
            leave = std::min(leave, t1);
            if (enter > leave) return false;
        }
        if (enter >= line.fraction) return false;
        line.fraction = enter;
        return true;
    }

    bool LineInVolume(const Native::Header *volume, Line &line) {
        switch (volume->type) {
        case Native::kOrientedBox: {
            const auto *box = reinterpret_cast<const Native::OrientedBox *>(volume);
            Line local = MakeLine(RowTransform(line.start, box->inverse), RowTransform(line.end, box->inverse) - RowTransform(line.start, box->inverse));
            local.fraction = line.fraction;
            if (!LineVsBox(V(box->minExtent), V(box->maxExtent), local)) return false;
            line.fraction = local.fraction;
            line.hit      = volume;
            return true;
        }
        case Native::kSphere: {
            const auto *sphere = reinterpret_cast<const Native::Sphere *>(volume);
            const float lengthSq = glm::dot(line.direction, line.direction);
            const glm::vec3 toCenter = V(sphere->center) - line.start;
            const float projected = glm::dot(toCenter, line.direction);
            const float discriminant = projected * projected - (glm::dot(toCenter, toCenter) - sphere->radius * sphere->radius) * lengthSq;
            if (discriminant < 0.0f) return false;
            const float fraction = (projected - std::sqrt(discriminant)) / lengthSq;
            if (fraction < 0.0f || fraction >= line.fraction) return false;
            line.fraction = fraction;
            line.hit      = volume;
            return true;
        }
        default: return false;
        }
    }

    // LineInGridCell (0x5c66a0), verbatim apart from the volume subset.
    bool LineInGridCell(const Native::GridCell &cell, Line &line) {
        bool hit = false;
        for (uint32_t i = 0; i < cell.count; ++i) {
            const Native::Header *volume = cell.volumes[i];
            if (volume->type & Native::kNonFaceTypeMask) {
                hit |= LineInVolume(volume, line);
                continue;
            }
            const auto *face = reinterpret_cast<const Native::Face *>(volume);
            const glm::vec3 a = V(*face->vertices[0]), b = V(*face->vertices[1]), c = V(*face->vertices[2]);
            if (c.x < line.minX) continue;
            if (a.x > line.maxX) return hit;
            float minimumZ = a.z, maximumZ = a.z;
            if (c.z < minimumZ) minimumZ = c.z;
            else if (c.z > maximumZ) maximumZ = c.z;
            if (b.z < minimumZ) minimumZ = b.z;
            else if (b.z > maximumZ) maximumZ = b.z;
            if (minimumZ > line.maxZ || maximumZ < line.minZ) continue;
            const glm::vec3 normal = V(face->normal);
            const float denominator = glm::dot(normal, line.direction);
            if (denominator < 1e-8f) continue;
            const float fraction = -(glm::dot(normal, line.start) + face->d) / denominator;
            if (fraction < 0.0f || fraction >= line.fraction) continue;
            const glm::vec3 point = line.start + line.direction * fraction;
            const glm::vec3 edge0 = b - a, edge1 = c - a, relative = point - a;
            float first, second;
            switch (volume->type) {
            case Native::kFaceXConstantY:
                first = relative.y / edge1.y;
                if (first < 0.0f || first >= 1.0f) continue;
                second = (relative.z - edge1.z * first) / edge0.z;
                break;
            case Native::kFaceX:
                first = (edge0.y * relative.z - edge0.z * relative.y) / (edge0.y * edge1.z - edge1.y * edge0.z);
                if (first < 0.0f || first >= 1.0f) continue;
                second = (relative.y - edge1.y * first) / edge0.y;
                break;
            case Native::kFaceYConstantX:
                first = relative.x / edge1.x;
                if (first < 0.0f || first >= 1.0f) continue;
                second = (relative.z - edge1.z * first) / edge0.z;
                break;
            case Native::kFaceYConstantZ:
                first = relative.z / edge1.z;
                if (first < 0.0f || first >= 1.0f) continue;
                second = (relative.x - edge1.x * first) / edge0.x;
                break;
            case Native::kFaceY:
                first = (edge0.x * relative.z - edge0.z * relative.x) / (edge1.z * edge0.x - edge0.z * edge1.x);
                if (first < 0.0f || first >= 1.0f) continue;
                second = (relative.x - edge1.x * first) / edge0.x;
                break;
            case Native::kFaceZConstantX:
                first = relative.x / edge1.x;
                if (first < 0.0f || first >= 1.0f) continue;
                second = (relative.y - edge1.y * first) / edge0.y;
                break;
            case Native::kFaceZ:
                first = (edge0.x * relative.y - edge0.y * relative.x) / (edge1.y * edge0.x - edge0.y * edge1.x);
                if (first < 0.0f || first >= 1.0f) continue;
                second = (relative.x - edge1.x * first) / edge0.x;
                break;
            default: continue;
            }
            if (second < 0.0f || first + second >= 1.0f) continue;
            line.fraction = fraction;
            line.normal   = normal;
            line.hit      = &face->header;
            hit           = true;
        }
        return hit;
    }

    // SphereInFace's resting-contact test: a sphere touching the face from
    // its open side, with the edge signs SphereTriangleEdgeSide gives.
    float EdgeSide(const glm::vec3 &start, const glm::vec3 &end, const glm::vec3 &reference, const glm::vec3 &direction) {
        return glm::dot(glm::cross(start, end), direction) + glm::dot(start - end, reference);
    }

    bool SphereRestsOn(const Native::Face &face, const glm::vec3 &center, float radius, const glm::vec3 &velocity) {
        const glm::vec3 normal = V(face.normal);
        const glm::vec3 v0 = V(*face.vertices[0]), v1 = V(*face.vertices[1]), v2 = V(*face.vertices[2]);
        const bool doubleSided = (face.header.sortInfo & Native::kFaceSortPositiveWinding) != 0;
        if (glm::dot(glm::normalize(velocity), normal) < 0.0f) return false;
        const float planeDistance = glm::dot(center, normal) + face.d;
        if (planeDistance > 0.0f || planeDistance <= -radius) return false;
        const glm::vec3 cross = glm::cross(normal, center);
        const float s01 = EdgeSide(v0, v1, cross, normal), s12 = EdgeSide(v1, v2, cross, normal), s20 = EdgeSide(v2, v0, cross, normal);
        return (!doubleSided && s01 <= 0.0f && s12 <= 0.0f && s20 <= 0.0f) || (doubleSided && s01 >= 0.0f && s12 >= 0.0f && s20 >= 0.0f);
    }

    // --- A small tree with one retail floor face and one retail box ---------
    struct FakeTree {
        Native::TreeLevel tree {};
        std::vector<Native::GridCell> cells;
        std::vector<Native::GridCell *> cellPointers;
        std::vector<std::vector<Native::Header *>> volumes;
        std::vector<std::vector<uint8_t>> neighbours;
        std::array<Native::Vector, 3> floorVertices {};
        Native::Face floor {};
        Native::OrientedBox crate {};

        FakeTree() {
            tree.worldMinX = 0.0f;
            tree.worldMinZ = 0.0f;
            tree.worldMaxX = 40.0f;
            tree.worldMaxZ = 40.0f;
            tree.cellSizeX = tree.cellSizeZ = 10.0f;
            tree.width = tree.length = 4;
            cells.resize(16);
            volumes.resize(16);
            neighbours.resize(16);
            for (auto &cell : cells) cell.maxY = -10000.0f;
            // A floor triangle in cell (0, 0), stored as retail does: an
            // upward walking surface, normal pointing down into the ground.
            floorVertices = {{{0.5f, 0.0f, 0.5f}, {1.0f, 0.0f, 9.0f}, {9.0f, 0.0f, 1.0f}}};
            floor.header   = {Native::kFaceYConstantX, 0, 0, 1};
            floor.header.type = Native::kFaceY;
            for (int i = 0; i < 3; ++i) floor.vertices[i] = &floorVertices[i];
            floor.normal = {0.0f, -1.0f, 0.0f};
            floor.d      = 0.0f;
            const glm::vec3 a = V(floorVertices[0]), b = V(floorVertices[1]), c = V(floorVertices[2]);
            floor.header.sortInfo = glm::dot(V(floor.normal), glm::cross(b - a, c - a)) > 0.0f ? 1 : 0;
            // A retail XTOBB crate in cell (0, 0).
            crate.header = {Native::kOrientedBox, 0, 0, 33};
            const glm::mat4 crateTransform = glm::translate(glm::mat4(1.0f), {5.0f, 0.5f, 5.0f});
            std::memcpy(crate.transform, glm::value_ptr(crateTransform), sizeof(crate.transform));
            const glm::mat4 crateInverse = glm::inverse(crateTransform);
            std::memcpy(crate.inverse, glm::value_ptr(crateInverse), sizeof(crate.inverse));
            crate.minExtent = {-0.5f, -0.5f, -0.5f};
            crate.maxExtent = {0.5f, 0.5f, 0.5f};
            crate.min       = {4.5f, 0.0f, 4.5f};
            crate.max       = {5.5f, 1.0f, 5.5f};
            volumes[0]      = {&crate.header, &floor.header};
            neighbours[0]   = {0, 0};
            cells[0].maxY   = 1.0f;
            for (size_t i = 0; i < cells.size(); ++i) {
                cells[i].count      = static_cast<uint32_t>(volumes[i].size());
                cells[i].volumes    = volumes[i].empty() ? nullptr : volumes[i].data();
                cells[i].neighbours = neighbours[i].empty() ? nullptr : neighbours[i].data();
                cellPointers.push_back(&cells[i]);
            }
            tree.cells = cellPointers.data();
            tree.faceCount = 1;
            tree.faces = &floor;
        }

        // The retail grid walk reduced to the cells a short line touches.
        const Native::Header *Cast(glm::vec3 start, glm::vec3 direction, float *fraction = nullptr) {
            Line line = MakeLine(start, direction);
            const int x0 = std::clamp(static_cast<int>(std::floor(line.minX / 10.0f)), 0, 3), x1 = std::clamp(static_cast<int>(std::floor(line.maxX / 10.0f)), 0, 3);
            const int z0 = std::clamp(static_cast<int>(std::floor(line.minZ / 10.0f)), 0, 3), z1 = std::clamp(static_cast<int>(std::floor(line.maxZ / 10.0f)), 0, 3);
            const float minimumY = std::min(start.y, start.y + direction.y);
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                    if (tree.Cell(x, z)->maxY >= minimumY) LineInGridCell(*tree.Cell(x, z), line);
            if (fraction) *fraction = line.fraction;
            return line.hit;
        }
    };

    // A closed unit cube's twelve triangles around center.
    std::vector<CollisionShape::Triangle> Cube(const glm::mat4 &transform) {
        const glm::vec3 p[8] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
        const int faces[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {3, 6, 2}, {3, 7, 6}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
        std::vector<CollisionShape::Triangle> triangles;
        for (const auto &face : faces) {
            CollisionShape::Triangle triangle;
            triangle.a        = glm::vec3(transform * glm::vec4(0.5f * p[face[0]], 1.0f));
            triangle.b        = glm::vec3(transform * glm::vec4(0.5f * p[face[1]], 1.0f));
            triangle.c        = glm::vec3(transform * glm::vec4(0.5f * p[face[2]], 1.0f));
            triangle.material = 4;
            triangles.push_back(triangle);
        }
        return triangles;
    }

    void TestMotion() {
        Motion::MotionSegment segment;
        segment.startPosition  = {0.0f, 0.0f, 0.0f};
        segment.targetPosition = {10.0f, 0.0f, 0.0f};
        segment.targetRotation = glm::angleAxis(1.0f, glm::vec3(0.0f, 1.0f, 0.0f));
        segment.speed          = 2.0f;
        segment.moving         = true;
        CHECK(Near(Motion::MotionDuration(segment), 5.0f));
        auto half = Motion::SampleMotion(segment, 2.5f);
        CHECK(!half.done && Near(half.position.x, 5.0f));
        CHECK(Near(glm::angle(half.rotation), 0.5f));
        auto end = Motion::SampleMotion(segment, 6.0f);
        CHECK(end.done && Near(end.position.x, 10.0f));
        // Turning in place: speed is radians per second.
        segment.targetPosition = segment.startPosition;
        CHECK(Near(Motion::MotionDuration(segment), 0.5f));
        segment.moving = false;
        CHECK(Motion::SampleMotion(segment, 1.0f).done && Near(Motion::SampleMotion(segment, 1.0f).position.x, 0.0f));

        CHECK(Motion::ValidModelName("bar_fence.4ds"));
        CHECK(Motion::ValidModelName("xpumpa 2.i3d"));
        CHECK(!Motion::ValidModelName("..\\x.4ds"));
        CHECK(!Motion::ValidModelName("fence"));
        CHECK(!Motion::ValidModelName("a/b.4ds"));
        CHECK(Motion::CacheModelName("Bar_Fence.4DS") == "bar_fence.i3d");
        CHECK(Motion::ValidMaterial(4) && !Motion::ValidMaterial(24) && !Motion::ValidMaterial(41) && !Motion::ValidMaterial(47));
    }

    // Cell invariants the retail loops depend on.
    void CheckCell(const Native::GridCell &cell) {
        bool face = false;
        float last = -1e30f;
        for (uint32_t i = 0; i < cell.count; ++i) {
            const auto *volume = cell.volumes[i];
            if (volume->type & 0x80) {
                CHECK(!face);
                continue;
            }
            face = true;
            const auto *stored = reinterpret_cast<const Native::Face *>(volume);
            CHECK(stored->vertices[0]->x >= last);
            last = stored->vertices[0]->x;
            CHECK(stored->vertices[0]->x <= stored->vertices[1]->x && stored->vertices[1]->x <= stored->vertices[2]->x);
            const glm::vec3 a = V(*stored->vertices[0]), b = V(*stored->vertices[1]), c = V(*stored->vertices[2]);
            CHECK(Near(glm::dot(V(stored->normal), a) + stored->d, 0.0f));
        }
    }

    void TestPatch() {
        FakeTree fake;
        // Retail geometry answers as before.
        CHECK(fake.Cast({2.0f, 5.0f, 2.0f}, {0.0f, -10.0f, 0.0f}) == &fake.floor.header);
        CHECK(fake.Cast({3.0f, 0.5f, 5.0f}, {4.0f, 0.0f, 0.0f}) == &fake.crate.header);

        StaticCollision collision;
        collision.Attach(&fake.tree);
        // A wall cube spanning cells (1,1)..(2,1), and a ball in cell (0, 0).
        CollisionShape shape;
        const glm::mat4 wall = glm::translate(glm::mat4(1.0f), {20.0f, 1.0f, 15.0f}) * glm::scale(glm::mat4(1.0f), {6.0f, 2.0f, 2.0f});
        shape.triangles = Cube(wall);
        shape.balls.push_back({{7.0f, 0.5f, 2.0f}, 0.5f, 33, 0});
        int frameLink   = 0;
        const uint32_t body = collision.Add(shape, &frameLink);
        CHECK(body != 0);
        CHECK(collision.LinkedPrimitives(body) == 25);
        for (int i = 0; i < 16; ++i) CheckCell(*fake.tree.cells[i]);
        CHECK(fake.tree.Cell(1, 1)->count == 20 && fake.tree.Cell(2, 1)->count == 20 && fake.tree.Cell(0, 0)->count == 3);
        CHECK(Near(fake.tree.Cell(1, 1)->maxY, 2.0f));
        CHECK(fake.tree.Cell(1, 1)->neighbours[0] == 0x8 && fake.tree.Cell(2, 1)->neighbours[0] == 0x2);
        // The retail line loop hits the wall from both sides and from above,
        // and the retail floor and crate still answer in their cell.
        float fraction = 0.0f;
        const auto *front = fake.Cast({20.0f, 1.0f, 10.0f}, {0.0f, 0.0f, 10.0f}, &fraction);
        CHECK(front && Near(fraction, 0.4f));
        CHECK(front && reinterpret_cast<const Native::Face *>(front)->header.material == 4);
        CHECK(fake.Cast({20.0f, 1.0f, 20.0f}, {0.0f, 0.0f, -10.0f}, &fraction) && Near(fraction, 0.4f));
        CHECK(fake.Cast({18.0f, 9.0f, 15.0f}, {0.0f, -10.0f, 0.0f}, &fraction) && Near(fraction, 0.7f));
        // From inside, the twin faces stop it too.
        CHECK(fake.Cast({20.0f, 1.0f, 15.0f}, {0.0f, 0.0f, 5.0f}, &fraction) && Near(fraction, 0.2f));
        CHECK(!fake.Cast({30.0f, 1.0f, 10.0f}, {0.0f, 0.0f, 10.0f}));
        CHECK(fake.Cast({7.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 4.0f}, &fraction) && Near(fraction, 0.375f));
        CHECK(fake.Cast({3.0f, 0.5f, 5.0f}, {4.0f, 0.0f, 0.0f}) == &fake.crate.header);
        CHECK(fake.Cast({2.0f, 5.0f, 2.0f}, {0.0f, -10.0f, 0.0f}) == &fake.floor.header);
        // A sphere resting against the wall's front from outside touches the
        // twin facing it under the retail winding rule; one just inside the
        // front face, moving out, touches the other twin.
        int touching = 0, inside = 0;
        const auto *cell = fake.tree.Cell(2, 1);
        for (uint32_t i = 0; i < cell->count; ++i) {
            const auto &face = *reinterpret_cast<const Native::Face *>(cell->volumes[i]);
            touching += SphereRestsOn(face, {20.5f, 1.0f, 13.7f}, 0.4f, {0.0f, 0.0f, 1.0f}) ? 1 : 0;
            inside += SphereRestsOn(face, {20.5f, 1.0f, 14.3f}, 0.4f, {0.0f, 0.0f, -1.0f}) ? 1 : 0;
        }
        CHECK(touching == 1);
        CHECK(inside == 1);
        // The retail floor's stored winding bit agrees with the same rule.
        CHECK(SphereRestsOn(fake.floor, {2.0f, 0.3f, 2.0f}, 0.4f, {0.0f, -1.0f, 0.0f}));

        // Moving rewrites in place and rebins.
        const auto *before = fake.tree.Cell(1, 1)->volumes[0];
        CollisionShape moved = shape;
        moved.triangles = Cube(glm::translate(glm::mat4(1.0f), {15.0f, 1.0f, 35.0f}) * glm::scale(glm::mat4(1.0f), {2.0f, 2.0f, 2.0f}));
        collision.Update(body, moved);
        CHECK(fake.tree.Cell(2, 1)->count == 0 && fake.tree.Cell(2, 1)->volumes == nullptr);
        CHECK(fake.tree.Cell(1, 3)->count == 24 && fake.tree.Cell(1, 3)->volumes[0] == before);
        CHECK(!fake.Cast({20.0f, 1.0f, 10.0f}, {0.0f, 0.0f, 10.0f}));
        CHECK(fake.Cast({15.0f, 1.0f, 30.0f}, {0.0f, 0.0f, 10.0f}, &fraction) && Near(fraction, 0.4f));

        // Removal restores the retail cell exactly and keeps the memory a
        // stale reader could still hold, disabled.
        collision.Remove(body);
        CHECK(collision.PatchedCellCount() == 0);
        CHECK(fake.tree.Cell(0, 0)->count == 2 && fake.tree.Cell(0, 0)->volumes == fake.volumes[0].data() && Near(fake.tree.Cell(0, 0)->maxY, 1.0f));
        CHECK(fake.tree.Cell(1, 3)->count == 0 && fake.tree.Cell(1, 3)->maxY == -10000.0f);
        CHECK(before->type == Native::kDisabled);
        CHECK(collision.RetiredBytes() > 0);

        // A body outside the grid links nowhere; detach restores cells and
        // a body links again into the next tree.
        CollisionShape far;
        far.triangles = Cube(glm::translate(glm::mat4(1.0f), {500.0f, 1.0f, 500.0f}));
        const uint32_t outside = collision.Add(far, &frameLink);
        CHECK(outside != 0 && collision.LinkedPrimitives(outside) == 0);
        collision.Remove(outside);
        const uint32_t again = collision.Add(shape, &frameLink);
        collision.Detach();
        CHECK(fake.tree.Cell(1, 1)->count == 0);
        collision.Attach(&fake.tree);
        CHECK(fake.tree.Cell(1, 1)->count == 20);
        CHECK(collision.RetiredBytes() == 0);
        collision.Remove(again);
        collision.Clear();
        CHECK(fake.tree.Cell(0, 0)->count == 2);
    }

    void TestTemplate() {
        // A klz XTOBB of a scaled, turned instance comes back in its model
        // space and lands where it was when rebuilt at the instance's pose.
        const glm::mat4 instance = glm::translate(glm::mat4(1.0f), {100.0f, 2.0f, -50.0f}) * glm::mat4_cast(glm::angleAxis(0.7f, glm::vec3(0.0f, 1.0f, 0.0f))) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
        const glm::mat4 boxWorld = glm::translate(glm::mat4(1.0f), {101.0f, 3.0f, -49.0f}) * glm::mat4_cast(glm::angleAxis(0.7f, glm::vec3(0.0f, 1.0f, 0.0f)));
        Mafia1Online::Features::MapObject::KlzFile file;
        file.records = {{2, "Primary sector"}, {2, "bench12"}, {1, "lekarna.Box01"}};
        Mafia1Online::Features::MapObject::KlzFile::Volume volume;
        volume.header    = {Native::kOrientedBox, 0, 0, 41};
        volume.link      = 1;
        volume.minimum   = {-1.0f, -0.5f, -0.25f};
        volume.maximum   = {1.0f, 0.5f, 0.25f};
        volume.transform = boxWorld;
        file.volumes.push_back(volume);
        Mafia1Online::Features::MapObject::KlzFile::Cylinder cylinder;
        cylinder.header = {Native::kCylinder, 0, 0, 19};
        cylinder.link   = 1;
        cylinder.x      = 100.0f;
        cylinder.z      = -50.0f;
        cylinder.radius = 0.2f;
        file.cylinders.push_back(cylinder);
        Mafia1Online::Features::MapObject::KlzFile::Face face;
        face.header   = {Native::kFaceY, 0, 0, 17};
        face.vertices = {0, 1, 2};
        face.link     = 2;
        file.faces.push_back(face);
        Mafia1Online::Features::MapObject::CollisionTemplates templates;
        templates.Load(file, {{"bench12", "bench.i3d"}, {"lekarna", "9lekarna.i3d"}, {"other", "bench.i3d"}});
        CHECK(templates.Candidates("bench.i3d") == std::vector<std::string> {"bench12"});
        CHECK(templates.Candidates("9lekarna.i3d") == std::vector<std::string> {"lekarna"});
        const auto bench = templates.Localize("bench12", instance);
        CHECK(bench.boxes.size() == 1 && bench.posts.size() == 1 && bench.faces.empty());
        CHECK(bench.boxes[0].material == 33); // crash frame becomes universal
        const auto shape = Mafia1Online::Features::MapObject::BuildShape(&bench, {}, instance, 4, {-1.0f, 0.0f, -1.0f}, {1.0f, 1.5f, 1.0f});
        CHECK(shape.boxes.size() == 1 && shape.posts.size() == 1);
        if (shape.boxes.size() == 1) {
            CHECK(Near(shape.boxes[0].transform[3].x, 101.0f) && Near(shape.boxes[0].transform[3].z, -49.0f));
            CHECK(Near(shape.boxes[0].halfExtent.x, 1.0f) && Near(shape.boxes[0].halfExtent.z, 0.25f));
        }
        if (shape.posts.size() == 1) {
            CHECK(Near(shape.posts[0].centerXZ.x, 100.0f) && Near(shape.posts[0].centerXZ.y, -50.0f) && Near(shape.posts[0].radius, 0.2f));
            CHECK(Near(shape.posts[0].topY, 5.0f));
        }
        // Elsewhere, unscaled: the box follows the object.
        const glm::mat4 placed = glm::translate(glm::mat4(1.0f), {10.0f, 0.0f, 10.0f});
        const auto moved = Mafia1Online::Features::MapObject::BuildShape(&bench, {}, placed, 4, {-1.0f, 0.0f, -1.0f}, {1.0f, 1.5f, 1.0f});
        if (moved.boxes.size() == 1) {
            const glm::vec3 offset = glm::vec3(glm::inverse(instance) * glm::vec4(101.0f, 3.0f, -49.0f, 1.0f));
            CHECK(Near(moved.boxes[0].transform[3].x, 10.0f + offset.x) && Near(moved.boxes[0].transform[3].y, offset.y));
            CHECK(Near(moved.boxes[0].halfExtent.x, 0.5f));
        }
        // A face template resolves against the part named in the record.
        const auto lekarna = templates.Localize("lekarna", glm::mat4(1.0f));
        CHECK(lekarna.faces.size() == 1 && lekarna.faces[0].child == "Box01");
        Mafia1Online::Features::MapObject::MeshPart part;
        part.child    = "Box01";
        part.world    = placed;
        part.vertices = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}};
        const auto faces = Mafia1Online::Features::MapObject::BuildShape(&lekarna, {part}, placed, 4, {}, {});
        CHECK(faces.triangles.size() == 1 && faces.triangles[0].material == 17 && Near(faces.triangles[0].a.x, 10.0f));
        // Without a template: the mesh, in the requested material.
        part.triangles = {{0, 1, 2}};
        const auto mesh = Mafia1Online::Features::MapObject::BuildShape(nullptr, {part}, placed, 13, {}, {});
        CHECK(mesh.triangles.size() == 1 && mesh.triangles[0].material == 13);
    }

    // Optional: python-extracted stock files, to check the parsers against
    // the real thing (tests/map_object_tests <tree.klz> <scene2.bin>).
    void TestFiles(const char *klzPath, const char *scenePath) {
        std::ifstream klzStream(klzPath, std::ios::binary), sceneStream(scenePath, std::ios::binary);
        const std::vector<uint8_t> klz((std::istreambuf_iterator<char>(klzStream)), {}), scene((std::istreambuf_iterator<char>(sceneStream)), {});
        Mafia1Online::Features::MapObject::KlzFile file;
        std::vector<Mafia1Online::Features::MapObject::ModelInstance> instances;
        CHECK(Mafia1Online::Features::MapObject::ParseKlz(klz, file));
        CHECK(Mafia1Online::Features::MapObject::ReadModelInstances(scene, instances));
        size_t axis = 0, oriented = 0, plates = 0;
        for (const auto &volume : file.volumes) {
            axis += volume.header.type == Native::kAxisBox;
            oriented += volume.header.type == Native::kOrientedBox;
            plates += volume.header.type == Native::kPlate;
        }
        std::printf("klz: %zu records, %zu faces, %zu AABB, %zu XTOBB, %zu OBB, %zu spheres, %zu cylinders; scene2: %zu model instances\n", file.records.size(),
                    file.faces.size(), axis, oriented, plates, file.spheres.size(), file.cylinders.size(), instances.size());
        Mafia1Online::Features::MapObject::CollisionTemplates templates;
        templates.Load(std::move(file), instances);
        size_t models = 0;
        std::vector<std::string> seen;
        for (const auto &instance : instances) {
            if (std::find(seen.begin(), seen.end(), instance.model) != seen.end()) continue;
            seen.push_back(instance.model);
            models += templates.Candidates(instance.model).empty() ? 0 : 1;
        }
        std::printf("%zu of %zu placed models have mission collision to reuse\n", models, seen.size());
    }

    // --- Traffic signs -------------------------------------------------------
    template <typename T>
    T RoundTrip(T value) {
        MafiaNet::BitStream stream;
        value.Serialize(&stream, true);
        T read;
        read.Serialize(&stream, false);
        return read;
    }

    void TestSigns() {
        namespace Props = Mafia1Online::Shared::World::Props;
        namespace World = Mafia1Online::SDK::World;
        Props::SignBreak sign;
        sign.segment = 12;
        sign.placement = 345;
        sign.primitiveKind = static_cast<uint8_t>(World::TreePrimitiveKind::OrientedBox);
        sign.primitiveIndex = 70001;
        sign.position = {-1520.0f, 2.0f, 610.0f};
        sign.hit = {-1513.0f, -1.5f, 616.0f};
        sign.impulse = {30.0f, 0.0f, -40.0f};
        CHECK(sign.Valid());
        CHECK(sign.Key() == (12u << 16 | 345u));

        // Where HandleSignalMarkCrash finds the sign: under 10 m across and
        // 5 m up or down of the hit.
        auto distant = sign;
        distant.hit = {-1520.0f + 8.0f, 2.0f, 610.0f + 6.1f};
        CHECK(!distant.Valid());
        distant.hit = {-1520.0f, 7.5f, 610.0f};
        CHECK(!distant.Valid());
        auto odd = sign;
        odd.primitiveKind = Props::kSignPrimitiveKinds;
        CHECK(!odd.Valid());
        odd = sign;
        odd.impulse.x = std::numeric_limits<float>::quiet_NaN();
        CHECK(!odd.Valid());

        auto strong = sign;
        strong.impulse = {300.0f, 0.0f, -400.0f};
        strong.Clamp();
        CHECK(Near(strong.impulse.Length(), Props::kMaxBreakImpulse) && Near(strong.impulse.x, 60.0f) && Near(strong.impulse.z, -80.0f));

        Props::SignBreakReport report;
        report.missionGeneration = 7;
        report.sign = sign;
        const auto read = RoundTrip(report);
        CHECK(read.missionGeneration == 7 && read.sign.Key() == sign.Key() && read.sign.primitiveKind == sign.primitiveKind &&
              read.sign.primitiveIndex == 70001 && Near(read.sign.hit.z, 616.0f) && Near(read.sign.impulse.z, -40.0f) && read.sign.Valid());

        Props::Snapshot snapshot;
        snapshot.missionGeneration = 7;
        snapshot.broken.resize(1);
        snapshot.broken[0].name = "PHbox01 12";
        snapshot.moved.resize(2);
        snapshot.signs = {sign, sign};
        snapshot.signs[1].placement = 346;
        const auto readSnapshot = RoundTrip(snapshot);
        CHECK(readSnapshot.broken.size() == 1 && readSnapshot.broken[0].name == "PHbox01 12" && readSnapshot.moved.size() == 2);
        CHECK(readSnapshot.signs.size() == 2 && readSnapshot.signs[1].Key() == (12u << 16 | 346u) && readSnapshot.signs[0].Valid());

        // A snapshot claiming more signs than kMaxSigns is dropped whole.
        MafiaNet::BitStream stream;
        uint64_t generation = 7;
        uint16_t brokenCount = 0, movedCount = 0, signCount = static_cast<uint16_t>(Props::kMaxSigns + 1);
        stream.Write(generation);
        stream.Write(brokenCount);
        stream.Write(movedCount);
        stream.Write(signCount);
        Props::Snapshot forged;
        forged.Serialize(&stream, false);
        CHECK(forged.signs.empty() && forged.broken.empty() && forged.moved.empty());

        // A break's pieces at rest: up to kMaxPieces, near the prop.
        auto piece = [](uint16_t index, Props::Vector position) {
            Props::Pose pose;
            pose.index = index;
            pose.position = position;
            pose.direction = {1.0f, 0.0f, 0.0f};
            pose.up = {0.0f, 1.0f, 0.0f};
            return pose;
        };
        std::vector<Props::Pose> pieces {piece(0, {-1521.0f, 1.0f, 612.0f}), piece(2, {-1518.0f, 0.5f, 615.0f})};
        CHECK(Props::ValidPieces(pieces, sign.position));
        CHECK(!Props::ValidPieces({}, sign.position));
        auto flung = pieces;
        flung[1].position = {-1520.0f + Props::kMaxPieceTravel + 1.0f, 2.0f, 610.0f};
        CHECK(!Props::ValidPieces(flung, sign.position));
        flung = pieces;
        flung[0].index = static_cast<uint16_t>(Props::kMaxPieces);
        CHECK(!Props::ValidPieces(flung, sign.position));
        flung = pieces;
        flung[0].up = {1.0f, 0.0f, 0.0f};
        CHECK(!Props::ValidPieces(flung, sign.position));
        std::vector<Props::Pose> many(Props::kMaxPieces + 1, piece(0, sign.position));
        CHECK(!Props::ValidPieces(many, sign.position));

        Props::PiecesReport piecesReport;
        piecesReport.missionGeneration = 7;
        piecesReport.sign = true;
        piecesReport.signKey = sign.Key();
        piecesReport.pieces = pieces;
        const auto readPieces = RoundTrip(piecesReport);
        CHECK(readPieces.sign && readPieces.signKey == sign.Key() && readPieces.name.empty() && readPieces.pieces.size() == 2 && readPieces.pieces[1].index == 2 &&
              Near(readPieces.pieces[1].position.z, 615.0f) && Props::ValidPieces(readPieces.pieces, sign.position));
        Props::PiecesReport namedPieces;
        namedPieces.name = "PHbox01 12";
        namedPieces.pieces = pieces;
        CHECK(RoundTrip(namedPieces).name == "PHbox01 12" && RoundTrip(namedPieces).pieces.size() == 2);

        // A snapshot carries each break's pieces.
        Props::Snapshot withPieces;
        withPieces.missionGeneration = 7;
        withPieces.broken.resize(2);
        withPieces.broken[0].name = "kos01 3";
        withPieces.broken[0].pieces = pieces;
        withPieces.broken[1].name = "popel01 9";
        withPieces.signs = {sign};
        withPieces.signs[0].pieces = pieces;
        const auto readWithPieces = RoundTrip(withPieces);
        CHECK(readWithPieces.broken.size() == 2 && readWithPieces.broken[0].pieces.size() == 2 && readWithPieces.broken[1].pieces.empty() &&
              readWithPieces.broken[1].name == "popel01 9");
        CHECK(readWithPieces.signs.size() == 1 && readWithPieces.signs[0].pieces.size() == 2 && Near(readWithPieces.signs[0].pieces[0].position.x, -1521.0f));

        // A struck primitive is named by its kind's array and index.
        std::array<Native::Face, 3> faces {};
        std::array<Native::Cylinder, 2> cylinders {};
        std::array<Native::OrientedBox, 2> boxes {};
        Native::TreeLevel tree {};
        tree.faceCount = static_cast<uint32_t>(faces.size());
        tree.faces = faces.data();
        tree.cylinderCount = static_cast<uint32_t>(cylinders.size());
        tree.cylinders = cylinders.data();
        tree.orientedBoxCount = static_cast<uint32_t>(boxes.size());
        tree.orientedBoxes = boxes.data();
        const auto cylinder = World::IdentifyTreePrimitive(&tree, &cylinders[1].header);
        CHECK(cylinder.kind == World::TreePrimitiveKind::Cylinder && cylinder.index == 1);
        CHECK(World::ResolveTreePrimitive(&tree, cylinder) == reinterpret_cast<uint32_t *>(&cylinders[1].header));
        const auto face = World::IdentifyTreePrimitive(&tree, &faces[2].header);
        CHECK(face.kind == World::TreePrimitiveKind::Face && face.index == 2);
        const auto box = World::IdentifyTreePrimitive(&tree, &boxes[1].header);
        CHECK(box.kind == World::TreePrimitiveKind::OrientedBox && box.index == 1);
        // Inside an element, or another body's memory: none.
        CHECK(World::IdentifyTreePrimitive(&tree, &faces[1].normal).kind == World::TreePrimitiveKind::None);
        Native::Cylinder other {};
        CHECK(World::IdentifyTreePrimitive(&tree, &other.header).kind == World::TreePrimitiveKind::None);
        // An index past this tree's array, or no tree: nothing to turn off.
        CHECK(World::ResolveTreePrimitive(&tree, {World::TreePrimitiveKind::Cylinder, 2}) == nullptr);
        CHECK(World::ResolveTreePrimitive(&tree, {World::TreePrimitiveKind::Sphere, 0}) == nullptr);
        CHECK(World::ResolveTreePrimitive(&tree, {}) == nullptr);
        CHECK(World::ResolveTreePrimitive(nullptr, cylinder) == nullptr);
    }
} // namespace

int main(int argc, char **argv) {
    TestMotion();
    TestPatch();
    TestTemplate();
    TestSigns();
    if (argc == 3) {
        TestFiles(argv[1], argv[2]);
    }
    if (gFailures) {
        std::printf("%d map object checks failed\n", gFailures);
        return 1;
    }
    std::puts("Map object motion, collision patching, templates and sign reports behave as the retail queries expect");
    return 0;
}
