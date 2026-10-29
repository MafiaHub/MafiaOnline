#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace Mafia1Online::Features::Player {
    // Retail's stop pose: C_human HUMAN_ANIM_STOP (1, the breath), which a
    // traffic walker's TRAFFIC_ANIM_IDLE (GAME_ANIM_BREATH) is too.
    inline constexpr int16_t kStopState = 1;

    // C_human's movement states, HUMAN_ANIM_MOVEMENT_FIRST..LAST (5-20,
    // low nine bits): the runs and walks, less the turns on the spot (8, 9,
    // and HUMAN_ANIM_TURN 10). A walker's walk (6), panic run (5) and web
    // walk (11) are the same GAME_ANIM ids.
    inline bool MovingGait(int32_t state) {
        const int32_t base = state & 0x1ff;
        return base >= 5 && base <= 20 && base != 8 && base != 9 && base != 10;
    }

    // A replica stands still only because its stream stalled: the snapshot
    // buffer ran its newest pose (at `newestTime`, moving at `velocity`) on
    // for its whole extrapolation bound and holds it there. Retail never
    // runs a cycle on a body that goes nowhere; the next pose resumes it.
    // Slower than the buffer's kRestSpeed (0.1 m/s) is a body at rest, which
    // keeps the state it was sent with (an unchanged pose is not resent).
    inline bool Stalled(double renderTime, double newestTime, float maxExtrapolationMs, const glm::vec3 &velocity) {
        return newestTime > 0.0 && renderTime >= newestTime + maxExtrapolationMs && glm::length(glm::vec3(velocity.x, 0.0f, velocity.z)) >= 0.1f;
    }
} // namespace Mafia1Online::Features::Player
