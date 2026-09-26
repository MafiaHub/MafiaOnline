#pragma once

#include "game/entities/native_object_registry.h"
#include "shared/features/car/car_debris.h"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::SDK::Car { struct NativeCar; }
namespace Mafia1Online::SDK::Player { struct NativeActor; }
namespace Mafia1Online::Features::World { class WorldService; }

namespace Mafia1Online::Features::Car {
    class DebrisService final {
      public:
        void Update(World::WorldService &world);
        void Reset(World::WorldService &world);
        bool AllowNativeDropOut(World::WorldService &world, SDK::Car::NativeCar *car) const;
        void OnNativeDropOut(World::WorldService &world, SDK::Car::NativeCar *car,
                             SDK::Player::NativeActor *actor, int type, int partIndex,
                             const void *parameters);
        void OnNativeDestroyed(World::WorldService &world, void *actor);

      private:
        struct LiveActor {
            Game::Entities::NativeObjectHandle handle;
            bool removalQueued = false;
            uint32_t movementSequence = 0;
            std::chrono::steady_clock::time_point lastMovement;
        };
        void CreateNative(World::WorldService &world, uint64_t networkId);
        void ReportMovement(World::WorldService &world, uint64_t networkId, LiveActor &live);
        void ApplyPose(World::WorldService &world, uint64_t networkId, LiveActor &live);
        void QueueRemoval(World::WorldService &world, uint64_t networkId);

        std::unordered_map<uint64_t, LiveActor> _live;
        std::unordered_map<uint32_t, SDK::Player::NativeActor *> _pending;
        std::unordered_set<uint32_t> _expired;
        std::unordered_set<uint64_t> _goneRequested;
        // Replicas this client does not control that the native C_DropOut
        // pool evicted. Recreating them would relaunch the part each frame.
        std::unordered_set<uint64_t> _evicted;
        uint32_t _localSequence = 0;
    };
} // namespace Mafia1Online::Features::Car
