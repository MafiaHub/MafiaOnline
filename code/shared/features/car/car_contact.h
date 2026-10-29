#pragma once

#include <networking/rpc/rpc.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
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
    // Hits of one pair closer together than this are one contact: only a
    // stronger one than any before it is reported.
    inline constexpr double kContactEpisodeGapMs = 500.0;
    // A controller that saw the contact itself within this of when the
    // reporter did (ms, estimated through both round trips) has applied it.
    inline constexpr double kContactWindowMs = 150.0;
    // How long a report waits for that before it is dropped as stale.
    inline constexpr double kContactExpiryMs = 2000.0;
    // Server bounds: the reporting car's and the hit car's poses within this
    // of the report (the reporter sees the hit car a transit late).
    inline constexpr int64_t kContactPoseBeforeMs = 500;
    inline constexpr int64_t kContactPoseAfterMs  = 150;
    // The fewest ms between two accepted reports of one pair.
    inline constexpr int64_t kContactReportGapMs = 20;

    // The velocity change DoCollision gives a car of `mass` for `impulse`.
    inline float ContactDeltaV(float impulse, float mass) {
        return mass > 0.0f ? kContactForce * impulse / mass : 0.0f;
    }

    // `impulse` scaled down so that its velocity change on a car of `mass`
    // stays within `maxDeltaV`, the server's bound from the two cars' speeds:
    // no report can fling a car faster than they closed.
    inline glm::vec3 ClampContactImpulse(const glm::vec3 &impulse, float mass, float maxDeltaV) {
        const float deltaV = ContactDeltaV(glm::length(impulse), mass);
        if (!(deltaV > maxDeltaV) || !(deltaV > 0.0f)) {
            return impulse;
        }
        return impulse * (std::max(maxDeltaV, 0.0f) / deltaV);
    }

    // The most a hit may change the hit car's velocity, given the fastest the
    // two cars moved around the report: an inelastic hit changes it by at most
    // the closing speed times the striker's share of the mass, an elastic one
    // twice that; DoCollision's force and any spin take the rest.
    inline float MaxContactDeltaV(float strikerSpeed, float hitSpeed) {
        return 1.5f * (strikerSpeed + hitSpeed) + 1.0f;
    }

    // Reporter: whether a hit of `impulse` at `now` is sent, given the last
    // hit of the pair at `lastAt` and the strongest reported since `peak`
    // (both updated). Pushing keeps hitting at about the same strength.
    inline bool ContactReportDue(double now, float impulse, double &lastAt, float &peak) {
        if (now - lastAt > kContactEpisodeGapMs) {
            peak = 0.0f;
        }
        lastAt = now;
        if (!(impulse > peak)) {
            return false;
        }
        peak = impulse;
        return true;
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
