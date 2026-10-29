#pragma once

#include <networking/replication/network_entity.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace Mafia1Online::Shared::Entities {
    // A police road block barrier the simulation host's police manager set
    // up. Players build their own native C_irenka from the same model at
    // this pose, so their cars collide with it as with the host's.
    class RoadBarrierEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::RoadBarrier";
        // The model C_shvestky::SetActive fills the barrier pool with.
        static constexpr const char *kModel = "barrier.i3d";

        uint64_t missionGeneration = 0;
        // Barriers never move; the pose rides with the construction.
        glm::vec3 placedPosition {0.0f};
        glm::quat placedRotation {1.0f, 0.0f, 0.0f, 0.0f};

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(missionGeneration);
            fields.Field(placedPosition);
            fields.Field(placedRotation);
        }
    };
} // namespace Mafia1Online::Shared::Entities
