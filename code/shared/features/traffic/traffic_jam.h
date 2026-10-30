#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Mafia1Online::Shared::Traffic {
    // Server: jams of the simulation host's traffic. Players can block a road
    // (a parked car, pushed props), and retail traffic waits behind it for
    // good: the host never despawns a car its driver holds at a blocked path,
    // so a jam far from everyone also stays and fills the host's car budget.
    // A jam is minCars traffic cars within radius of one of them, each of
    // which has kept within kStuckDrift of one spot below maxSpeed for
    // stuckSeconds. The server asks the host to take them off the road, which
    // it does through retail's own pool deactivation, and the host refills
    // the road out of every player's sight.
    struct JamSettings {
        bool enabled       = true;
        uint32_t minCars   = 6;
        float radius       = 35.0f;
        float maxSpeed     = 1.0f;
        float stuckSeconds = 60.0f;
        // No second reset within radius of the last one for this long, so
        // the refilled road gets a chance first.
        float cooldownSeconds = 30.0f;
        // A car with a player on foot this close stays: they may be at its
        // door or pulling its driver out (Shared::Traffic::kStealReach).
        float playerClearance = 8.0f;
    };

    // A stopped car still rocks and creeps; it moves on once it leaves this
    // far from where it stopped.
    inline constexpr float kStuckDrift = 2.0f;
    // Bounds the scripts and server.json may set.
    inline constexpr uint32_t kMaxJamCars = 64;
    inline constexpr float kMaxJamRadius  = 200.0f;
    inline constexpr float kMaxJamSeconds = 3600.0f;

    inline bool ValidJamSettings(const JamSettings &settings) {
        const auto within = [](float value, float low, float high) {
            return std::isfinite(value) && value >= low && value <= high;
        };
        const bool cars  = settings.minCars >= 2 && settings.minCars <= kMaxJamCars;
        const bool space = within(settings.radius, 1.0f, kMaxJamRadius) && within(settings.playerClearance, 0.0f, kMaxJamRadius);
        const bool hold  = within(settings.stuckSeconds, 1.0f, kMaxJamSeconds) && within(settings.cooldownSeconds, 0.0f, kMaxJamSeconds);
        return cars && space && hold && within(settings.maxSpeed, 0.0f, 20.0f);
    }

    // Where a car stopped and since when (seconds on any monotonic clock).
    struct StuckTrack {
        glm::vec3 anchor {0.0f};
        double since = 0.0;
        bool valid   = false;
    };

    // How long the car has stood: a car faster than maxSpeed, or farther
    // than kStuckDrift from where it stopped, starts over from here.
    inline double UpdateStuck(StuckTrack &track, const glm::vec3 &position, float speed, double now, const JamSettings &settings) {
        if (!track.valid || !(speed <= settings.maxSpeed) || glm::distance(track.anchor, position) > kStuckDrift) {
            track.anchor = position;
            track.since  = now;
            track.valid  = true;
        }
        return now - track.since;
    }

    // What the server knows of one car the host drives.
    struct JamCar {
        uint64_t id = 0;
        glm::vec3 position {0.0f};
        double stuckSeconds = 0.0;
        // Not exploded, drowned or fallen.
        bool active = true;
        // A police car: road blocks stand still on purpose.
        bool police = false;
        // A player sits in it.
        bool playerAboard = false;
        // A host NPC sits in it: a police or mafia crew.
        bool npcCrew = false;
        // A scripted NPC sits in it; scripts own those.
        bool scriptedCrew = false;
        // Its traffic driver sits in it, alive or shot: not a parked car.
        bool driverAboard = true;
        // The host was asked to take it off the road a moment ago.
        bool resetPending = false;
        // The host was asked before and kept it (a police case or pursuit
        // ran through it then, or it was not the pool's to take).
        bool resetKept = false;
        // Distance to the nearest player on foot.
        float nearestPlayer = 1.0e9f;
    };

    enum class JamScope : uint8_t {
        // The server's own jam watch: the ordinary traffic stuck behind
        // something.
        Automatic,
        // A script's area reset or a traffic restart: also parked cars, and
        // police and mafia cars with their crews, which the host retires
        // only once no police case or pursuit runs through them
        // (client crew_car_retire.h). A car it kept is asked again.
        Manual,
    };

    // Cars the server may ask the host to take off the road. A player or a
    // scripted NPC in a seat, or a player at it, always keeps it.
    inline bool JamEligible(const JamCar &car, const JamSettings &settings, JamScope scope) {
        if (!car.active || car.playerAboard || car.scriptedCrew || car.resetPending || car.nearestPlayer < settings.playerClearance) {
            return false;
        }
        if (scope == JamScope::Manual) {
            return true;
        }
        return !car.police && !car.npcCrew && !car.resetKept && car.driverAboard;
    }

    struct Jam {
        std::vector<uint64_t> cars;
        glm::vec3 center {0.0f};
        // How long the most recently stopped of them has stood.
        double stuckSeconds = 0.0;
    };

    // Jams among the cars, largest first; no car is in two. Deterministic for
    // the same input: ties go to the lower id.
    inline std::vector<Jam> FindJams(const std::vector<JamCar> &cars, const JamSettings &settings) {
        std::vector<Jam> jams;
        if (!settings.enabled) {
            return jams;
        }
        std::vector<const JamCar *> stuck;
        for (const auto &car : cars) {
            if (JamEligible(car, settings, JamScope::Automatic) && car.stuckSeconds >= settings.stuckSeconds) {
                stuck.push_back(&car);
            }
        }
        const float radiusSq = settings.radius * settings.radius;
        const auto together  = [&](const JamCar &a, const JamCar &b) {
            const glm::vec3 offset = a.position - b.position;
            return glm::dot(offset, offset) <= radiusSq;
        };
        while (stuck.size() >= settings.minCars) {
            const JamCar *seed = nullptr;
            size_t best        = 0;
            for (const auto *candidate : stuck) {
                const auto count = static_cast<size_t>(std::count_if(stuck.begin(), stuck.end(), [&](const JamCar *other) {
                    return together(*candidate, *other);
                }));
                if (count > best || (count == best && seed && candidate->id < seed->id)) {
                    best = count;
                    seed = candidate;
                }
            }
            if (!seed || best < settings.minCars) {
                break;
            }
            Jam jam;
            jam.stuckSeconds = seed->stuckSeconds;
            std::vector<const JamCar *> rest;
            for (const auto *car : stuck) {
                if (together(*seed, *car)) {
                    jam.cars.push_back(car->id);
                    jam.center += car->position;
                    jam.stuckSeconds = std::min(jam.stuckSeconds, car->stuckSeconds);
                }
                else {
                    rest.push_back(car);
                }
            }
            jam.center /= static_cast<float>(jam.cars.size());
            std::sort(jam.cars.begin(), jam.cars.end());
            jams.push_back(std::move(jam));
            stuck = std::move(rest);
        }
        return jams;
    }
} // namespace Mafia1Online::Shared::Traffic
