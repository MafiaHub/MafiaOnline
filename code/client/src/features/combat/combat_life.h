#pragma once

#include "shared/features/npc/npc_entity.h"
#include "shared/features/player/player_entity.h"

#include <networking/replication/replication_manager.h>

#include <cstdint>
#include <optional>

namespace Mafia1Online::Features::Combat {
    // The replicated life a Combat::State belongs to: a player's spawn, or a
    // simulation host NPC, which has one life and is always spawned.
    struct CombatLife {
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration   = 0;
        bool spawned               = false;
        // An NPC's death reaches its entity and its Combat::State on separate
        // channels: either one is final.
        bool killed = false;
    };

    inline std::optional<CombatLife> FindCombatLife(Framework::Networking::Replication::ReplicationManager &replication, uint64_t networkId) {
        if (auto *player = replication.GetEntity<Shared::Entities::PlayerEntity>(networkId)) {
            return CombatLife {player->missionGeneration, player->spawnGeneration, player->spawned};
        }
        if (auto *npc = replication.GetEntity<Shared::Entities::NpcEntity>(networkId)) {
            return CombatLife {npc->missionGeneration, Shared::Entities::NpcEntity::kSpawnGeneration, true, !npc->alive};
        }
        return std::nullopt;
    }
} // namespace Mafia1Online::Features::Combat
