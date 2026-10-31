#pragma once

#include "shared/features/pedestrian/pedestrian_knockdown.h"

#include <networking/rpc/rpc.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Mafia1Online::Shared::Pedestrian {
    // A walker's reaction to a car, as retail decides it in the car's own
    // frame: C_traffic_element::Tick's threat dodge (0x454221-0x45488d) and
    // C_don_ocas::Collision's Hit (0x44ca50, 0x4588d0). In multiplayer the
    // car's simulation controller is the one client that has the car where
    // it really is, so it decides; every client plays the reaction when its
    // own copy of the car gets to that moment, and the simulation host
    // applies it to the walker every replica follows.

    // Tick's dodges (0x4546f1-0x45475d), each followed by its mirror (+1),
    // which Tick takes for a walker on the car's other side. Hit's slow
    // contact picks the same four.
    inline constexpr int32_t kDodgeBack = 322;
    inline constexpr int32_t kDodgeFront = 324;
    inline constexpr int32_t kDodgeSideLeft = 326;
    inline constexpr int32_t kDodgeSideRight = 328;
    inline constexpr int32_t kFirstDodge = 322;
    inline constexpr int32_t kLastDodge = 329;
    // The mega panic's crouch, kneel and prone (330-338): Tick returns
    // before the threat test for a walker in one.
    inline constexpr int32_t kFirstMegaPanic = 330;
    inline constexpr int32_t kLastMegaPanic = 338;

    // Tick's watch distance for a car (0x454238-0x45426c): while a horn's
    // g_iTrafficCollisionCooldown (0x63d738) runs, 50 for a car faster than
    // 1 m/s and 10 for a slower one; otherwise -1, which has
    // C_car::ChcesMeSejmout (0x4230e0) watch speed x 1.8 ahead and only a car
    // with a wheel on the road.
    inline constexpr float kHornFastWatch = 50.0f;
    inline constexpr float kHornSlowWatch = 10.0f;
    inline constexpr float kHornSlowSpeed = 1.0f;
    // AI_drive's horn: the cooldown it adds at the press, capped (0x591de5).
    inline constexpr int32_t kHornCooldownMs = 1000;

    inline float ThreatWatchDistance(float carSpeed, bool horn) {
        if (!horn) {
            return -1.0f;
        }
        return carSpeed > kHornSlowSpeed ? kHornFastWatch : kHornSlowWatch;
    }

    // Tick's distance gate for a car ChcesMeSejmout returned (0x454299-
    // 0x45467a), skipped while a horn's cooldown runs: within 30 m (squared
    // 900) of the car's frame, 540 when its heading is within 45 degrees of
    // the walker's, and 0.8 of that within 135 degrees.
    inline constexpr float kThreatSideDistanceSq = 900.0f;
    inline constexpr float kThreatFrontDistanceSq = 540.0f;
    inline constexpr float kThreatSideScale = 0.8f;
    inline constexpr float kQuarterPi = 0.78539819f;
    inline constexpr float kThreeQuarterPi = 2.3561945f;

    // S_vector::AngleTo (0x60fc7e).
    inline float AngleBetween(const glm::vec3 &a, const glm::vec3 &b) {
        const float lengths = glm::length(a) * glm::length(b);
        if (!(lengths > 0.0f)) {
            return 0.0f;
        }
        return std::acos(std::clamp(glm::dot(a, b) / lengths, -1.0f, 1.0f));
    }

    // AngleTo2 (0x408490): the signed turn on the ground from `from` to `to`.
    inline float SignedGroundAngle(const glm::vec3 &from, const glm::vec3 &to) {
        constexpr float kPi = 3.14159265f;
        float delta = std::atan2(to.z, to.x) - std::atan2(from.z, from.x);
        if (delta < -kPi) {
            delta += 2.0f * kPi;
        }
        if (delta > kPi) {
            delta -= 2.0f * kPi;
        }
        return delta;
    }

    inline bool ThreatClose(bool horn, const glm::vec3 &carHeading, const glm::vec3 &carPosition, const glm::vec3 &walkerFacing, const glm::vec3 &walkerPosition) {
        if (horn) {
            return true;
        }
        const float angle = AngleBetween(walkerFacing, carHeading);
        float limit = angle < kQuarterPi ? kThreatFrontDistanceSq : kThreatSideDistanceSq;
        if (angle < kThreeQuarterPi) {
            limit *= kThreatSideScale;
        }
        const glm::vec3 separation = carPosition - walkerPosition;
        return glm::dot(separation, separation) <= limit;
    }

    // The dodge for a car moving along `carVelocity` at `carPosition`
    // (Tick 0x4546ba-0x45483c, Hit's slow contact the same): out of its way
    // ahead, back or to a side by the angle to the walker's heading, and the
    // mirror clip for a walker on the car's other side.
    inline int32_t CarDodge(const glm::vec3 &walkerPosition, const glm::vec3 &walkerFacing, const glm::vec3 &carVelocity, const glm::vec3 &carPosition) {
        const float angle = AngleBetween(walkerFacing, carVelocity);
        int32_t animation = kDodgeFront;
        if (!(angle < kQuarterPi)) {
            if (angle > kThreeQuarterPi) {
                animation = kDodgeBack;
            }
            else {
                animation = SignedGroundAngle(walkerFacing, carVelocity) > 0.0f ? kDodgeSideRight : kDodgeSideLeft;
            }
        }
        float sideX = -carVelocity.z;
        float sideZ = carVelocity.x;
        const float length = std::sqrt(sideX * sideX + sideZ * sideZ);
        if (!(length < 1e-8f)) {
            sideX /= length;
            sideZ /= length;
        }
        const float walkerSide = sideX * walkerPosition.x + sideZ * walkerPosition.z;
        const float carSide = sideX * carPosition.x + sideZ * carPosition.z;
        if (walkerSide - carSide < 0.0f) {
            ++animation;
        }
        return animation;
    }

    inline bool IsDodge(int32_t animation) {
        return animation >= kFirstDodge && animation <= kLastDodge;
    }

    inline bool IsMegaPanic(int32_t animation) {
        return animation >= kFirstMegaPanic && animation <= kLastMegaPanic;
    }

    // Tick's dodge scream (0x454859-0x454888): one of six from 200, heard
    // from 5 to 40 m.
    inline constexpr int32_t kDodgeScreamFirst = 200;
    inline constexpr int32_t kDodgeScreamCount = 6;
    inline constexpr float kDodgeScreamMinimumRange = 5.0f;
    inline constexpr float kDodgeScreamMaximumRange = 40.0f;
    inline constexpr float kDodgeScreamCurve = 0.5f;
    inline constexpr float kDodgeScreamMinimumVolume = 0.4f;

    // Collision's thud (sound table 184) over a contact faster than 5 m/s
    // (squared 25), a metre above the walker.
    inline constexpr uint32_t kImpactSound = 184;
    inline constexpr float kImpactSoundSpeedSq = 25.0f;
    inline constexpr float kImpactSoundHeight = 1.0f;

    // Hit kills a walker whose health falls below 0.5 (0x45894c).
    inline constexpr float kFatalHealth = 0.5f;
    inline bool CarImpactKills(float health, float damage) {
        return health - damage < kFatalHealth;
    }

    enum class ReactionKind : uint8_t {
        // Tick's threat dodge: the walker leaps out of the car's way.
        Dodge = 0,
        // Hit below knockdown damage: the walker side-steps, the car pushes.
        Sidestep = 1,
        // Hit from knockdown damage: the fall, and the clip queued behind.
        Knockdown = 2,
    };

    // How far apart, at most, the controller's walker and the server's may
    // be: a replica trails the host by its render delay, a running walker
    // covers 5 m/s.
    inline constexpr float kReactionWalkerSlack = 6.0f;
    // How far from the car's frame a reaction may come: a horn's 50 m watch
    // plus a car length, and a contact a car length and a step at speed.
    inline constexpr float kDodgeReach = 60.0f;
    inline constexpr float kContactReach = 10.0f;
    inline constexpr float kMaxContactSpeed = 80.0f;
    // One dodge and one contact per walker within this, as the controller's
    // own contact cooldown; a dodge's clip lasts longer anyway.
    inline constexpr double kReactionGapMs = 400.0;

    // Controller -> server -> simulation host and the players near the
    // walker: the reaction retail would have played now. carClock dates it
    // on the controller's physics clock (the frame clock its car's poses
    // carry), so each player plays it once its copy of the car is there.
    struct Reaction {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianReaction");

        uint64_t missionGeneration = 0;
        uint64_t walkerId = 0;
        // Server -> host: the walker's key there.
        uint32_t key = 0;
        uint64_t carId = 0;
        uint32_t carClockMs = 0;
        uint8_t carClockFraction = 0;
        ReactionKind kind = ReactionKind::Dodge;
        int16_t animation = 0;
        // Knockdown: the clip queued behind the fall, 0 for none.
        int16_t queued = 0;
        // Knockdown: the controller's walker died of it.
        bool fatal = false;
        // Tick's scream (kDodgeScreamFirst + 0-5), -1 for none.
        int16_t scream = -1;
        // Where the walker was, and its heading once it reacts (a knockdown
        // turns it to the impact).
        glm::vec3 position {0.0f};
        glm::vec3 facing {0.0f, 0.0f, 1.0f};
        // Sidestep and knockdown: Collision's direction, the car's velocity,
        // whose length x 4.2 is Hit's damage.
        glm::vec3 impact {0.0f};

        double CarClock() const {
            return static_cast<double>(carClockMs) + static_cast<double>(carClockFraction) / 256.0;
        }
        void SetCarClock(double clock) {
            const double whole = std::floor(clock);
            carClockMs = static_cast<uint32_t>(whole);
            carClockFraction = static_cast<uint8_t>(std::clamp((clock - whole) * 256.0, 0.0, 255.0));
        }

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, walkerId);
            stream->Serialize(write, key);
            stream->Serialize(write, carId);
            stream->Serialize(write, carClockMs);
            stream->Serialize(write, carClockFraction);
            auto kindValue = static_cast<uint8_t>(kind);
            stream->Serialize(write, kindValue);
            kind = static_cast<ReactionKind>(kindValue);
            stream->Serialize(write, animation);
            stream->Serialize(write, queued);
            stream->Serialize(write, fatal);
            stream->Serialize(write, scream);
            for (glm::vec3 *vector : {&position, &facing, &impact}) {
                stream->Serialize(write, vector->x);
                stream->Serialize(write, vector->y);
                stream->Serialize(write, vector->z);
            }
        }
    };

    inline bool FiniteVector(const glm::vec3 &vector) {
        return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
    }

    // Whether a reaction is one retail could have played: its kind's clips,
    // a fall's collision delay and queued clip, a dodge's scream.
    inline bool ReactionWellFormed(const Reaction &reaction) {
        if (!FiniteVector(reaction.position) || !FiniteVector(reaction.facing) || !FiniteVector(reaction.impact) || !(glm::length(reaction.facing) > 0.5f)) {
            return false;
        }
        switch (reaction.kind) {
        case ReactionKind::Dodge:
            return IsDodge(reaction.animation) && reaction.queued == 0 && !reaction.fatal &&
                   (reaction.scream == -1 || (reaction.scream >= kDodgeScreamFirst && reaction.scream < kDodgeScreamFirst + kDodgeScreamCount));
        case ReactionKind::Sidestep:
            return IsDodge(reaction.animation) && reaction.queued == 0 && reaction.scream == -1 && !KnocksDown(reaction.impact) &&
                   glm::length(reaction.impact) > 0.0f;
        case ReactionKind::Knockdown: {
            if (!KnocksDown(reaction.impact) || glm::length(reaction.impact) > kMaxContactSpeed || reaction.scream != -1 ||
                KnockdownCollisionDelay(reaction.animation) == 0) {
                return false;
            }
            const bool front = KnockdownCollisionDelay(reaction.animation) == 2;
            if (front) {
                return reaction.queued == 0 && (reaction.animation == kFallFront) != reaction.fatal;
            }
            return reaction.fatal ? (reaction.queued == kFallEndA || reaction.queued == kFallEndB) : reaction.queued == kFallRecover;
        }
        }
        return false;
    }

    // A player: whether its copy of the car shows the moment of a reaction
    // (shownClock on the controller's clock, nothing for a car it does not
    // show), or it waited long enough for one that never gets there.
    inline constexpr double kReactionWaitLimitMs = 1000.0;
    inline bool ReactionDue(const double *shownClock, double reactionClock, double waitedMs) {
        return !shownClock || *shownClock >= reactionClock || waitedMs >= kReactionWaitLimitMs;
    }

    // A replica that played a reaction itself rejoins the host's poses once
    // the host's walker is past it; the gap closes over this long.
    inline constexpr float kHandOverSeconds = 0.25f;
    // How much of the gap is left `elapsed` seconds in (smoothstep).
    inline float HandOverRemaining(float elapsed) {
        const float t = std::clamp(elapsed / kHandOverSeconds, 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }
} // namespace Mafia1Online::Shared::Pedestrian
