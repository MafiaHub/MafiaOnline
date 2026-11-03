#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <unordered_set>

namespace Mafia1Online::Shared::Car {
    struct MeshVertex {
        uint8_t zone = 0;
        uint8_t lod = 0;
        // Retail version-9 vehicle state: vertex index in bits 18..31,
        // signed six-bit x/y/z displacement components in bits 12/6/0.
        uint32_t displacement = 0;
        bool operator==(const MeshVertex &) const = default;
    };

    inline constexpr uint16_t kMeshVerticesPerChunk = 64;
    inline constexpr uint16_t kMaxMeshVertices = 8192;
    inline constexpr uint16_t kMaxMeshChunks = kMaxMeshVertices / kMeshVerticesPerChunk;

    inline bool ValidateMeshChunkShape(uint16_t total, uint16_t index, uint16_t count, uint16_t payloadCount) {
        if (total > kMaxMeshVertices || count == 0 || count > kMaxMeshChunks) { return false; }
        const uint16_t expectedCount = std::max<uint16_t>(1, static_cast<uint16_t>((total + kMeshVerticesPerChunk - 1) / kMeshVerticesPerChunk));
        if (count != expectedCount || index >= count) { return false; }
        const unsigned offset = static_cast<unsigned>(index) * kMeshVerticesPerChunk;
        return payloadCount == std::min<unsigned>(kMeshVerticesPerChunk, static_cast<unsigned>(total) - offset);
    }

    inline bool ValidateMeshVertexSet(std::span<const MeshVertex> vertices, uint8_t zoneCount) {
        if (vertices.size() > kMaxMeshVertices) { return false; }
        std::unordered_set<uint32_t> seen;
        seen.reserve(vertices.size());
        for (const auto &vertex : vertices) {
            if (vertex.zone >= zoneCount || vertex.lod > 1) { return false; }
            const uint32_t key = (static_cast<uint32_t>(vertex.zone) << 15) |
                                 (static_cast<uint32_t>(vertex.lod) << 14) |
                                 (vertex.displacement >> 18);
            if (!seen.insert(key).second) { return false; }
        }
        return true;
    }

    // Whether a receiver assembles a checkpoint chunk of `revision`, taken
    // against repair `repairRevision`, beside the car's `currentRepair` and
    // the newest revisions it applied, assembled and is assembling. Any
    // newer one is wanted, not only the one the car's replicated
    // meshRevision names: the server pushes each revision before that field
    // follows, and in a pile-up a newer revision is always in flight. One
    // from before a repair is not.
    inline bool WantsMeshCheckpoint(uint64_t revision, uint64_t repairRevision, uint64_t currentRepair, uint64_t applied, uint64_t assembled,
                                    uint64_t assembling) {
        return revision != 0 && repairRevision >= currentRepair && revision > applied && revision > assembled && revision >= assembling;
    }
} // namespace Mafia1Online::Shared::Car
