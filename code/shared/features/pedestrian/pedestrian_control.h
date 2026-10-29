#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Pedestrian {
    // Server -> simulation host: what scripts set through Pedestrian. Sent
    // when the host attaches and on every change.
    struct Control {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianControl");

        // Off retires every walker and stops spawning.
        bool enabled = true;
        // Scales every player cluster's retail walker count, 0 to 1.
        float density = 1.0f;
        // A corpse retires this long after death even with players around;
        // 0 keeps the retail rule (it retires once every player is far away).
        uint32_t corpseLifetimeMs = 30'000;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, enabled);
            stream->Serialize(write, density);
            stream->Serialize(write, corpseLifetimeMs);
        }
    };

    // Server -> simulation host: retire one walker life now.
    struct Remove {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianRemove");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
        }
    };

    // Player -> server: a car this client simulates collided with a walker's
    // replica (C_don_ocas::Collision). The car's physics runs here alone, so
    // its driver reports the contact; the server checks it and forwards it.
    struct CarImpact {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianCarImpact");

        uint64_t missionGeneration = 0;
        uint64_t walkerId = 0;
        uint64_t carId = 0;
        // The Collision direction: the contact velocity, whose length sets
        // the damage (x 4.2).
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, walkerId);
            stream->Serialize(write, carId);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
        }
    };

    // Server -> simulation host: apply that car impact to the walker.
    struct Impact {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianImpact");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t carId = 0;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, carId);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
        }
    };

    // Server -> simulation host: a player's melee swing hit a walker's
    // replica (the attacker's Combat::HitReport, Kind::Melee, which the
    // server checked against its accepted swing). The host applies it
    // through the walker collision's own Hit, as C_human::AI's sweep would.
    struct MeleeHit {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianMeleeHit");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t attackerId = 0;
        // The accepted swing's stock item (0 fists, 2-4) and whether it was
        // the held heavy blow; the host's backstab rule reads both.
        uint8_t weaponId = 0;
        bool heavy = false;
        // The item's retail sweep damage; a walker takes no heavy doubling.
        float damage = 0.0f;
        // The unit swing direction and where it struck the replica.
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, attackerId);
            stream->Serialize(write, weaponId);
            stream->Serialize(write, heavy);
            stream->Serialize(write, damage);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
        }
    };

    // Server -> simulation host: a player's bullet hit a walker's replica
    // (Combat::HitReport, Kind::Bullet, checked against the accepted shot).
    // The host's own replay of the shot need not line up with its walker, so
    // it applies this straight to C_traffic_element::Hit with the body part
    // the player hit; a gangster walker becomes its armed C_entity there.
    struct ShotHit {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianShotHit");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t attackerId = 0;
        // C_ShotSkeleton's body type on the player's replica (1-6).
        uint8_t bodyPart = 0;
        // The pellet's damage after retail's range falloff.
        float damage = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, attackerId);
            stream->Serialize(write, bodyPart);
            stream->Serialize(write, damage);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
        }
    };

    // Simulation host -> server: retail C_traffic_element::Hit ran on a walker.
    struct Damaged {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianDamaged");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        // Network id of the native attacker (a player's human or car), 0
        // when it has none.
        uint64_t attackerId = 0;
        // Retail E_hit_type: 0 bullet, 1 hard knockdown, 2 melee, 3 explosion...
        int32_t hitType = 0;
        float damage = 0.0f;
        float healthBefore = 0.0f;
        float healthAfter = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, attackerId);
            stream->Serialize(write, hitType);
            stream->Serialize(write, damage);
            stream->Serialize(write, healthBefore);
            stream->Serialize(write, healthAfter);
        }
    };
} // namespace Mafia1Online::Shared::Pedestrian
