#pragma once

#include "shared/net/quantize.h"

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Combat {
    // A held aim moving: where the weapon points and where the upper body
    // looks, while the aim flag itself does not change (that, and crouching,
    // go through the reliable Intent and State). Unreliable both ways:
    // controller -> server, then server -> the peers that stream the body.
    // A lost one is replaced by the next; `sequence` drops a late one.
    struct AimUpdate {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CombatAimUpdate");
        // Pose target offsets the server accepts, as Intent::Aim (1..25 m).
        static constexpr float kMaxPoseOffset = 32.0f;

        uint64_t networkId       = 0;
        uint64_t spawnGeneration = 0;
        // The sender's own count: the controller's upstream, the server's for
        // the relay (State::aimSequence says which relay a State includes).
        uint16_t sequence = 0;
        bool aiming       = false;
        glm::vec3 direction {0.0f, 0.0f, 1.0f};
        glm::vec3 poseTargetOffset {0.0f};

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->SerializeCompressed(write, networkId);
            stream->SerializeCompressed(write, spawnGeneration);
            stream->Serialize(write, sequence);
            stream->Serialize(write, aiming);
            if (aiming) {
                Net::SerializeDirection(stream, write, direction, 12, 11);
            }
            // 1/1024 m over +-32 m.
            Net::SerializeFixed(stream, write, poseTargetOffset.x, -kMaxPoseOffset, kMaxPoseOffset, 16);
            Net::SerializeFixed(stream, write, poseTargetOffset.y, -kMaxPoseOffset, kMaxPoseOffset, 16);
            Net::SerializeFixed(stream, write, poseTargetOffset.z, -kMaxPoseOffset, kMaxPoseOffset, 16);
        }
    };

    // Whether `incoming` is newer than `last` in a wrapping 16-bit count.
    inline bool NewerSequence(uint16_t incoming, uint16_t last) {
        return static_cast<int16_t>(incoming - last) > 0;
    }
} // namespace Mafia1Online::Shared::Combat
