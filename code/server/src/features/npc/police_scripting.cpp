#include "police_scripting.h"

#include "core/server.h"
#include "features/script/script_runtime.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

namespace Mafia1Online::Scripting {
    namespace {
        // S_shvestky crime bits (C_shvestky detectors and Tick), named after
        // the retail police messages 2100-2110.
        struct CrimeName {
            uint16_t bit;
            const char *name;
        };
        constexpr CrimeName kCrimes[] {
            {0x0001, "weaponSeen"},
            {0x0002, "firing"},
            {0x0004, "speeding"},
            {0x0008, "crashObject"},
            {0x0010, "crashCar"},
            {0x0020, "crashPedestrian"},
            {0x0040, "sidewalk"},
            {0x0200, "violence"},
            {0x0400, "redLight"},
            {0x0800, "carTheft"},
            {0x1000, "signDamage"},
            {0x2000, "assaultOnPolice"},
            {0x4000, "forcedArrest"},
            {0x8000, "policeCarRammed"},
        };

        uint64_t ReadPlayer(const v8::FunctionCallbackInfo<v8::Value> &info, const char *usage) {
            const uint64_t id = info.Length() >= 1 ? ScriptArgs::ReadEntityId(info.GetIsolate(), info[0]) : 0;
            if (!id) {
                ScriptArgs::Throw(info.GetIsolate(), usage);
            }
            return id;
        }

        void JS_GetRecord(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const uint64_t id = ReadPlayer(info, "Police.getRecord(player) expects a Player");
            if (!id) {
                return;
            }
            auto *isolate = info.GetIsolate();
            const auto record = GetServer().Police().Find(id);
            if (!record) {
                info.GetReturnValue().SetNull();
                return;
            }
            auto context = isolate->GetCurrentContext();
            auto crimes = v8::Array::New(isolate);
            uint32_t index = 0;
            for (const auto &crime : kCrimes) {
                if ((record->crimes & crime.bit) != 0) {
                    crimes->Set(context, index++, v8pp::to_v8(isolate, crime.name)).Check();
                }
            }
            auto object = v8::Object::New(isolate);
            SetField(isolate, context, object, "level", v8::Uint32::New(isolate, record->level));
            SetField(isolate, context, object, "crimes", crimes);
            SetField(isolate, context, object, "officers", v8::Uint32::New(isolate, record->officers));
            info.GetReturnValue().Set(object);
        }

        void JS_SetWanted(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const uint64_t id = ReadPlayer(info, "Police.setWanted(player, level) expects a Player and a level from 0 to 3");
            uint32_t level = 0;
            if (!id) {
                return;
            }
            if (info.Length() != 2 || !ScriptArgs::ReadUInt(info[1], Features::Npc::PoliceService::kLevelLethal, level)) {
                ScriptArgs::Throw(info.GetIsolate(), "Police.setWanted(player, level) expects a Player and a level from 0 to 3");
                return;
            }
            info.GetReturnValue().Set(GetServer().Police().SetWanted(id, static_cast<uint8_t>(level)));
        }

        void JS_Clear(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (const uint64_t id = ReadPlayer(info, "Police.clear(player) expects a Player")) {
                info.GetReturnValue().Set(GetServer().Police().Clear(id));
            }
        }

        void JS_SetEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsBoolean()) {
                ScriptArgs::Throw(info.GetIsolate(), "Police.setEnabled(enabled) expects a boolean");
                return;
            }
            GetServer().Police().SetEnabled(info[0]->BooleanValue(info.GetIsolate()));
        }

        void JS_IsEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(GetServer().Police().IsEnabled());
        }
    } // namespace

    const char *PoliceCrimeName(uint16_t crime) {
        for (const auto &entry : kCrimes) {
            if (entry.bit == crime) {
                return entry.name;
            }
        }
        return nullptr;
    }

    void Police::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        v8pp::module police(isolate, ServerCatalog(), "Police",
            "The retail police, which the simulation host runs for every player: each player's crime record, the wanted level it reaches and the officers it sends. "
            "Officers come from police walkers and from the traffic's police cars, which also chase players and set up roadblocks.");
        police.function("getRecord", &JS_GetRecord,
            docs("PoliceRecord | null", {param("player", "Player", false, "A spawned player.")}, "Returns the player's crime record as the host last reported it.",
                "The record, or null when the player is not spawned."));
        police.function("setWanted", &JS_SetWanted,
            docs("boolean", {param("player", "Player", false, "A spawned player."), param("level", "number", false, "0 none, 1 minor, 2 arrest, 3 lethal.")},
                "Raises the record to this level, which sends the police the retail way, or lowers it (calling off the officers first). playerWantedChange follows.",
                "False when the player is not spawned, no simulation host is attached, or the police are off and the level would rise."));
        police.function("clear", &JS_Clear,
            docs("boolean", {param("player", "Player", false, "A spawned player.")}, "Settles the player's record and calls off every officer after them.",
                "False when the player is not spawned or no simulation host is attached."));
        police.function("setEnabled", &JS_SetEnabled,
            docs("void", {param("enabled", "boolean", false, "Whether crimes raise wanted levels.")},
                "Turns the police reaction to crimes on or off for everyone. While off, crimes raise nothing, Police.setWanted cannot raise a level and a hit officer "
                "opens no new case. Officers already on a case keep it; Police.clear calls them off."));
        police.function("isEnabled", &JS_IsEnabled, docs("boolean", {}, "Whether crimes raise wanted levels.", "True unless Police.setEnabled(false)."));
        police.publish(global);
    }
} // namespace Mafia1Online::Scripting
