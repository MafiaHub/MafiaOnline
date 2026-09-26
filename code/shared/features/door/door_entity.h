#pragma once

#include <networking/replication/network_entity.h>

#include <cstdint>
#include <cstddef>
#include <string>

namespace Mafia1Online::Shared::Entities {
    // A named door actor in the current stock mission. The server owns the
    // target state; construction carries the complete late-join snapshot.
    class DoorEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::Door";
        static constexpr size_t kMaxNameLength = 63;
        static constexpr size_t kMaxMissionDoors = 1024;

        std::string frameName;
        uint64_t missionGeneration = 0;
        bool open = false;
        bool locked = false;
        bool enabled = true;
        bool reverse = false;
        bool pairedReverse = false;
        float openFraction = 1.0f;
        uint64_t revision = 1;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(frameName);
            fields.Field(missionGeneration);
            fields.Field(position.x);
            fields.Field(position.y);
            fields.Field(position.z);
            fields.Field(open);
            fields.Field(locked);
            fields.Field(enabled);
            fields.Field(reverse);
            fields.Field(pairedReverse);
            fields.Field(openFraction);
            fields.Field(revision);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(open);
            fields.Field(locked);
            fields.Field(enabled);
            fields.Field(reverse);
            fields.Field(pairedReverse);
            fields.Field(openFraction);
            fields.Field(revision);
        }
    };
} // namespace Mafia1Online::Shared::Entities
