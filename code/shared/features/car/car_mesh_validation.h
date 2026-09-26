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
} // namespace Mafia1Online::Shared::Car
