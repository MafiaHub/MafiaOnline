#pragma once

#include <networking/replication/network_entity.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Entities {
    // What made a simulation host NPC; every kind is a retail C_entity.
    enum class NpcKind : uint8_t {
        // A gangster pedestrian someone hit (CreateMafianos).
        Gangster = 1,
        // A police pedestrian the police manager recruited against a
        // player's crime (FizlosNamakatDoAkce).
        Police = 2,
        // A traffic driver turned into a C_entity: thrown out of its car by a
        // player (Do_ThrowCocotFromCar) or a mafia car's crew (Krles).
        Driver = 3,
    };

    inline bool KnownNpcKind(NpcKind kind) {
        return kind == NpcKind::Gangster || kind == NpcKind::Police || kind == NpcKind::Driver;
    }

    inline const char *NpcKindName(NpcKind kind) {
        switch (kind) {
        case NpcKind::Gangster: return "gangster";
        case NpcKind::Police: return "police";
        case NpcKind::Driver: return "driver";
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
        // The walker this NPC replaced, or 0. A player who sees that walker
        // swaps it for this NPC where it stands on that player's screen.
        uint64_t sourcePedestrianId = 0;
        // The car a stolen traffic car's driver was thrown out of, or 0.
        uint64_t thrownFromCarId = 0;
        // Server only: when that throw started on the host.
        std::chrono::steady_clock::time_point thrownAt {};
        // How long before this copy was constructed the throw started, in
        // ms (construction is written per destination); kThrowAgeUnknown
        // for none. A player constructing it during the throw replays it.
        static constexpr uint16_t kThrowAgeUnknown = 0xffff;
        uint16_t throwAgeMs = kThrowAgeUnknown;
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
            fields.Field(sourcePedestrianId);
            if (fields.Writing()) {
                const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - thrownAt).count();
                throwAgeMs = thrownFromCarId ? static_cast<uint16_t>(std::clamp<long long>(age, 0, kThrowAgeUnknown)) : kThrowAgeUnknown;
            }
            fields.Field(thrownFromCarId);
            fields.Field(throwAgeMs);
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
