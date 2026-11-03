#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Player {
    // The controlling client's player whistled on foot (the horn key in
    // C_player::AI_walk, 7b_zip.wav). The server plays the same wave where
    // the player stands for everyone else.
    struct Whistle {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PlayerWhistle");

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration   = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
        }
    };

    // C_player::AI_walk's whistle: PLAYER_ZIP_SOUND and its range, at most
    // once per PLAYER_ZIP_SOUND_INTERVAL.
    inline constexpr const char *kWhistleWave    = "7b_zip.wav";
    inline constexpr float kWhistleRange         = 15.0f;
    inline constexpr float kWhistleHeight        = 1.5f;
    inline constexpr uint32_t kWhistleIntervalMs = 2000;
} // namespace Mafia1Online::Shared::Player
