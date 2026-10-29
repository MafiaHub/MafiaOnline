#pragma once

#include "shared/features/car/seat_action.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>

namespace Mafia1Online::SDK::Seat { struct NativeHuman; struct NativeCar; }
namespace Mafia1Online::SDK::Player { struct NativeActor; }
namespace Mafia1Online::Features::World { class WorldService; }

namespace Mafia1Online::Features::Seat {
    // Retail C_game::InvalidateActor clears a seated owner during queued human
    // removal. An unfinished door entry still needs its boarding lock released
    // first. A disappearing car needs every occupant released while it is live.
    void ReleaseHumanSeat(SDK::Seat::NativeHuman *human);
    void ReleaseCarOccupants(SDK::Seat::NativeCar *car);

    class SeatService final {
      public:
        void RegisterRPC();
        void Update(World::WorldService &world);
        void Reset();
        void SetUseFilter(std::function<bool(const void *)> filter) { _useFilter = std::move(filter); }
        bool AllowsNativeUse(const void *human) const { return !_useFilter || _useFilter(human); }
        // Simulation host: its NPCs never get into a player's network car,
        // whose seats only the server assigns and whose physics run on a
        // player's client. Retail AI enters cars through Use_Actor (fight
        // party, follow link, script). A scripted NPC may take a passenger
        // seat of a car a player drives (a taxi fare, a patient): the server
        // records it there and the player keeps the car.
        bool BlocksNpcEntry(SDK::Seat::NativeHuman *human, SDK::Seat::NativeCar *car, int seat) const;
        // Players: a car the simulation host drives takes the local player
        // only into a seat nobody holds, or the driver's seat of a traffic
        // car as a steal (its driver is thrown out); never a police car's.
        bool AllowsTrafficUse(SDK::Seat::NativeHuman *human, SDK::Seat::NativeCar *car, int seat);
        // Players: retail throws a seated human out straight from the occupied
        // seat's use (C_player::Do_AB_OwnerNULL 2009 -> Do_ThrowCocotFromCar),
        // past AllowsTrafficUse. In a car the host drives that human is the
        // host's NPC: allowed as the server allows it (not a police car's,
        // the car nearly still, within reach).
        bool AllowsNativeSteal(SDK::Seat::NativeHuman *human, SDK::Seat::NativeCar *car, SDK::Player::NativeActor *victim) const;
        SDK::Seat::NativeCar *CurrentNetworkCar(SDK::Seat::NativeHuman *human);
        void OnNativeUse(SDK::Seat::NativeHuman *human, SDK::Seat::NativeCar *car, int action, int seat);
        // victim: the seat's owner before the throw.
        void OnNativeSteal(SDK::Seat::NativeHuman *human, SDK::Seat::NativeCar *car, int seat, SDK::Player::NativeActor *victim);
        // Players: a host NPC's door entry or exit its replica was not
        // placed for (it streamed in during one, or was never seated here).
        // An exit seats it first. Only into a free seat, and never during
        // another door animation.
        bool ReplayNpcDoor(World::WorldService &world, uint64_t npcId, uint64_t carId, uint8_t seat, Shared::Car::SeatAction action);

      private:
        struct PendingLocal {
            uint64_t carId = 0;
            uint64_t playerId = 0;
            uint64_t spawnGeneration = 0;
            uint64_t missionGeneration = 0;
            uint8_t seat = 0;
            bool stealing = false;
            std::chrono::steady_clock::time_point until;
            // The outcome was sent; hold reconciliation until the replicated
            // occupant array reflects it or the acknowledgement times out.
            bool sent = false;
            bool moving = false;
        };
        struct QueuedEvent {
            Shared::Car::SeatEvent event;
            std::chrono::steady_clock::time_point until;
        };
        struct Animation {
            std::chrono::steady_clock::time_point until;
            uint64_t carId = 0;
            uint8_t seat = 0;
        };

        void Send(uint64_t carId, uint64_t playerId, uint8_t seat, Shared::Car::SeatAction action);
        bool ReplayNpcDoor(SDK::Seat::NativeHuman *human, SDK::Seat::NativeCar *car, uint64_t npcId, uint64_t carId, uint8_t seat, Shared::Car::SeatAction action);
        // Simulation host: one of its NPCs threw a player's replica out.
        void OnNpcSteal(SDK::Seat::NativeHuman *human, SDK::Seat::NativeCar *car, int seat, SDK::Player::NativeActor *victim);
        void PollLocal(World::WorldService &world);
        void Replay(World::WorldService &world);
        void Reconcile(World::WorldService &world);

        World::WorldService *_world = nullptr;
        std::function<bool(const void *)> _useFilter;
        PendingLocal _pending;
        std::deque<QueuedEvent> _events;
        std::unordered_map<uint64_t, Animation> _animations;
        std::unordered_map<uint64_t, uint64_t> _seatSequences;
        // Traffic cars this client's player started a steal of, until the
        // server takes it (the car stops being the host's) or refuses it.
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _pendingSteals;
        uint64_t _lastServerSequence = 0;
        uint32_t _localSequence = 0;
    };
} // namespace Mafia1Online::Features::Seat
