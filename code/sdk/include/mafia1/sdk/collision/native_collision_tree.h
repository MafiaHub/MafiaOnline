#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Collision {
    // g_collision's static tree, verified against reM
    // projects/Game/src/collision/g_collision.{h,cpp} (byte-matched to the
    // retail Game.exe) and IDA. The tree is one allocation LoadCollision
    // (0x5c2b70) reads from missions\<name>\tree.klz, plus the cell pointer
    // table; DeleteCollsLevel (0x5c28d0) frees both. Every free goes through
    // DeleteCollsLevel: LoadCollision calls it first (0x5c2b82) and
    // g_collision::GameDone (0x5d5900, from C_game::Done 0x5a49d2) calls it
    // after DynWipeOut. It is __thiscall with no arguments (plain retn).
    inline constexpr uintptr_t kDeleteCollsLevel = 0x5C28D0;
    inline constexpr uintptr_t kTreeLevelOffset  = 0x284; // g_collision::m_pTreeLevel

    struct Vector {
        float x;
        float y;
        float z;
    };

    // g_collision_header: one packed word the static filters test whole
    // (volume->packed & filter). Bullets skip flags 0x20 and 0x40
    // (COLLISION_STATIC_FILTER_BULLET 0x600000).
    struct Header {
        uint8_t type;
        // Faces: bit 0 is set when the stored vertex order (sorted by x)
        // winds positively about the stored normal; SphereInFace (0x5cc470)
        // then inverts its edge tests. The tools set it on about half the
        // faces of the stock trees.
        uint8_t sortInfo;
        uint8_t flags;
        uint8_t material;
    };
    static_assert(sizeof(Header) == 4);

    enum VolumeType : uint8_t {
        kFaceUnoptimized       = 0x00,
        kFaceXConstantY        = 0x01,
        kFaceX                 = 0x02,
        kFaceYConstantX        = 0x03,
        kFaceYConstantZ        = 0x04,
        kFaceY                 = 0x05,
        kFaceZConstantX        = 0x06,
        kFaceZ                 = 0x07,
        // Faces and volumes alike: no query matches it (the type has the
        // 0xa0 non-face bits and no volume case).
        kDisabled              = 0x20,
        kOrientedBox           = 0x80, // XTOBB
        kAxisBox               = 0x81, // AABB
        kSphere                = 0x82,
        kPlate                 = 0x83, // OBB: a quad at local y = 0
        kCylinder              = 0x84, // vertical, unbounded in y
    };
    inline constexpr uint8_t kNonFaceTypeMask = 0xa0;
    inline constexpr uint8_t kFaceSortPositiveWinding = 0x01;

    // g_volume_triface. The stored normal points into the solid: lines hit
    // it only travelling along the normal (LineInGridCell 0x5c66a0) and
    // spheres only from its negative side (SphereInFace 0x5cc470). The
    // vertex pointers are world positions. The per-cell loops skip a face
    // whose vertex 2 x lies before the query and stop at one whose vertex 0
    // x lies past it, so they take vertex 0 as the smallest x and vertex 2
    // as the largest (the client logs CheckTree's count of exceptions).
    struct Face {
        Header header;
        const Vector *vertices[3];
        Vector normal;
        float d; // normal . p + d = 0
    };

    struct AxisBox {
        Header header;
        void *frame;
        Vector min;
        Vector max;
    };

    // Matrices are LS3D row-major, transforming row vectors (v * M), with
    // the translation in row 3.
    struct OrientedBox {
        Header header;
        void *frame;
        Vector min; // world bounds of the transformed extents
        Vector max;
        Vector minExtent;
        Vector maxExtent;
        float transform[16];
        float inverse[16];
    };

    struct Sphere {
        Header header;
        void *frame;
        Vector center;
        float radius;
    };

    struct Plate {
        Header header;
        void *frame;
        Vector minExtent;
        Vector maxExtent;
        float transform[16];
        float inverse[16];
    };

    struct Cylinder {
        Header header;
        void *frame;
        float x;
        float z;
        float radius;
    };

    // tCollGridCell. Non-face volumes come first, then faces ascending by
    // vertex 0 x: the per-cell loops stop at the first face whose vertex 0
    // lies past the query. neighbours holds, per reference, which adjacent
    // cells also list it (0x1 -z, 0x2 -x, 0x4 +z, 0x8 +x); nothing reads it.
    // maxY is the highest y of the cell's volumes, -10000 when empty; the
    // grid walks skip a cell whose maxY is below the query.
    struct GridCell {
        uint32_t count;
        Header **volumes;
        uint8_t *neighbours;
        float maxY;
    };

    // tCollTreeLevel. Cells are uniform: cell (x, z) spans
    // worldMin + index * cellSize on each axis.
    struct TreeLevel {
        float worldMinX;
        float worldMinZ;
        float worldMaxX;
        float worldMaxZ;
        float cellSizeX;
        float cellSizeZ;
        int32_t width;
        int32_t length;
        float dynamicCellSize;
        float *boundsX;
        float *boundsZ;
        GridCell **cells;
        uint32_t faceCount;
        Face *faces;
        uint32_t orientedBoxCount;
        OrientedBox *orientedBoxes;
        uint32_t axisBoxCount;
        AxisBox *axisBoxes;
        uint32_t sphereCount;
        Sphere *spheres;
        uint32_t plateCount;
        Plate *plates;
        uint32_t cylinderCount;
        Cylinder *cylinders;
        uint32_t standaloneVertexCount;
        Vector *standaloneVertices;

        GridCell *Cell(int32_t x, int32_t z) const {
            return cells[x + z * width];
        }
    };

    // The retail layout is the 32-bit one.
    static_assert(sizeof(void *) != 4 || sizeof(Face) == 0x20);
    static_assert(sizeof(void *) != 4 || sizeof(AxisBox) == 0x20);
    static_assert(sizeof(void *) != 4 || sizeof(OrientedBox) == 0xb8);
    static_assert(sizeof(void *) != 4 || sizeof(Sphere) == 0x18);
    static_assert(sizeof(void *) != 4 || sizeof(Plate) == 0xa0);
    static_assert(sizeof(void *) != 4 || sizeof(Cylinder) == 0x14);
    static_assert(sizeof(void *) != 4 || sizeof(GridCell) == 0x10);
    static_assert(sizeof(void *) != 4 || sizeof(TreeLevel) == 0x68);
    static_assert(sizeof(void *) != 4 || offsetof(OrientedBox, transform) == 0x38);
    static_assert(sizeof(void *) != 4 || offsetof(Plate, transform) == 0x20);
    static_assert(sizeof(void *) != 4 || offsetof(TreeLevel, cells) == 0x2c);
    static_assert(sizeof(void *) != 4 || offsetof(TreeLevel, faceCount) == 0x30);
    static_assert(sizeof(void *) != 4 || offsetof(TreeLevel, standaloneVertices) == 0x64);

    // The loaded tree, or null between missions.
    inline TreeLevel *LoadedTree(uintptr_t collision) {
        return *reinterpret_cast<TreeLevel **>(collision + kTreeLevelOffset);
    }
} // namespace Mafia1Online::SDK::Collision
