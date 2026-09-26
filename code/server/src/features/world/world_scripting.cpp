#include "world_scripting.h"

#include "core/server.h"
#include "features/script/script_runtime.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/door/door_entity.h"
#include "features/door/door_scripting.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/sound/sound_entity.h"
#include "shared/features/world/world_effects.h"
#include "shared/features/world/world_state_entity.h"
#include "shared/scripting_catalog.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <string>
#include <string_view>

namespace Mafia1Online::Scripting {
    namespace {
        using Shared::Entities::SoundEntity;
        using Shared::Entities::WorldStateEntity;
        using Weather = WorldStateEntity::Weather;

        constexpr std::array<std::string_view, WorldStateEntity::kWeatherCount> kWeatherNames {"default", "clear", "rain", "snow"};

        Features::World::WorldScriptService &Service() {
            return GetServer().WorldScript();
        }

        v8::Local<v8::Object> Options(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return index < info.Length() && info[index]->IsObject() ? info[index].As<v8::Object>() : v8::Local<v8::Object>();
        }

        bool ValidOptions(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return index >= info.Length() || info[index]->IsUndefined() || info[index]->IsObject();
        }

        bool OptionalFloat(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *name, float minimum, float maximum, float &out) {
            if (options.IsEmpty()) {
                return true;
            }
            v8::Local<v8::Value> value;
            if (!options->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, name)).ToLocal(&value) || value->IsUndefined()) {
                return true;
            }
            float read = 0.0f;
            if (!ScriptArgs::ReadFloat(value, read)) {
                return false;
            }
            out = std::clamp(read, minimum, maximum);
            return true;
        }

        void JS_GetMission(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(v8pp::to_v8(info.GetIsolate(), GetServer().Mission()));
        }

        void JS_GetMissionGeneration(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<double>(GetServer().MissionGeneration()));
        }

        void JS_ChangeMission(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            if (info.Length() != 1 || !info[0]->IsString()) {
                ScriptArgs::Throw(isolate, "World.changeMission(mission) expects a stock mission name");
                return;
            }
            info.GetReturnValue().Set(GetServer().ChangeMission(v8pp::from_v8<std::string>(isolate, info[0])));
        }

        void JS_IsReady(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(GetServer().AllPlayersReady());
        }

        void JS_GetPlayers(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            GetServer().Players().ForEach([&](Shared::Entities::PlayerEntity *player) {
                list->Set(context, index++, WrapPlayer(isolate, player->GetNetworkID())).Check();
            });
            info.GetReturnValue().Set(list);
        }

        void JS_GetVehicles(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            GetServer().Cars().ForEach([&](Shared::Entities::CarEntity *car) {
                list->Set(context, index++, WrapVehicle(isolate, car->GetNetworkID())).Check();
            });
            info.GetReturnValue().Set(list);
        }

        void JS_GetPickups(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            for (const uint64_t id : GetServer().Pickups().List()) {
                list->Set(context, index++, WrapPickup(isolate, id)).Check();
            }
            info.GetReturnValue().Set(list);
        }

        void JS_GetDoors(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto list = v8::Array::New(isolate);
            uint32_t index = 0;
            Framework::CoreModules::GetReplication()->ForEach<Shared::Entities::DoorEntity>([&](Shared::Entities::DoorEntity *door) {
                if (door->missionGeneration == GetServer().MissionGeneration()) {
                    list->Set(context, index++, WrapDoor(isolate, door->GetNetworkID())).Check();
                }
            });
            info.GetReturnValue().Set(list);
        }

        void JS_SetWeather(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            float intensity = -1.0f;
            if (info.Length() < 1 || info.Length() > 2 || !info[0]->IsString() || (info.Length() == 2 && !ScriptArgs::ReadFloat(info[1], intensity))) {
                ScriptArgs::Throw(isolate, "World.setWeather(weather, intensity?) expects \"default\", \"clear\", \"rain\" or \"snow\" and an intensity from 0 to 100");
                return;
            }
            const auto name = v8pp::from_v8<std::string>(isolate, info[0]);
            const auto it   = std::find(kWeatherNames.begin(), kWeatherNames.end(), name);
            if (it == kWeatherNames.end()) {
                ScriptArgs::Throw(isolate, "World.setWeather: unknown weather; use \"default\", \"clear\", \"rain\" or \"snow\"");
                return;
            }
            auto &state         = Service().State();
            state.weather       = static_cast<uint8_t>(std::distance(kWeatherNames.begin(), it));
            state.rainIntensity = info.Length() == 2 ? std::clamp(intensity, 0.0f, 100.0f) : -1.0f;
        }

        void JS_GetWeather(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate      = info.GetIsolate();
            auto context       = isolate->GetCurrentContext();
            const auto &state  = Service().State();
            auto weather       = v8::Object::New(isolate);
            const auto kind    = state.weather < kWeatherNames.size() ? kWeatherNames[state.weather] : kWeatherNames[0];
            SetField(isolate, context, weather, "weather", v8pp::to_v8(isolate, std::string(kind)));
            SetField(isolate, context, weather, "intensity", state.rainIntensity < 0.0f ? v8::Null(isolate).As<v8::Value>() : v8::Number::New(isolate, state.rainIntensity).As<v8::Value>());
            info.GetReturnValue().Set(weather);
        }

        void JS_SetCityMusic(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsBoolean()) {
                ScriptArgs::Throw(info.GetIsolate(), "World.setCityMusic(enabled) expects a boolean");
                return;
            }
            Service().State().cityMusicEnabled = info[0].As<v8::Boolean>()->Value();
        }

        void JS_IsCityMusicEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(Service().State().cityMusicEnabled);
        }

        void JS_SetNightMode(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || (!info[0]->IsBoolean() && !info[0]->IsNull())) {
                ScriptArgs::Throw(info.GetIsolate(), "World.setNightMode(enabled) expects a boolean or null");
                return;
            }
            Service().State().nightMode = info[0]->IsNull() ? -1 : (info[0].As<v8::Boolean>()->Value() ? 1 : 0);
        }

        void JS_GetNightModeOverride(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const int8_t mode = Service().State().nightMode;
            info.GetReturnValue().Set(mode < 0 ? v8::Null(info.GetIsolate()).As<v8::Value>()
                                              : v8::Boolean::New(info.GetIsolate(), mode != 0).As<v8::Value>());
        }

        void JS_SetFrameVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            if (info.Length() != 2 || !info[0]->IsString() || !info[1]->IsBoolean()) {
                ScriptArgs::Throw(isolate, "World.setFrameVisible(name, visible) expects a frame name and a boolean");
                return;
            }
            const auto name = v8pp::from_v8<std::string>(isolate, info[0]);
            if (!WorldStateEntity::ValidFrameName(name)) {
                ScriptArgs::Throw(isolate, "World.setFrameVisible: invalid frame name");
                return;
            }
            auto &state = Service().State();
            std::string frames;
            size_t count = 0;
            WorldStateEntity::ForEachFrame(state.frames, [&](std::string_view frame, bool visible) {
                if (frame != name) {
                    frames.append(frame).append(visible ? "=1\n" : "=0\n");
                    ++count;
                }
            });
            if (count >= WorldStateEntity::kMaxFrames) {
                info.GetReturnValue().Set(false);
                return;
            }
            frames.append(name).append(info[1].As<v8::Boolean>()->Value() ? "=1\n" : "=0\n");
            state.frames          = std::move(frames);
            state.frameGeneration = GetServer().MissionGeneration();
            info.GetReturnValue().Set(true);
        }

        void JS_SetFrameOpacity(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            float opacity = 0.0f;
            if (info.Length() != 2 || !info[0]->IsString() || !ScriptArgs::ReadFloat(info[1], opacity)
                || opacity < 0.0f || opacity > 1.0f) {
                ScriptArgs::Throw(isolate, "World.setFrameOpacity(name, opacity) expects a frame name and opacity from 0 to 1");
                return;
            }
            const auto name = v8pp::from_v8<std::string>(isolate, info[0]);
            if (!WorldStateEntity::ValidFrameName(name)) {
                ScriptArgs::Throw(isolate, "World.setFrameOpacity: invalid frame name");
                return;
            }
            auto &state = Service().State();
            std::string values;
            size_t count = 0;
            WorldStateEntity::ForEachFrameOpacity(state.frameOpacities, [&](std::string_view frame, float existing) {
                if (frame != name) {
                    char text[32];
                    const auto result = std::to_chars(text, text + sizeof(text), existing);
                    if (result.ec == std::errc {}) {
                        values.append(frame).append("=").append(text, result.ptr).append("\n");
                        ++count;
                    }
                }
            });
            if (count >= WorldStateEntity::kMaxFrameOpacities) {
                info.GetReturnValue().Set(false);
                return;
            }
            char text[32];
            const auto result = std::to_chars(text, text + sizeof(text), opacity);
            if (result.ec != std::errc {}) {
                info.GetReturnValue().Set(false);
                return;
            }
            values.append(name).append("=").append(text, result.ptr).append("\n");
            state.frameOpacities = std::move(values);
            state.frameGeneration = GetServer().MissionGeneration();
            info.GetReturnValue().Set(true);
        }

        void JS_ResetFrameOpacity(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            if (info.Length() > 1 || (info.Length() == 1 && !info[0]->IsString() && !info[0]->IsUndefined())) {
                ScriptArgs::Throw(isolate, "World.resetFrameOpacity(name?) expects an optional frame name");
                return;
            }
            auto &state = Service().State();
            if (info.Length() == 0 || info[0]->IsUndefined()) {
                state.frameOpacities.clear();
                return;
            }
            const auto name = v8pp::from_v8<std::string>(isolate, info[0]);
            if (!WorldStateEntity::ValidFrameName(name)) {
                ScriptArgs::Throw(isolate, "World.resetFrameOpacity: invalid frame name");
                return;
            }
            std::string values;
            WorldStateEntity::ForEachFrameOpacity(state.frameOpacities, [&](std::string_view frame, float existing) {
                if (frame != name) {
                    char text[32];
                    const auto result = std::to_chars(text, text + sizeof(text), existing);
                    if (result.ec == std::errc {}) {
                        values.append(frame).append("=").append(text, result.ptr).append("\n");
                    }
                }
            });
            state.frameOpacities = std::move(values);
        }

        void JS_ResetFrames(const v8::FunctionCallbackInfo<v8::Value> &info) {
            (void)info;
            Service().State().frames.clear();
        }

        void JS_CreateExplosion(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            glm::vec3 position;
            int next = 0;
            Shared::World::Explosion explosion;
            if (!ScriptArgs::ReadPosition(info, 0, position, next) || next + 1 < info.Length() || !ValidOptions(info, next) ||
                !OptionalFloat(isolate, Options(info, next), "radius", 0.5f, Shared::World::Explosion::kMaxRadius, explosion.radius) ||
                !OptionalFloat(isolate, Options(info, next), "damage", 0.0f, Shared::World::Explosion::kMaxDamage, explosion.damage)) {
                ScriptArgs::Throw(isolate, "World.createExplosion(position, { radius?, damage? }) expects a position and finite options");
                return;
            }
            auto &server                = GetServer();
            explosion.missionGeneration = server.MissionGeneration();
            explosion.x                 = position.x;
            explosion.y                 = position.y;
            explosion.z                 = position.z;
            if (explosion.damage > 0.0f) {
                server.Combat().ApplyExplosion(0, explosion.missionGeneration, position, explosion.radius, explosion.damage);
            }
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(explosion);
        }

        void JS_CreateFire(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            glm::vec3 position;
            int next = 0;
            Shared::World::Fire fire;
            float duration = static_cast<float>(fire.lifeMs);
            if (!ScriptArgs::ReadPosition(info, 0, position, next) || next + 1 < info.Length() || !ValidOptions(info, next) ||
                !OptionalFloat(isolate, Options(info, next), "duration", 500.0f, static_cast<float>(Shared::World::Fire::kMaxLifeMs), duration) ||
                !OptionalFloat(isolate, Options(info, next), "radius", 0.5f, Shared::World::Fire::kMaxRadius, fire.radius) ||
                !OptionalFloat(isolate, Options(info, next), "damage", 0.0f, Shared::World::Fire::kMaxDamage, fire.damage)) {
                ScriptArgs::Throw(isolate, "World.createFire(position, { duration?, radius?, damage? }) expects a position and finite options");
                return;
            }
            auto &server           = GetServer();
            fire.missionGeneration = server.MissionGeneration();
            fire.x                 = position.x;
            fire.y                 = position.y;
            fire.z                 = position.z;
            fire.lifeMs            = static_cast<uint32_t>(duration);
            if (fire.damage > 0.0f) {
                server.Combat().StartFire(0, fire.missionGeneration, position, std::chrono::milliseconds(fire.lifeMs), fire.radius, fire.damage);
            }
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(fire);
        }

        struct SoundOptions {
            float radius = 25.0f;
            float volume = 1.0f;
            uint64_t attachTo = 0;
        };

        bool ReadSound(const v8::FunctionCallbackInfo<v8::Value> &info, std::string &wave, glm::vec3 &position, SoundOptions &options) {
            auto *isolate = info.GetIsolate();
            int next      = 0;
            if (info.Length() < 2 || !info[0]->IsString() || !ScriptArgs::ReadPosition(info, 1, position, next) || next + 1 < info.Length() || !ValidOptions(info, next)) {
                return false;
            }
            wave = v8pp::from_v8<std::string>(isolate, info[0]);
            if (!SoundEntity::ValidWave(wave)) {
                return false;
            }
            const auto object = Options(info, next);
            if (!OptionalFloat(isolate, object, "radius", 1.0f, SoundEntity::kMaxRadius, options.radius) ||
                !OptionalFloat(isolate, object, "volume", 0.0f, SoundEntity::kMaxVolume, options.volume)) {
                return false;
            }
            if (!object.IsEmpty()) {
                v8::Local<v8::Value> attach;
                if (object->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "attachTo")).ToLocal(&attach) && !attach->IsUndefined() && !attach->IsNull()) {
                    options.attachTo = ScriptArgs::ReadEntityId(isolate, attach);
                    auto &server     = GetServer();
                    if (options.attachTo == 0 || (!server.Players().FindByNetworkId(options.attachTo) && !server.Cars().Find(options.attachTo))) {
                        return false;
                    }
                }
            }
            return true;
        }

        void JS_SoundCreate(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            std::string wave;
            glm::vec3 position;
            SoundOptions options;
            if (!ReadSound(info, wave, position, options)) {
                ScriptArgs::Throw(isolate, "Sound.create(wave, position, { radius?, volume?, attachTo? }) expects a game sound file, a position and valid options");
                return;
            }
            auto *sound = Service().CreateSound(wave, position, options.radius, options.volume, GetServer().MissionGeneration());
            if (!sound) {
                info.GetReturnValue().SetNull();
                return;
            }
            sound->attachedId = options.attachTo;
            Sound::GetClass(isolate);
            info.GetReturnValue().Set(v8pp::class_<Sound>::create_object(isolate, sound->GetNetworkID()));
        }

        void JS_SoundPlay(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::string wave;
            glm::vec3 position;
            SoundOptions options;
            if (!ReadSound(info, wave, position, options) || options.attachTo != 0) {
                ScriptArgs::Throw(info.GetIsolate(), "Sound.play(wave, position, { radius?, volume? }) expects a game sound file, a position and valid options");
                return;
            }
            Shared::World::PlaySound play;
            play.missionGeneration = GetServer().MissionGeneration();
            play.wave              = wave;
            play.x                 = position.x;
            play.y                 = position.y;
            play.z                 = position.z;
            play.radius            = options.radius;
            play.volume            = options.volume;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(play);
        }

        void JS_SoundAttachTo(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = v8pp::class_<Sound>::unwrap_object(isolate, info.This());
            auto *sound   = self ? self->ResolveSound() : nullptr;
            if (!sound || info.Length() != 1) {
                info.GetReturnValue().Set(false);
                return;
            }
            if (info[0]->IsNull() || info[0]->IsUndefined()) {
                sound->attachedId = 0;
                info.GetReturnValue().Set(true);
                return;
            }
            const uint64_t id = ScriptArgs::ReadEntityId(isolate, info[0]);
            auto &server      = GetServer();
            if (id == 0 || (!server.Players().FindByNetworkId(id) && !server.Cars().Find(id))) {
                ScriptArgs::Throw(isolate, "Sound.attachTo(entity) expects a Player, a Vehicle or null");
                return;
            }
            sound->attachedId = id;
            info.GetReturnValue().Set(true);
        }

        void JS_SoundGetAttached(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = v8pp::class_<Sound>::unwrap_object(isolate, info.This());
            const auto *sound = self ? self->ResolveSound() : nullptr;
            if (!sound || sound->attachedId == 0) {
                info.GetReturnValue().SetNull();
                return;
            }
            auto player = WrapPlayer(isolate, sound->attachedId);
            info.GetReturnValue().Set(player->IsNull() ? WrapVehicle(isolate, sound->attachedId) : player);
        }
    } // namespace

    std::unique_ptr<v8pp::class_<Sound>> Sound::_class;

    SoundEntity *Sound::ResolveSound() const {
        return Service().FindSound(_id);
    }

    std::string Sound::GetWave() const {
        const auto *sound = ResolveSound();
        return sound ? sound->wave : std::string();
    }

    double Sound::GetVolume() const {
        const auto *sound = ResolveSound();
        return sound ? sound->volume : 0.0;
    }

    void Sound::SetVolume(double volume) {
        if (auto *sound = ResolveSound(); sound && std::isfinite(volume)) {
            sound->volume = std::clamp(static_cast<float>(volume), 0.0f, SoundEntity::kMaxVolume);
        }
    }

    double Sound::GetRadius() const {
        const auto *sound = ResolveSound();
        return sound ? sound->radius : 0.0;
    }

    void Sound::SetRadius(double radius) {
        if (auto *sound = ResolveSound(); sound && std::isfinite(radius)) {
            sound->radius = std::clamp(static_cast<float>(radius), 1.0f, SoundEntity::kMaxRadius);
        }
    }

    bool Sound::GetEnabled() const {
        const auto *sound = ResolveSound();
        return sound && sound->enabled;
    }

    void Sound::SetEnabled(bool enabled) {
        if (auto *sound = ResolveSound()) {
            sound->enabled = enabled;
        }
    }

    bool Sound::Destroy() {
        return Service().DestroySound(_id);
    }

    std::string Sound::ToString() const {
        const auto *sound = ResolveSound();
        return sound ? "Sound(" + std::to_string(_id) + ", " + sound->wave + ")" : "Sound(" + std::to_string(_id) + ", destroyed)";
    }

    v8pp::class_<Sound> &Sound::GetClass(v8::Isolate *isolate) {
        if (_class) {
            return *_class;
        }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Sound>>(isolate, ServerCatalog(), "Sound",
            "A replicated positional sound. Every client in range builds its own native sound for it and hears it the same way; a late joiner hears it too. A mission change removes every sound.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing sound; use Sound.create to place one."));
        cls.function("toString", &Sound::ToString, docs("string", {}, "Formats this sound for logging.", "The sound ID and wave."));
        cls.property("wave", &Sound::GetWave, property_docs("string", "Game sound file, such as \"00_dog.wav\"."));
        cls.property("volume", &Sound::GetVolume, &Sound::SetVolume, property_docs("number", "Volume from 0 to 1."));
        cls.property("radius", &Sound::GetRadius, &Sound::SetRadius, property_docs("number", "Audible radius from 1 to 200 units."));
        cls.property("enabled", &Sound::GetEnabled, &Sound::SetEnabled, property_docs("boolean", "Whether the sound plays; a disabled sound keeps its place."));
        cls.prototype_function("attachTo", &JS_SoundAttachTo,
            docs("boolean", {param("entity", "Player | Vehicle | null", false, "Entity to follow, or null to stay where it is.")}, "Makes the sound follow a player or vehicle.", "False when the sound was destroyed."));
        cls.prototype_function("getAttached", &JS_SoundGetAttached, docs("Player | Vehicle | null", {}, "Returns the followed entity.", "The entity, or null for a fixed sound."));
        cls.function("destroy", &Sound::Destroy, docs("boolean", {}, "Removes the sound for everyone.", "False when it was already gone."));
        cls.static_function("create", &JS_SoundCreate,
            docs("Sound | null", {param("wave", "string", false, "Game sound file under the Sounds directory or its archives."), param("position", "Vector3 | { x: number; y: number; z: number }", false, "Where the sound plays."),
                                     param("options", "{ radius?: number; volume?: number; attachTo?: Player | Vehicle }", true, "Radius 1 to 200 (default 25), volume 0 to 1 (default 1) and an entity to follow.")},
                "Places a looping positional sound.", "The sound, or null when the limit of 64 sounds is reached."));
        cls.static_function("play", &JS_SoundPlay,
            docs("void", {param("wave", "string", false, "Game sound file."), param("position", "Vector3 | { x: number; y: number; z: number }", false, "Where the sound plays."),
                             param("options", "{ radius?: number; volume?: number }", true, "Radius and volume as for create.")},
                "Plays a sound once for every client currently in the mission."));
        return cls;
    }

    void Sound::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &cls    = GetClass(isolate);
        auto context = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "Sound"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }

    void World::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        auto &weather = ServerCatalog().data_type("WeatherInfo", "The replicated weather.");
        weather.add_property("weather", "\"default\" | \"clear\" | \"rain\" | \"snow\"", "Weather preset; default keeps the mission's own weather.");
        weather.add_property("intensity", "number | null", "Rain or snow intensity from 0 to 100, or null for the preset's own.");

        v8pp::module world(isolate, ServerCatalog(), "World", "The shared Mafia 1 world: mission, collections, weather, scene frames, city music and effects everyone sees.");
        world.function("getMission", &JS_GetMission, docs("string", {}, "Returns the current stock mission.", "Stock mission directory name."));
        world.function("getMissionGeneration", &JS_GetMissionGeneration, docs("number", {}, "Returns the current mission generation.", "It increases on every change, including a reload."));
        world.function("changeMission", &JS_ChangeMission,
            docs("boolean", {param("mission", "string", false, "Stock mission name, such as \"freeride\".")}, "Resets the world and asks every client to load the mission. Fires missionChange.",
                "False for an unknown mission."));
        world.function("isReady", &JS_IsReady, docs("boolean", {}, "Whether every connected player has loaded the current mission.", "True when all are ready."));
        world.function("getPlayers", &JS_GetPlayers, docs("Player[]", {}, "Lists the connected players.", "Every connected player."));
        world.function("getVehicles", &JS_GetVehicles, docs("Vehicle[]", {}, "Lists the server vehicles.", "Every vehicle."));
        world.function("getPickups", &JS_GetPickups, docs("Pickup[]", {}, "Lists the weapon pickups.", "Every pickup."));
        world.function("getDoors", &JS_GetDoors, docs("Door[]", {}, "Lists the registered native mission doors.", "Every managed door in the current mission."));
        world.function("setWeather", &JS_SetWeather,
            docs("void", {param("weather", "\"default\" | \"clear\" | \"rain\" | \"snow\"", false, "Weather preset."), param("intensity", "number", true, "Rain or snow intensity from 0 to 100.")},
                "Changes the weather for everyone, including late joiners; it carries over mission changes."));
        world.function("getWeather", &JS_GetWeather, docs("WeatherInfo", {}, "Returns the replicated weather.", "The weather preset and intensity."));
        world.function("setCityMusic", &JS_SetCityMusic, docs("void", {param("enabled", "boolean", false, "Whether the city's ambient music plays.")}, "Turns the city music on or off for everyone."));
        world.function("isCityMusicEnabled", &JS_IsCityMusicEnabled, docs("boolean", {}, "Whether the city music plays.", "True when enabled."));
        world.function("setNightMode", &JS_SetNightMode,
            docs("void", {param("enabled", "boolean | null", false, "True or false overrides the mission's night flag; null restores the mission's own flag.")},
                "Replicates the native GAME_NIGHTMISSION flag to every client, including late joiners. It controls night behavior such as vehicle lights; it does not change the sky or time of day."));
        world.function("getNightModeOverride", &JS_GetNightModeOverride,
            docs("boolean | null", {}, "Returns the server's night-mode override.", "Null when each mission script controls its own flag."));
        world.function("setFrameVisible", &JS_SetFrameVisible,
            docs("boolean", {param("name", "string", false, "Name of a static frame in the current mission's scene."), param("visible", "boolean", false, "Whether it shows.")},
                "Shows or hides a scene object for everyone, including late joiners, until the mission changes.", "False when 128 frames are already overridden."));
        world.function("resetFrames", &JS_ResetFrames, docs("void", {}, "Restores every overridden frame to the scene's own visibility."));
        world.function("setFrameOpacity", &JS_SetFrameOpacity,
            docs("boolean", {param("name", "string", false, "Named static visual frame in the current mission's scene."), param("opacity", "number", false, "Opacity from 0 (transparent) to 1 (opaque).")},
                "Applies FRM_SETALPHA to a supported visual frame for everyone in this mission, including late joiners.", "False when 128 opacity overrides already exist."));
        world.function("resetFrameOpacity", &JS_ResetFrameOpacity,
            docs("void", {param("name", "string", true, "One frame to restore; omit to restore every overridden frame.")}, "Restores the scene's original opacity for one or all overridden frames."));
        world.function("createExplosion", &JS_CreateExplosion,
            docs("void", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Explosion centre."),
                             param("options", "{ radius?: number; damage?: number }", true, "Radius up to 30 (default 15) and damage up to 1000 (default 400). Damage 0 is a visual explosion.")},
                "Explodes for every client. The server applies player damage and fires playerDamage and playerDeath; clients damage cars and objects."));
        world.function("createFire", &JS_CreateFire,
            docs("void", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Fire position."),
                             param("options", "{ duration?: number; radius?: number; damage?: number }", true, "Duration 500 to 60000 ms (default 5000), radius up to 10 (default 2.5) and damage per second up to 200 (default 50).")},
                "Starts a fire every client sees; the server burns players standing in it."));
        world.publish(global);
    }
} // namespace Mafia1Online::Scripting
