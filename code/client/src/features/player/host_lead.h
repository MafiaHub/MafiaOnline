#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

// Simulation host: its AI aims at, chases and avoids players' replicas,
// so it shows each one where the player is now rather than a render delay
// and the poses' transit behind, as it does player cars (CarService).
namespace Mafia1Online::Features::Player {
    // How far past the newest pose a player is now: half the server's round
    // trip to the player (PlayerEntity::ping) and half this host's. Within
    // what the snapshot buffer extrapolates past a pose one send interval
    // old, so the body never reaches its bound between poses.
    inline float HostTransitMs(uint16_t playerPing, int hostPing, float intervalMs, float maxExtrapolationMs) {
        const float transit = 0.5f * (static_cast<float>(playerPing) + static_cast<float>(std::max(hostPing, 0)));
        return std::clamp(transit, 0.0f, std::max(maxExtrapolationMs - intervalMs, 0.0f));
    }

    // The render clock's lead, eased toward its target critically damped over
    // about `smoothSeconds` (CarPoseBuffer::AdvanceLead): a change of lead is
    // one of the body's speed on screen, so it must neither step nor
    // overshoot.
    struct HostLead {
        float ms   = 0.0f;
        float rate = 0.0f;

        float Advance(float targetMs, float dtSeconds, float smoothSeconds) {
            const float omega  = 2.0f / smoothSeconds;
            const float x      = omega * std::max(dtSeconds, 0.0f);
            const float decay  = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
            const float change = ms - targetMs;
            const float pull   = (rate + omega * change) * std::max(dtSeconds, 0.0f);
            rate               = (rate - omega * pull) * decay;
            ms                 = targetMs + (change + pull) * decay;
            return ms;
        }
    };

    // A new pose moves what the extrapolated body should show at once (a
    // player who stopped, turned or backed off): the jump becomes an offset
    // that decays as CarPoseBuffer's corrections do, 7% left after 200 ms.
    // One beyond kSnapDistance is a teleport and applies at once.
    struct HostCorrection {
        static constexpr float kSeconds      = 0.075f;
        static constexpr float kSnapDistance = 5.0f;

        glm::vec3 offset {0.0f};

        // `before` and `after` are the pose at the same render time without
        // and with the new one.
        void Rebase(const glm::vec3 &before, const glm::vec3 &after) {
            offset += before - after;
            if (!(glm::length(offset) <= kSnapDistance)) {
                offset = glm::vec3(0.0f);
            }
        }

        glm::vec3 Advance(float dtSeconds) {
            offset *= std::exp(-std::max(dtSeconds, 0.0f) / kSeconds);
            return offset;
        }
    };
} // namespace Mafia1Online::Features::Player
