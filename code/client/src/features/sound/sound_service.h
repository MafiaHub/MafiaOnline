#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::SDK::Sound {
    struct NativeSound;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Sound {
    // One native looping I3D_sound per replicated SoundEntity, built the way
    // C_fire builds its loop, and the one-shot PlaySound RPC through
    // C_game::Play3DSound, whose sounds the game releases itself.
    class SoundService final {
      public:
        void RegisterRPC(World::WorldService &world);
        void Update(World::WorldService &world);
        // Every owned sound is stopped, unlinked and released while the
        // scene is still alive.
        void OnMissionClosing();
        void Reset();

        // Script sounds for this client only. The id is the game's own
        // Play3DSound id, valid until the mission closes.
        int32_t PlayLocal(const std::string &wave, const glm::vec3 *position, float radius, float volume, bool loop);
        bool StopLocal(int32_t id);

      private:
        struct Voice {
            SDK::Sound::NativeSound *sound = nullptr;
            std::string wave;
            glm::vec3 position {0.0f};
            glm::vec3 sectorPosition {0.0f};
            float radius  = 0.0f;
            float volume  = 0.0f;
            bool enabled  = false;
        };
        void Release(Voice &voice);
        bool Build(Voice &voice, const glm::vec3 &position);
        glm::vec3 SoundPosition(World::WorldService &world, uint64_t attachedId, const glm::vec3 &fallback) const;

        World::WorldService *_world = nullptr;
        std::unordered_map<uint64_t, Voice> _voices;
        std::unordered_set<std::string> _missingWaves;
        std::unordered_set<int32_t> _local;
    };
} // namespace Mafia1Online::Features::Sound
