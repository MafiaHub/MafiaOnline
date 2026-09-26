#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Car {
    // A local shoot record's impact on a network car. The server consumes the
    // matching accepted Fire pellet before forwarding an authoritative hit.
    struct HitReport {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarHitReport");

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
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarAuthoritativeHit");

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

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, carId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, controllerGuid);
            stream->Serialize(write, serverSequence);
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
} // namespace Mafia1Online::Shared::Car
