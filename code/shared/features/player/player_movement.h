#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Player {
    struct Movement {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PlayerMovement");

        uint64_t networkId = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration = 0;
        uint32_t sequence = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        // The controller's native locomotion state (C_human m_iAnimState).
        int16_t locomotion = 1;
        // Aboard a tram or metro train (Shared::Railway), or kNoRailway: the
        // railway index, its body (0) or wagon, and the position and yaw in
        // that part's basis (Railway::kNoRailway documents it).
        uint8_t railway = 0xff;
        uint8_t railwayPart = 0;
        float railwayX = 0.0f;
        float railwayY = 0.0f;
        float railwayZ = 0.0f;
        float railwayYaw = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
            stream->Serialize(write, sequence);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, yaw);
            stream->Serialize(write, locomotion);
            stream->Serialize(write, railway);
            if (railway != 0xff) {
                stream->Serialize(write, railwayPart);
                stream->Serialize(write, railwayX);
                stream->Serialize(write, railwayY);
                stream->Serialize(write, railwayZ);
                stream->Serialize(write, railwayYaw);
            }
        }
    };
} // namespace Mafia1Online::Shared::Player
