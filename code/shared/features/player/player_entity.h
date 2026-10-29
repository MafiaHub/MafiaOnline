#pragma once

#include "shared/net/quantize.h"

#include <networking/replication/nametag_state.h>
#include <networking/replication/network_entity.h>

#include <mafianet/string.h>

#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Entities {
    // The server owns the entire replica. controllerGuid identifies the client
    // allowed to submit movement; it is deliberately separate from ownerGUID,
    // which would also give that client authority over health and spawn fields.
    class PlayerEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::Player";
        static constexpr uint32_t kMaxMoney = 1'000'000'000;

        std::string nickname;
        // Server-selected native human model; retained across lives.
        std::string model = "Tommy.i3d";
        uint16_t playerIndex = 0xFFFF;
        uint64_t controllerGuid = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration = 0;
        // Where the server put this life, reliable with spawnGeneration: the
        // controller never gets its own pose back (GetTransformSource), and a
        // lost unreliable one must not leave it at the old place.
        glm::vec3 spawnPosition {0.0f};
        float spawnYaw = 0.0f;
        float health = 100.0f;
        // Server-owned Free Ride balance. Client scripts may read but never write it.
        uint32_t money = 0;
        bool spawned = false;
        bool alive = false;
        // Server-owned camera follow target. Zero restores this player's own camera.
        uint64_t cameraTargetId = 0;
        // Server-authored: scripts choose the label, color and components.
        Framework::Networking::Replication::NametagState nametag;

        // The controller's native locomotion state; it rides with the pose so
        // observers play the same walk, run, strafe or idle at the same time.
        int16_t locomotion = 1;
        // Aboard a tram or metro train: the railway index (0xff for none), its
        // body (0) or wagon, and the position and yaw in that part's basis
        // (Shared::Railway). Observers place the body there on their copy.
        uint8_t railway = 0xff;
        uint8_t railwayPart = 0;
        glm::vec3 railwayLocal {0.0f};
        float railwayYaw = 0.0f;
        // The controller's sample clock for this pose (Shared::Net).
        uint16_t sampleClock = 0;

        MafiaNet::PeerGuid GetTransformSource() const override {
            return static_cast<MafiaNet::PeerGuid>(controllerGuid);
        }

        // The server owns the entity; the controller is the player it is the
        // avatar of, so voice places that player's speech here.
        MafiaNet::PeerGuid GetAvatarGuid() const override {
            return static_cast<MafiaNet::PeerGuid>(controllerGuid);
        }

        // About 18 bytes on foot (61 as raw floats), 26 aboard a railway:
        // position at 1/64 m, yaw in 10 bits, velocity at 1/32 m/s, and the
        // ride only while aboard, its offset at 1/1024 m.
        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            auto *stream = fields.PlainStream();
            if (!stream) {
                NetworkEntity::SerializeTransform(fields);
                fields.Field(sampleClock);
                fields.Field(locomotion);
                fields.Field(railway);
                fields.Field(railwayPart);
                fields.Field(railwayLocal);
                fields.Field(railwayYaw);
                return;
            }
            const bool write = fields.Writing();
            bool riding      = railway < kMaxRailwayIndex + 1 && railwayPart <= kMaxRailwayPart;
            uint8_t index    = riding ? railway : 0;
            uint8_t part     = riding ? railwayPart : 0;
            bool good        = Net::SerializeWorldPosition(stream, write, position) && Net::SerializeHeading(stream, write, rotation, 10) &&
                        Net::SerializeVelocity(stream, write, velocity) && stream->Serialize(write, sampleClock) && stream->Serialize(write, locomotion) &&
                        stream->Serialize(write, riding);
            if (good && riding) {
                good = stream->SerializeBitsFromIntegerRange(write, index, uint8_t {0}, kMaxRailwayIndex) &&
                       stream->SerializeBitsFromIntegerRange(write, part, uint8_t {0}, kMaxRailwayPart) &&
                       Net::SerializeFixed(stream, write, railwayLocal.x, -32.0f, 32.0f, 16) && Net::SerializeFixed(stream, write, railwayLocal.y, -32.0f, 32.0f, 16) &&
                       Net::SerializeFixed(stream, write, railwayLocal.z, -32.0f, 32.0f, 16) && Net::SerializeAngle(stream, write, railwayYaw, 10);
            }
            if (!good) {
                fields.Fail();
                return;
            }
            if (!write) {
                railway     = riding ? index : 0xff;
                railwayPart = part;
                if (!riding) {
                    railwayLocal = glm::vec3(0.0f);
                    railwayYaw   = 0.0f;
                }
            }
        }

        // Shared::Railway::kMaxRailways - 1 and kMaxWagons, restated so this
        // header stays free of the railway report.
        static constexpr uint8_t kMaxRailwayIndex = 63;
        static constexpr uint8_t kMaxRailwayPart  = 4;

        Framework::Networking::Replication::NametagState *GetNametag() override {
            return &nametag;
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            MafiaNet::RakString name(nickname.c_str());
            MafiaNet::RakString modelName(model.c_str());
            fields.Field(name);
            fields.Field(modelName);
            fields.Field(playerIndex);
            fields.Field(controllerGuid);
            fields.Field(missionGeneration);
            fields.Field(spawnGeneration);
            fields.Field(spawnPosition);
            fields.Field(spawnYaw);
            fields.Field(health);
            fields.Field(money);
            fields.Field(spawned);
            fields.Field(alive);
            fields.Field(cameraTargetId);
            nametag.Serialize(fields);
            if (!fields.Writing()) {
                nickname = name.C_String();
                model = modelName.C_String();
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(nickname);
            fields.Field(model);
            fields.Field(missionGeneration);
            fields.Field(spawnGeneration);
            fields.Field(spawnPosition);
            fields.Field(spawnYaw);
            fields.Field(health);
            fields.Field(money);
            fields.Field(spawned);
            fields.Field(alive);
            fields.Field(cameraTargetId);
            nametag.Serialize(fields);
        }
    };
} // namespace Mafia1Online::Shared::Entities
