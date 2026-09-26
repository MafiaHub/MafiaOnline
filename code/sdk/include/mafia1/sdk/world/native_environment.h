#pragma once

#include <mafia1/sdk/core/game.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::World {
    // I3D_scene WS_PARAM ids, the order of WEATHER_SETPARAM's name table at
    // 0x624240.
    enum class WeatherParam : uint32_t {
        On        = 0,
        Sectors   = 1,
        Dummies   = 2,
        ColorHigh = 3,
        ColorLow  = 4,
        Speed     = 5,
        Length    = 6,
        Width     = 7,
        MaxDist   = 8,
        MaxHeight = 9,
        MaxCount  = 10,
        DirX      = 11,
        DirY      = 12,
        DirZ      = 13,
        // 1 selects WeatherSystemReset's alternate (slow, short, wide) preset.
        Mode = 14,
    };
    inline constexpr uint32_t kWeatherParamCount = 15;

    // C_game::OsefujPocasi(bool save, WS_PARAM), ret 8. With save it reads the
    // scene's On (0) or MaxCount (10) into C_game+0x3630/+0x3634, then
    // reapplies both scaled by the actor detail (low /20, medium /8), as
    // WEATHER_SETPARAM does after either parameter.
    inline constexpr uintptr_t kSaveWeather = 0x5b9540;
    // C_game::CityMusicPaused(bool), ret 4: stores C_game+0x2fc0 and fades
    // the current city_music show out. C_game::Init clears it.
    inline constexpr uintptr_t kCityMusicPaused = 0x5ae1b0;
    // C_game::SetNightMode(bool), ret 4, writes the mode at C_game+0x2d44.
    // GAME_NIGHTMISSION writes that same field directly.
    inline constexpr uintptr_t kSetNightMode = 0x47b590;
    // C_game::ScoreSetOn(bool) and ScoreSet(int), ret 4; the freeride score
    // is the indicators' bonus counter. C_game::Init turns it off.
    inline constexpr uintptr_t kScoreSetOn = 0x5b9fa0;
    inline constexpr uintptr_t kScoreSet   = 0x5b9fe0;
    // G_Camera::SetSwing(bool, float intensity, I3D_frame* target), ret 0xc.
    // Disabling resets the backdrop sector's roll only with a target frame.
    inline constexpr uintptr_t kCameraSetSwing = 0x5ed210;

    inline void SaveWeather(Core::Game::NativeGame *game, WeatherParam param) {
        reinterpret_cast<void(__thiscall *)(Core::Game::NativeGame *, bool, uint32_t)>(kSaveWeather)(game, true, static_cast<uint32_t>(param));
    }

    // The unscaled values C_game::Init took from the mission scene.
    inline bool SavedWeatherOn(const Core::Game::NativeGame *game) {
        return *reinterpret_cast<const bool *>(reinterpret_cast<const std::byte *>(game) + 0x3630);
    }
    inline uint32_t SavedWeatherCount(const Core::Game::NativeGame *game) {
        return *reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(game) + 0x3634);
    }

    inline void SetCityMusicPaused(Core::Game::NativeGame *game, bool paused) {
        reinterpret_cast<void(__thiscall *)(Core::Game::NativeGame *, bool)>(kCityMusicPaused)(game, paused);
    }

    inline bool IsNightMode(const Core::Game::NativeGame *game) {
        return *reinterpret_cast<const bool *>(reinterpret_cast<const std::byte *>(game) + 0x2d44);
    }

    inline void SetNightMode(Core::Game::NativeGame *game, bool enabled) {
        reinterpret_cast<void(__thiscall *)(Core::Game::NativeGame *, bool)>(kSetNightMode)(game, enabled);
    }

    inline void ScoreSetOn(Core::Game::NativeGame *game, bool on) {
        reinterpret_cast<void(__thiscall *)(Core::Game::NativeGame *, bool)>(kScoreSetOn)(game, on);
    }
    inline void ScoreSet(Core::Game::NativeGame *game, int32_t score) {
        reinterpret_cast<void(__thiscall *)(Core::Game::NativeGame *, int32_t)>(kScoreSet)(game, score);
    }
    // ScoreSetOn reads these fields at C_game+0x363e/+0x3640, and ScoreSet
    // writes the value there. Read the game state so retail scripts are seen.
    inline bool ScoreVisible(const Core::Game::NativeGame *game) {
        return *reinterpret_cast<const bool *>(reinterpret_cast<const std::byte *>(game) + 0x363e);
    }
    inline int32_t ScoreValue(const Core::Game::NativeGame *game) {
        return *reinterpret_cast<const int32_t *>(reinterpret_cast<const std::byte *>(game) + 0x3640);
    }

    // G_Camera is embedded at C_game+0x4c (GetCamera 0x47b510).
    inline void CameraSetSwing(Core::Game::NativeGame *game, bool enabled, float intensity, void *target) {
        auto *camera = reinterpret_cast<std::byte *>(game) + 0x4c;
        reinterpret_cast<void(__thiscall *)(void *, bool, float, void *)>(kCameraSetSwing)(camera, enabled, intensity, target);
    }
} // namespace Mafia1Online::SDK::World
