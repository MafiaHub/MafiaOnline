#pragma once

#include <networking/rpc/rpc.h>
#include "shared/features/car/car_mesh_validation.h"

#include <array>
#include <cstdint>

namespace Mafia1Online::Shared::Car {
    template <typename Packet> void SerializeMeshChunk(Packet &packet, MafiaNet::BitStream *stream, bool write) {
        stream->Serialize(write, packet.networkId);
        stream->Serialize(write, packet.missionGeneration);
        stream->Serialize(write, packet.sequence);
        stream->Serialize(write, packet.chunkIndex);
        stream->Serialize(write, packet.chunkCount);
        stream->Serialize(write, packet.totalVertices);
        stream->Serialize(write, packet.vertexCount);
        for (auto &vertex : packet.vertices) {
            stream->Serialize(write, vertex.zone);
            stream->Serialize(write, vertex.lod);
            stream->Serialize(write, vertex.displacement);
        }
    }

    struct MeshReportChunk {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarMeshReportChunk");
        uint64_t networkId = 0;
        uint64_t missionGeneration = 0;
        uint64_t baseRepairRevision = 0;
        uint32_t sequence = 0;
        uint16_t chunkIndex = 0;
        uint16_t chunkCount = 0;
        uint16_t totalVertices = 0;
        uint16_t vertexCount = 0;
        std::array<MeshVertex, kMeshVerticesPerChunk> vertices {};
        void Serialize(MafiaNet::BitStream *stream, bool write) {
            SerializeMeshChunk(*this, stream, write);
            stream->Serialize(write, baseRepairRevision);
        }
    };

    struct MeshCheckpointChunk {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarMeshCheckpointChunk");
        uint64_t networkId = 0;
        uint64_t missionGeneration = 0;
        uint32_t sequence = 0; // low bits of the server revision
        uint16_t chunkIndex = 0;
        uint16_t chunkCount = 0;
        uint16_t totalVertices = 0;
        uint16_t vertexCount = 0;
        std::array<MeshVertex, kMeshVerticesPerChunk> vertices {};
        uint64_t revision = 0;
        uint64_t sourceGuid = 0;
        void Serialize(MafiaNet::BitStream *stream, bool write) {
            SerializeMeshChunk(*this, stream, write);
            stream->Serialize(write, revision);
            stream->Serialize(write, sourceGuid);
        }
    };

    struct MeshCheckpointRequest {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarMeshCheckpointRequest");
        uint64_t networkId = 0;
        uint64_t missionGeneration = 0;
        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
        }
    };
} // namespace Mafia1Online::Shared::Car
