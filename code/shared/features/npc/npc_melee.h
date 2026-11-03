#pragma once

#include "shared/features/combat/combat_protocol.h"

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Npc {
    // Host -> server: one of its NPCs pressed, released or let go of a melee
    // attack in C_human::Do_Shoot (the AI calls it through
    // C_entity::Do_Shoot_Timed 0x528680, 0x52872d/0x52873d), read the way
    // the local player's swing is. The server relays it as a
    // Combat::Event, which players replay on the NPC's replica.
    struct Melee {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcMelee");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        // Combat::Action::MeleeStart, Melee or MeleeCancel.
        Combat::Action action = Combat::Action::MeleeStart;
        uint8_t weaponId = 0;
        // Melee: the combo index 0..3, or Combat::kHeavyMelee.
        int8_t meleeIndex = 0;
        uint16_t holdMs = 0;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;
        // When the host's AI attacked, on the clock its poses are sampled by
        // (Shared::Net::SampleClock): players replay it once the NPC's
        // replica, drawn one render delay behind, gets there.
        uint16_t hostClockMs = 0;
        // The player (network id) its AI fights, else 0: that player's
        // client closes the replica to striking range for the swing.
        uint64_t targetNetworkId = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto actionValue = static_cast<uint8_t>(action);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, actionValue);
            stream->Serialize(write, weaponId);
            stream->Serialize(write, meleeIndex);
            stream->Serialize(write, holdMs);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
            stream->Serialize(write, hostClockMs);
            stream->Serialize(write, targetNetworkId);
            if (!write) {
                action = static_cast<Combat::Action>(actionValue);
            }
        }
    };
} // namespace Mafia1Online::Shared::Npc
