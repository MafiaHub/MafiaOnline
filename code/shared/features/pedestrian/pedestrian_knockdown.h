#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Mafia1Online::Shared::Pedestrian {
    // A car meeting a walker: C_don_ocas::Collision (0x44ca50) hits it with
    // the car's velocity as the direction and |v| x 4.2 as the damage, and
    // C_traffic_element::Hit (0x4588d0) knocks it down from 16 (0x458a54).
    // Slower it only dodges and keeps its collision, so the car pushes it.
    inline constexpr float kCarImpactDamageScale = 4.2f;
    inline constexpr float kKnockdownDamage = 16.0f;
    // ActivateFree gives a walker 60 + rnd(20) health and Hit kills it below
    // 0.5: more than this kills any walker a car meets unhurt.
    inline constexpr float kSurelyFatalDamage = 78.5f;

    // Hit's knockdown falls (0x458f19-0x459013); the melee strike's are the
    // same.
    inline constexpr int32_t kFallBack = 106;
    inline constexpr int32_t kFallLeft = 107;
    inline constexpr int32_t kFallRight = 108;
    inline constexpr int32_t kFallFrontDeadA = 112;
    inline constexpr int32_t kFallFrontDeadB = 113;
    inline constexpr int32_t kFallFront = 114;

    inline float CarImpactDamage(const glm::vec3 &velocity) {
        return glm::length(velocity) * kCarImpactDamageScale;
    }

    inline bool KnocksDown(const glm::vec3 &velocity) {
        const float damage = CarImpactDamage(velocity);
        return std::isfinite(damage) && damage >= kKnockdownDamage;
    }

    // m_uCollisionUpdateDelay (+0x14c) Hit leaves with each fall: 2 for a
    // front one, 3 otherwise. SetAnim (0x459450) puts the collision back when
    // it reads 1, so after the fall and the clip queued behind it. 0 for an
    // animation that is no knockdown.
    inline uint32_t KnockdownCollisionDelay(int32_t animation) {
        switch (animation) {
        case kFallFrontDeadA:
        case kFallFrontDeadB:
        case kFallFront: return 2;
        case kFallBack:
        case kFallLeft:
        case kFallRight: return 3;
        default: return 0;
        }
    }

    struct Knockdown {
        int32_t animation = 0;
        uint32_t collisionDelay = 0;
        // The walker's new heading.
        glm::vec3 facing {0.0f, 0.0f, 1.0f};
    };

    // Hit's knockdown for a car impact along `impact` on a walker facing
    // `facing` (0x458f0c-0x45916f): within 45 degrees of its heading it falls
    // forward and turns to the impact, from behind 135 degrees it falls back
    // and turns against it, otherwise it falls to the side it was hit from.
    // A fatal front fall picks one of two clips at random; this takes B.
    inline Knockdown CarKnockdown(const glm::vec3 &impact, const glm::vec3 &facing, bool fatal) {
        constexpr float kQuarterPi = 0.78539819f;
        constexpr float kThreeQuarterPi = 2.3561945f;
        constexpr float kPi = 3.14159265f;
        const float lengths = glm::length(impact) * glm::length(facing);
        const float angle = lengths > 0.0f ? std::acos(std::clamp(glm::dot(impact, facing) / lengths, -1.0f, 1.0f)) : 0.0f;
        Knockdown result;
        glm::vec3 turned = facing;
        if (angle < kQuarterPi) {
            result.animation = fatal ? kFallFrontDeadB : kFallFront;
            turned = impact;
        }
        else if (!(angle < kThreeQuarterPi)) {
            result.animation = kFallBack;
            turned = -impact;
        }
        else {
            // AngleTo2 (0x408490) from the heading to the impact on the ground.
            float delta = std::atan2(impact.z, impact.x) - std::atan2(facing.z, facing.x);
            if (delta < -kPi) {
                delta += 2.0f * kPi;
            }
            if (delta > kPi) {
                delta -= 2.0f * kPi;
            }
            result.animation = delta < 0.0f ? kFallRight : kFallLeft;
        }
        result.collisionDelay = KnockdownCollisionDelay(result.animation);
        turned.y = 0.0f;
        const float length = glm::length(turned);
        result.facing = length > 1e-4f ? turned / length : glm::vec3(facing.x, 0.0f, facing.z);
        return result;
    }
} // namespace Mafia1Online::Shared::Pedestrian
