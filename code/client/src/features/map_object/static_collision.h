#pragma once

#include <mafia1/sdk/collision/native_collision_tree.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::Features::MapObject {
    // One object's collision in world space, in the static tree's own
    // primitive types. Matrices here are glm column-vector transforms.
    struct CollisionShape {
        struct Triangle {
            glm::vec3 a, b, c;
            uint8_t material = 0;
            uint8_t flags    = 0;
        };
        // An oriented box (XTOBB): local [-halfExtent, halfExtent].
        struct Box {
            glm::mat4 transform {1.0f}; // rigid
            glm::vec3 halfExtent {0.0f};
            uint8_t material = 0;
            uint8_t flags    = 0;
        };
        // An OBB: a quad at local y = 0, x and z within halfExtent; retail
        // gives every plate a local y range of +-0.1.
        struct Plate {
            glm::mat4 transform {1.0f}; // rigid
            glm::vec3 halfExtent {0.0f};
            uint8_t material = 0;
            uint8_t flags    = 0;
        };
        struct Ball {
            glm::vec3 center {0.0f};
            float radius     = 0.0f;
            uint8_t material = 0;
            uint8_t flags    = 0;
        };
        // A vertical cylinder; queries ignore y, the grid uses topY.
        struct Post {
            glm::vec2 centerXZ {0.0f};
            float radius     = 0.0f;
            float topY       = 0.0f;
            uint8_t material = 0;
            uint8_t flags    = 0;
        };

        std::vector<Triangle> triangles;
        std::vector<Box> boxes;
        std::vector<Plate> plates;
        std::vector<Ball> balls;
        std::vector<Post> posts;

        bool Empty() const {
            return triangles.empty() && boxes.empty() && plates.empty() && balls.empty() && posts.empty();
        }
    };

    // What a loaded retail tree shows of the layout the patches rely on,
    // logged once per tree: vertices sorted by x, the winding bit, faces after
    // volumes and ascending by vertex 0 x within a cell.
    struct TreeCheck {
        size_t faces             = 0;
        size_t unsortedVertices  = 0;
        size_t windingMismatches = 0;
        size_t cellFaces         = 0;
        size_t facesOutOfOrder   = 0;
        size_t volumesAfterFaces = 0;
    };
    TreeCheck CheckTree(const SDK::Collision::TreeLevel &tree);

    // Adds script objects' collision to the loaded static tree by giving the
    // grid cells they overlap a reference list of their own: the cell's own
    // references with the object's primitives merged in, in the order the
    // retail cell loops need. Every collision query (on foot, cars, bullets,
    // AI) then finds them as it finds the mission's own geometry.
    //
    // The tree's memory stays untouched; patched cells point at lists this
    // class owns and get their own fields back on Detach. Primitive memory
    // never moves while a body lives (a move rewrites it in place), and a
    // removed body's primitives are disabled and kept until the next tree
    // attaches, because humans, wheels and shots keep the last primitive they
    // touched across frames.
    class StaticCollision final {
      public:
        StaticCollision() = default;
        ~StaticCollision();
        StaticCollision(const StaticCollision &)            = delete;
        StaticCollision &operator=(const StaticCollision &) = delete;

        // A tree became current: links every body into it and frees what
        // earlier trees' bodies left behind.
        void Attach(SDK::Collision::TreeLevel *tree);
        // Before the tree is freed: restores the cells and forgets the tree.
        // Bodies stay and link into the next tree that attaches.
        void Detach();
        SDK::Collision::TreeLevel *Tree() const {
            return _tree;
        }

        // frame is stored as each volume's frame link; retail links free
        // volumes to the primary sector. Returns 0 for an empty shape.
        uint32_t Add(const CollisionShape &shape, void *frame);
        // Moves a body; same primitive counts rewrite it in place.
        void Update(uint32_t body, const CollisionShape &shape);
        void Remove(uint32_t body);
        // Drops every body and the retired memory. Detach first when a tree
        // is still attached.
        void Clear();

        size_t BodyCount() const {
            return _bodies.size();
        }
        // Primitives of this body that lie in at least one cell.
        size_t LinkedPrimitives(uint32_t body) const;
        size_t PatchedCellCount() const {
            return _cells.size();
        }
        size_t RetiredBytes() const {
            return _retiredBytes;
        }

      private:
        struct Storage {
            std::unique_ptr<SDK::Collision::Face[]> faces;
            std::unique_ptr<SDK::Collision::Vector[]> vertices;
            std::unique_ptr<SDK::Collision::OrientedBox[]> boxes;
            std::unique_ptr<SDK::Collision::Plate[]> plates;
            std::unique_ptr<SDK::Collision::Sphere[]> balls;
            std::unique_ptr<SDK::Collision::Cylinder[]> posts;
            size_t faceCount  = 0;
            size_t boxCount   = 0;
            size_t plateCount = 0;
            size_t ballCount  = 0;
            size_t postCount  = 0;
            size_t Bytes() const;
            void Disable();
        };
        // One primitive's reference in one cell.
        struct CellRef {
            int32_t cell;
            SDK::Collision::Header *volume;
            float key; // a face's vertex 0 x
            float maxY;
            uint8_t neighbours;
            bool face;
        };
        struct Body {
            Storage storage;
            void *frame = nullptr;
            std::vector<CellRef> refs;
            // Per primitive in storage order: the xz rectangle and top y.
            struct Extent {
                SDK::Collision::Header *volume;
                float minX, minZ, maxX, maxZ, maxY, key;
                bool face;
            };
            std::vector<Extent> extents;
        };
        struct CellPatch {
            SDK::Collision::GridCell *cell = nullptr;
            uint32_t count                 = 0;
            SDK::Collision::Header **volumes = nullptr;
            uint8_t *neighbours              = nullptr;
            float maxY                       = 0.0f;
            std::vector<SDK::Collision::Header *> mergedVolumes;
            std::vector<uint8_t> mergedNeighbours;
            std::vector<uint32_t> bodies;
        };

        static Storage Allocate(const CollisionShape &shape);
        static void Write(Storage &storage, const CollisionShape &shape, void *frame, std::vector<Body::Extent> &extents);
        static bool SameCounts(const Storage &storage, const CollisionShape &shape);
        void Link(uint32_t id, Body &body, std::vector<int32_t> &touched);
        void Unlink(uint32_t id, Body &body, std::vector<int32_t> &touched);
        void Rebuild(int32_t cell);
        void Retire(Storage storage);

        SDK::Collision::TreeLevel *_tree = nullptr;
        uint32_t _nextBody               = 1;
        std::unordered_map<uint32_t, Body> _bodies;
        std::unordered_map<int32_t, CellPatch> _cells;
        std::vector<Storage> _retired;
        size_t _retiredBytes = 0;
    };
} // namespace Mafia1Online::Features::MapObject
