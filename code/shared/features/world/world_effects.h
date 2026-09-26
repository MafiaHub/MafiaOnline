#pragma once

#include <networking/rpc/rpc.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::World {
    // One-shot script effects. Every client drops a message whose mission
    // generation is not the mission it has loaded; the server has already
    // applied any player damage.
    struct Explosion {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::WorldExplosion");
        static constexpr float kMaxRadius        = 30.0f;
        static constexpr float kMaxDamage        = 1000.0f;

        uint64_t missionGeneration = 0;
        float x                    = 0.0f;
        float y                    = 0.0f;
        float z                    = 0.0f;
        float radius               = 15.0f;
        // Zero is a visual explosion that leaves cars and objects alone.
        float damage = 400.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, radius);
            stream->Serialize(write, damage);
        }
    };

    struct Fire {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::WorldFire");
        static constexpr uint32_t kMaxLifeMs     = 60000;
        static constexpr float kMaxRadius        = 10.0f;
        static constexpr float kMaxDamage        = 200.0f;

        uint64_t missionGeneration = 0;
        float x                    = 0.0f;
        float y                    = 0.0f;
        float z                    = 0.0f;
        uint32_t lifeMs            = 5000;
        float radius               = 2.5f;
        float damage               = 50.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, lifeMs);
            stream->Serialize(write, radius);
            stream->Serialize(write, damage);
        }
    };

    // A positional sound the game plays once and releases itself.
    struct PlaySound {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::WorldPlaySound");

        uint64_t missionGeneration = 0;
        std::string wave;
        float x      = 0.0f;
        float y      = 0.0f;
        float z      = 0.0f;
        float radius = 25.0f;
        float volume = 1.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, wave);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, radius);
            stream->Serialize(write, volume);
        }
    };

} // namespace Mafia1Online::Shared::World
