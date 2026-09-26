#pragma once

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace Mafia1Online::Shared::Entities {
    class WeaponPickupEntity;
}

namespace Mafia1Online::Scripting {
    // A handle to a weapon pickup streamed from the server. No native item or
    // frame is retained: the entity is resolved for every getter.
    class Pickup final: public Framework::Scripting::Builtins::Entity {
      public:
        explicit Pickup(uint64_t networkId);

        Shared::Entities::WeaponPickupEntity *ResolvePickup() const;
        Framework::Scripting::Builtins::Vector3 GetPickupPosition() const;
        Framework::Scripting::Builtins::Quaternion GetPickupRotation() const;
        uint32_t GetWeaponId() const;
        double GetHeading() const;
        uint32_t GetLoaded() const;
        uint32_t GetReserve() const;
        double GetMissionGeneration() const;
        std::string ToString() const override;

        static v8pp::class_<Pickup> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static std::unique_ptr<v8pp::class_<Pickup>> _class;
    };
} // namespace Mafia1Online::Scripting
