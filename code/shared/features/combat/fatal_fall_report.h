#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Combat {
    // A native fatal-fall candidate. The server accepts it only for the
    // controller's current life after observing a sufficient vertical drop.
    struct FatalFallReport {
        enum class Cause : uint8_t {
            Fall,
            // Movement's water latch or a fall volume (material 40). The
            // player reports only its own death, so no drop is required.
            Drowned,
            FallVolume,
        };

        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::FatalFallReport");

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration   = 0;
        Cause cause                = Cause::Fall;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
            auto value = static_cast<uint8_t>(cause);
            stream->Serialize(write, value);
            if (!write) {
                cause = value <= static_cast<uint8_t>(Cause::FallVolume) ? static_cast<Cause>(value) : Cause::Fall;
            }
        }
    };
} // namespace Mafia1Online::Shared::Combat
