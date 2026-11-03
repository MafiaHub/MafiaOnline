#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Pedestrian {
    // One C_traffic_generator::ElementNewScream call (0x4510b0): every walker
    // scream, pain cry and remark goes through it. The simulation host has no
    // sound driver, so it reports the call; the server forwards it to the
    // players in range, and each plays it with its own camera range check
    // and cooldown, as single player does.
    struct Scream {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianScream");

        uint64_t missionGeneration = 0;
        int32_t voice = 0;
        int32_t screamId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float minimumRange = 0.0f;
        float maximumRange = 0.0f;
        float curve = 0.0f;
        float minimumVolume = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, voice);
            stream->Serialize(write, screamId);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, minimumRange);
            stream->Serialize(write, maximumRange);
            stream->Serialize(write, curve);
            stream->Serialize(write, minimumVolume);
        }
    };
} // namespace Mafia1Online::Shared::Pedestrian
