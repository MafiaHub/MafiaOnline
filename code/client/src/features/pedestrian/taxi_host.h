#pragma once

#include <mafia1/sdk/player/native_actor.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <random>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::SDK::Traffic {
    struct NativeTrafficElement;
}
namespace Mafia1Online::SDK::Police {
    struct NativePoliceManager;
}

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Pedestrian {
    // Simulation host: retail taxi hailing for every player a script lets
    // walkers hail (Shared::Taxi::Drivers), as single player runs it for its
    // own player in a taxi.
    //
    // Each driver keeps the retail countdown; when it runs out the walker
    // selection of GeneratePassengerForTaxi (0x44f7d0) runs from that
    // driver's taxi and the chosen walker starts the retail wave. Element
    // Tick then runs MavaNaTaxi (0x451ff0) on it with C_game's player set to
    // its driver, so it turns to the taxi and waves. Where retail would make
    // it the taxi_pas.dat passenger, the host reports a Pickup and keeps it
    // waving; the server's script turns it into a scripted NPC passenger.
    // Every wave, its start and its end go to the server, which shows them
    // on the walker's replicas.
    class TaxiHost final {
      public:
        // A Pickup the script never answers ends the wave after this long.
        static constexpr std::chrono::seconds kPickupWait {15};
        // MavaNaTaxi never boards a walker here: the taxi's driver speed
        // reads at least this while it runs.
        static constexpr float kNoBoardingSpeed = 1.0f;

        bool Install();
        void Uninstall();
        void RegisterRPC();
        void Reset();

        // Before the walkers tick: the drivers' replicas and taxis this frame.
        void Prepare(World::WorldService &world);
        // Around one walker's retail Tick. Begin returns true when the walker
        // waves for a known driver; End must follow then. A wave nobody
        // answers ends here.
        bool BeginTick(SDK::Traffic::NativeTrafficElement *element);
        void EndTick();
        // After the walkers ticked: the drivers' countdowns and hails, then
        // the ended waves. keyOf names a reported walker (0 for none yet).
        void Finish(uint32_t frameMs, const std::function<uint32_t(SDK::Traffic::NativeTrafficElement *)> &keyOf);
        // MavaNaTaxi hook body.
        void OnWave(SDK::Traffic::NativeTrafficElement *element, uint32_t frameMs, void(__thiscall *original)(SDK::Traffic::NativeTrafficElement *, uint32_t));
        // A walker becomes an NPC passenger (Npc.spawn from a pedestrian):
        // its wave ends with it.
        void Forget(SDK::Traffic::NativeTrafficElement *element);

      private:
        struct Driver {
            SDK::Player::NativeActor *human = nullptr;
            // The car its replica sits in, any seat, or null.
            SDK::Player::NativeActor *car = nullptr;
            uint64_t carId = 0;
            uint8_t seat = 0;
            // The car's speed as its driver reports it: the host's copy of
            // a player's car runs no physics of its own.
            float speed = 0.0f;
            bool taxi = false;
        };
        struct Hail {
            uint64_t driverId = 0;
            uint32_t key = 0;
            uint8_t wave = 0;
            bool pickupReported = false;
            std::chrono::steady_clock::time_point pickupAt;
        };

        // GeneratePassengerForTaxi for one driver.
        void Generate(uint64_t driverId, Driver &driver, const std::function<uint32_t(SDK::Traffic::NativeTrafficElement *)> &keyOf);
        static SDK::Police::NativePoliceManager *Police();
        static bool Wanted(SDK::Player::NativeActor *human);

        World::WorldService *_world = nullptr;
        uint64_t _missionGeneration = 0;
        std::unordered_set<uint64_t> _allowed;
        std::unordered_map<uint64_t, Driver> _drivers;
        // The retail countdown (0x63d72c) per driver.
        std::unordered_map<uint64_t, int32_t> _timers;
        std::unordered_map<SDK::Traffic::NativeTrafficElement *, Hail> _hails;
        // The walker whose Tick runs now, its hail and C_game's player before.
        Hail *_ticking = nullptr;
        SDK::Player::NativeActor *_previousPlayer = nullptr;
        std::minstd_rand _random {std::random_device {}()};
    };
} // namespace Mafia1Online::Features::Pedestrian
