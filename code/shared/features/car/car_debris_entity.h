#pragma once

#include "shared/features/car/car_debris.h"

#include <networking/replication/network_entity.h>

#include <cstdint>

namespace Mafia1Online::Shared::Entities {
    class CarDebrisEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::CarDebris";

        uint64_t carId = 0;
        uint64_t missionGeneration = 0;
        uint64_t creatorGuid = 0;
        uint64_t controllerGuid = 0;
        uint32_t creatorSequence = 0;
        uint8_t type = 0;
        uint8_t partIndex = 0;
        Shared::Car::DebrisParameters parameters;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(carId);
            fields.Field(missionGeneration);
            fields.Field(creatorGuid);
            fields.Field(controllerGuid);
            fields.Field(creatorSequence);
            fields.Field(type);
            fields.Field(partIndex);
            parameters.Fields([&](auto &value) { fields.Field(value); });
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(controllerGuid);
        }
    };
} // namespace Mafia1Online::Shared::Entities
