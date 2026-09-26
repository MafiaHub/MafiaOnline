#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Player {
    // Sent by the controlling client only to observers currently following it.
    // Pitch is a fraction of pi; yaw is radians around the vertical axis.
    struct CameraView {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PlayerCameraView");

        uint64_t networkId = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration = 0;
        uint32_t sequence = 0;
        float pitch = 0.5f;
        float yaw = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
            stream->Serialize(write, sequence);
            stream->Serialize(write, pitch);
            stream->Serialize(write, yaw);
        }
    };
} // namespace Mafia1Online::Shared::Player
