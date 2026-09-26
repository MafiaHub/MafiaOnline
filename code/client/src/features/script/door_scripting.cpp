#include "door_scripting.h"

#include "shared/features/door/door_entity.h"
#include "shared/scripting_catalog.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <stdexcept>

namespace Mafia1Online::Scripting {
    using Shared::Entities::DoorEntity;
    using v8pp::metadata::docs;
    using v8pp::metadata::param;
    using v8pp::metadata::property_docs;

    std::unique_ptr<v8pp::class_<Door>> Door::_class;

    Door::Door(uint64_t id): Framework::Scripting::Builtins::Entity(id) {
        if (!ResolveDoor()) {
            throw std::runtime_error("Door handle does not refer to a streamed mission door");
        }
    }

    v8::Local<v8::Value> WrapDoor(v8::Isolate *isolate, uint64_t id) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!id || !replication || !replication->GetEntity<DoorEntity>(id)) {
            return v8::Null(isolate);
        }
        Door::GetClass(isolate);
        return v8pp::class_<Door>::create_object(isolate, id);
    }

    DoorEntity *Door::ResolveDoor() const { return dynamic_cast<DoorEntity *>(Resolve()); }
    Framework::Scripting::Builtins::Vector3 Door::GetDoorPosition() const {
        const auto *door = ResolveDoor();
        return door ? Framework::Scripting::Builtins::Vector3(door->position.x, door->position.y, door->position.z)
                    : Framework::Scripting::Builtins::Vector3();
    }
    std::string Door::GetFrameName() const { const auto *door = ResolveDoor(); return door ? door->frameName : std::string(); }
    bool Door::GetOpen() const { const auto *door = ResolveDoor(); return door && door->open; }
    bool Door::GetLocked() const { const auto *door = ResolveDoor(); return door && door->locked; }
    bool Door::GetEnabled() const { const auto *door = ResolveDoor(); return door && door->enabled; }
    bool Door::GetReverse() const { const auto *door = ResolveDoor(); return door && door->reverse; }
    double Door::GetOpenFraction() const { const auto *door = ResolveDoor(); return door && door->open ? door->openFraction : 0.0; }
    double Door::GetMissionGeneration() const { const auto *door = ResolveDoor(); return door ? static_cast<double>(door->missionGeneration) : 0.0; }
    std::string Door::ToString() const { return "Door(" + std::to_string(GetId()) + ", " + GetFrameName() + ")"; }

    v8pp::class_<Door> &Door::GetClass(v8::Isolate *isolate) {
        if (_class) { return *_class; }
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Door>>(isolate, ClientCatalog(), "Door",
            "Read-only view of a server-managed mission door, including its paired leaf's shared target state.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Door network ID.")}, "Creates a handle for a streamed mission door."));
        cls.property("position", &Door::GetDoorPosition, property_docs("Vector3", "Replicated hinge position, read only on the client."));
        cls.property("frameName", &Door::GetFrameName, property_docs("string", "Name of the root door frame."));
        cls.property("open", &Door::GetOpen, property_docs("boolean", "Server target open state."));
        cls.property("locked", &Door::GetLocked, property_docs("boolean", "Whether native opening is locked."));
        cls.property("enabled", &Door::GetEnabled, property_docs("boolean", "Whether native use is enabled."));
        cls.property("reverse", &Door::GetReverse, property_docs("boolean", "Swing side of the root leaf."));
        cls.property("openFraction", &Door::GetOpenFraction, property_docs("number", "Target open fraction from 0 to 1."));
        cls.property("missionGeneration", &Door::GetMissionGeneration, property_docs("number", "Mission generation this door belongs to."));
        cls.function("toString", &Door::ToString, docs("string", {}, "Formats the door ID and frame name."));
        return cls;
    }

    void Door::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) { GetClass(isolate).publish(global); }
} // namespace Mafia1Online::Scripting
