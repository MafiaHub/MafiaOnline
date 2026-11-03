#pragma once

#include "shared/features/railway/railway_report.h"

#include <mafianet/types.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <utility>

namespace Mafia1Online::Features::Player {
    // Aboard a railway: its index (Shared::Railway::kNoRailway for none),
    // the frame the rider is linked to (0 body, n wagon n - 1) and the
    // position and yaw in that frame (RailwayService::PlaceRider).
    struct RemoteRide {
        uint8_t railway = Shared::Railway::kNoRailway;
        uint8_t part    = 0;
        glm::vec3 local {0.0f};
        float yaw = 0.0f;
    };

    // Boarding and stepping off blend the placement on the receiver's
    // railway copy with the streamed world track over this long, instead of
    // popping between the two (the copy runs its own render delay).
    inline constexpr double kRideBlendMs = 150.0;

    // The ride at `time` in `rides` (each pose's ride by its time, oldest
    // first) and how far the body is aboard it: 1 riding, 0 on the world
    // track, between while it boards or steps off. False when it is not
    // near a ride at all. Offsets on one part interpolate, as the pose does.
    inline bool RideAt(const std::deque<std::pair<MafiaNet::Time, RemoteRide>> &rides, double time, RemoteRide &ride, float &weight) {
        if (rides.empty()) {
            return false;
        }
        const auto riding = [](const RemoteRide &sample) {
            return sample.railway != Shared::Railway::kNoRailway;
        };
        // The newest sample at or before the render time.
        size_t at = 0;
        while (at + 1 < rides.size() && static_cast<double>(rides[at + 1].first) <= time) {
            ++at;
        }
        if (riding(rides[at].second)) {
            ride = rides[at].second;
            if (at + 1 < rides.size() && rides[at + 1].second.railway == ride.railway && rides[at + 1].second.part == ride.part) {
                const double span = static_cast<double>(rides[at + 1].first - rides[at].first);
                const float u     = span > 0.0 ? static_cast<float>(std::clamp((time - static_cast<double>(rides[at].first)) / span, 0.0, 1.0)) : 0.0f;
                const auto &next  = rides[at + 1].second;
                ride.local        = glm::mix(ride.local, next.local, u);
                float turn        = std::fmod(next.yaw - ride.yaw, glm::two_pi<float>());
                turn              = turn > glm::pi<float>() ? turn - glm::two_pi<float>() : (turn < -glm::pi<float>() ? turn + glm::two_pi<float>() : turn);
                ride.yaw += turn * u;
            }
            size_t boarded = at;
            while (boarded > 0 && rides[boarded - 1].second.railway == ride.railway) {
                --boarded;
            }
            // Aboard since before this replica's history began: no blend.
            weight = boarded == 0 ? 1.0f : static_cast<float>(std::clamp((time - static_cast<double>(rides[boarded].first)) / kRideBlendMs, 0.0, 1.0));
            return true;
        }
        size_t last = at;
        while (last > 0 && !riding(rides[last].second)) {
            --last;
        }
        if (!riding(rides[last].second) || last + 1 >= rides.size()) {
            return false;
        }
        ride   = rides[last].second;
        weight = 1.0f - static_cast<float>(std::clamp((time - static_cast<double>(rides[last + 1].first)) / kRideBlendMs, 0.0, 1.0));
        return weight > 0.0f;
    }
} // namespace Mafia1Online::Features::Player
