#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Combat {
    // The thrower's client reports where its native grenade or Molotov went
    // off. The server checks it against the accepted throw and applies the
    // retail damage from its own player positions.
    struct DetonationReport {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CombatDetonationReport");

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration   = 0;
        uint32_t sequence          = 0;
        float x                    = 0.0f;
        float y                    = 0.0f;
        float z                    = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
            stream->Serialize(write, sequence);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
        }
    };

    // Server decision. Other clients replace their display copy with the
    // native explosion or fire here. visualOnly marks an expired throw.
    struct Detonation {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CombatDetonation");

        uint64_t throwerNetworkId  = 0;
        uint64_t missionGeneration = 0;
        uint32_t sequence          = 0;
        uint8_t grenadeType        = 0;
        bool visualOnly            = false;
        float x                    = 0.0f;
        float y                    = 0.0f;
        float z                    = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, throwerNetworkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, sequence);
            stream->Serialize(write, grenadeType);
            stream->Serialize(write, visualOnly);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
        }
    };
} // namespace Mafia1Online::Shared::Combat
