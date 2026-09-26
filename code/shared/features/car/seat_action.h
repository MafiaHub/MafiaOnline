#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Car {
    enum class SeatAction : uint8_t {
        Enter = 1,
        StealBegin = 2,
        Steal = 3,
        Exit = 4,
        ExitBlocked = 5,
        EnterBegin = 6,
        Move = 7,
    };

    struct SeatIntent {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::SeatIntent");

        uint64_t carId = 0;
        uint64_t playerId = 0;
        uint64_t spawnGeneration = 0;
        uint64_t missionGeneration = 0;
        uint32_t sequence = 0;
        uint8_t seat = 0;
        SeatAction action = SeatAction::Enter;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, carId);
            stream->Serialize(write, playerId);
            stream->Serialize(write, spawnGeneration);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, sequence);
            stream->Serialize(write, seat);
            auto value = static_cast<uint8_t>(action);
            stream->Serialize(write, value);
            if (!write) {
                action = static_cast<SeatAction>(value);
            }
        }
    };

    struct SeatEvent: SeatIntent {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::SeatEvent");
        uint64_t serverSequence = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            SeatIntent::Serialize(stream, write);
            stream->Serialize(write, serverSequence);
        }
    };
} // namespace Mafia1Online::Shared::Car
