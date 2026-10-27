#pragma once

#include <networking/replication/network_entity.h>

#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Entities {
    // What made a simulation host NPC; every kind is a retail C_entity.
    enum class NpcKind : uint8_t {
        // A gangster pedestrian someone hit (CreateMafianos).
        Gangster = 1,
    };

    inline bool KnownNpcKind(NpcKind kind) {
        return kind == NpcKind::Gangster;
    }

    inline const char *NpcKindName(NpcKind kind) {
        switch (kind) {
        case NpcKind::Gangster: return "gangster";
        }
        return "unknown";
    }

    // An armed human the simulation host runs with the retail AI. The server
    // owns it and streams it to nearby players. Its inventory, aim, health
    // and death travel as a Combat::State under this network id, and its
    // shots and reloads as Combat::Events, the same as a player's.
    class NpcEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::Npc";
        static constexpr size_t kMaxModelLength = 63;
        // An NPC has one life; its combat records use this spawn generation.
        static constexpr uint64_t kSpawnGeneration = 1;

        std::string model;
        uint64_t missionGeneration = 0;
        NpcKind kind = NpcKind::Gangster;
        // Death is final: the corpse stays until the host retires it.
        bool alive = true;
        // The host's native locomotion state (C_human m_iAnimState), which
        // rides with the pose like a player's.
        int16_t locomotion = 1;

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            NetworkEntity::SerializeTransform(fields);
            fields.Field(locomotion);
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            auto kindValue = static_cast<uint8_t>(kind);
            fields.Field(model);
            fields.Field(missionGeneration);
            fields.Field(kindValue);
            fields.Field(alive);
            if (!fields.Writing()) {
                kind = static_cast<NpcKind>(kindValue);
                if (model.size() > kMaxModelLength) {
                    model.clear();
                }
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(alive);
        }
    };
} // namespace Mafia1Online::Shared::Entities
