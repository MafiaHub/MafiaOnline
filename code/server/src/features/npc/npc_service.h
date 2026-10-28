#pragma once

#include "shared/features/combat/combat_protocol.h"
#include "shared/features/npc/npc_create.h"
#include "shared/features/npc/npc_order.h"
#include "shared/features/npc/npc_task.h"
#include "shared/features/npc/npc_task_done.h"

#include <mafianet/types.h>
#include <mafianet/virtual_world.h>

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace Mafia1Online::Shared::Entities {
    class NpcEntity;
}
namespace Mafia1Online::Shared::Npc {
    struct CarHit;
    struct Kick;
    struct Seat;
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
        // Script-spawned NPCs (Npc.spawn): at most this many at once. One the
        // host has not confirmed within kPendingTimeout goes; a dead one is
        // removed kCorpseLifetime after its death.
        static constexpr size_t kMaxScripted = 64;
        static constexpr std::chrono::seconds kPendingTimeout {15};
        static constexpr std::chrono::seconds kCorpseLifetime {60};

        struct Events {
            std::function<void(Shared::Entities::NpcEntity &)> spawn;
            std::function<void(Shared::Entities::NpcEntity &)> retire;
            std::function<void(Shared::Entities::NpcEntity &, uint64_t attackerId, int32_t hitType, float damage, float healthBefore, float healthAfter)> damage;
            std::function<void(Shared::Entities::NpcEntity &, uint64_t killerId)> death;
            // A police officer arrested (or, fine, fined) a player. The host
            // never ends the game for it.
            std::function<void(Shared::Entities::NpcEntity &, uint64_t playerId, bool fine)> caught;
            // A scripted NPC's task ended on the host.
            std::function<void(Shared::Entities::NpcEntity &, uint32_t taskId, Shared::Npc::TaskType, Shared::Npc::TaskDone::Result)> taskDone;
        };
        // The cars scripted NPCs board: claim(carId) makes the simulation
        // host drive it (false when it cannot: a traffic car, a player in
        // it), release(carId) gives it back to the players, seatedCar(npcId)
        // is the car the NPC sits in, or 0.
        struct Vehicles {
            std::function<bool(uint64_t)> claim;
            std::function<void(uint64_t)> release;
            std::function<uint64_t(uint64_t)> seatedCar;
        };
        struct SpawnRequest {
            std::string model;
            glm::vec3 position {0.0f};
            float heading = 0.0f;
            float health = 100.0f;
            Shared::Npc::Behaviour behaviour = Shared::Npc::Behaviour::Neutral;
            uint8_t weaponId = 0;
            uint16_t loaded = 0;
            uint16_t reserve = 0;
            // Players see it only in this virtual world (GLOBAL: in every
            // one). The host runs it in its single retail world regardless.
            uint32_t virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
        };
        struct CurrentTask {
            uint32_t id = 0;
            Shared::Npc::TaskType type = Shared::Npc::TaskType::Stop;
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
        void SetVehicles(Vehicles vehicles) {
            _vehicles = std::move(vehicles);
        }
        // walkerId(key): network id of the host's reported walker, or 0.
        void SetWalkerLookup(std::function<uint64_t(uint32_t)> walkerId) {
            _walkerId = std::move(walkerId);
        }
        // kick(npcId, position, kick): it threw a player out of a car seat.
        void SetKick(std::function<void(uint64_t, const glm::vec3 &, const Shared::Npc::Kick &)> kick) {
            _kick = std::move(kick);
        }
        // seat(npcId, seat): it sat down in or left a car the host drives.
        void SetSeat(std::function<void(uint64_t, const Shared::Npc::Seat &)> seat) {
            _seat = std::move(seat);
        }
        // carHit(npcId, hit): one of its bullets hit a player's car body.
        void SetCarHit(std::function<void(uint64_t, const Shared::Npc::CarHit &)> carHit) {
            _carHit = std::move(carHit);
        }
        void SetMissionGeneration(uint64_t generation);
        void Update();
        // A joining player gets every current state; later revisions follow.
        void OnPlayerConnect(MafiaNet::PeerGuid guid) const;
        // Host gone or mission changed: every NPC of the old world goes.
        void Clear();

        // Asks the host to retire the NPC now; npcRetire follows.
        bool Remove(uint64_t networkId);
        // A scripted NPC: the entity exists at once, the host creates the
        // retail C_entity and npcSpawn follows its first report. Null when
        // no host is attached, no mission is loaded or the limit is reached.
        Shared::Entities::NpcEntity *Spawn(const SpawnRequest &request);
        // Gives a scripted NPC a task (targetId: a player, NPC or car network
        // id); the task id, or 0 when refused. It replaces the current one.
        uint32_t GiveTask(uint64_t networkId, Shared::Npc::Task task);
        bool SendOrder(uint64_t networkId, Shared::Npc::Order order);
        bool IsScripted(uint64_t networkId) const;
        std::optional<Shared::Npc::Behaviour> GetBehaviour(uint64_t networkId) const;
        // The task still running, if any.
        std::optional<CurrentTask> GetTask(uint64_t networkId) const;
        // A host NPC's seat changed (it may leave a claimed car).
        void OnSeatChanged(uint64_t networkId);
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
            // Script-spawned; confirmed once the host reported it.
            bool scripted = false;
            bool confirmed = false;
            Shared::Npc::Behaviour behaviour = Shared::Npc::Behaviour::Neutral;
            std::chrono::steady_clock::time_point createdAt;
            std::chrono::steady_clock::time_point diedAt;
            bool removeSent = false;
            CurrentTask task;
            bool taskRunning = false;
            // The car the host drives for this NPC (enterVehicle), or 0.
            uint64_t claimedCar = 0;
        };
        using Iterator = std::unordered_map<uint32_t, Record>::iterator;

        bool FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const;
        Record *FindKey(uint32_t key);
        // Sends the state as a new revision to every player.
        void Publish(Record &npc);
        // An accepted action as a Combat::Event of the current revision.
        void Emit(const Record &npc, Shared::Combat::Event &event);
        void Retire(Iterator npc);
        // Gives a claimed car back once the NPC neither boards nor sits in it.
        void ReleaseIfUnused(Record &npc);
        void ReleaseClaim(Record &npc);
        // Gives the car back when no NPC claims it any more.
        void ReleaseCar(uint64_t carId);
        Record *FindNetwork(uint64_t networkId);
        const Record *FindNetwork(uint64_t networkId) const;

        const Simulation::SimulationService *_simulation = nullptr;
        Player::PlayerService *_players = nullptr;
        Combat::CombatService *_combat = nullptr;
        uint64_t _missionGeneration = 0;
        Events _events;
        Drops _drops;
        Vehicles _vehicles;
        uint32_t _nextScriptedKey = 1;
        uint32_t _nextTaskId = 1;
        std::function<void(uint64_t, const Shared::Npc::CarHit &)> _carHit;
        std::function<uint64_t(uint32_t)> _walkerId;
        std::function<void(uint64_t, const glm::vec3 &, const Shared::Npc::Kick &)> _kick;
        std::function<void(uint64_t, const Shared::Npc::Seat &)> _seat;
        std::unordered_map<uint32_t, Record> _npcs;
        std::unordered_map<uint64_t, uint32_t> _keyByNetworkId;
    };
} // namespace Mafia1Online::Features::Npc
