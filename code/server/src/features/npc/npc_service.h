#pragma once

#include "shared/features/combat/combat_protocol.h"

#include <mafianet/types.h>

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace Mafia1Online::Shared::Entities {
    class NpcEntity;
}

namespace Mafia1Online::Features::Simulation {
    class SimulationService;
}
namespace Mafia1Online::Features::Player {
    class PlayerService;
}
namespace Mafia1Online::Features::Combat {
    class CombatService;
}

namespace Mafia1Online::Features::Npc {
    // Server-owned NPC humans (C_entity) the simulation host runs, of every
    // kind. Only the attached host may report them, and only for the current
    // mission generation; one it stops reporting goes after kStaleAfter.
    // Each keeps a Combat::State, which is also its inventory record. Players
    // see each as a native human driven by that state and by Combat::Events,
    // as a remote player is: weapon, aim, shots, reloads, hits and death.
    class NpcService final {
      public:
        static constexpr size_t kMaxNpcs = 256;
        static constexpr std::chrono::seconds kStaleAfter {3};
        // Players see NPCs this far from their position. The host keeps one
        // alive while a player is within 150 of it (TickDoRiciActors).
        static constexpr float kStreamRange = 150.0f;
        // A weapon an NPC drops stays on the ground as long as a player's.
        static constexpr std::chrono::seconds kDropLifetime {120};

        struct Events {
            std::function<void(Shared::Entities::NpcEntity &)> spawn;
            std::function<void(Shared::Entities::NpcEntity &)> retire;
            std::function<void(Shared::Entities::NpcEntity &, uint64_t attackerId, int32_t hitType, float damage, float healthBefore, float healthAfter)> damage;
            std::function<void(Shared::Entities::NpcEntity &, uint64_t killerId)> death;
        };
        // drop(weaponId, loaded, reserve, position, yaw) creates a world
        // pickup and returns its id or 0; dropped(pickupId, npcId) follows.
        struct Drops {
            std::function<uint64_t(uint8_t, uint16_t, uint16_t, const glm::vec3 &, float)> drop;
            std::function<void(uint64_t, uint64_t)> dropped;
        };

        void RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Combat::CombatService &combat);
        void SetEvents(Events events) {
            _events = std::move(events);
        }
        void SetDrops(Drops drops) {
            _drops = std::move(drops);
        }
        void SetMissionGeneration(uint64_t generation);
        void Update();
        // A joining player gets every current state; later revisions follow.
        void OnPlayerConnect(MafiaNet::PeerGuid guid) const;
        // Host gone or mission changed: every NPC of the old world goes.
        void Clear();

        // Asks the host to retire the NPC now; npcRetire follows.
        bool Remove(uint64_t networkId);
        Shared::Entities::NpcEntity *Find(uint64_t networkId) const;
        // Health, aim and the inventory record.
        const Shared::Combat::State *GetState(uint64_t networkId) const;
        void ForEach(const std::function<void(Shared::Entities::NpcEntity &)> &visit) const;
        size_t Count() const {
            return _npcs.size();
        }

      private:
        struct Record {
            Shared::Entities::NpcEntity *entity = nullptr;
            Shared::Combat::State state;
            std::chrono::steady_clock::time_point lastSeen;
            uint64_t lastAttackerId = 0;
        };
        using Iterator = std::unordered_map<uint32_t, Record>::iterator;

        bool FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const;
        Record *FindKey(uint32_t key);
        // Sends the state as a new revision to every player.
        void Publish(Record &npc);
        // An accepted action as a Combat::Event of the current revision.
        void Emit(const Record &npc, Shared::Combat::Event &event);
        void Retire(Iterator npc);

        const Simulation::SimulationService *_simulation = nullptr;
        Player::PlayerService *_players = nullptr;
        Combat::CombatService *_combat = nullptr;
        uint64_t _missionGeneration = 0;
        Events _events;
        Drops _drops;
        std::unordered_map<uint32_t, Record> _npcs;
        std::unordered_map<uint64_t, uint32_t> _keyByNetworkId;
    };
} // namespace Mafia1Online::Features::Npc
