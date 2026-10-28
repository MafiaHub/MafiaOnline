#pragma once

#include "features/car/car_service.h"
#include "shared/features/car/car_entity.h"

#include <mafia1/sdk/player/native_actor.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::SDK::Car {
    struct NativeCar;
}
namespace Mafia1Online::SDK::Traffic {
    struct NativeTrafficCar;
    struct NativeTrafficCarElement;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}
namespace Mafia1Online::Features::Npc {
    class NpcHost;
}

namespace Mafia1Online::Features::Traffic {
    // Simulation host: the retail car traffic, run for every player at once.
    //
    // C_traffic_car spawns, despawns and drives its pool around the active
    // camera. Here the camera reads are taken over: spawns are placed around
    // each player in turn and kept out of every player's sight, a car goes
    // only once it is out of every player's reach, collision response is on
    // near any player, and the pool is several times the retail size.
    //
    // Every car the host drives (a pool car, or a temporary car the retail
    // game made: a stolen pool car's replacement, a mafia car's pursuit car,
    // a police car) is reported once, becomes a network car with the host as
    // controller and is adopted by CarService. When retail swaps a pool car
    // for a temporary one, the network car moves to the new native car.
    class TrafficHost final {
      public:
        using Kind = Shared::Entities::CarEntity::TrafficKind;
        using CrewState = Shared::Entities::CarEntity::CrewState;

        // Host only.
        bool Install(Car::CarService &cars, Npc::NpcHost &npcs);
        void Uninstall();
        void RegisterRPC(World::WorldService &world);
        // After NpcHost::Update and before CarService::Update.
        void Update(World::WorldService &world);
        // Mission closing: the natives go with it.
        void Reset();
        void OnNativeCarDestroyed(void *car);
        // Reported (or being moved to a replacement native car) and not yet
        // bound to its network car, for at most kBindingWait: the server
        // may never take the report.
        [[nodiscard]] bool AwaitsBinding(const void *car) const;

        // Hook bodies.
        using TrafficAI = void(__thiscall *)(SDK::Traffic::NativeTrafficCar *, uint32_t);
        void TickTraffic(SDK::Traffic::NativeTrafficCar *traffic, uint32_t frameMs, TrafficAI original);
        void BeforeDeactivate(SDK::Traffic::NativeTrafficCar *traffic, int32_t slot);
        void OnRammed(SDK::Car::NativeCar *car, SDK::Player::NativeActor *actor, float force);
        void OnReplaced(SDK::Car::NativeCar *poolCar, SDK::Car::NativeCar *replacement);
        void OnOccupantShot(SDK::Car::NativeCar *car, void *frame);
        void BeforePoliceCars(std::vector<SDK::Car::NativeCar *> &pending);
        void AfterPoliceCars(SDK::Car::NativeCar *const *first, SDK::Car::NativeCar *const *last, size_t before, const std::vector<SDK::Car::NativeCar *> &pending);

      private:
        struct Observer {
            SDK::Player::Vector3 position;
            SDK::Player::Vector3 direction;
            uint64_t networkId = 0;
        };
        // What players are shown of a car's traffic crew.
        struct CrewView {
            std::string driverModel;
            std::string passengerModel;
            CrewState driver = CrewState::None;
            CrewState passenger = CrewState::None;
            bool operator==(const CrewView &) const = default;
        };
        struct Record {
            uint32_t key = 0;
            SDK::Car::NativeCar *car = nullptr;
            Car::CarService::Origin origin = Car::CarService::Origin::Pool;
            Kind kind = Kind::Civil;
            uint64_t networkId = 0;
            // The network car to adopt again after a failed move to a
            // replacement native car.
            uint64_t retryId = 0;
            // A player has it now; the host only observes it. Set by the
            // server's Takeover or by the replicated hostDriven flag,
            // whichever arrives first.
            bool released = false;
            // The Takeover itself ran (steal throw, ReplaceStolen), so retail
            // traffic no longer drives it.
            bool takeoverApplied = false;
            bool driverDead = false;
            bool passengerDead = false;
            // Its traffic crew models stayed with the pool car it replaced.
            bool crewGone = false;
            bool reported = false;
            // Since when it waits for a network car to bind to.
            std::chrono::steady_clock::time_point unboundSince;
            CrewView reportedCrew;
            bool reportedSiren = false;
        };

        void RefreshObservers(World::WorldService &world);
        void Discover(World::WorldService &world);
        void Bind(World::WorldService &world);
        void Watch(World::WorldService &world);
        uint32_t Add(SDK::Car::NativeCar *car, Car::CarService::Origin origin, Kind kind, const SDK::Traffic::NativeTrafficCarElement *element);
        // release: unbind the native car here. Not for a temporary car the
        // server removed, which CarService's own removal deletes natively.
        void Retire(uint32_t key, bool report, bool release = true);
        void Transfer(SDK::Car::NativeCar *from, SDK::Car::NativeCar *to);
        Record *FindByCar(const void *car);
        CrewView CrewOf(const Record &record, const SDK::Traffic::NativeTrafficCarElement *element) const;
        // Road block barriers and parked police cars of the police manager.
        void WatchBarriers();
        // A player's replica, or a car driven by one.
        bool IsPlayerCaused(SDK::Player::NativeActor *actor) const;
        // Cars reported to the server and still driven here: the server
        // takes at most Shared::Traffic::kMaxHostCars of them.
        [[nodiscard]] size_t DrivenCount() const;
        // A police car waiting in gSpawnedCars goes back into traffic.
        void ReturnCar(SDK::Traffic::NativeTrafficCar &traffic);
        void SpawnAround(SDK::Traffic::NativeTrafficCar &traffic);
        void Despawn(SDK::Traffic::NativeTrafficCar &traffic);
        float NearestObserverSq(const SDK::Player::Vector3 &position) const;

        static constexpr std::chrono::seconds kBindingWait {3};

        World::WorldService *_world = nullptr;
        Car::CarService *_cars = nullptr;
        Npc::NpcHost *_npcs = nullptr;
        uint64_t _missionGeneration = 0;
        uint32_t _nextKey = 1;
        std::unordered_map<uint32_t, Record> _records;
        std::unordered_map<const void *, uint32_t> _keyByCar;
        std::vector<Observer> _observers;
        size_t _nextObserver = 0;
        uint32_t _spawnTimerMs = 0;
        // The host's world holds a single C_traffic_car actor.
        SDK::Traffic::NativeTrafficCar *_traffic = nullptr;
        // Road block barriers (C_irenka, the spike strips) reported: key and
        // the pose players were sent.
        struct ReportedBarrier {
            uint32_t key = 0;
            SDK::Player::Vector3 position {};
            SDK::Player::Vector3 forward {};
        };
        std::unordered_map<const void *, ReportedBarrier> _barrierKeys;
        uint32_t _nextBarrierKey = 1;
    };
} // namespace Mafia1Online::Features::Traffic
