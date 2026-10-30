// The server's jam watch over the simulation host's traffic
// (TrafficService::WatchJams): which cars count, and how they cluster.
#include "shared/features/traffic/traffic_jam.h"
#include "shared/features/traffic/traffic_report.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Mafia1Online::Shared::Traffic;

namespace {
    int failures = 0;

#define CHECK(condition)                                                                                                                                                                                                                                                               \
    do {                                                                                                                                                                                                                                                                               \
        if (!(condition)) {                                                                                                                                                                                                                                                            \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                                                                                                                                                \
            ++failures;                                                                                                                                                                                                                                                                \
        }                                                                                                                                                                                                                                                                              \
    } while (false)

    JamCar Stuck(uint64_t id, float x, float z, double seconds = 120.0) {
        JamCar car;
        car.id           = id;
        car.position     = {x, 0.0f, z};
        car.stuckSeconds = seconds;
        return car;
    }

    // A queue of count cars, 6 units apart along x from (x, z).
    std::vector<JamCar> Queue(uint64_t firstId, size_t count, float x, float z) {
        std::vector<JamCar> cars;
        for (size_t i = 0; i < count; ++i) {
            cars.push_back(Stuck(firstId + i, x + static_cast<float>(i) * 6.0f, z));
        }
        return cars;
    }

    void Eligibility() {
        const JamSettings settings;
        CHECK(JamEligible(Stuck(1, 0, 0), settings, JamScope::Automatic));

        // A player or a scripted NPC in a seat, or a player at its door,
        // keeps it from any reset.
        auto occupied         = Stuck(1, 0, 0);
        occupied.playerAboard = true;
        CHECK(!JamEligible(occupied, settings, JamScope::Automatic));
        CHECK(!JamEligible(occupied, settings, JamScope::Manual));
        auto scripted         = Stuck(1, 0, 0);
        scripted.scriptedCrew = true;
        CHECK(!JamEligible(scripted, settings, JamScope::Automatic));
        CHECK(!JamEligible(scripted, settings, JamScope::Manual));
        // A police or mafia crew: the jam watch leaves it, a script's reset
        // or a restart asks the host, which retires the crew with it.
        auto crewed    = Stuck(1, 0, 0);
        crewed.npcCrew = true;
        CHECK(!JamEligible(crewed, settings, JamScope::Automatic));
        CHECK(JamEligible(crewed, settings, JamScope::Manual));
        auto watched          = Stuck(1, 0, 0);
        watched.nearestPlayer = settings.playerClearance - 0.1f;
        CHECK(!JamEligible(watched, settings, JamScope::Manual));
        watched.nearestPlayer = settings.playerClearance;
        CHECK(JamEligible(watched, settings, JamScope::Automatic));

        // A road block stands still on purpose: no jam, but a reset takes
        // it once its case is over (the host judges that). A wreck goes on
        // its own timer.
        auto police   = Stuck(1, 0, 0);
        police.police = true;
        CHECK(!JamEligible(police, settings, JamScope::Automatic));
        CHECK(JamEligible(police, settings, JamScope::Manual));
        auto wreck   = Stuck(1, 0, 0);
        wreck.active = false;
        CHECK(!JamEligible(wreck, settings, JamScope::Manual));

        // Asked already: not asked twice.
        auto pending         = Stuck(1, 0, 0);
        pending.resetPending = true;
        CHECK(!JamEligible(pending, settings, JamScope::Automatic));
        CHECK(!JamEligible(pending, settings, JamScope::Manual));
        // Kept by the host before: the jam watch gives up on it, a reset asks
        // again (the case that kept it may be over).
        auto kept      = Stuck(1, 0, 0);
        kept.resetKept = true;
        CHECK(!JamEligible(kept, settings, JamScope::Automatic));
        CHECK(JamEligible(kept, settings, JamScope::Manual));

        // A parked car (no driver aboard) is no jam, but an area reset takes it.
        auto parked         = Stuck(1, 0, 0);
        parked.driverAboard = false;
        CHECK(!JamEligible(parked, settings, JamScope::Automatic));
        CHECK(JamEligible(parked, settings, JamScope::Manual));
    }

    void Stopping() {
        const JamSettings settings;
        StuckTrack track;
        CHECK(UpdateStuck(track, {0, 0, 0}, 0.0f, 10.0, settings) == 0.0);
        // Rocking in place and creeping within the drift keep the clock.
        CHECK(UpdateStuck(track, {0.5f, 0, 0}, 0.4f, 40.0, settings) == 30.0);
        CHECK(UpdateStuck(track, {kStuckDrift - 0.1f, 0, 0}, 0.9f, 70.0, settings) == 60.0);
        // Driving off starts over, by speed or by distance.
        CHECK(UpdateStuck(track, {kStuckDrift - 0.1f, 0, 0}, settings.maxSpeed + 0.5f, 75.0, settings) == 0.0);
        CHECK(UpdateStuck(track, {kStuckDrift - 0.1f, 0, 0}, 0.0f, 90.0, settings) == 15.0);
        CHECK(UpdateStuck(track, {10.0f, 0, 0}, 0.0f, 95.0, settings) == 0.0);
        // A report the host could not measure is no standstill.
        CHECK(UpdateStuck(track, {10.0f, 0, 0}, std::nanf(""), 100.0, settings) == 0.0);
    }

    void Clustering() {
        JamSettings settings;
        // Six cars in a 30 unit queue: one jam, centered, ids sorted.
        auto queue = Queue(10, 6, 0.0f, 0.0f);
        auto jams  = FindJams(queue, settings);
        CHECK(jams.size() == 1);
        CHECK(jams.size() == 1 && jams[0].cars.size() == 6 && jams[0].cars.front() == 10 && jams[0].cars.back() == 15);
        CHECK(jams.size() == 1 && std::fabs(jams[0].center.x - 15.0f) < 0.01f);

        // One short of minCars: nothing.
        CHECK(FindJams(Queue(10, 5, 0.0f, 0.0f), settings).empty());

        // Not stood long enough: a crossing's wait is no jam.
        auto waiting            = Queue(10, 8, 0.0f, 0.0f);
        waiting[3].stuckSeconds = settings.stuckSeconds - 1.0;
        waiting[4].stuckSeconds = settings.stuckSeconds - 1.0;
        waiting[5].stuckSeconds = settings.stuckSeconds - 1.0;
        CHECK(FindJams(waiting, settings).empty());

        // The jam's age is that of its most recently stopped car.
        auto aged            = Queue(10, 6, 0.0f, 0.0f);
        aged[2].stuckSeconds = 75.0;
        jams                 = FindJams(aged, settings);
        CHECK(jams.size() == 1 && jams[0].stuckSeconds == 75.0);

        // Excluded cars do not make up a jam's count.
        auto guarded        = Queue(10, 6, 0.0f, 0.0f);
        guarded[0].npcCrew = true;
        CHECK(FindJams(guarded, settings).empty());

        // Two jams apart, and a spread of cars that is none.
        auto two = Queue(10, 6, 0.0f, 0.0f);
        for (const auto &car : Queue(30, 7, 500.0f, 500.0f)) {
            two.push_back(car);
        }
        for (uint64_t i = 0; i < 10; ++i) {
            two.push_back(Stuck(100 + i, -1000.0f - static_cast<float>(i) * 100.0f, 0.0f));
        }
        jams = FindJams(two, settings);
        CHECK(jams.size() == 2);
        // Largest first; no car in two jams.
        CHECK(jams.size() == 2 && jams[0].cars.size() == 7 && jams[0].cars.front() == 30 && jams[1].cars.size() == 6 && jams[1].cars.front() == 10);

        // A long queue is cut into jams of at most what one radius holds.
        settings.radius  = 10.0f;
        settings.minCars = 2;
        jams             = FindJams(Queue(10, 6, 0.0f, 0.0f), settings);
        size_t covered   = 0;
        for (const auto &found : jams) {
            covered += found.cars.size();
            CHECK(found.cars.size() <= 3);
        }
        CHECK(covered == 6);

        // Off: nothing, however jammed.
        settings         = {};
        settings.enabled = false;
        CHECK(FindJams(Queue(10, 20, 0.0f, 0.0f), settings).empty());
    }

    // The server's Reset to the host: keys survive the trip, and no more than
    // the host can drive are read.
    void ResetWire() {
        Reset sent;
        sent.missionGeneration = 7;
        sent.keys              = {3, 9, 400000};
        MafiaNet::BitStream stream;
        sent.Serialize(&stream, true);
        Reset read;
        read.Serialize(&stream, false);
        CHECK(read.missionGeneration == 7 && !read.all && read.keys == sent.keys);

        Reset all;
        all.all = true;
        all.keys.assign(kMaxHostCars + 10, 1);
        MafiaNet::BitStream bounded;
        all.Serialize(&bounded, true);
        Reset readAll;
        readAll.Serialize(&bounded, false);
        CHECK(readAll.all && readAll.keys.size() == kMaxHostCars);
    }

    void Settings() {
        CHECK(ValidJamSettings({}));
        JamSettings settings;
        settings.minCars = 1;
        CHECK(!ValidJamSettings(settings));
        settings        = {};
        settings.radius = std::nanf("");
        CHECK(!ValidJamSettings(settings));
        settings              = {};
        settings.stuckSeconds = 0.0f;
        CHECK(!ValidJamSettings(settings));
        settings                 = {};
        settings.cooldownSeconds = 0.0f;
        settings.playerClearance = 0.0f;
        CHECK(ValidJamSettings(settings));
    }
} // namespace

int main() {
    Eligibility();
    Stopping();
    Clustering();
    Settings();
    ResetWire();
    if (failures == 0) {
        std::printf("traffic jam tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
