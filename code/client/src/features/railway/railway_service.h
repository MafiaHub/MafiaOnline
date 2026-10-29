#pragma once

#include "shared/features/railway/railway_report.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include <limits>
#include <optional>

namespace Mafia1Online::SDK::Rail {
    struct NativeRailway;
}
namespace Mafia1Online::SDK::Seat {
    struct NativeHuman;
}
namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}
namespace Mafia1Online::SDK::Player {
    struct NativeActor;
    struct NativeHuman;
    struct Vector3;
} // namespace Mafia1Online::SDK::Player
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Railway {
    // Trams and metro trains. Every client loads the same scene2.bin railways
    // in the same order (GameInit). The simulation host runs them natively,
    // everywhere on the map, and reports them all; players make theirs
    // network controlled, so C_actor::Tick runs no AI on them, and place
    // them from those reports, which C_railway::Update draws and collides.
    class RailwayService final {
      public:
        bool Install();
        void Uninstall();
        void RegisterRPC();
        void Update(World::WorldService &world);
        // The mission's railways are going (mission close, disconnect).
        void Reset();

        void OnGameInit(SDK::Rail::NativeRailway *railway);
        void OnGameDone(SDK::Rail::NativeRailway *railway);
        // Players: places the railway from the host's reports just before
        // its C_railway::Update draws them, so its frames, its passengers
        // and its riders all stand on the same placement this frame.
        void BeforeUpdate(SDK::Rail::NativeRailway *railway, int frameMs);
        // Simulation host: one of its railways ran into a player's replica
        // (C_human::Collision, railway branch, into the Hit hook).
        void OnHitPlayer(World::WorldService &world, SDK::Player::NativeHuman &target, SDK::Player::NativeActor *railway, const SDK::Player::Vector3 &direction, float damage);

        // Where a human aboard a railway stands, in the basis of the railway
        // part (body 0 or wagon) nearest to it; see Shared::Railway.
        struct Ride {
            uint8_t index = Shared::Railway::kNoRailway;
            uint8_t part = 0;
            glm::vec3 local {0.0f};
            float yaw = 0.0f;
        };
        // The local player's ride, when it is aboard one of the mission's
        // railways (C_human +0x98): the frame Intern_UseRailway linked it to
        // (C_human +0x584) and the pose in that frame's full matrix.
        [[nodiscard]] std::optional<Ride> RideOf(void *human) const;
        // A host passenger's world pose (Ride in the nearest part's yaw
        // basis) on this client's copy of the railway; false when this client
        // does not have that railway placed.
        [[nodiscard]] bool PlaceRide(const Ride &ride, glm::vec3 &position, glm::vec3 &direction) const;
        // A remote player's world pose from a RideOf ride, through the
        // linked frame's world matrix on this client's copy.
        [[nodiscard]] bool PlaceRider(const Ride &ride, glm::vec3 &position, glm::vec3 &direction) const;
        // Players: the local player's bullet hit a part of this railway on
        // this screen (C_railway::Hit), after the native trace; reported in
        // that part's yaw frame unless a rider's replica took the pellet.
        void OnLocalShotHit(SDK::Rail::NativeRailway *railway, const SDK::Player::Vector3 &direction, const SDK::Player::Vector3 &point, SDK::Scene::NativeFrame *frame);
        // Simulation host: the attacker is a player's replica, whose bullets
        // reach its railways only as that player's ShotHit.
        [[nodiscard]] bool IsPlayerShot(SDK::Player::NativeActor *attacker) const;
        // A human natively aboard a railway goes back to world space first
        // (Intern_UseRailway(false)): its frame is the wagon's child and its
        // collisions sit in the wagon's list, so neither may outlive the other.
        void DetachRider(void *human);

      private:
        struct Sample {
            uint32_t hostTimeMs = 0;
            Shared::Railway::State state;
        };
        // Players: a model standing in for one of the host's passengers,
        // which this client's railway (no AI) never moves, animates or lets
        // off. It eases from the previous report to the newest one.
        struct Puppet {
            SDK::Scene::NativeFrame *model = nullptr;
            std::string modelName;
            uint8_t animation = 0xff;
            bool animating = false;
            int animatedMs = 0;
            Ride from;
            Ride to;
            std::chrono::steady_clock::time_point reportedAt {};
            // Where it last took its scene sector (SetFrameSectorPos).
            glm::vec3 sectorPosition {std::numeric_limits<float>::max()};
        };
        struct Entry {
            SDK::Rail::NativeRailway *railway = nullptr;
            uint32_t nameHash = 0;
            bool prepared = false;
            std::deque<Sample> samples;
            std::unordered_map<uint16_t, Puppet> puppets;
            // Players: when the host last listed this railway's passengers.
            std::chrono::steady_clock::time_point lastPassengers {};
            // Players: when the newest report arrived, for PlaceRide.
            std::chrono::steady_clock::time_point lastReport {};
            // Players: how far behind the host this railway is drawn; far
            // railways are reported less often and need a longer one.
            double renderDelayMs = 0.0;
            // Players: the doorway use objects this client registered.
            enum class Entries : uint8_t { None, Outer, Inner } entries = Entries::None;
            // Host: since when a hit driver has stopped it (state 11).
            std::chrono::steady_clock::time_point driverHitSince {};
            // Host: since when players' steps have held it at this stop.
            std::chrono::steady_clock::time_point holdSince {};
        };
        // Host: a player's step aboard holding a railway (Lock).
        struct BoardLock {
            uint64_t playerId = 0;
            uint8_t index = 0;
            std::chrono::steady_clock::time_point until {};
        };
        // Players: the local player's step aboard or off (UseAction),
        // waiting for, or past, the host.
        struct PendingBoard {
            uint8_t index = Shared::Railway::kNoRailway;
            int action = 0;
            int seat = 0;
            int animationSpeedState = 0;
            bool approved = false;
            std::chrono::steady_clock::time_point until {};
        };

        // This client has railway `index` placed from recent reports (or hosts it).
        [[nodiscard]] bool Placed(uint8_t index) const;
        void Prepare(Entry &entry);
        void SendSnapshot(World::WorldService &world);
        void SendPassengers(World::WorldService &world);
        // Host: a player's checked ShotHit, replayed as C_railway::Hit at the
        // same place in this copy of the part.
        void ApplyShotHit(const Shared::Railway::ShotHit &shot);
        void ApplyPassengers(const Shared::Railway::Passengers &passengers);
        void UpdatePuppets(Entry &entry, int frameMs);
        void ShowPuppet(Puppet &puppet, const Shared::Railway::Passenger &passenger);
        static void ReleasePuppet(Puppet &puppet);
        static void ReleasePuppets(Entry &entry);
        void Place(Entry &entry, double hostNow, int frameMs);
        void UpdateEntries(Entry &entry, void *localHuman);
        void UpdateBoarding(World::WorldService &world);
        void ReleaseLocks(uint64_t playerId);
        void FinishBoarding(World::WorldService &world);
        static bool OnUse(SDK::Seat::NativeHuman *human, SDK::Player::NativeActor *railway, int action, int seat, int animationSpeedState);
        [[nodiscard]] int IndexOf(const void *railway) const;
        // The human is within kBoardReach of the body or a wagon.
        [[nodiscard]] static bool WithinReach(SDK::Rail::NativeRailway &railway, void *human);
        [[nodiscard]] static double LocalMs();

        std::vector<Entry> _railways;
        uint64_t _missionGeneration = 0;
        std::chrono::steady_clock::time_point _lastSnapshot {};
        std::chrono::steady_clock::time_point _lastPassengers {};
        // Host: an id per passenger, kept while the same model rides.
        struct PassengerId {
            const void *model = nullptr;
            uint16_t id = 0;
            // Reports sent so far, for the model name's first ones.
            uint8_t reports = 0;
        };
        std::unordered_map<const void *, PassengerId> _passengerIds;
        uint16_t _nextPassengerId = 0;
        uint32_t _passengerReports = 0;
        // Players: the last puppet tick.
        std::chrono::steady_clock::time_point _lastPuppetTick {};
        std::chrono::steady_clock::time_point _lastSummary {};
        // Players: local clock minus host clock, slewed toward the least
        // over recent packets (the least delayed one), to date the samples
        // here.
        std::deque<double> _clockOffsets;
        double _clockOffset = 0.0;
        bool _hasClockOffset = false;
        // Players: the reporting host's session (Simulation::HostSession).
        uint32_t _hostSession = 0;
        // Host: when each player was last reported hit, per railway contact.
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _lastHits;
        std::vector<BoardLock> _locks;
        PendingBoard _board;
        World::WorldService *_world = nullptr;
        bool _patched = false;
        bool _reshowPlayer = false;
        bool _reportedPlacement = false;
        bool _reportedMismatch = false;
    };
} // namespace Mafia1Online::Features::Railway
