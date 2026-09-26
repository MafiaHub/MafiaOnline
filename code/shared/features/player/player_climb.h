#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Player {
    // A native C_human::Do_Climb the controlling client started, with the pose
    // it started from. The server relays it so observers replay the same
    // climb; the animation's root motion, not the pose stream, lifts the body.
    struct Climb {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PlayerClimb");

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration   = 0;
        float x                    = 0.0f;
        float y                    = 0.0f;
        float z                    = 0.0f;
        float directionX           = 0.0f;
        float directionZ           = 1.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionZ);
        }
    };
} // namespace Mafia1Online::Shared::Player
