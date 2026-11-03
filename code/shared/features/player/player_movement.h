#pragma once

#include "shared/net/quantize.h"

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Player {
    // The controller's pose, unreliable at 20 Hz. About 23 bytes on foot
    // (47 before): ids compressed, position at 1/64 m, yaw in 12 bits, the
    // ride only while aboard.
    struct Movement {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PlayerMovement");

        uint64_t networkId = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration = 0;
        uint32_t sequence = 0;
        // When the controller sampled this pose (Shared::Net::SampleClock).
        uint16_t clockMs = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        // The controller's native locomotion state (C_human m_iAnimState).
        int16_t locomotion = 1;
        // Aboard a tram or metro train (Shared::Railway), or kNoRailway: the
        // railway index, the frame the rider is linked to (0 body, n wagon
        // n - 1, C_human +0x584) and the position and yaw in that frame.
        uint8_t railway = 0xff;
        uint8_t railwayPart = 0;
        float railwayX = 0.0f;
        float railwayY = 0.0f;
        float railwayZ = 0.0f;
        float railwayYaw = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->SerializeCompressed(write, networkId);
            stream->SerializeCompressed(write, missionGeneration);
            stream->SerializeCompressed(write, spawnGeneration);
            stream->Serialize(write, sequence);
            stream->Serialize(write, clockMs);
            glm::vec3 position {x, y, z};
            Net::SerializeWorldPosition(stream, write, position);
            if (!write) {
                x = position.x;
                y = position.y;
                z = position.z;
            }
            Net::SerializeAngle(stream, write, yaw, 12);
            stream->Serialize(write, locomotion);
            stream->Serialize(write, railway);
            if (railway != 0xff) {
                stream->Serialize(write, railwayPart);
                Net::SerializeFixed(stream, write, railwayX, -32.0f, 32.0f, 16);
                Net::SerializeFixed(stream, write, railwayY, -32.0f, 32.0f, 16);
                Net::SerializeFixed(stream, write, railwayZ, -32.0f, 32.0f, 16);
                Net::SerializeAngle(stream, write, railwayYaw, 10);
            }
        }
    };
} // namespace Mafia1Online::Shared::Player
