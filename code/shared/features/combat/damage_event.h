#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Combat {
    // Server-accepted impact. The matching/recent State is the durable truth;
    // this event only drives a one-shot native reaction on live recipients.
    struct DamageEvent {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CombatDamageEvent");

        uint64_t targetNetworkId       = 0;
        uint64_t missionGeneration     = 0;
        uint64_t targetSpawnGeneration = 0;
        uint32_t targetRevision        = 0;
        uint8_t bodyPart               = 0;
        // Reaction kind: 0 bullet, 1 melee hard knockdown, 4 car impact.
        uint8_t hitType                = 0;
        float damage                   = 0.0f;
        float directionX               = 0.0f;
        float directionY               = 0.0f;
        float directionZ               = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, targetNetworkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, targetRevision);
            stream->Serialize(write, bodyPart);
            stream->Serialize(write, hitType);
            stream->Serialize(write, damage);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
        }
    };
} // namespace Mafia1Online::Shared::Combat
