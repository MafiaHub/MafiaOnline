#include "weapon_scripting.h"

#include "features/combat/combat_service.h"
#include "features/script/script_runtime.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <string_view>

namespace Mafia1Online::Scripting {
    namespace {
        void JS_WeaponList(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            for (uint32_t weaponId = 0; weaponId < Shared::Combat::kWeaponSlots; ++weaponId) {
                if (Weapon::IsSupported(weaponId)) {
                    list->Set(context, index++, Weapon::Describe(isolate, context, weaponId)).Check();
                }
            }
            info.GetReturnValue().Set(list);
        }

        void JS_WeaponGet(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate     = info.GetIsolate();
            uint32_t weaponId = 0;
            if (info.Length() != 1 || !ScriptArgs::ReadUInt(info[0], Shared::Combat::kWeaponSlots - 1, weaponId)) {
                ScriptArgs::Throw(isolate, "Weapon.get(weaponId) expects a weapon ID");
                return;
            }
            info.GetReturnValue().Set(Weapon::Describe(isolate, isolate->GetCurrentContext(), weaponId));
        }
    } // namespace

    bool Weapon::IsSupported(uint32_t weaponId) {
        return weaponId < Shared::Combat::kWeaponSlots && Features::Combat::CombatService::Weapon(static_cast<uint8_t>(weaponId)).has_value();
    }

    uint16_t Weapon::DefaultLoaded(uint8_t weaponId) {
        const auto weapon = Features::Combat::CombatService::Weapon(weaponId);
        return weapon && std::string_view(weapon->kind) == "firearm" ? weapon->magazine : 1;
    }

    v8::Local<v8::Value> Weapon::Describe(v8::Isolate *isolate, v8::Local<v8::Context> context, uint32_t weaponId) {
        const auto weapon = weaponId < Shared::Combat::kWeaponSlots ? Features::Combat::CombatService::Weapon(static_cast<uint8_t>(weaponId)) : std::nullopt;
        if (!weapon) {
            return v8::Null(isolate);
        }
        auto object = v8::Object::New(isolate);
        SetField(isolate, context, object, "weaponId", v8::Uint32::New(isolate, weaponId));
        SetField(isolate, context, object, "name", v8pp::to_v8(isolate, weapon->name));
        SetField(isolate, context, object, "kind", v8pp::to_v8(isolate, weapon->kind));
        SetField(isolate, context, object, "magazine", v8::Uint32::New(isolate, weapon->magazine));
        return object;
    }

    void Weapon::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &info = ServerCatalog().data_type("WeaponInfo", "A stock Mafia 1 weapon the server accepts.");
        info.add_property("weaponId", "number", "Stock item identifier, 2 through 15.");
        info.add_property("name", "string", "Item name.");
        info.add_property("kind", "\"melee\" | \"firearm\" | \"throwable\"", "How the item is used.");
        info.add_property("magazine", "number", "Rounds per magazine for a firearm; 1 otherwise.");

        v8pp::module weapon(isolate, ServerCatalog(), "Weapon", "Catalog of the stock weapons the server accepts. 2 is the knuckle duster, 5 the Molotov and 15 the grenade.");
        weapon.function("list", &JS_WeaponList, v8pp::metadata::docs("WeaponInfo[]", {}, "Lists every weapon the server accepts.", "The supported weapons in ID order."));
        weapon.function("get", &JS_WeaponGet,
            v8pp::metadata::docs("WeaponInfo | null", {v8pp::metadata::param("weaponId", "number", false, "Stock item identifier.")}, "Describes one weapon.", "The weapon, or null when the server does not accept it."));
        weapon.publish(global);
    }
} // namespace Mafia1Online::Scripting
