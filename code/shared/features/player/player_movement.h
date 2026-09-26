#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Player {
    struct Movement {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PlayerMovement");

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
        }
    };
} // namespace Mafia1Online::Shared::Player
