#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::SDK::Player {
    struct NativeActor;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Traffic {
    // Players: the road block barriers of the simulation host's police. Each
    // network barrier is a native C_irenka built as C_shvestky builds its
    // pool (barrier.i3d, Init, frame owned by the actor) and placed as its
    // road block spawn places one (0x49d750), so cars collide with it here
    // as on the host.
    class RoadBarrierService final {
      public:
        void Update(World::WorldService &world);
        // Mission closing: C_game::Done takes its temporary actors along.
        void OnMissionClosing();
        // Disconnect or shutdown: the mission stays; the barriers go.
        void Reset(World::WorldService &world);
        void OnNativeDestroyed(void *actor);

      private:
        void Create(uint64_t networkId);
        void Remove(World::WorldService &world, SDK::Player::NativeActor *actor);

        std::unordered_map<uint64_t, SDK::Player::NativeActor *> _live;
        // Barriers that would not build are not retried every frame.
        std::unordered_set<uint64_t> _failed;
        uint64_t _missionGeneration = 0;
    };
} // namespace Mafia1Online::Features::Traffic
