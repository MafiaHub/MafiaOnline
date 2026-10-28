#include "npc_scripting.h"

#include "core/server.h"
#include "features/combat/weapon_scripting.h"
#include "features/script/script_runtime.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>

#include <cmath>
#include <sstream>

namespace Mafia1Online::Scripting {
    std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Npc>>> Npc::_classes;

    namespace {
        void List(const v8::FunctionCallbackInfo<v8::Value> &info, const std::function<bool(const Shared::Entities::NpcEntity &)> &include) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            GetServer().Npcs().ForEach([&](Shared::Entities::NpcEntity &npc) {
                if (include(npc)) {
                    list->Set(context, index++, WrapNpc(isolate, npc.GetNetworkID())).Check();
                }
            });
            info.GetReturnValue().Set(list);
        }

        void JS_NpcGetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            List(info, [](const Shared::Entities::NpcEntity &) { return true; });
        }

        // Npc.getInRange(position, radius)
        void JS_NpcGetInRange(const v8::FunctionCallbackInfo<v8::Value> &info) {
            glm::vec3 center;
            float radius = 0.0f;
            int next = 0;
            if (!ScriptArgs::ReadPosition(info, 0, center, next) || info.Length() != next + 1 || !ScriptArgs::ReadFloat(info[next], radius) || radius < 0.0f) {
                ScriptArgs::Throw(info.GetIsolate(), "Npc.getInRange(position, radius) expects a finite position and a radius of 0 or more");
                return;
            }
            const float radiusSq = radius * radius;
            List(info, [&](const Shared::Entities::NpcEntity &npc) {
                const auto offset = npc.position - center;
                return offset.x * offset.x + offset.y * offset.y + offset.z * offset.z <= radiusSq;
            });
        }

        void JS_NpcGetCount(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<uint32_t>(GetServer().Npcs().Count()));
        }

        void JS_NpcGetInventory(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Npc>::unwrap_object(info.GetIsolate(), info.This());
            info.GetReturnValue().Set(self ? self->GetInventory(info.GetIsolate()) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        // The car a stolen traffic car's driver was thrown out of, if it is
        // still a vehicle of this mission.
        void JS_NpcGetThrownFromVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self = v8pp::class_<Npc>::unwrap_object(isolate, info.This());
            const auto *npc = self ? self->ResolveNpc() : nullptr;
            const uint64_t carId = npc ? npc->thrownFromCarId : 0;
            info.GetReturnValue().Set(carId && GetServer().Cars().Find(carId) ? WrapVehicle(isolate, carId) : v8::Null(isolate).As<v8::Value>());
        }
    } // namespace

    Shared::Entities::NpcEntity *Npc::ResolveNpc() const {
        return GetServer().Npcs().Find(_id);
    }

    std::string Npc::GetKind() const {
        const auto *npc = ResolveNpc();
        return npc ? Shared::Entities::NpcKindName(npc->kind) : std::string {};
    }

    std::string Npc::GetModel() const {
        const auto *npc = ResolveNpc();
        return npc ? npc->model : std::string {};
    }

    Framework::Scripting::Builtins::Vector3 Npc::GetNpcPosition() const {
        const auto *npc = ResolveNpc();
        return npc ? Framework::Scripting::Builtins::Vector3(npc->position) : Framework::Scripting::Builtins::Vector3();
    }

    double Npc::GetHeading() const {
        const auto *npc = ResolveNpc();
        // The host reports a rotation about the Y axis only.
        return npc ? 2.0 * std::atan2(npc->rotation.y, npc->rotation.w) : 0.0;
    }

    bool Npc::GetAlive() const {
        const auto *npc = ResolveNpc();
        return npc && npc->alive;
    }

    double Npc::GetHealth() const {
        const auto *state = GetServer().Npcs().GetState(_id);
        return state ? state->health : 0.0;
    }

    uint32_t Npc::GetWeapon() const {
        const auto *state = GetServer().Npcs().GetState(_id);
        return state ? state->selectedWeapon : 0;
    }

    v8::Local<v8::Value> Npc::GetInventory(v8::Isolate *isolate) const {
        const auto *state = GetServer().Npcs().GetState(_id);
        if (!state) {
            return v8::Null(isolate);
        }
        return Weapon::Inventory(isolate, isolate->GetCurrentContext(), *state);
    }

    bool Npc::Remove() {
        return GetServer().Npcs().Remove(_id);
    }

    std::string Npc::ToString() const {
        std::ostringstream stream;
        stream << "Npc{ id: " << _id << ", kind: " << GetKind() << ", model: " << GetModel() << " }";
        return stream.str();
    }

    void Npc::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    v8pp::class_<Npc> &Npc::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (_class) {
            return *_class;
        }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Npc>>(isolate, ServerCatalog(), "Npc",
            "An armed human the simulation host runs with the retail AI, such as a gangster pedestrian someone hit that draws a Thompson and fights back.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing NPC."));
        cls.function("toString", &Npc::ToString, docs("string", {}, "Formats this NPC for logging.", "The NPC ID, kind and model."));
        cls.property("kind", &Npc::GetKind, property_docs("string", "What it is: \"gangster\", \"police\" or \"driver\" (a traffic driver thrown out of its car)."));
        cls.property("model", &Npc::GetModel, property_docs("string", "Retail .i3d model filename."));
        cls.property("position", &Npc::GetNpcPosition, property_docs("Vector3", "Position the simulation host last reported."));
        cls.property("heading", &Npc::GetHeading, property_docs("number", "Facing in radians."));
        cls.property("alive", &Npc::GetAlive, property_docs("boolean", "False once it died; the corpse stays until it retires."));
        cls.property("health", &Npc::GetHealth, property_docs("number", "Health the simulation host last reported; a gangster starts at 200."));
        cls.property("weapon", &Npc::GetWeapon, property_docs("number", "Retail item id of the weapon in hand, 0 for none; 10 is the Thompson."));
        cls.prototype_function("getInventory", &JS_NpcGetInventory,
            docs("Inventory | null", {}, "Returns the selected weapon and the held weapons the server records for it.", "The inventory, or null when it no longer exists."));
        cls.prototype_function("getThrownFromVehicle", &JS_NpcGetThrownFromVehicle,
            docs("Vehicle | null", {}, "Returns the car a driver NPC was thrown out of when a player stole it.",
                "The vehicle, or null for another kind of NPC or once that car is gone."));
        cls.function("remove", &Npc::Remove, docs("boolean", {}, "Asks the simulation host to retire it now; npcRetire follows.", "False when it no longer exists or no host is attached."));
        return cls;
    }

    void Npc::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        auto &cls = GetClass(isolate);
        cls.static_function("getAll", &JS_NpcGetAll, docs("Npc[]", {}, "Lists every NPC.", "The current NPCs."));
        cls.static_function("getInRange", &JS_NpcGetInRange,
            docs("Npc[]", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Center; three numbers are accepted too."), param("radius", "number", false, "Distance in units.")},
                "Lists the NPCs within radius of position.", "The NPCs in range."));
        cls.static_function("getCount", &JS_NpcGetCount, docs("number", {}, "Counts the NPCs.", "The number of NPCs."));
        auto context = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "Npc"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }
} // namespace Mafia1Online::Scripting
