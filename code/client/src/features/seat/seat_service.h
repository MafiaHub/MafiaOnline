#pragma once

#include "shared/features/car/seat_action.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>

namespace Mafia1Online::SDK::Seat { struct NativeHuman; struct NativeCar; }
namespace Mafia1Online::Features::World { class WorldService; }

namespace Mafia1Online::Features::Seat {
    // Retail C_game::InvalidateActor clears only a seated owner. A door
    // reservation, or a seated human whose car disappears, must be released
    // while both native actors are still live.
    void ReleaseHumanSeat(SDK::Seat::NativeHuman *human);
    void ReleaseCarOccupants(SDK::Seat::NativeCar *car);

    class SeatService final {
      public:
        void RegisterRPC();
        void Update(World::WorldService &world);
        void Reset();
        SDK::Seat::NativeCar *CurrentNetworkCar(SDK::Seat::NativeHuman *human);
        bool BlockLocalExit(SDK::Seat::NativeHuman *human, SDK::Seat::NativeCar *car, int seat);
        void OnNativeUse(SDK::Seat::NativeHuman *human, SDK::Seat::NativeCar *car, int action, int seat);
        void OnNativeSteal(SDK::Seat::NativeHuman *human, SDK::Seat::NativeCar *car, int seat);

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
        void PollLocal(World::WorldService &world);
        void Replay(World::WorldService &world);
        void Reconcile(World::WorldService &world);

        World::WorldService *_world = nullptr;
        PendingLocal _pending;
        std::deque<QueuedEvent> _events;
        std::unordered_map<uint64_t, Animation> _animations;
        std::unordered_map<uint64_t, uint64_t> _seatSequences;
        uint64_t _lastServerSequence = 0;
        uint32_t _localSequence = 0;
    };
} // namespace Mafia1Online::Features::Seat
