#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Car {
    // The simulation controller's native car entered water or a fall volume.
    // State uses CarEntity::TerminalState: 2 submerged, 3 out of bounds.
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
