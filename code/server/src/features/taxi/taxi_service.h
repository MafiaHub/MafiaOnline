#pragma once

#include <mafianet/types.h>

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::Features::Simulation {
    class SimulationService;
}
namespace Mafia1Online::Features::Player {
    class PlayerService;
}
namespace Mafia1Online::Features::Car {
    class CarService;
}
namespace Mafia1Online::Features::Pedestrian {
    class PedestrianService;
}
namespace Mafia1Online::Features::Npc {
    class NpcService;
}

namespace Mafia1Online::Features::Taxi {
    // Taxi passengers, the retail way. The simulation host runs retail's
    // hailing for the players scripts allow (SetHailable): walkers wave at
    // their taxis, and a driver who stops by one gets a Pickup. A script
    // then boards the walker (Board): it becomes a scripted NPC where it
    // stands and gets into the back of the player's car, rides along on
    // every client and gets out again when the script drops it off, when
    // the driver leaves the cab or when the cab stops at the destination the
    // script set and the script says so. This follows retail's
    // system\taxi_pas.dat: wait for the cab to stop within 15, board the
    // rear seat (seat 3 of a car with more than three, else seat 1), ride
    // while the player stays the owner, and at the destination (stopped
    // within 15) pay and get out; the walker then goes back to the streets.
    class TaxiService final {
      public:
        // taxi_pas.dat's stop: car_getspeed at most 0.5 within 15 of the
        // destination frame.
        static constexpr float kArriveSpeed = 0.5f;
        static constexpr float kArriveRadius = 15.0f;
        // A passenger that has not sat down by then gives up.
        static constexpr std::chrono::seconds kBoardTimeout {45};
        // One still seated this long after the exit order is removed.
        static constexpr std::chrono::seconds kExitTimeout {15};
        // A passenger who got out walks this far from the car, then goes
        // once no player is within kReleaseRange or after kReleaseLifetime.
        static constexpr float kWalkAway = 6.0f;
        static constexpr float kReleaseRange = 90.0f;
        static constexpr std::chrono::seconds kReleaseLifetime {60};

        enum class ExitReason : uint8_t {
            // Taxi.dropOff.
            DropOff,
            // The driver left the driver's seat (taxi_pas.dat's owner check).
            DriverLeft,
            // It never sat down: no way to the door, the car drove off.
            Failed,
            Died,
            // Removed, or the mission or host went.
            Gone,
        };
        static const char *ExitReasonName(ExitReason reason);

        struct Events {
            std::function<void(uint64_t playerId, uint64_t pedestrianId, uint64_t carId)> hail;
            // reason: "boarded", "ended" or "gone".
            std::function<void(uint64_t playerId, uint64_t pedestrianId, const char *reason)> hailEnd;
            std::function<void(uint64_t playerId, uint64_t pedestrianId, uint64_t carId)> pickup;
            std::function<void(uint64_t playerId, uint64_t npcId, uint64_t carId)> enter;
            std::function<void(uint64_t playerId, uint64_t npcId, uint64_t carId)> arrive;
            std::function<void(uint64_t playerId, uint64_t npcId, uint64_t carId, ExitReason reason, bool boarded)> exit;
        };
        struct HailInfo {
            uint64_t playerId = 0;
            uint64_t carId = 0;
            bool pickedUp = false;
        };
        struct RideInfo {
            uint64_t playerId = 0;
            uint64_t carId = 0;
            uint8_t seat = 0;
            // boarding, riding, leaving or released.
            const char *phase = "";
            std::optional<glm::vec3> destination;
            float radius = kArriveRadius;
            bool arrived = false;
        };

        void RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Car::CarService &cars, Pedestrian::PedestrianService &pedestrians,
                         Npc::NpcService &npcs);
        void SetEvents(Events events) {
            _events = std::move(events);
        }
        void SetMissionGeneration(uint64_t generation);
        void Update();
        void Clear();

        // Whether walkers may hail the player's taxi (retail's
        // EnableTaxiDriver, per player). False for an unknown player.
        bool SetHailable(uint64_t playerId, bool hailable);
        bool IsHailable(uint64_t playerId) const {
            return _hailable.contains(playerId);
        }
        // A simulation host runs the walkers; without one nobody hails.
        bool Available() const;
        std::optional<HailInfo> FindHail(uint64_t pedestrianId) const;
        // Ends the walker's wave; false when it does not wave.
        bool Decline(uint64_t pedestrianId);
        // The walker hailing the player becomes its passenger and boards the
        // player's car: the NPC's network id, or 0 when the player does not
        // drive a car with a free passenger seat, already has a passenger, the
        // walker does not hail it or no NPC can be made. seat: a passenger
        // seat, or retail's choice. owner: the resource it goes with.
        uint64_t Board(uint64_t playerId, uint64_t pedestrianId, std::optional<uint8_t> seat, std::string owner);
        // Where the ride goes; taxiArrive follows once the cab stops there.
        bool SetDestination(uint64_t npcId, std::optional<glm::vec3> destination, float radius);
        // The passenger gets out once the cab is nearly still and walks off.
        bool DropOff(uint64_t npcId);
        // The player's passenger (boarding or seated), or 0.
        uint64_t PassengerOf(uint64_t playerId) const;
        std::optional<RideInfo> FindRide(uint64_t npcId) const;

      private:
        enum class Phase : uint8_t { Boarding, Riding, Leaving, Released };
        struct Hail {
            uint64_t playerId = 0;
            uint64_t carId = 0;
            uint64_t pedestrianId = 0;
            bool pickedUp = false;
            uint64_t passengerId = 0;
        };
        struct Ride {
            uint64_t playerId = 0;
            uint64_t carId = 0;
            uint8_t seat = 0;
            Phase phase = Phase::Boarding;
            std::chrono::steady_clock::time_point phaseAt;
            std::optional<glm::vec3> destination;
            float radius = kArriveRadius;
            bool arrived = false;
            bool boarded = false;
            ExitReason reason = ExitReason::DropOff;
        };

        bool FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const;
        void SendDrivers() const;
        void EndHail(uint32_t key, const char *reason);
        // The player sits in this car's driver's seat.
        bool Drives(uint64_t playerId, uint64_t carId) const;
        void Leave(uint64_t npcId, Ride &ride, ExitReason reason);
        void Exited(uint64_t npcId, Ride &ride);
        void UpdateRide(uint64_t npcId, Ride &ride, std::chrono::steady_clock::time_point now);

        const Simulation::SimulationService *_simulation = nullptr;
        Player::PlayerService *_players = nullptr;
        Car::CarService *_cars = nullptr;
        Pedestrian::PedestrianService *_pedestrians = nullptr;
        Npc::NpcService *_npcs = nullptr;
        Events _events;
        uint64_t _missionGeneration = 0;
        std::optional<MafiaNet::PeerGuid> _host;
        std::unordered_set<uint64_t> _hailable;
        // By walker key.
        std::unordered_map<uint32_t, Hail> _hails;
        // By passenger NPC network id.
        std::unordered_map<uint64_t, Ride> _rides;
    };
} // namespace Mafia1Online::Features::Taxi
