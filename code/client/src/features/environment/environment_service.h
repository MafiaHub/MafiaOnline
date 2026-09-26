#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Environment {
    // Applies the replicated WorldStateEntity to the loaded mission: the
    // LS3DF weather particle system, scene frame visibility and the city
    // music. Everything is reapplied after each mission load, because
    // C_mission::Open reloads the scene's weather and C_game::Init resumes
    // the city music.
    class EnvironmentService final {
      public:
        void Update(World::WorldService &world);
        // While the scene is alive; the next load rebuilds everything.
        void OnMissionClosing();
        void Reset();

      private:
        struct Applied {
            uint8_t weather     = 0;
            float intensity     = -1.0f;
            bool cityMusic      = true;
            int8_t nightMode    = -1;
            uint64_t frameGeneration = 0;
            std::string frames;
            std::string frameOpacities;
        };
        void ApplyWeather(uint8_t weather, float intensity);
        void ApplyFrames(uint64_t generation, const std::string &frames);
        void ApplyFrameOpacities(uint64_t generation, const std::string &opacities);

        uint64_t _missionGeneration = 0;
        Applied _applied;
        // The mission's own weather, read back after the load.
        std::array<uint32_t, 15> _missionWeather {};
        bool _missionWeatherOn         = false;
        bool _missionNightMode          = false;
        uint32_t _missionWeatherCount  = 0;
        std::unordered_map<std::string, bool> _originalFrames;
        std::unordered_map<std::string, float> _originalOpacities;
    };
} // namespace Mafia1Online::Features::Environment
