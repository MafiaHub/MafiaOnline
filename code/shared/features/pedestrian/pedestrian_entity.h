#pragma once

#include <networking/replication/network_entity.h>

#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Entities {
    // One life of a walker the simulation host runs. The server owns it and
    // streams it to nearby players only; the pose and animation come from the
    // host's snapshots. A walker that retires and walks again is a new entity.
    class PedestrianEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::Pedestrian";
        static constexpr size_t kMaxModelLength = 63;

        std::string model;
        uint64_t missionGeneration = 0;
        uint8_t generator = 0;
        uint8_t walkStyle = 0;
        // Death is final for a life: the host's corpse stays until it retires.
        bool alive = true;
        int16_t animation = 0;
        bool animationFlag = false;
        bool umbrella = false;
        uint8_t alpha = 0;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(model);
            fields.Field(missionGeneration);
            fields.Field(generator);
            fields.Field(walkStyle);
            fields.Field(alive);
            fields.Field(animation);
            fields.Field(animationFlag);
            fields.Field(umbrella);
            fields.Field(alpha);
            if (!fields.Writing() && model.size() > kMaxModelLength) {
                model.clear();
            }
        }

        // The animation travels with the reliable fields: a one-shot reaction
        // or death must not be skipped by a lost pose.
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(alive);
            fields.Field(animation);
            fields.Field(animationFlag);
            fields.Field(umbrella);
            fields.Field(alpha);
        }
    };
} // namespace Mafia1Online::Shared::Entities
