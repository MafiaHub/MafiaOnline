#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Mafia1Online::Features::Npc {
    // The walk C_human plays for a body moving along `motion` while it
    // faces `facing` (only their ground plane counts), and its speed:
    // walk1 (6) at HUMAN_MOVE_WALK_FORWARD_SPEED 1.7 m/s, back1 (7) at
    // HUMAN_MOVE_BACK_SPEED 1.5, strafL1 (11) and strafR1 (12) at
    // HUMAN_MOVE_WALK_SIDE_SPEED 1.5 (C_human.cpp, before m_fSpeed, 1 for
    // most). Its right is (z, 0, -x) of its facing, as C_human::Go_Right
    // moves it.
    struct Step {
        int16_t state;
        float speed;
    };
    inline Step StepFor(const glm::vec3 &motion, const glm::vec3 &facing) {
        const glm::vec3 way(motion.x, 0.0f, motion.z);
        const glm::vec3 ahead(facing.x, 0.0f, facing.z);
        if (!(glm::length(way) > 0.0001f) || !(glm::length(ahead) > 0.0001f)) {
            return {6, 1.7f};
        }
        const glm::vec3 forward = glm::normalize(ahead);
        const glm::vec3 right(forward.z, 0.0f, -forward.x);
        const float along = glm::dot(way, forward);
        const float across = glm::dot(way, right);
        if (std::abs(along) >= std::abs(across)) {
            return along >= 0.0f ? Step {6, 1.7f} : Step {7, 1.5f};
        }
        return across >= 0.0f ? Step {12, 1.5f} : Step {11, 1.5f};
    }

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

    // A copy that did not replay its NPC's door clip (streamed in too late
    // into it) draws the host's body still going through the clip at
    // `time`. It stands where that body's clip ended instead: the time of
    // the first later pose that came without a door state, or the newest
    // pose while there is none yet. `time` itself otherwise. Each entry
    // has the pose's time `at` and its `locomotion` state.
    template <typename Entries, typename IsDoor>
    double DoorEndTime(const Entries &entries, double time, IsDoor isDoor) {
        // The state drawn with the pose at `time`, as GaitAt picks it.
        bool door = false;
        bool seen = false;
        for (const auto &entry : entries) {
            if (static_cast<double>(entry.at) > time && seen) {
                break;
            }
            door = isDoor(entry.locomotion);
            seen = true;
        }
        if (!door) {
            return time;
        }
        double end = time;
        for (const auto &entry : entries) {
            if (static_cast<double>(entry.at) <= time) {
                continue;
            }
            end = static_cast<double>(entry.at);
            if (!isDoor(entry.locomotion)) {
                break;
            }
        }
        return end;
    }
} // namespace Mafia1Online::Features::Npc
