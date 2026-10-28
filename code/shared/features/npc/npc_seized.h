#pragma once

#include "shared/features/npc/npc_report.h"

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Npc {
    // Host -> server -> every player but the host: a police officer started
    // the arrest or fine animations on a player's replica (police action
    // state 11, IDA 0x5171c1-0x51721c). Retail plays the target's half on its
    // own player; the arrested player's client plays it on its ped with the
    // controls locked, every other client on that player's replica. The host
    // already plays it natively and reports Caught once both halves end.
    struct Seized {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcSeized");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t targetNetworkId = 0;
        uint64_t targetSpawnGeneration = 0;
        Caught::Outcome outcome = Caught::Outcome::Arrest;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto outcomeValue = static_cast<uint8_t>(outcome);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, targetNetworkId);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, outcomeValue);
            if (!write) {
                outcome = static_cast<Caught::Outcome>(outcomeValue);
            }
        }
    };

    // The target's animation: PolicajtZatykany for the arrest (0x517215),
    // PolicajtPokutovany for the fine (0x5171f0).
    inline const char *SeizedAnimation(Caught::Outcome outcome) {
        return outcome == Caught::Outcome::Fine ? "PolicajtPokutovany.I3D" : "PolicajtZatykany.I3D";
    }
} // namespace Mafia1Online::Shared::Npc
