#include "shared/features/car/car_mesh_validation.h"

#include <array>
#include <cstdio>

using namespace Mafia1Online::Shared::Car;

int main() {
    if (!ValidateMeshChunkShape(0, 0, 1, 0) ||
        !ValidateMeshChunkShape(65, 0, 2, 64) ||
        !ValidateMeshChunkShape(65, 1, 2, 1)) {
        return 1;
    }
    if (ValidateMeshChunkShape(65, 2, 2, 0) ||
        ValidateMeshChunkShape(65, 1, 2, 64) ||
        ValidateMeshChunkShape(65, 0, 1, 64) ||
        ValidateMeshChunkShape(kMaxMeshVertices + 1, 0, 1, 0)) {
        return 2;
    }
    const std::array<MeshVertex, 2> valid {{{0, 0, 7U << 18}, {0, 1, 7U << 18}}};
    if (!ValidateMeshVertexSet(valid, 1)) {
        return 3;
    }
    const std::array<MeshVertex, 2> duplicate {{{0, 0, 7U << 18}, {0, 0, (7U << 18) | 1U}}};
    if (ValidateMeshVertexSet(duplicate, 1)) {
        return 4;
    }
    const std::array<MeshVertex, 1> badZone {{{1, 0, 0}}};
    const std::array<MeshVertex, 1> badLod {{{0, 2, 0}}};
    if (ValidateMeshVertexSet(badZone, 1) || ValidateMeshVertexSet(badLod, 1)) {
        return 5;
    }
    // A pushed revision ahead of the replicated meshRevision, or behind a
    // newer one in flight, is wanted; a duplicate, an older one or one from
    // before a repair is not.
    if (!WantsMeshCheckpoint(8, 1, 1, 5, 6, 0) || !WantsMeshCheckpoint(8, 1, 1, 5, 6, 8) || !WantsMeshCheckpoint(9, 2, 1, 5, 6, 0)) {
        return 6;
    }
    if (WantsMeshCheckpoint(0, 1, 1, 0, 0, 0) || WantsMeshCheckpoint(5, 1, 1, 5, 0, 0) || WantsMeshCheckpoint(6, 1, 1, 5, 6, 0) ||
        WantsMeshCheckpoint(7, 1, 1, 5, 6, 8) || WantsMeshCheckpoint(9, 0, 1, 5, 6, 0)) {
        return 7;
    }
    std::puts("Mesh checkpoint bounds and duplicate validation passed");
    return 0;
}
