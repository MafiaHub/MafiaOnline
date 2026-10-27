#pragma once

#include "shared/features/npc/npc_entity.h"
#include "shared/features/npc/npc_report.h"

#include <mafia1/sdk/player/native_actor.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::SDK::Player {
    struct NativeHuman;
    struct NativeGameItem;
    enum class NativeHitType : int32_t;
}
namespace Mafia1Online::SDK::Police {
    struct NativePoliceManager;
}
namespace Mafia1Online::SDK::World {
    struct NativeItemVector;
}
namespace Mafia1Online::Features::Combat {
    struct NativeShotData;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Npc {
    // Simulation host: the retail C_entity humans that join the world while
    // it runs, of every kind. A spawn hook per kind hands each one to Track
    // (gangster_spawn.cpp). Each runs its native AI here and is reported to
    // the server, which streams it to players. The police manager retires one
    // once no player is within 150 of it, instead of the free camera. The host
    // never binds them as network objects, so their own hits stay native.
    class NpcHost final {
      public:
        static constexpr std::chrono::milliseconds kSnapshotInterval {100};

        // Host only.
        bool Install();
        void Uninstall();
        void RegisterRPC(World::WorldService &world);
        void Update(World::WorldService &world);
        // The native NPCs are gone (mission closing); the server drops the
        // reported ones when the generation changes or they go stale.
        void Reset();

        // Starts reporting a C_entity the retail game just created.
        void Track(SDK::Player::NativeActor *actor, Shared::Entities::NpcKind kind, const std::string &model);
        [[nodiscard]] bool Owns(const void *actor) const {
            return _npcs.contains(const_cast<void *>(actor));
        }
        void OnNativeDestroyed(void *actor);
        void OnShot(SDK::Player::NativeHuman &human, const Combat::NativeShotData &shot);
        // Retail DropOutItems on one of them; the server creates the pickups.
        void OnNativeDrop(SDK::Player::NativeHuman &human, const SDK::World::NativeItemVector &items);
        // A native Hit by one of them on a player's protected replica.
        void OnHitPlayer(World::WorldService &world, SDK::Player::NativeHuman &target, SDK::Player::NativeHitType type, const SDK::Player::Vector3 &direction, float damage,
                         SDK::Player::NativeActor *attacker, uint32_t bodyPart);
        // After a native Hit on one of them.
        void OnHit(World::WorldService &world, SDK::Player::NativeHuman &npc, SDK::Player::NativeHitType type, const SDK::Player::Vector3 &direction, float damage,
                   SDK::Player::NativeActor *attacker, uint32_t bodyPart, float healthBefore);
        // The police manager's retirement pass (hook body).
        char TickRetirement(SDK::Police::NativePoliceManager *police, char(__thiscall *original)(SDK::Police::NativePoliceManager *, bool));

      private:
        struct Tracked {
            uint32_t key = 0;
            SDK::Player::Vector3 lastPosition {};
            // The inventory last reported to the server.
            Shared::Npc::Inventory inventory;
            bool removeRequested = false;
        };

        void SendSnapshots(float seconds);
        // Inventory changes and the reloads they reveal.
        void SendInventory(Tracked &tracked, const SDK::Player::NativeHuman &human, bool dead);
        bool NearObserver(const SDK::Player::Vector3 &position) const;

        World::WorldService *_world = nullptr;
        uint64_t _missionGeneration = 0;
        uint32_t _nextKey = 1;
        std::unordered_map<void *, Tracked> _npcs;
        // Spawned players' positions, refreshed every Update.
        std::vector<SDK::Player::Vector3> _observers;
        std::chrono::steady_clock::time_point _lastSnapshot;
    };

    // Native inventory in the reported form.
    Shared::Npc::Inventory ReadInventory(const SDK::Player::NativeHuman &human);
} // namespace Mafia1Online::Features::Npc
