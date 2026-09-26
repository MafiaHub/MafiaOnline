#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Car {
    struct SirenIntent {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarSirenIntent");

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration   = 0;
        bool on                    = false;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
            stream->Serialize(write, on);
        }
    };
} // namespace Mafia1Online::Shared::Car
