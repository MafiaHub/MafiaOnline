#pragma once

#include "shared/features/combat/combat_protocol.h"

#include <v8.h>

#include <cstdint>

namespace Mafia1Online::Scripting {
    // Static catalog of the stock weapons the server accepts, exposed as the
    // global `Weapon`.
    class Weapon final {
      public:
        // {weaponId, name, kind, magazine} for a supported weapon, otherwise
        // null.
        static v8::Local<v8::Value> Describe(v8::Isolate *isolate, v8::Local<v8::Context> context, uint32_t weaponId);
        // The ammunition a new item gets when a script gives no amount: a full
        // magazine for a firearm, one for anything else.
        static uint16_t DefaultLoaded(uint8_t weaponId);
        static bool IsSupported(uint32_t weaponId);
        // A combat state's held weapons, each Describe plus loaded and reserve.
        static v8::Local<v8::Array> HeldList(v8::Isolate *isolate, v8::Local<v8::Context> context, const Shared::Combat::State &state);
        // {selected, weapons}: the Inventory type players and NPCs share.
        static v8::Local<v8::Object> Inventory(v8::Isolate *isolate, v8::Local<v8::Context> context, const Shared::Combat::State &state);

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };
} // namespace Mafia1Online::Scripting
