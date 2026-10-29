#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Mafia1Online::SDK::Player {
    struct NativeHuman;
}

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Voice {
    // Lip flap: moves a talking player's mouth with the loudness of their
    // voice, through the head morphs retail lip-sync drives (Dabing). Every
    // player this client hears, and the local player while transmitting.
    class MouthService final {
      public:
        void Update(World::WorldService &world);
        void Reset();

      private:
        struct Mouth {
            float open     = 0.0f;
            float presence = 0.0f; // blend from the rest face into speech
            float phase    = 0.0f; // walks the open shape while the mouth moves
            bool rested    = true; // the rest face is written; nothing to do while silent
            uint32_t seen  = 0;
        };
        void Drive(SDK::Player::NativeHuman &human, bool talking, float level, float dt);

        // Keyed by the base mesh read from the live human this frame, never
        // dereferenced from here: a human that went away is simply not seen.
        std::unordered_map<SDK::Scene::NativeFrame *, Mouth> _mouths;
        std::chrono::steady_clock::time_point _last {};
        uint32_t _frame = 0;
    };
} // namespace Mafia1Online::Features::Voice
