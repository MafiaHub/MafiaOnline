#pragma once

#include "shared/features/pickup/weapon_pickup_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace Mafia1Online::Scripting {
    class Pickup final: public Framework::Scripting::Builtins::Entity {
      public:
        Pickup(uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        Shared::Entities::WeaponPickupEntity *ResolvePickup() const;

        uint32_t GetWeaponId() const;
        double GetHeading() const;
        uint32_t GetLoaded() const;
        uint32_t GetReserve() const;
        bool Destroy();
        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<Pickup> &GetClass(v8::Isolate *isolate);

      private:
        static std::unique_ptr<v8pp::class_<Pickup>> _class;
    };
} // namespace Mafia1Online::Scripting
