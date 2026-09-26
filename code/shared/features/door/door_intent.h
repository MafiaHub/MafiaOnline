#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Door {
    // A local native use of a mission door. The server validates the player's
    // current life, generation, proximity and rate before accepting it.
    struct DoorIntent {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::DoorIntent");

        uint64_t playerId = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration = 0;
        std::string frameName;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        bool open = false;
        bool reverse = false;
        bool pairedReverse = false;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, playerId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
            stream->Serialize(write, frameName);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, open);
            stream->Serialize(write, reverse);
            stream->Serialize(write, pairedReverse);
        }
    };
} // namespace Mafia1Online::Shared::Door
