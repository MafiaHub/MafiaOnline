#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Player {
    // The selected retail mission initially creates the local stock player.
    // This reports its position as a suggestion for server script spawning;
    // the server still decides whether and where to spawn the player.
    struct NativePose {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PlayerNativePose");

        uint64_t networkId = 0;
        uint64_t missionGeneration = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, yaw);
        }
    };
} // namespace Mafia1Online::Shared::Player
