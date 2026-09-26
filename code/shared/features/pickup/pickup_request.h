#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Pickup {
    // The local player pressed use on a replicated pickup. The server checks
    // reach, life and inventory before moving the weapon into it.
    struct PickupRequest {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PickupRequest");

        uint64_t pickupId          = 0;
        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration   = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, pickupId);
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
        }
    };
} // namespace Mafia1Online::Shared::Pickup
