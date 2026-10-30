#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace Framework::Voice {
    struct SpeechFrame;
}

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
    // player this client hears, and the local player while their voice goes
    // out, timed as the others hear it.
    class MouthService final {
      public:
        void Update(World::WorldService &world);
        void Reset();

      private:
        struct Mouth {
            float open     = 0.0f;
            float presence = 0.0f;   // blend from the rest face into speech
            float split    = 0.46f;  // which open shape, after the voice's brightness
            float peakDb   = -12.0f; // the talker's recent loudest frame
            bool rested    = true;   // the rest face is written; nothing to do while silent
            uint32_t seen  = 0;
        };
        void Drive(SDK::Player::NativeHuman &human, bool talking, const Framework::Voice::SpeechFrame &speech, float dt);

        // Keyed by the base mesh read from the live human this frame, never
        // dereferenced from here: a human that went away is simply not seen.
        std::unordered_map<SDK::Scene::NativeFrame *, Mouth> _mouths;
        std::chrono::steady_clock::time_point _last {};
        // The local microphone last carried speech until this, plus the hold
        // a remote talker gets (kSpeakerTalkingHoldMs).
        std::chrono::steady_clock::time_point _localVoicedUntil {};
        uint32_t _frame = 0;
    };
} // namespace Mafia1Online::Features::Voice
