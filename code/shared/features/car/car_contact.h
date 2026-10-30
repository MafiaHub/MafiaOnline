#pragma once

#include <networking/rpc/rpc.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vector_relational.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>

namespace Mafia1Online::Shared::Car {
    // One car's body sweep hitting another hands the hit car C_car::Collision
    // (0x425040) with the impulse it took itself; for a car that is
    // C_Vehicle::DoCollision (0x4cbd70) at this force: the impulse times it
    // over the hit car's mass, Deform and DamageVehicle at the contact.
    inline constexpr float kContactForce = 1.2f;
    // A hit changing the hit car's velocity by less than this (m/s) is a
    // push, not worth a report: it shows nothing its controller's poses
    // would not.
    inline constexpr float kContactMinDeltaV = 0.5f;
    // Hits of one pair closer together than this are one contact, reported
    // once, at its first hit: the closing speed and with it the impulse are
    // largest then, and pushing keeps hitting at about the same strength.
    inline constexpr double kContactEpisodeGapMs = 500.0;
    // A controller that saw the contact itself within this of when the
    // reporter did (ms, estimated through both round trips) has applied it.
    inline constexpr double kContactWindowMs = 150.0;
    // How long a report waits for that before it is dropped as stale.
    inline constexpr double kContactExpiryMs = 2000.0;
    // Server: the fewest ms between two accepted reports of one pair, the
    // reporter's episode gap less the network's jitter.
    inline constexpr double kContactReportGapMs = kContactEpisodeGapMs - 100.0;
    // Server: the hit car's poses are dated at most this far before the
    // report (the reporter shows it a transit ahead of its own).
    inline constexpr double kContactPoseLagMs = 500.0;
    // Server: a moving car's newest pose stands for it this long (movement
    // goes out every 33 ms); a car at rest reports once a second.
    inline constexpr double kContactPoseHoldMs = 150.0;
    inline constexpr double kContactRestHoldMs = 1500.0;
    // Server: a car's speed is its displacement over this long.
    inline constexpr double kContactSpeedBaselineMs = 250.0;
    // Server: a stock car's body about its frame, in its frame (right, up,
    // forward; m). Server car models carry no collision bounds, so this
    // takes the largest stock vehicles, trucks included.
    inline constexpr glm::vec3 kContactBodyMin {-1.5f, -1.5f, -4.5f};
    inline constexpr glm::vec3 kContactBodyMax {1.5f, 2.8f, 4.5f};
    // C_Vehicle's body sweep reports the point on the striker's sweep
    // sphere: that far outside its body at most.
    inline constexpr float kContactSweepRadius = 0.5f;
    // Server: the reporter's copy of the hit car is predicted to the
    // present, so it may be off by this plus its motion over the timing
    // error; the striker's newest pose may be that much behind its report.
    inline constexpr float kContactPoseSlack    = 0.5f;
    inline constexpr float kContactTimingSlackS = 0.1f;

    // The velocity change DoCollision gives a car of `mass` for `impulse`.
    inline float ContactDeltaV(float impulse, float mass) {
        return mass > 0.0f ? kContactForce * impulse / mass : 0.0f;
    }

    // `impulse` scaled down so that its velocity change on a car of `mass`
    // stays within `maxDeltaV`, the server's bound from the cars' closing
    // speed: no report can fling a car faster than they closed.
    inline glm::vec3 ClampContactImpulse(const glm::vec3 &impulse, float mass, float maxDeltaV) {
        const float deltaV = ContactDeltaV(glm::length(impulse), mass);
        if (!(deltaV > maxDeltaV) || !(deltaV > 0.0f)) {
            return impulse;
        }
        return impulse * (std::max(maxDeltaV, 0.0f) / deltaV);
    }

    // The most a hit may change the hit car's velocity, given the speed at
    // which the cars closed along the contact normal. BodyCollision's
    // impulse is that closing speed over both inverse masses (inelastic), so
    // DoCollision's 1.2 times it changes the hit car's velocity by at most
    // 1.2 times the closing speed; a quarter more takes the spin at the
    // contact and the averaged speeds, 1 m/s a touch between cars at rest.
    inline float MaxContactDeltaV(float closingSpeed) {
        return 1.25f * kContactForce * closingSpeed + 1.0f;
    }

    // Reporter: whether a hit at `now` opens a contact of its pair, the last
    // hit of which was at `lastAt` (updated).
    inline bool ContactReportDue(double now, double &lastAt) {
        const bool due = now - lastAt > kContactEpisodeGapMs;
        lastAt         = now;
        return due;
    }

    // Server: whether a report of a pair at `now` opens another contact, the
    // last accepted of the pair at `lastAt` (updated if so).
    inline bool ContactReportAccepted(double now, double &lastAt) {
        if (now - lastAt < kContactReportGapMs) {
            return false;
        }
        lastAt = now;
        return true;
    }

    // Server: one accepted pose of a car, at the server's clock (ms).
    struct ContactPose {
        double atMs = 0.0;
        glm::vec3 position {0.0f};
        glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
    };

    // A car's pose at `atMs`, from its poses in time order: between the two
    // around it, or the newest while it stands for the car (longer for a car
    // at rest, the newest two in one place); nothing before the oldest.
    inline std::optional<ContactPose> ContactPoseAt(std::span<const ContactPose> poses, double atMs) {
        if (poses.empty() || atMs < poses.front().atMs) {
            return std::nullopt;
        }
        const auto after = std::upper_bound(poses.begin(), poses.end(), atMs, [](double at, const ContactPose &pose) { return at < pose.atMs; });
        if (after == poses.end()) {
            const auto &newest = poses.back();
            const bool atRest  = poses.size() == 1 || glm::distance(newest.position, poses[poses.size() - 2].position) < 0.05f;
            if (atMs - newest.atMs > (atRest ? kContactRestHoldMs : kContactPoseHoldMs)) {
                return std::nullopt;
            }
            return newest;
        }
        const auto &before = *(after - 1);
        const double span  = after->atMs - before.atMs;
        const float t      = span > 0.0 ? static_cast<float>((atMs - before.atMs) / span) : 1.0f;
        return ContactPose {atMs, glm::mix(before.position, after->position, t), glm::slerp(before.rotation, after->rotation, t)};
    }

    // A car's velocity over the kContactSpeedBaselineMs before `atMs`, from
    // where its poses put it: what its controller says of its speed does not
    // count.
    inline std::optional<glm::vec3> ContactVelocityAt(std::span<const ContactPose> poses, double atMs) {
        const auto now    = ContactPoseAt(poses, atMs);
        const auto before = ContactPoseAt(poses, atMs - kContactSpeedBaselineMs);
        if (!now || !before) {
            return std::nullopt;
        }
        return (now->position - before->position) / static_cast<float>(kContactSpeedBaselineMs / 1000.0);
    }

    // Whether the frame-local point `local` lies within `margin` of a stock
    // car's body.
    inline bool OnContactBody(const glm::vec3 &local, float margin) {
        return glm::all(glm::greaterThanEqual(local, kContactBodyMin - margin)) && glm::all(glm::lessThanEqual(local, kContactBodyMax + margin));
    }

    // Controller: whether one of its own contacts with the striker, at the
    // local clock times `seen`, is the reported one estimated at `contactAt`.
    inline bool ContactSeenLocally(std::span<const double> seen, double contactAt) {
        return std::any_of(seen.begin(), seen.end(), [&](double at) { return std::abs(at - contactAt) <= kContactWindowMs; });
    }

    // Client -> server: a car this client simulates (striker) hit a car
    // another client simulates. The contact is in the hit car's frame, as the
    // controller replays it on its own copy; the world contact point lets
    // the server check it against both cars' poses.
    struct ContactReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarContactReport");

        uint64_t carId             = 0;
        uint64_t strikerId         = 0;
        uint64_t missionGeneration = 0;
        glm::vec3 position {0.0f};
        glm::vec3 localPosition {0.0f};
        glm::vec3 localImpulse {0.0f};
        glm::vec3 localNormal {0.0f};
        // C_car::Collision's mass argument, the striker's.
        float mass = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, carId);
            stream->Serialize(write, strikerId);
            stream->Serialize(write, missionGeneration);
            for (glm::vec3 *vector : {&position, &localPosition, &localImpulse, &localNormal}) {
                stream->Serialize(write, vector->x);
                stream->Serialize(write, vector->y);
                stream->Serialize(write, vector->z);
            }
            stream->Serialize(write, mass);
        }
    };

    // Server: the bound on a reported contact's velocity change
    // (MaxContactDeltaV), or nothing if the two cars' bodies did not meet
    // there. `car` and `striker` are each car's accepted poses, in time
    // order; the striker's are the reporter's own, so they had the contact
    // as the report arrived (`strikerAtMs`), the hit car's when its
    // controller had it (`carAtMs`, the reporter shows it a transit ahead).
    inline std::optional<float> CheckContact(const ContactReport &report, std::span<const ContactPose> car, double carAtMs,
                                             std::span<const ContactPose> striker, double strikerAtMs) {
        const auto carPose         = ContactPoseAt(car, carAtMs);
        const auto strikerPose     = ContactPoseAt(striker, strikerAtMs);
        const auto carVelocity     = ContactVelocityAt(car, carAtMs);
        const auto strikerVelocity = ContactVelocityAt(striker, strikerAtMs);
        const float impulse        = glm::length(report.localImpulse);
        const float normalLength   = glm::length(report.localNormal);
        // BodyCollision's impulse is the contact normal scaled.
        if (!carPose || !strikerPose || !carVelocity || !strikerVelocity || !(impulse > 0.0f) || !(normalLength > 0.0f) ||
            std::abs(glm::dot(report.localImpulse / impulse, report.localNormal / normalLength)) < 0.95f ||
            !OnContactBody(report.localPosition, kContactSweepRadius)) {
            return std::nullopt;
        }
        // Where the reporter put the point on the hit car is where the hit
        // car was, and on the striker's body.
        const glm::vec3 onCar = carPose->position + carPose->rotation * report.localPosition;
        if (glm::distance(onCar, report.position) > kContactPoseSlack + glm::length(*carVelocity) * kContactTimingSlackS ||
            !OnContactBody(glm::inverse(strikerPose->rotation) * (report.position - strikerPose->position),
                           kContactSweepRadius + glm::length(*strikerVelocity) * kContactTimingSlackS)) {
            return std::nullopt;
        }
        // Either sign: the speeds are averaged over the baseline.
        const glm::vec3 normal = carPose->rotation * (report.localNormal / normalLength);
        return MaxContactDeltaV(std::abs(glm::dot(*strikerVelocity - *carVelocity, normal)));
    }

    // Server -> the hit car's controller: an accepted report, to apply once
    // unless its own physics had the contact.
    struct ContactImpact {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarContactImpact");

        uint64_t carId             = 0;
        uint64_t strikerId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t controllerGuid    = 0;
        uint64_t serverSequence    = 0;
        glm::vec3 localPosition {0.0f};
        glm::vec3 localImpulse {0.0f};
        glm::vec3 localNormal {0.0f};
        float mass = 0.0f;
        // ClampContactImpulse's bound.
        float maxDeltaV = 0.0f;
        // About how long ago the reporter had the contact: half its round
        // trip to the server and half the controller's, in ms.
        uint16_t ageMs = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, carId);
            stream->Serialize(write, strikerId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, controllerGuid);
            stream->Serialize(write, serverSequence);
            for (glm::vec3 *vector : {&localPosition, &localImpulse, &localNormal}) {
                stream->Serialize(write, vector->x);
                stream->Serialize(write, vector->y);
                stream->Serialize(write, vector->z);
            }
            stream->Serialize(write, mass);
            stream->Serialize(write, maxDeltaV);
            stream->Serialize(write, ageMs);
        }
    };
} // namespace Mafia1Online::Shared::Car
