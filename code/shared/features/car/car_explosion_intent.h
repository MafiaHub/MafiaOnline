#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Car {
    struct ExplosionIntent {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarExplosionIntent");

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
        }
    };
} // namespace Mafia1Online::Shared::Car
