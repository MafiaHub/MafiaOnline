#include "environment_service.h"

#include "features/world/world_service.h"
#include "shared/features/world/world_state_entity.h"

#include <core_modules.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/sound/native_sound.h>
#include <mafia1/sdk/world/native_environment.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>

namespace Mafia1Online::Features::Environment {
    namespace {
        using Shared::Entities::WorldStateEntity;
        using Weather = WorldStateEntity::Weather;
        using SDK::World::WeatherParam;

        // Retail FINDFRAME: every frame type in the primary sector plus the
        // backdrop sector.
        constexpr uint32_t kFindFrameFlags = 0x4ffff;
        constexpr uint32_t kFrameSound     = 4;
        // WeatherSystemReset's own particle budget.
        constexpr uint32_t kPresetParticles = 1000;
        // The alternate preset draws grey streaks; snow reads as white.
        constexpr uint32_t kSnowColorHigh = 0xc0f0f0f0;
        constexpr uint32_t kSnowColorLow  = 0x40e0e0e0;

        uint32_t WeatherParticleCount(float intensity) {
            return intensity < 0.0f ? kPresetParticles
                                    : static_cast<uint32_t>(std::lround(std::clamp(intensity, 0.0f, 100.0f) / 100.0f * WorldStateEntity::kMaxParticles));
        }

        void SetParam(SDK::Scene::NativeScene *scene, WeatherParam param, uint32_t value) {
            scene->SetWeatherParam(static_cast<uint32_t>(param), value);
        }

        void SetFrameOn(SDK::Scene::NativeFrame *frame, bool on) {
            if (frame->FrameType() == kFrameSound) {
                reinterpret_cast<SDK::Sound::NativeSound *>(frame)->vtable->setOnUpdate(reinterpret_cast<SDK::Sound::NativeSound *>(frame), on, true);
                return;
            }
            frame->SetOn(on);
        }

        const WorldStateEntity *FindState() {
            const WorldStateEntity *state = nullptr;
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<WorldStateEntity>([&](WorldStateEntity *entity) {
                    state = entity;
                });
            }
            return state;
        }
    } // namespace

    void EnvironmentService::Update(World::WorldService &world) {
        auto *mission = SDK::Core::Mission::Get();
        if (!world.IsReady() || !mission || !mission->Game() || !mission->GetScene()) {
            return;
        }
        const auto *state = FindState();
        if (!state) {
            return;
        }
        auto *scene = mission->GetScene();
        auto *game  = mission->Game();
        const bool loaded = _missionGeneration != world.LoadedMissionGeneration();
        if (loaded) {
            _missionGeneration = world.LoadedMissionGeneration();
            for (uint32_t param = 0; param < SDK::World::kWeatherParamCount; ++param) {
                _missionWeather[param] = scene->GetWeatherParam(param);
            }
            _missionWeatherOn    = SDK::World::SavedWeatherOn(game);
            _missionNightMode    = SDK::World::IsNightMode(game);
            _missionWeatherCount = SDK::World::SavedWeatherCount(game);
            _applied             = {};
            _originalFrames.clear();
            _originalOpacities.clear();
        }

        if (state->weather != _applied.weather) {
            ApplyWeather(state->weather, state->rainIntensity);
            _applied.weather   = state->weather;
            _applied.intensity = state->rainIntensity;
        }
        else if (state->rainIntensity != _applied.intensity) {
            // A passing front changes only the particle budget. ResetWeather
            // recreates the emitter and causes a visible hitch every minute.
            if (static_cast<Weather>(state->weather) == Weather::Rain || static_cast<Weather>(state->weather) == Weather::Snow) {
                SetParam(scene, WeatherParam::MaxCount, WeatherParticleCount(state->rainIntensity));
                SDK::World::SaveWeather(game, WeatherParam::MaxCount);
            }
            _applied.intensity = state->rainIntensity;
        }
        if (state->cityMusicEnabled != _applied.cityMusic) {
            SDK::World::SetCityMusicPaused(game, !state->cityMusicEnabled);
            _applied.cityMusic = state->cityMusicEnabled;
        }
        if (state->nightMode < 0 && _applied.nightMode >= 0) {
            SDK::World::SetNightMode(game, _missionNightMode);
        }
        else if (state->nightMode >= 0 && SDK::World::IsNightMode(game) != (state->nightMode != 0)) {
            SDK::World::SetNightMode(game, state->nightMode != 0);
        }
        _applied.nightMode = state->nightMode;
        const uint64_t frameGeneration = state->frameGeneration == _missionGeneration ? state->frameGeneration : 0;
        const bool frameGenerationChanged = frameGeneration != _applied.frameGeneration;
        const std::string &frames      = frameGeneration != 0 ? state->frames : std::string();
        if (frameGenerationChanged || frames != _applied.frames) {
            ApplyFrames(frameGeneration, frames);
            _applied.frameGeneration = frameGeneration;
            _applied.frames          = frames;
        }
        const std::string &opacities = frameGeneration != 0 ? state->frameOpacities : std::string();
        if (frameGenerationChanged || opacities != _applied.frameOpacities) {
            ApplyFrameOpacities(frameGeneration, opacities);
            _applied.frameOpacities = opacities;
        }
    }

    void EnvironmentService::ApplyWeather(uint8_t weather, float intensity) {
        auto *mission = SDK::Core::Mission::Get();
        auto *scene   = mission->GetScene();
        auto *game    = mission->Game();
        switch (static_cast<Weather>(weather)) {
        case Weather::Clear:
            SetParam(scene, WeatherParam::On, 0);
            SDK::World::SaveWeather(game, WeatherParam::On);
            return;
        case Weather::Rain:
        case Weather::Snow: {
            const bool snow = static_cast<Weather>(weather) == Weather::Snow;
            // Reset picks its preset from the mode flag.
            SetParam(scene, WeatherParam::Mode, snow ? 1 : 0);
            scene->ResetWeather();
            // Dummies would confine the particles to the mission's weather
            // volumes, which most missions do not have.
            SetParam(scene, WeatherParam::Dummies, 0);
            if (snow) {
                SetParam(scene, WeatherParam::ColorHigh, kSnowColorHigh);
                SetParam(scene, WeatherParam::ColorLow, kSnowColorLow);
            }
            SetParam(scene, WeatherParam::MaxCount, WeatherParticleCount(intensity));
            SDK::World::SaveWeather(game, WeatherParam::MaxCount);
            SetParam(scene, WeatherParam::On, 1);
            SDK::World::SaveWeather(game, WeatherParam::On);
            return;
        }
        default: {
            SetParam(scene, WeatherParam::Mode, _missionWeather[static_cast<uint32_t>(WeatherParam::Mode)]);
            for (uint32_t param = 1; param < static_cast<uint32_t>(WeatherParam::Mode); ++param) {
                if (param != static_cast<uint32_t>(WeatherParam::MaxCount)) {
                    scene->SetWeatherParam(param, _missionWeather[param]);
                }
            }
            SetParam(scene, WeatherParam::MaxCount, _missionWeatherCount);
            SDK::World::SaveWeather(game, WeatherParam::MaxCount);
            SetParam(scene, WeatherParam::On, _missionWeatherOn ? 1 : 0);
            SDK::World::SaveWeather(game, WeatherParam::On);
            return;
        }
        }
    }

    void EnvironmentService::ApplyFrames(uint64_t generation, const std::string &frames) {
        auto *scene = SDK::Core::Mission::Get()->GetScene();
        std::unordered_map<std::string, bool> wanted;
        if (generation != 0) {
            WorldStateEntity::ForEachFrame(frames, [&](std::string_view name, bool visible) {
                wanted[std::string(name)] = visible;
            });
        }
        // Frame pointers are looked up again every time: a scene frame can be
        // broken off or released while the mission runs.
        for (auto it = _originalFrames.begin(); it != _originalFrames.end();) {
            if (wanted.contains(it->first)) {
                ++it;
                continue;
            }
            if (auto *frame = scene->FindFrame(it->first.c_str(), kFindFrameFlags)) {
                SetFrameOn(frame, it->second);
            }
            it = _originalFrames.erase(it);
        }
        for (const auto &[name, visible] : wanted) {
            auto *frame = scene->FindFrame(name.c_str(), kFindFrameFlags);
            if (!frame) {
                continue;
            }
            _originalFrames.try_emplace(name, frame->IsOn());
            SetFrameOn(frame, visible);
        }
    }

    void EnvironmentService::ApplyFrameOpacities(uint64_t generation, const std::string &opacities) {
        auto *scene = SDK::Core::Mission::Get()->GetScene();
        std::unordered_map<std::string, float> wanted;
        if (generation != 0) {
            WorldStateEntity::ForEachFrameOpacity(opacities, [&](std::string_view name, float opacity) {
                wanted[std::string(name)] = opacity;
            });
        }
        for (auto it = _originalOpacities.begin(); it != _originalOpacities.end();) {
            if (wanted.contains(it->first)) {
                ++it;
                continue;
            }
            if (auto *frame = scene->FindFrame(it->first.c_str(), kFindFrameFlags); frame && frame->SupportsTransparency()) {
                frame->SetTransparency(it->second);
            }
            it = _originalOpacities.erase(it);
        }
        for (const auto &[name, opacity] : wanted) {
            auto *frame = scene->FindFrame(name.c_str(), kFindFrameFlags);
            if (!frame || !frame->SupportsTransparency()) {
                continue;
            }
            _originalOpacities.try_emplace(name, frame->Transparency());
            frame->SetTransparency(opacity);
        }
    }

    void EnvironmentService::OnMissionClosing() {
        _missionGeneration = 0;
        _applied           = {};
        _originalFrames.clear();
        _originalOpacities.clear();
    }

    void EnvironmentService::Reset() {
        OnMissionClosing();
    }
} // namespace Mafia1Online::Features::Environment
