#pragma once

#include "map_object_motion.h"

#include <networking/replication/network_entity.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Entities {
    // A script-placed world object built from a stock model, like SA-MP's
    // CreateObject. The server owns its pose: a resting object carries it in
    // the motion segment's start, and a moving one carries the segment every
    // client plays at the same speed. Construction also carries how far a
    // moving segment has run, so a late joiner starts where the others are.
    class MapObjectEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::MapObject";

        std::string model;
        uint64_t missionGeneration = 0;
        bool collision = true;
        // kAutomaticMaterial keeps the mission's own collision materials for
        // the model, and concrete for collision built from its mesh.
        static constexpr uint8_t kAutomaticMaterial = 0xff;
        uint8_t material = kAutomaticMaterial;
        glm::vec3 scale {1.0f};

        // Bumped by every teleport, move and stop; a client restarts its
        // playback when it changes.
        uint32_t motionRevision = 0;
        MapObject::MotionSegment motion;
        // How far the moving segment had run when construction was written.
        uint32_t constructionElapsedMs = 0;
        // Server: when the current segment started. Client: when this peer
        // received it, backdated by constructionElapsedMs for the segment
        // construction carried.
        std::chrono::steady_clock::time_point motionStartedAt {};

        // The pose travels reliably with the segment; there is no
        // high-frequency transform to send.
        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &) override {}

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            if (fields.Writing() && fields.ServerAuthored()) {
                constructionElapsedMs = 0;
                if (motion.moving) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - motionStartedAt).count();
                    constructionElapsedMs = elapsed > 0 ? static_cast<uint32_t>(elapsed > 0x7fffffff ? 0x7fffffff : elapsed) : 0;
                }
            }
            fields.Field(model);
            fields.Field(missionGeneration);
            fields.Field(collision);
            fields.Field(material);
            fields.Field(scale);
            fields.Field(constructionElapsedMs);
        }

        void OnConstructed() override {
            motionStartedAt = std::chrono::steady_clock::now() - std::chrono::milliseconds(constructionElapsedMs);
            _receivedRevision = motionRevision;
        }

        void OnDeserialized(bool) override {
            if (motionRevision != _receivedRevision) {
                _receivedRevision = motionRevision;
                motionStartedAt   = std::chrono::steady_clock::now();
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(motionRevision);
            fields.Field(motion.startPosition);
            fields.Field(motion.startRotation);
            fields.Field(motion.targetPosition);
            fields.Field(motion.targetRotation);
            fields.Field(motion.speed);
            fields.Field(motion.moving);
        }

      private:
        uint32_t _receivedRevision = 0;
    };
} // namespace Mafia1Online::Shared::Entities
