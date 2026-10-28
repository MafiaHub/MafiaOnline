#pragma once

#include <mafianet/types.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace Mafia1Online::Shared::Entities {
    class PedestrianEntity;
}

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
    // Server-owned walker entities, one per walker life on the simulation
    // host. Only the attached host may report walkers, and only for the
    // current mission generation. A walker the host stops reporting is
    // removed after kStaleAfter, so a stalled or lost host cannot leave
    // frozen pedestrians behind. Scripts control the host through Settings.
    class PedestrianService final {
      public:
        static constexpr size_t kMaxWalkers = 4096;
        static constexpr std::chrono::seconds kStaleAfter {3};
        // Players see walkers this far from their position.
        static constexpr float kStreamRange = 150.0f;
        // A scream reaches the players within its range plus this margin, so
        // one walking into range while it plays still hears its start.
        static constexpr float kScreamRangeMargin = 20.0f;
        static constexpr uint32_t kMaxCorpseLifetimeMs = 3'600'000;

        struct Settings {
            bool enabled = true;
            float density = 1.0f;
            // Retail keeps a corpse until every player is far away; a
            // player standing by it would keep it forever.
            uint32_t corpseLifetimeMs = 30'000;
        };
        // Script events. A mission change removes every walker without
        // pedestrianRetire, as it does vehicles.
        struct Events {
            std::function<void(Shared::Entities::PedestrianEntity &)> spawn;
            std::function<void(Shared::Entities::PedestrianEntity &)> retire;
            std::function<void(Shared::Entities::PedestrianEntity &, uint64_t attackerId, int32_t hitType, float damage, float healthBefore, float healthAfter)> damage;
            std::function<void(Shared::Entities::PedestrianEntity &, uint64_t killerId)> death;
        };

        // A driver may report a car contact this far from the walker's
        // last reported position (car length plus 100 ms at speed).
        static constexpr float kCarImpactReach = 10.0f;
        static constexpr float kMaxImpactSpeed = 80.0f;
        // One impact per walker in this window, as the driver's own cooldown.
        static constexpr std::chrono::milliseconds kCarImpactCooldown {500};

        void RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Car::CarService &cars);
        void SetEvents(Events events) {
            _events = std::move(events);
        }
        void SetMissionGeneration(uint64_t generation);
        void Update();
        // Host gone or mission changed: every walker of the old world goes.
        void Clear();

        // The host attached: it starts from the current settings.
        void OnHostAttached();
        const Settings &GetSettings() const {
            return _settings;
        }
        bool SetSettings(const Settings &settings);
        // Asks the host to retire the walker now; pedestrianRetire follows.
        bool Remove(uint64_t networkId);
        Shared::Entities::PedestrianEntity *Find(uint64_t networkId) const;
        // Network id of the walker the host reported with this key, or 0.
        uint64_t NetworkIdOfKey(uint32_t key) const;
        void ForEach(const std::function<void(Shared::Entities::PedestrianEntity &)> &visit) const;

        size_t Count() const {
            return _walkers.size();
        }

      private:
        struct Walker {
            Shared::Entities::PedestrianEntity *entity = nullptr;
            std::chrono::steady_clock::time_point lastSeen;
            // Network id of the last attacker the host reported, for the
            // killer of pedestrianDeath.
            uint64_t lastAttackerId = 0;
        };

        bool FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const;
        void SendSettings() const;
        void Retire(std::unordered_map<uint32_t, Walker>::iterator walker);

        const Simulation::SimulationService *_simulation = nullptr;
        Player::PlayerService *_players = nullptr;
        uint64_t _missionGeneration = 0;
        Settings _settings;
        Events _events;
        std::unordered_map<uint32_t, Walker> _walkers;
        std::unordered_map<uint64_t, uint32_t> _keyByNetworkId;
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _lastCarImpact;
    };
} // namespace Mafia1Online::Features::Pedestrian
