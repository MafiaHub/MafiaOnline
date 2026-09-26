#pragma once

#include <networking/replication/network_entity.h>

#include <cstdint>

namespace Mafia1Online::Shared::Entities {
    // A server-owned weapon lying in the world: a script pickup, a dropped
    // weapon or a dead player's weapon. Clients show it as a native item the
    // use key finds, and ask the server to take it.
    class WeaponPickupEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::WeaponPickup";

        uint64_t missionGeneration = 0;
        uint8_t weaponId = 0;
        float yaw = 0.0f;
        uint16_t loaded = 0;
        uint16_t reserve = 0;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(missionGeneration);
            fields.Field(weaponId);
            fields.Field(position.x);
            fields.Field(position.y);
            fields.Field(position.z);
            fields.Field(yaw);
            fields.Field(loaded);
            fields.Field(reserve);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(loaded);
            fields.Field(reserve);
        }
    };
} // namespace Mafia1Online::Shared::Entities
