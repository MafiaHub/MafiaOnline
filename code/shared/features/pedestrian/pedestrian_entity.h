#pragma once

#include "shared/net/quantize.h"

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
        // When the animation, its flag or alive last changed, on the host's
        // sample clock (sampleClock's): a copy plays it once it draws the
        // body at that moment, so the clip starts where the body does.
        uint16_t animationClock = 0;
        bool umbrella = false;
        uint8_t alpha = 0;
        // Waving for a taxi (Shared::Taxi::kWave*): the retail "MavaniNN
        // HPT.i3d" extended animation, 0 for none.
        uint8_t taxiWave = 0;
        // The host's sample clock for this pose (Shared::Net).
        uint16_t sampleClock = 0;
        // Server only: the simulation host whose snapshots move it.
        MafiaNet::PeerGuid poseSource = MafiaNet::UNASSIGNED_PEER_GUID;

        MafiaNet::PeerGuid GetTransformSource() const override {
            return poseSource;
        }

        // 14 bytes (41 as raw floats): position at 1/64 m, yaw in 10 bits and
        // the planar velocity, which is all the host measures.
        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            auto *stream = fields.PlainStream();
            if (!stream) {
                NetworkEntity::SerializeTransform(fields);
                fields.Field(sampleClock);
                return;
            }
            const bool write = fields.Writing();
            if (!Net::SerializeWorldPosition(stream, write, position) || !Net::SerializeHeading(stream, write, rotation, 10) ||
                !Net::SerializePlanarVelocity(stream, write, velocity) || !stream->Serialize(write, sampleClock)) {
                fields.Fail();
            }
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(model);
            fields.Field(missionGeneration);
            fields.Field(generator);
            fields.Field(walkStyle);
            fields.Field(alive);
            fields.Field(animation);
            fields.Field(animationFlag);
            fields.Field(animationClock);
            fields.Field(umbrella);
            fields.Field(alpha);
            fields.Field(taxiWave);
            if (!fields.Writing() && model.size() > kMaxModelLength) {
                model.clear();
            }
        }

        // The animation travels with the reliable fields: a one-shot reaction
        // or death must not be skipped by a lost pose. Its clock dates it on
        // the poses' timeline.
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(alive);
            fields.Field(animation);
            fields.Field(animationFlag);
            fields.Field(animationClock);
            fields.Field(umbrella);
            fields.Field(alpha);
            fields.Field(taxiWave);
        }
    };
} // namespace Mafia1Online::Shared::Entities
