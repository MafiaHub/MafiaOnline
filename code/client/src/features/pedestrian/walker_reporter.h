#pragma once

#include <mafia1/sdk/player/native_actor.h>

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::SDK::Traffic {
    struct NativeTrafficElement;
    struct NativeTrafficGenerator;
}

namespace Mafia1Online::Features::Pedestrian {
    // Simulation host: tells the server about every walker life. A walker
    // that becomes active is a new life with a fresh key, even when the same
    // pool slot walked before; one that is no longer active has retired.
    class WalkerReporter final {
      public:
        static constexpr std::chrono::milliseconds kSnapshotInterval {100};

        // generators in mission actor order: a walker names its pool by index.
        // corpseLifetime of zero keeps a corpse until it retires natively.
        void Update(uint64_t missionGeneration, const std::vector<SDK::Traffic::NativeTrafficGenerator *> &generators, std::chrono::milliseconds corpseLifetime);
        // The key of a reported walker life; 0 when it has none yet.
        uint32_t KeyOf(SDK::Traffic::NativeTrafficElement *element) const;
        // Retires the life with this key now; false when it is not active.
        bool Retire(uint32_t key);
        // The active walker with this key, or null.
        SDK::Traffic::NativeTrafficElement *ActiveElement(uint32_t key) const;
        // The native walkers are gone (mission closing); the server drops the
        // reported lives on its own when the generation changes or they go stale.
        void Reset();

      private:
        struct Reported {
            uint32_t key = 0;
            SDK::Player::Vector3 lastPosition {};
            bool dead = false;
            std::chrono::steady_clock::time_point deadSince;
        };

        void SendSnapshots(uint64_t missionGeneration, float seconds);

        uint64_t _missionGeneration = 0;
        uint32_t _nextKey = 1;
        std::unordered_map<SDK::Traffic::NativeTrafficElement *, Reported> _reported;
        std::chrono::steady_clock::time_point _lastSnapshot;
    };
} // namespace Mafia1Online::Features::Pedestrian
