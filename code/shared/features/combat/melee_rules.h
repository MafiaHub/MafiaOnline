#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Mafia1Online::Shared::Combat {
    // Retail reach is the attack animation itself; the stock item range is 2
    // units. A reported melee hit must come from an attacker this close to
    // the target's server position, roughly facing it, and strike the
    // target's body.
    inline constexpr float kMeleeReach = 2.6f;
    inline constexpr float kMeleeHeightDifference = 1.5f;
    inline constexpr float kMeleeHitRadius = 1.6f;
    inline constexpr float kMeleeHitBelowFeet = 0.5f;
    inline constexpr float kMeleeHitAboveFeet = 2.5f;
    // cos(100 degrees): the target may be a little behind the attacker's side.
    inline constexpr float kMeleeFacingDot = -0.17f;

    // C_don_ocas::Hit passes a walker's melee hit on as the torso (5); the
    // attacker reports that body part.
    inline constexpr uint8_t kWalkerMeleeBodyPart = 5;
    // A walker keeps walking on the simulation host while the attacker sees
    // its replica one render delay (about 200 ms) plus the host's report
    // and relay late, so the server's walker position leads the replica
    // the attacker hit by its speed times that delay.
    inline constexpr float kWalkerMeleeLagSeconds = 0.5f;
    inline constexpr float kWalkerMeleeMaximumLag = 2.0f;

    inline float WalkerMeleeLagAllowance(const glm::vec3 &velocity) {
        const float speed = glm::length(glm::vec3(velocity.x, 0.0f, velocity.z));
        return std::isfinite(speed) ? std::min(speed * kWalkerMeleeLagSeconds, kWalkerMeleeMaximumLag) : 0.0f;
    }

    // The geometry of a reported melee hit against the server's positions;
    // lagAllowance widens the horizontal reach and hit radius for a target
    // that moves on another peer.
    inline bool MeleeHitPlausible(const glm::vec3 &attacker, const glm::vec3 &attackerFacing, const glm::vec3 &target, const glm::vec3 &hit, float lagAllowance = 0.0f) {
        const glm::vec3 offset = target - attacker;
        const glm::vec3 flat(offset.x, 0.0f, offset.z);
        const float distance = glm::length(flat);
        if (!std::isfinite(distance) || !std::isfinite(lagAllowance) || lagAllowance < 0.0f || distance > kMeleeReach + lagAllowance ||
            std::abs(offset.y) > kMeleeHeightDifference || !std::isfinite(hit.x) || !std::isfinite(hit.y) || !std::isfinite(hit.z) ||
            glm::length(glm::vec3(hit.x - target.x, 0.0f, hit.z - target.z)) > kMeleeHitRadius + lagAllowance || hit.y < target.y - kMeleeHitBelowFeet ||
            hit.y > target.y + kMeleeHitAboveFeet) {
            return false;
        }
        return distance <= 0.2f || glm::dot(flat / distance, attackerFacing) >= kMeleeFacingDot;
    }

    // A strike on a car lands on its body, which is too big for a target
    // position: the hit point itself must be within reach in front of the
    // attacker (the server matches it against the car's body separately).
    inline constexpr float kMeleeCarReach = kMeleeReach + 0.6f;
    inline constexpr float kMeleeCarHitBelowFeet = 0.5f;
    inline constexpr float kMeleeCarHitAboveFeet = 2.5f;

    inline bool MeleeCarHitPlausible(const glm::vec3 &attacker, const glm::vec3 &attackerFacing, const glm::vec3 &hit) {
        const glm::vec3 offset = hit - attacker;
        const glm::vec3 flat(offset.x, 0.0f, offset.z);
        const float distance = glm::length(flat);
        if (!std::isfinite(distance) || distance > kMeleeCarReach || offset.y < -kMeleeCarHitBelowFeet || offset.y > kMeleeCarHitAboveFeet) {
            return false;
        }
        return distance <= 0.2f || glm::dot(flat / distance, attackerFacing) >= kMeleeFacingDot;
    }

    // C_human::AI books HumanBoxing for every swing that strikes a walker's
    // collision or a human (0x571342, 0x5714e6). On the simulation host a
    // player's replica only replays that player's swing, so its own strikes
    // book nothing: the player's validated hit books it, once.
    inline bool HostBooksBoxing(bool networkPlayer, bool validatedHit) {
        return !networkPlayer || validatedHit;
    }

    // One report per accepted swing: C_human::AI stops sweeping a swing once
    // it struck something, and the server consumes the swing on its first
    // hit, so a second report of the same swing is never valid.
    class MeleeReportGate final {
      public:
        bool Admit(uint32_t swingSequence) {
            if (swingSequence == 0 || swingSequence == _lastSequence) {
                return false;
            }
            _lastSequence = swingSequence;
            return true;
        }
        void Reset() {
            _lastSequence = 0;
        }

      private:
        uint32_t _lastSequence = 0;
    };
} // namespace Mafia1Online::Shared::Combat
