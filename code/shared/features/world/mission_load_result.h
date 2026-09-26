#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::World {
    enum class MissionLoadState : uint8_t {
        Unloaded,
        Ready,
        SceneFailed,
        CollisionFailed,
        GameInitFailed,
    };

    struct MissionLoadResult {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::MissionLoadResult");

        uint64_t generation = 0;
        MissionLoadState state = MissionLoadState::Unloaded;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, generation);
            auto value = static_cast<uint8_t>(state);
            stream->Serialize(write, value);
            if (!write) {
                state = static_cast<MissionLoadState>(value);
            }
        }
    };
} // namespace Mafia1Online::Shared::World
