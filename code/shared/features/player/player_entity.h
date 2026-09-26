#pragma once

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

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            NetworkEntity::SerializeTransform(fields);
            fields.Field(locomotion);
        }

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
            fields.Field(health);
            fields.Field(money);
            fields.Field(spawned);
            fields.Field(alive);
            fields.Field(cameraTargetId);
            nametag.Serialize(fields);
        }
    };
} // namespace Mafia1Online::Shared::Entities
