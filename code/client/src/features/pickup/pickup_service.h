#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace Mafia1Online::SDK::World {
    struct NativeUsingObject;
    struct NativeItemVector;
}
namespace Mafia1Online::SDK::Player {
    struct NativeHuman;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}
namespace Mafia1Online::Features::Combat {
    class CombatService;
}

namespace Mafia1Online::Features::Pickup {
    // Shows server pickups as native world items (a model plus a use-key
    // record) and turns the use key on one into a server request. Native
    // drops create nothing; the server decides what lies on the ground.
    class PickupService final {
      public:
        PickupService();
        ~PickupService();
        bool Install(World::WorldService &world, Combat::CombatService &combat);
        void Shutdown();
        void Update(World::WorldService &world);
        // Before the native mission closes and before the game clears its
        // use-key registry.
        void OnMissionClosing();
        void Reset();

        // FindNearObjects result filter: true when the nearest item is a
        // replicated pickup, which the caller then hides from the native use.
        bool OnNearObjects(SDK::World::NativeItemVector &items);
        bool SuppressNativeDrop() const;
        void OnNativeDrop(SDK::Player::NativeHuman &human, const SDK::World::NativeItemVector &items);

      private:
        struct Item {
            std::unique_ptr<SDK::World::NativeUsingObject> record;
            uint16_t loaded = 0;
            uint16_t reserve = 0;
        };
        void Remove(uint64_t id);
        void Create(World::WorldService &world, uint64_t id);

        World::WorldService *_world = nullptr;
        Combat::CombatService *_combat = nullptr;
        std::unordered_map<uint64_t, Item> _items;
        uint64_t _lastRequestId = 0;
        uint64_t _lastRequestAt = 0;
    };
} // namespace Mafia1Online::Features::Pickup
