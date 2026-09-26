#include "script_service.h"

#include "player_scripting.h"
#include "door_scripting.h"
#include "script_runtime.h"
#include "visual_scripting.h"

#include "features/world/world_service.h"
#include "shared/features/pickup/weapon_pickup_entity.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <v8pp/convert.hpp>

#include <unordered_set>
#include <vector>

namespace Mafia1Online::Features::Script {
    namespace {
        void EmitMission(const char *name, const std::string &mission, uint64_t generation) {
            Scripting::EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context> context, Scripting::EventArguments &args) {
                auto info = v8::Object::New(isolate);
                info->Set(context, v8pp::to_v8(isolate, "mission"), v8pp::to_v8(isolate, mission)).Check();
                info->Set(context, v8pp::to_v8(isolate, "missionGeneration"), v8::Number::New(isolate, static_cast<double>(generation))).Check();
                args.push_back(info);
            });
        }

        void EmitPlayer(const char *name, uint64_t id) {
            Scripting::EmitEvent(name, [id](v8::Isolate *isolate, v8::Local<v8::Context>, Scripting::EventArguments &args) {
                args.push_back(Scripting::WrapPlayer(isolate, id));
            });
        }

        void EmitPickup(const char *name, uint64_t id) {
            Scripting::EmitEvent(name, [id](v8::Isolate *isolate, v8::Local<v8::Context>, Scripting::EventArguments &args) {
                args.push_back(Scripting::WrapPickup(isolate, id));
            });
        }
        void EmitDoor(const char *name, uint64_t id) {
            Scripting::EmitEvent(name, [id](v8::Isolate *isolate, v8::Local<v8::Context>, Scripting::EventArguments &args) {
                args.push_back(Scripting::WrapDoor(isolate, id));
            });
        }
    } // namespace

    void ScriptService::EmitPickupStreamOut(const PickupSnapshot &snapshot) {
        Scripting::EmitEvent("pickupStreamOut", [snapshot](v8::Isolate *isolate, v8::Local<v8::Context> context, Scripting::EventArguments &args) {
            const auto number = [isolate](double value) { return v8::Number::New(isolate, value); };
            auto position = v8::Object::New(isolate);
            position->Set(context, v8pp::to_v8(isolate, "x"), number(snapshot.x)).Check();
            position->Set(context, v8pp::to_v8(isolate, "y"), number(snapshot.y)).Check();
            position->Set(context, v8pp::to_v8(isolate, "z"), number(snapshot.z)).Check();
            auto lastKnown = v8::Object::New(isolate);
            lastKnown->Set(context, v8pp::to_v8(isolate, "id"), number(static_cast<double>(snapshot.id))).Check();
            lastKnown->Set(context, v8pp::to_v8(isolate, "missionGeneration"), number(static_cast<double>(snapshot.missionGeneration))).Check();
            lastKnown->Set(context, v8pp::to_v8(isolate, "weaponId"), number(snapshot.weaponId)).Check();
            lastKnown->Set(context, v8pp::to_v8(isolate, "position"), position).Check();
            lastKnown->Set(context, v8pp::to_v8(isolate, "heading"), number(snapshot.heading)).Check();
            lastKnown->Set(context, v8pp::to_v8(isolate, "loaded"), number(snapshot.loaded)).Check();
            lastKnown->Set(context, v8pp::to_v8(isolate, "reserve"), number(snapshot.reserve)).Check();
            args.push_back(number(static_cast<double>(snapshot.id)));
            args.push_back(lastKnown);
        });
    }

    void ScriptService::EmitDoorStreamOut(const DoorSnapshot &snapshot) {
        Scripting::EmitEvent("doorStreamOut", [snapshot](v8::Isolate *isolate, v8::Local<v8::Context> context, Scripting::EventArguments &args) {
            auto lastKnown = v8::Object::New(isolate);
            const auto set = [&](const char *key, v8::Local<v8::Value> value) {
                lastKnown->Set(context, v8pp::to_v8(isolate, key), value).Check();
            };
            set("id", v8::Number::New(isolate, static_cast<double>(snapshot.id)));
            set("missionGeneration", v8::Number::New(isolate, static_cast<double>(snapshot.missionGeneration)));
            set("frameName", v8pp::to_v8(isolate, snapshot.frameName));
            auto position = v8::Object::New(isolate);
            position->Set(context, v8pp::to_v8(isolate, "x"), v8::Number::New(isolate, snapshot.x)).Check();
            position->Set(context, v8pp::to_v8(isolate, "y"), v8::Number::New(isolate, snapshot.y)).Check();
            position->Set(context, v8pp::to_v8(isolate, "z"), v8::Number::New(isolate, snapshot.z)).Check();
            set("position", position);
            set("open", v8::Boolean::New(isolate, snapshot.open));
            set("locked", v8::Boolean::New(isolate, snapshot.locked));
            set("enabled", v8::Boolean::New(isolate, snapshot.enabled));
            set("reverse", v8::Boolean::New(isolate, snapshot.reverse));
            set("openFraction", v8::Number::New(isolate, snapshot.openFraction));
            args.push_back(v8::Number::New(isolate, static_cast<double>(snapshot.id)));
            args.push_back(lastKnown);
        });
    }

    void ScriptService::Update(World::WorldService &world) {
        Scripting::BeginDrawFrame();
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        std::unordered_set<uint64_t> seen;
        std::vector<std::pair<const char *, uint64_t>> events;
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            const uint64_t id = player->GetNetworkID();
            seen.insert(id);
            const bool alive = player->spawned && player->alive;
            auto it          = _players.find(id);
            if (it == _players.end()) {
                _players[id] = {player->spawnGeneration, alive};
                events.emplace_back("playerStreamIn", id);
                return;
            }
            if (alive && (!it->second.alive || it->second.spawnGeneration != player->spawnGeneration)) {
                events.emplace_back("playerSpawn", id);
            }
            else if (!alive && it->second.alive && player->spawned) {
                events.emplace_back("playerDeath", id);
            }
            it->second = {player->spawnGeneration, alive};
        });
        std::vector<uint64_t> gone;
        for (const auto &[id, state] : _players) {
            if (!seen.contains(id)) {
                gone.push_back(id);
            }
        }
        for (const uint64_t id : gone) {
            _players.erase(id);
            Scripting::EmitEvent("playerStreamOut", [id](v8::Isolate *isolate, v8::Local<v8::Context>, Scripting::EventArguments &args) {
                args.push_back(v8::Number::New(isolate, static_cast<double>(id)));
            });
        }
        // Handlers run after the ForEach, so a script can walk the replicas.
        for (const auto &[name, id] : events) {
            EmitPlayer(name, id);
        }

        std::unordered_set<uint64_t> seenPickups;
        std::vector<uint64_t> newPickups;
        if (world.IsReady()) {
            replication->ForEach<Shared::Entities::WeaponPickupEntity>([&](Shared::Entities::WeaponPickupEntity *pickup) {
                if (pickup->missionGeneration != world.LoadedMissionGeneration()) {
                    return;
                }
                const uint64_t id = pickup->GetNetworkID();
                seenPickups.insert(id);
                if (!_pickups.contains(id)) {
                    newPickups.push_back(id);
                }
                _pickups[id] = {id, pickup->missionGeneration, pickup->weaponId, pickup->position.x, pickup->position.y, pickup->position.z,
                    pickup->yaw, pickup->loaded, pickup->reserve};
            });
        }
        std::vector<PickupSnapshot> gonePickups;
        for (const auto &[id, snapshot] : _pickups) {
            if (!seenPickups.contains(id)) {
                gonePickups.push_back(snapshot);
            }
        }
        for (const auto &snapshot : gonePickups) {
            _pickups.erase(snapshot.id);
            EmitPickupStreamOut(snapshot);
        }
        for (uint64_t id : newPickups) {
            EmitPickup("pickupStreamIn", id);
        }

        std::unordered_set<uint64_t> seenDoors;
        std::vector<uint64_t> newDoors;
        std::vector<uint64_t> changedDoors;
        if (world.IsReady()) {
            replication->ForEach<Shared::Entities::DoorEntity>([&](Shared::Entities::DoorEntity *door) {
                if (door->missionGeneration != world.LoadedMissionGeneration()) { return; }
                const uint64_t id = door->GetNetworkID();
                seenDoors.insert(id);
                const auto it = _doors.find(id);
                if (it == _doors.end()) { newDoors.push_back(id); }
                else if (it->second.revision != door->revision) { changedDoors.push_back(id); }
                _doors[id] = {id, door->missionGeneration, door->revision, door->frameName,
                    door->position.x, door->position.y, door->position.z, door->open, door->locked,
                    door->enabled, door->reverse, door->open ? door->openFraction : 0.0f};
            });
        }
        std::vector<DoorSnapshot> goneDoors;
        for (const auto &[id, snapshot] : _doors) {
            if (!seenDoors.contains(id)) { goneDoors.push_back(snapshot); }
        }
        for (const auto &snapshot : goneDoors) {
            _doors.erase(snapshot.id);
            EmitDoorStreamOut(snapshot);
        }
        for (uint64_t id : newDoors) { EmitDoor("doorStreamIn", id); }
        for (uint64_t id : changedDoors) { EmitDoor("doorChange", id); }

        if (world.IsReady() && _readyGeneration != world.LoadedMissionGeneration()) {
            _readyGeneration = world.LoadedMissionGeneration();
            _readyMission    = world.SelectedMission();
            EmitMission("missionReady", _readyMission, _readyGeneration);
        }
        if (world.IsReady()) {
            Scripting::EmitEvent("render");
        }
    }

    void ScriptService::OnMissionClosing() {
        std::unordered_map<uint64_t, PickupSnapshot> pickups;
        pickups.swap(_pickups);
        for (const auto &[id, snapshot] : pickups) {
            EmitPickupStreamOut(snapshot);
        }
        std::unordered_map<uint64_t, DoorSnapshot> doors;
        doors.swap(_doors);
        for (const auto &[id, snapshot] : doors) {
            EmitDoorStreamOut(snapshot);
        }
        if (_readyGeneration != 0) {
            EmitMission("missionUnload", _readyMission, _readyGeneration);
        }
        Scripting::ResetLocalVisuals();
        _readyGeneration = 0;
        _readyMission.clear();
    }

    void ScriptService::Reset() {
        Scripting::ResetLocalVisuals();
        _players.clear();
        _pickups.clear();
        _doors.clear();
        _readyGeneration = 0;
        _readyMission.clear();
    }
} // namespace Mafia1Online::Features::Script
