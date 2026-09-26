#include "door_scripting.h"

#include "core/server.h"
#include "features/script/script_events.h"
#include "features/script/script_runtime.h"
#include "features/world/world_script_service.h"
#include "shared/scripting_catalog.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <v8pp/convert.hpp>

#include <cmath>

namespace Mafia1Online::Scripting {
    std::unique_ptr<v8pp::class_<Door>> Door::_class;

    namespace {
        void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            glm::vec3 position;
            int next = 0;
            if (info.Length() != 2 || !info[0]->IsString() || !ScriptArgs::ReadPosition(info, 1, position, next) || next != 2) {
                ScriptArgs::Throw(isolate, "Door.create(frameName, position) expects a frame name and finite world position");
                return;
            }
            const auto name = v8pp::from_v8<std::string>(isolate, info[0]);
            if (!Features::World::WorldScriptService::ValidDoorName(name)) {
                ScriptArgs::Throw(isolate, "Door.create: invalid frame name");
                return;
            }
            auto &server = GetServer();
            auto *door = server.WorldScript().CreateDoor(name, position, server.MissionGeneration());
            info.GetReturnValue().Set(WrapDoor(isolate, door ? door->GetNetworkID() : 0));
        }

        void JS_Get(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            if (info.Length() != 1 || !info[0]->IsString()) {
                ScriptArgs::Throw(isolate, "Door.get(frameName) expects a frame name");
                return;
            }
            auto *door = GetServer().WorldScript().FindDoor(v8pp::from_v8<std::string>(isolate, info[0]));
            info.GetReturnValue().Set(WrapDoor(isolate, door ? door->GetNetworkID() : 0));
        }

        void JS_GetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
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
    } // namespace

    v8::Local<v8::Value> WrapDoor(v8::Isolate *isolate, uint64_t id) {
        if (!id || !GetServer().WorldScript().FindDoor(id)) {
            return v8::Null(isolate);
        }
        Door::GetClass(isolate);
        return v8pp::class_<Door>::create_object(isolate, id);
    }

    Shared::Entities::DoorEntity *Door::Resolve() const {
        return GetServer().WorldScript().FindDoor(_id);
    }
    std::string Door::GetName() const { const auto *door = Resolve(); return door ? door->frameName : std::string(); }
    bool Door::GetOpen() const { const auto *door = Resolve(); return door && door->open; }
    bool Door::GetLocked() const { const auto *door = Resolve(); return door && door->locked; }
    void Door::SetLocked(bool locked) {
        if (auto *door = Resolve(); door && door->locked != locked) {
            door->locked = locked;
            ++door->revision;
            Features::Script::EmitDoorEvent(GetServer(), "doorLockChange", *door, 0);
        }
    }
    bool Door::GetEnabled() const { const auto *door = Resolve(); return door && door->enabled; }
    void Door::SetEnabled(bool enabled) {
        if (auto *door = Resolve(); door && door->enabled != enabled) {
            door->enabled = enabled;
            ++door->revision;
            Features::Script::EmitDoorEvent(GetServer(), "doorInteractionChange", *door, 0);
        }
    }
    bool Door::GetReverse() const { const auto *door = Resolve(); return door && door->reverse; }
    double Door::GetOpenFraction() const { const auto *door = Resolve(); return door && door->open ? door->openFraction : 0.0; }
    bool Door::Open(bool reverse, bool pairedReverse) {
        auto *door = Resolve();
        if (!door || door->locked) { return false; }
        if (door->open && door->openFraction == 1.0f && door->reverse == reverse && door->pairedReverse == pairedReverse) { return true; }
        door->open = true;
        door->reverse = reverse;
        door->pairedReverse = pairedReverse;
        door->openFraction = 1.0f;
        ++door->revision;
        Features::Script::EmitDoorEvent(GetServer(), "doorStateChange", *door, 0);
        return true;
    }
    bool Door::Close() {
        auto *door = Resolve();
        if (!door) { return false; }
        if (!door->open && door->openFraction == 1.0f) { return true; }
        door->open = false;
        door->openFraction = 1.0f;
        ++door->revision;
        Features::Script::EmitDoorEvent(GetServer(), "doorStateChange", *door, 0);
        return true;
    }
    bool Door::SetOpenFraction(double fraction, bool reverse, bool pairedReverse) {
        auto *door = Resolve();
        if (!door || !std::isfinite(fraction) || fraction < 0.0 || fraction > 1.0 || (door->locked && fraction > 0.0)) { return false; }
        door->open = fraction > 0.0;
        door->openFraction = static_cast<float>(fraction);
        door->reverse = reverse;
        door->pairedReverse = pairedReverse;
        ++door->revision;
        Features::Script::EmitDoorEvent(GetServer(), "doorStateChange", *door, 0);
        return true;
    }
    bool Door::Destroy() { return GetServer().WorldScript().RemoveDoor(_id); }
    std::string Door::ToString() const { return "Door(" + std::to_string(_id) + ", " + GetName() + ")"; }

    v8pp::class_<Door> &Door::GetClass(v8::Isolate *isolate) {
        if (_class) { return *_class; }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Door>>(isolate, ServerCatalog(), "Door",
            "A server-owned mission door. Native use opens or closes it for everyone; its lock, swing direction, paired leaf and pose survive reconnects until the mission changes.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Door network ID.")}, "Creates a handle for a replicated mission door."));
        cls.property("frameName", &Door::GetName, property_docs("string", "Name of the root door frame in the mission."));
        cls.property("open", &Door::GetOpen, property_docs("boolean", "Target open state."));
        cls.property("locked", &Door::GetLocked, &Door::SetLocked, property_docs("boolean", "Locks native use and script opening."));
        cls.property("enabled", &Door::GetEnabled, &Door::SetEnabled, property_docs("boolean", "Whether the native use prompt can operate this door."));
        cls.property("reverse", &Door::GetReverse, property_docs("boolean", "Current swing side of the root leaf."));
        cls.property("openFraction", &Door::GetOpenFraction, property_docs("number", "Target open fraction from 0 to 1."));
        cls.function("openDoor", &Door::Open, docs("boolean", {param("reverse", "boolean", false, "Root leaf swing side."), param("pairedReverse", "boolean", false, "Paired leaf swing side; use the opposite value for outward double doors.")}, "Animates the door open, including its paired leaf. Fires doorStateChange."));
        cls.function("closeDoor", &Door::Close, docs("boolean", {}, "Animates the door closed, including its paired leaf. Fires doorStateChange."));
        cls.function("setOpenFraction", &Door::SetOpenFraction, docs("boolean", {param("fraction", "number", false, "0 to 1."), param("reverse", "boolean", false, "Root swing side."), param("pairedReverse", "boolean", false, "Paired leaf swing side.")}, "Snaps both leaves to a partial or full pose and replicates it."));
        cls.function("destroy", &Door::Destroy, docs("boolean", {}, "Stops managing this mission door until its next native use."));
        cls.function("toString", &Door::ToString, docs("string", {}, "Formats the door ID and frame name."));
        return cls;
    }

    void Door::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        auto &cls = GetClass(isolate);
        cls.static_function("create", &JS_Create, docs("Door | null", {param("frameName", "string", false, "Root door frame name."), param("position", "Vector3 | { x: number; y: number; z: number }", false, "World hinge position for server reach checks.")}, "Registers a stock mission door for server control. Native use also registers nearby doors automatically."));
        cls.static_function("get", &JS_Get, docs("Door | null", {param("frameName", "string", false, "Root frame name.")}, "Finds a registered door by name."));
        cls.static_function("getAll", &JS_GetAll, docs("Door[]", {}, "Lists registered mission doors."));
        auto context = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "Door"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }
} // namespace Mafia1Online::Scripting
