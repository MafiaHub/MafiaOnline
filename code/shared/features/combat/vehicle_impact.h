#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Combat {
    // The car simulation owner observed a body contact with a remote player.
    // The server calculates damage from its own car speed and checks both poses.
    struct VehicleImpact {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::VehicleImpact");

        uint64_t carId = 0;
        uint64_t targetId = 0;
        uint64_t missionGeneration = 0;
        uint64_t targetSpawnGeneration = 0;
        float contactX = 0.0f;
        float contactY = 0.0f;
        float contactZ = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, carId);
            stream->Serialize(write, targetId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, contactX);
            stream->Serialize(write, contactY);
            stream->Serialize(write, contactZ);
        }
    };
} // namespace Mafia1Online::Shared::Combat
