#include "world_scripting.h"

#include "player_scripting.h"
#include "door_scripting.h"
#include "script_runtime.h"
#include "visual_scripting.h"

#include "features/world/world_service.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/pickup/weapon_pickup_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/world/world_state_entity.h"
#include "shared/scripting_catalog.h"

#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/world/native_environment.h>

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <array>
#include <string_view>

namespace Mafia1Online::Scripting {
    namespace {
        using Shared::Entities::WorldStateEntity;
        using v8pp::metadata::docs;
        using v8pp::metadata::param;

        constexpr std::array<std::string_view, WorldStateEntity::kWeatherCount> kWeatherNames {"default", "clear", "rain", "snow"};
        constexpr size_t kMaxFrameNameLength = 64;

        template <typename T>
        void List(const v8::FunctionCallbackInfo<v8::Value> &info, v8::Local<v8::Value> (*wrap)(v8::Isolate *, uint64_t)) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<T>([&](T *entity) {
                    auto handle = wrap(isolate, entity->GetNetworkID());
                    if (!handle->IsNull()) {
                        list->Set(context, index++, handle).Check();
                    }
                });
            }
            info.GetReturnValue().Set(list);
        }

        void JS_GetPlayers(const v8::FunctionCallbackInfo<v8::Value> &info) {
            List<Shared::Entities::PlayerEntity>(info, &WrapPlayer);
        }

        void JS_GetVehicles(const v8::FunctionCallbackInfo<v8::Value> &info) {
            List<Shared::Entities::CarEntity>(info, &WrapVehicle);
        }

        void JS_GetPickups(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto list    = v8::Array::New(isolate);
            uint32_t index = 0;
            const auto &world = GetWorld();
            if (world.IsReady()) {
                if (auto *replication = Framework::CoreModules::GetReplication()) {
                    replication->ForEach<Shared::Entities::WeaponPickupEntity>([&](Shared::Entities::WeaponPickupEntity *pickup) {
                        if (pickup->missionGeneration != world.LoadedMissionGeneration()) {
                            return;
                        }
                        auto handle = WrapPickup(isolate, pickup->GetNetworkID());
                        if (!handle->IsNull()) {
                            list->Set(context, index++, handle).Check();
                        }
                    });
                }
            }
            info.GetReturnValue().Set(list);
        }

        void JS_GetDoors(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto list = v8::Array::New(isolate);
            uint32_t index = 0;
            const auto &world = GetWorld();
            if (world.IsReady()) {
                if (auto *replication = Framework::CoreModules::GetReplication()) {
                    replication->ForEach<Shared::Entities::DoorEntity>([&](Shared::Entities::DoorEntity *door) {
                        if (door->missionGeneration == world.LoadedMissionGeneration()) {
                            list->Set(context, index++, WrapDoor(isolate, door->GetNetworkID())).Check();
                        }
                    });
                }
            }
            info.GetReturnValue().Set(list);
        }

        void JS_GetMission(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto &world = GetWorld();
            info.GetReturnValue().Set(world.IsReady() ? v8pp::to_v8(info.GetIsolate(), world.SelectedMission()).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_GetMissionGeneration(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<double>(GetWorld().LoadedMissionGeneration()));
        }

        void JS_IsReady(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(GetWorld().IsReady());
        }

        void JS_GetWeather(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto context  = isolate->GetCurrentContext();
            const WorldStateEntity *state = nullptr;
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<WorldStateEntity>([&](WorldStateEntity *entity) {
                    state = entity;
                });
            }
            auto weather       = v8::Object::New(isolate);
            const auto preset  = state && state->weather < kWeatherNames.size() ? kWeatherNames[state->weather] : kWeatherNames[0];
            const bool hasRain = state && state->rainIntensity >= 0.0f;
            weather->Set(context, v8pp::to_v8(isolate, "weather"), v8pp::to_v8(isolate, std::string(preset))).Check();
            weather->Set(context, v8pp::to_v8(isolate, "intensity"), hasRain ? v8::Number::New(isolate, state->rainIntensity).As<v8::Value>() : v8::Null(isolate).As<v8::Value>()).Check();
            weather->Set(context, v8pp::to_v8(isolate, "cityMusic"), v8::Boolean::New(isolate, !state || state->cityMusicEnabled)).Check();
            info.GetReturnValue().Set(weather);
        }

        void JS_GetNightMode(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *mission = SDK::Core::Mission::Get();
            if (!GetWorld().IsReady() || !mission || !mission->Game()) {
                info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
                return;
            }
            info.GetReturnValue().Set(SDK::World::IsNightMode(mission->Game()));
        }

        void JS_GetMissionFrame(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            if (info.Length() != 1 || !info[0]->IsString()) {
                Args::Throw(isolate, "World.getMissionFrame(name) expects a frame name");
                return;
            }
            const std::string name = v8pp::from_v8<std::string>(isolate, info[0]);
            if (name.empty() || name.size() > kMaxFrameNameLength || !GetWorld().IsReady()) {
                info.GetReturnValue().SetNull();
                return;
            }
            auto *scene = SDK::Core::Mission::Get()->GetScene();
            auto *frame = scene->FindFrame(name.c_str());
            if (!frame) {
                info.GetReturnValue().SetNull();
                return;
            }
            const auto position = frame->WorldPosition();
            const auto direction = frame->WorldDirection();
            auto context = isolate->GetCurrentContext();
            auto result = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "position"), Args::Position(isolate, {position.x, position.y, position.z})).Check();
            result->Set(context, v8pp::to_v8(isolate, "direction"), Args::Position(isolate, {direction.x, direction.y, direction.z})).Check();
            info.GetReturnValue().Set(result);
        }
    } // namespace

    void ClientWorld::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &weather = ClientCatalog().data_type("ClientWeatherInfo", "The weather the server replicated to this client.");
        weather.add_property("weather", "\"default\" | \"clear\" | \"rain\" | \"snow\"", "Weather preset; default keeps the mission's own weather.");
        weather.add_property("intensity", "number | null", "Rain or snow intensity from 0 to 100, or null for the preset's own.");
        weather.add_property("cityMusic", "boolean", "Whether the city's ambient music plays.");

        auto &frame = ClientCatalog().data_type("ClientMissionFrame", "A named frame from the loaded native mission scene. Its vectors are snapshots.");
        frame.add_property("position", "Vector3", "World position of the named frame.");
        frame.add_property("direction", "Vector3", "World forward direction of the named frame.");

        v8pp::module world(isolate, ClientCatalog(), "World", "Read-only view of the mission and the entities streamed to this client. The server owns the world; change it from a server script.");
        world.function("getPlayers", &JS_GetPlayers, docs("Player[]", {}, "Lists the players streamed to this client, including the local one.", "Every streamed player."));
        world.function("getVehicles", &JS_GetVehicles, docs("Vehicle[]", {}, "Lists the vehicles streamed to this client.", "Every streamed vehicle."));
        world.function("getPickups", &JS_GetPickups, docs("Pickup[]", {}, "Lists the weapon pickups streamed into the loaded mission.", "Script-created and dropped pickups in this mission; empty while no mission is loaded."));
        world.function("getDoors", &JS_GetDoors, docs("Door[]", {}, "Lists the mission doors the server is synchronizing.", "Empty before a mission has loaded."));
        world.function("getMission", &JS_GetMission, docs("string | null", {}, "Returns the loaded stock mission.", "The mission name, or null while none is loaded."));
        world.function("getMissionGeneration", &JS_GetMissionGeneration, docs("number", {}, "Returns the server generation of the loaded mission.", "The generation, or 0 while none is loaded."));
        world.function("isReady", &JS_IsReady, docs("boolean", {}, "Whether the native mission is loaded.", "True while a mission runs."));
        world.function("getWeather", &JS_GetWeather, docs("ClientWeatherInfo", {}, "Returns the replicated weather and city music state.", "The weather preset, intensity and city music flag."));
        world.function("getNightMode", &JS_GetNightMode, docs("boolean | null", {}, "Reads the loaded native mission's effective night flag.", "Null before the mission is ready."));
        world.function("getMissionFrame", &JS_GetMissionFrame,
            docs("ClientMissionFrame | null", {param("name", "string", false, "Native scene frame name, such as the Free Ride spawn anchor emeth_1.")},
                "Reads a named frame's world position and direction from the loaded mission.", "A snapshot, or null when the frame or mission is unavailable."));
        world.function("findWorldFrame", &JS_FindWorldFrame,
            docs("Frame | null", {param("name", "string", false, "Native scene frame name, such as emeth_1.")},
                "Finds a borrowed, read-only native mission frame. Its getters resolve fresh values on every call.", "A live Frame handle, or null when the frame or mission is unavailable."));
        world.publish(global);
    }
} // namespace Mafia1Online::Scripting
