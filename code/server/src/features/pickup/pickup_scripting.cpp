#include "pickup_scripting.h"

#include "core/server.h"
#include "features/combat/weapon_scripting.h"
#include "features/script/script_runtime.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>

#include <chrono>
#include <sstream>

namespace Mafia1Online::Scripting {
    std::unique_ptr<v8pp::class_<Pickup>> Pickup::_class;

    namespace {
        // Pickup.create(weaponId, position, heading?, loaded?, reserve?, lifetimeMs?)
        void JS_PickupCreate(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate     = info.GetIsolate();
            uint32_t weaponId = 0, loaded = 0, reserve = 0, lifetime = 0;
            glm::vec3 position;
            float heading = 0.0f;
            int next      = 0;
            const bool known = info.Length() >= 1 && ScriptArgs::ReadUInt(info[0], Shared::Combat::kWeaponSlots - 1, weaponId) && Weapon::IsSupported(weaponId);
            if (!known || !ScriptArgs::ReadPosition(info, 1, position, next) || info.Length() > next + 4 || (info.Length() > next && !ScriptArgs::ReadFloat(info[next], heading)) ||
                (info.Length() > next + 1 && !ScriptArgs::ReadUInt(info[next + 1], 1000, loaded)) || (info.Length() > next + 2 && !ScriptArgs::ReadUInt(info[next + 2], 1000, reserve)) ||
                (info.Length() > next + 3 && !ScriptArgs::ReadUInt(info[next + 3], 0x7fffffff, lifetime))) {
                ScriptArgs::Throw(isolate, "Pickup.create(weaponId, position, heading?, loaded?, reserve?, lifetimeMs?) expects a supported weapon, a finite position and ammunition from 0 to 1000");
                return;
            }
            if (info.Length() <= next + 1) {
                loaded = Weapon::DefaultLoaded(static_cast<uint8_t>(weaponId));
            }
            auto &server      = GetServer();
            const uint64_t id = server.Pickups().Create(static_cast<uint8_t>(weaponId), position, heading, static_cast<uint16_t>(loaded), static_cast<uint16_t>(reserve), server.MissionGeneration(),
                                                        std::chrono::milliseconds(lifetime));
            info.GetReturnValue().Set(WrapPickup(isolate, id));
        }

        void JS_PickupGetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            for (uint64_t id : GetServer().Pickups().List()) {
                list->Set(context, index++, WrapPickup(isolate, id)).Check();
            }
            info.GetReturnValue().Set(list);
        }
    } // namespace

    Shared::Entities::WeaponPickupEntity *Pickup::ResolvePickup() const {
        return GetServer().Pickups().Find(_id);
    }

    uint32_t Pickup::GetWeaponId() const {
        const auto *pickup = ResolvePickup();
        return pickup ? pickup->weaponId : 0;
    }

    double Pickup::GetHeading() const {
        const auto *pickup = ResolvePickup();
        return pickup ? pickup->yaw : 0.0;
    }

    uint32_t Pickup::GetLoaded() const {
        const auto *pickup = ResolvePickup();
        return pickup ? pickup->loaded : 0;
    }

    uint32_t Pickup::GetReserve() const {
        const auto *pickup = ResolvePickup();
        return pickup ? pickup->reserve : 0;
    }

    bool Pickup::Destroy() {
        return GetServer().Pickups().Destroy(_id);
    }

    std::string Pickup::ToString() const {
        std::ostringstream stream;
        stream << "Pickup{ id: " << _id << ", weaponId: " << GetWeaponId() << " }";
        return stream.str();
    }

    v8pp::class_<Pickup> &Pickup::GetClass(v8::Isolate *isolate) {
        if (_class) {
            return *_class;
        }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Pickup>>(isolate, ServerCatalog(), "Pickup",
            "A server-owned weapon in the world: a script pickup, a dropped weapon or a dead player's weapon. The use key asks the server, which checks reach, life and inventory.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing pickup; use Pickup.create to place one."));
        cls.function("toString", &Pickup::ToString, docs("string", {}, "Formats this pickup for logging.", "The pickup ID and weapon."));
        cls.property("weaponId", &Pickup::GetWeaponId, property_docs("number", "Weapon lying here."));
        cls.property("heading", &Pickup::GetHeading, property_docs("number", "Facing in radians."));
        cls.property("loaded", &Pickup::GetLoaded, property_docs("number", "Loaded rounds or throwable count."));
        cls.property("reserve", &Pickup::GetReserve, property_docs("number", "Reserve rounds."));
        cls.function("destroy", &Pickup::Destroy, docs("boolean", {}, "Removes the pickup from every client.", "False when it no longer exists."));
        return cls;
    }

    void Pickup::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &cls = GetClass(isolate);
        cls.static_function("create", &JS_PickupCreate,
            v8pp::metadata::docs("Pickup | null",
                {v8pp::metadata::param("weaponId", "number", false, "Supported weapon, 2 through 15."),
                    v8pp::metadata::param("position", "Vector3 | { x: number; y: number; z: number }", false, "Where it lies; three numbers are accepted too."),
                    v8pp::metadata::param("heading", "number", true, "Facing in radians; defaults to 0."),
                    v8pp::metadata::param("loaded", "number", true, "Loaded rounds or count, up to 1000; defaults to a full magazine for a firearm and 1 otherwise."),
                    v8pp::metadata::param("reserve", "number", true, "Reserve rounds, up to 1000; defaults to 0."),
                    v8pp::metadata::param("lifetimeMs", "number", true, "Removal delay; 0 or omitted keeps it until taken or destroyed.")},
                "Places a weapon pickup in the current mission. A mission change removes every pickup.", "The pickup, or null when the limit of 256 is reached."));
        cls.static_function("getAll", &JS_PickupGetAll, v8pp::metadata::docs("Pickup[]", {}, "Lists every pickup.", "The current pickups."));
        auto context = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "Pickup"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }
} // namespace Mafia1Online::Scripting
