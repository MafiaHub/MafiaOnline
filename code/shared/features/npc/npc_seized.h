#pragma once

#include "shared/features/npc/npc_report.h"

#include <networking/rpc/rpc.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace Mafia1Online::Shared::Npc {
    // Host -> server -> every player but the host: a police officer started
    // the arrest or fine animations on a player's replica (police action
    // state 11, IDA 0x5171c1-0x51721c). Retail plays the target's half on its
    // own player; the arrested player's client plays it on its ped with the
    // controls locked, every other client on that player's replica. The host
    // already plays it natively and reports Caught once both halves end.
    struct Seized {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcSeized");

        // Where state 10 (AI_POLICE_ARREST_WALK) stood both: the officer an
        // arm's length from the target, the two facing each other. Retail
        // places them with SetPos/SetDir and their halves line up only there.
        struct Pose {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float yaw = 0.0f;

            [[nodiscard]] bool Valid() const {
                return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(yaw);
            }
            [[nodiscard]] glm::vec3 Position() const {
                return {x, y, z};
            }
            [[nodiscard]] glm::vec3 Direction() const {
                return {std::sin(yaw), 0.0f, std::cos(yaw)};
            }
            void Serialize(MafiaNet::BitStream *stream, bool write) {
                stream->Serialize(write, x);
                stream->Serialize(write, y);
                stream->Serialize(write, z);
                stream->Serialize(write, yaw);
            }
        };

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t targetNetworkId = 0;
        uint64_t targetSpawnGeneration = 0;
        Caught::Outcome outcome = Caught::Outcome::Arrest;
        // The officer's NpcEntity; the host leaves it 0 and the server fills
        // it in from the key before relaying.
        uint64_t officerNetworkId = 0;
        Pose officer;
        Pose target;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto outcomeValue = static_cast<uint8_t>(outcome);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, targetNetworkId);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, outcomeValue);
            stream->Serialize(write, officerNetworkId);
            officer.Serialize(stream, write);
            target.Serialize(stream, write);
            if (!write) {
                outcome = static_cast<Caught::Outcome>(outcomeValue);
            }
        }
    };

    // A seized body farther than this from its aligned pose is not pulled to
    // it: it moved on (the stream) or the pose is stale; it plays in place.
    inline constexpr float kSeizedAlignReach = 3.0f;

    // Whether a body at `current` takes the aligned pose: close enough that
    // the move is the retail placement, not a teleport.
    inline bool SeizedAlignable(const glm::vec3 &current, const Seized::Pose &pose) {
        if (!pose.Valid() || !std::isfinite(current.x) || !std::isfinite(current.y) || !std::isfinite(current.z)) {
            return false;
        }
        return glm::distance(current, pose.Position()) <= kSeizedAlignReach;
    }

    // How long an arrest or fine half holds its body: the clip's length as
    // Do_PlayAnim returns it (state 11 waits the longer half in state 12,
    // 0x517231-0x51728b; the arrest ran 15.3 s), bounded. Not the work state: Do_PlayAnim leaves
    // these clips at work state 1, so a hold ending when the work state
    // left 4 ended at once, and the arrested player kept the controls and
    // walked out of the cuffs.
    inline constexpr std::chrono::milliseconds kSeizedHoldLimit {20000};
    inline std::chrono::milliseconds SeizedHold(uint32_t clipMs) {
        return std::min(std::chrono::milliseconds(clipMs), kSeizedHoldLimit);
    }

    // The target's animation: PolicajtZatykany for the arrest (0x517215),
    // PolicajtPokutovany for the fine (0x5171f0).
    inline const char *SeizedAnimation(Caught::Outcome outcome) {
        return outcome == Caught::Outcome::Fine ? "PolicajtPokutovany.I3D" : "PolicajtZatykany.I3D";
    }
    // The officer's: PolicajtZatykajici for the arrest (0x517202),
    // PolicajtPokutujici for the fine (0x5171dd).
    inline const char *SeizingAnimation(Caught::Outcome outcome) {
        return outcome == Caught::Outcome::Fine ? "PolicajtPokutujici.I3D" : "PolicajtZatykajici.I3D";
    }
} // namespace Mafia1Online::Shared::Npc
