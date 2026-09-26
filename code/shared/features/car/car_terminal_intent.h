#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Car {
    // The simulation controller observed a native body contact with material
    // 31 (water) or 40 (fall volume), or the retail invalid-fall condition.
    // State uses CarEntity::TerminalState: 2 water, 3 fall/invalid.
    struct TerminalIntent {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarTerminalIntent");

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint8_t state              = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, state);
        }
    };
} // namespace Mafia1Online::Shared::Car
