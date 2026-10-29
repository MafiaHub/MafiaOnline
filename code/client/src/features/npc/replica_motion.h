#pragma once

#include <algorithm>

namespace Mafia1Online::Features::Npc {
    // How much of the gap between where a replica's own animation left its
    // body and its stream still shows, `seconds` into a release lasting
    // `duration`: 1 at the release, 0 once it is over, easing out at both
    // ends so the body neither jumps off nor lands with a jolt.
    inline float ReleaseWeight(float seconds, float duration) {
        if (!(duration > 0.0f) || !(seconds < duration)) {
            return 0.0f;
        }
        const float t = std::clamp(seconds / duration, 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }
} // namespace Mafia1Online::Features::Npc
