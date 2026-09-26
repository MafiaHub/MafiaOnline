#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Combat {
    // A native bullet collision report. The server uses its own weapon rule,
    // life state and player positions; client damage is never authoritative.
    struct HitReport {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CombatHitReport");
        // C_car::Hit forwards an occupant hit with its own zero flags as the
        // body part; retail HitInCar ignores it. Seated targets report this.
        static constexpr uint8_t kInCarBodyPart = 7;
        enum class Kind : uint8_t { Bullet = 0, Melee = 1 };

        uint64_t shooterNetworkId       = 0;
        uint64_t targetNetworkId        = 0;
        uint64_t missionGeneration      = 0;
        uint64_t shooterSpawnGeneration = 0;
        uint64_t targetSpawnGeneration  = 0;
        // The accepted Fire or Melee intent sequence this impact belongs to.
        uint32_t shotSequence           = 0;
        Kind kind                       = Kind::Bullet;
        uint8_t pelletIndex             = 0;
        uint8_t bodyPart                = 0;
        float directionX                = 0.0f;
        float directionY                = 0.0f;
        float directionZ                = 0.0f;
        float hitX                      = 0.0f;
        float hitY                      = 0.0f;
        float hitZ                      = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, shooterNetworkId);
            stream->Serialize(write, targetNetworkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, shooterSpawnGeneration);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, shotSequence);
            uint8_t kindValue = static_cast<uint8_t>(kind);
            stream->Serialize(write, kindValue);
            if (!write) {
                kind = static_cast<Kind>(kindValue);
            }
            stream->Serialize(write, pelletIndex);
            stream->Serialize(write, bodyPart);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
            stream->Serialize(write, hitX);
            stream->Serialize(write, hitY);
            stream->Serialize(write, hitZ);
        }
    };
} // namespace Mafia1Online::Shared::Combat
