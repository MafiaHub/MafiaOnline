#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Car {
    enum class HitKind : uint8_t {
        // A pellet of an accepted Fire (C_car::Hit type 0).
        Bullet = 0,
        // A strike of an accepted melee swing: C_human::AI hits a car with
        // HIT_TYPE_HARD_KNOCKDOWN (1), which C_car::Hit deforms.
        Melee = 1,
    };
    // C_car::Hit's hit type for each kind.
    inline constexpr int32_t NativeHitType(HitKind kind) {
        return kind == HitKind::Melee ? 1 : 0;
    }
    // reM HUMAN_AI_MELEE_CAR_SOUND: the sound table entry C_human::AI plays
    // where a swing strikes a car.
    inline constexpr uint32_t kMeleeCarSound = 23;

    // A local shoot record's impact on a network car. The server consumes the
    // matching accepted Fire pellet (a melee strike: swing) before forwarding
    // an authoritative hit.
    struct HitReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarHitReport");

        HitKind kind = HitKind::Bullet;
        uint64_t carId = 0;
        uint64_t missionGeneration = 0;
        uint64_t shooterId = 0;
        uint64_t shooterSpawnGeneration = 0;
        uint32_t shotSequence = 0;
        uint8_t pelletIndex = 0;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;
        float hitX = 0.0f;
        float hitY = 0.0f;
        float hitZ = 0.0f;
        float normalX = 0.0f;
        float normalY = 0.0f;
        float normalZ = 0.0f;
        float localHitX = 0.0f;
        float localHitY = 0.0f;
        float localHitZ = 0.0f;
        float localDirectionX = 0.0f;
        float localDirectionY = 0.0f;
        float localDirectionZ = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto kindValue = static_cast<uint8_t>(kind);
            stream->Serialize(write, kindValue);
            kind = static_cast<HitKind>(kindValue);
            stream->Serialize(write, carId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, shooterId);
            stream->Serialize(write, shooterSpawnGeneration);
            stream->Serialize(write, shotSequence);
            stream->Serialize(write, pelletIndex);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
            stream->Serialize(write, hitX);
            stream->Serialize(write, hitY);
            stream->Serialize(write, hitZ);
            stream->Serialize(write, normalX);
            stream->Serialize(write, normalY);
            stream->Serialize(write, normalZ);
            stream->Serialize(write, localHitX);
            stream->Serialize(write, localHitY);
            stream->Serialize(write, localHitZ);
            stream->Serialize(write, localDirectionX);
            stream->Serialize(write, localDirectionY);
            stream->Serialize(write, localDirectionZ);
        }
    };

    struct AuthoritativeHit {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarAuthoritativeHit");

        uint64_t carId = 0;
        uint64_t missionGeneration = 0;
        uint64_t controllerGuid = 0;
        uint64_t serverSequence = 0;
        float damage = 0.0f;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;
        float hitX = 0.0f;
        float hitY = 0.0f;
        float hitZ = 0.0f;
        float normalX = 0.0f;
        float normalY = 0.0f;
        float normalZ = 0.0f;
        float localHitX = 0.0f;
        float localHitY = 0.0f;
        float localHitZ = 0.0f;
        float localDirectionX = 0.0f;
        float localDirectionY = 0.0f;
        float localDirectionZ = 0.0f;
        // The accepted report's shooter (a player or a host NPC), whose
        // replica the simulation host passes to C_car::Hit as the attacker.
        uint64_t shooterId = 0;
        HitKind kind = HitKind::Bullet;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto kindValue = static_cast<uint8_t>(kind);
            stream->Serialize(write, kindValue);
            kind = static_cast<HitKind>(kindValue);
            stream->Serialize(write, carId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, controllerGuid);
            stream->Serialize(write, serverSequence);
            stream->Serialize(write, shooterId);
            stream->Serialize(write, damage);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
            stream->Serialize(write, hitX);
            stream->Serialize(write, hitY);
            stream->Serialize(write, hitZ);
            stream->Serialize(write, normalX);
            stream->Serialize(write, normalY);
            stream->Serialize(write, normalZ);
            stream->Serialize(write, localHitX);
            stream->Serialize(write, localHitY);
            stream->Serialize(write, localHitZ);
            stream->Serialize(write, localDirectionX);
            stream->Serialize(write, localDirectionY);
            stream->Serialize(write, localDirectionZ);
        }
    };

    // Server -> every player but the attacker: an accepted melee strike on a
    // car, for its sound (the dent comes with the controller's mesh).
    struct MeleeImpact {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarMeleeImpact");

        uint64_t carId = 0;
        uint64_t missionGeneration = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, carId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
        }
    };
} // namespace Mafia1Online::Shared::Car
