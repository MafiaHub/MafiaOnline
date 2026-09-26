#pragma once

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Effects {
    // Replays the server's World.createExplosion and World.createFire. The
    // server has already applied player damage; native human hits stay
    // suppressed by the death hooks.
    class EffectsService final {
      public:
        void RegisterRPC(World::WorldService &world);

      private:
        World::WorldService *_world = nullptr;
    };
} // namespace Mafia1Online::Features::Effects
