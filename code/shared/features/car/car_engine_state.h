#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Car {
    struct EngineState {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarEngineState");

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint32_t sequence          = 0;
        bool on                    = false;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, sequence);
            stream->Serialize(write, on);
        }
    };
} // namespace Mafia1Online::Shared::Car
