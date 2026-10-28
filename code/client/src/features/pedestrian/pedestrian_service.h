#pragma once

#include "walker_reporter.h"

#include "shared/features/pedestrian/pedestrian_control.h"

#include <mafia1/sdk/player/native_actor.h>

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::SDK::Traffic {
    struct NativeTrafficElement;
    struct NativeTrafficGenerator;
}

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Pedestrian {
    // Retail pedestrian traffic, run for every player at once.
    //
    // Retail drives one generator from the camera: C_game::Tick activates
    // the generator nearest to it, Activate spawns ahead of it and
    // TickElements retires walkers by distance to it. On the simulation host
    // each spawned player is an observer instead. Every generator an observer
    // selects runs; its observers form clusters of players close enough to
    // share walkers, and each cluster gets the generator's retail count, so
    // two players across the city both see a full street. Spawns are placed
    // from a cluster member's view, and a walker retires only when it is far
    // from every observer. Players keep the pools idle; they never spawn
    // walkers of their own.
    class PedestrianService final {
      public:
        // Players: a car hit a walker replica (C_don_ocas::Collision).
        void OnReplicaCollision(SDK::Player::NativeActor *actor, const SDK::Player::Vector3 &direction, SDK::Scene::NativeFrame *model);
        bool IsNetworkObject(void *native) const;
        struct Stats {
            uint32_t walkers          = 0;
            uint32_t activeGenerators = 0;
            uint32_t clusters         = 0;
            uint32_t observers        = 0;
        };
        struct Observer {
            uint64_t id;
            SDK::Player::Vector3 position;
            SDK::Player::Vector3 forward;
            float speed;
        };

        bool Install();
        void Uninstall();
        void RegisterRPC();
        // Host only: refreshes observers from the replicated players and
        // activates the generators they select for the next game tick.
        void Update(World::WorldService &world);
        // Before mission teardown: drops every generator pointer.
        void Reset();
        Stats GetStats() const;
        const std::vector<Observer> &Observers() const {
            return _observers;
        }
        bool IsSelected(SDK::Traffic::NativeTrafficGenerator *generator) const {
            return _selected.contains(generator);
        }

        void OnGeneratorAI(SDK::Traffic::NativeTrafficGenerator *generator, uint32_t frameMs);
        // Host: retail Hit ran on a walker; scripts hear it as pedestrianDamage.
        // The key the host reported this walker under, or 0.
        uint32_t WalkerKey(SDK::Traffic::NativeTrafficElement *element) const {
            return _reporter.KeyOf(element);
        }
        void OnWalkerHit(SDK::Traffic::NativeTrafficElement *element, int32_t type, SDK::Player::NativeActor *attacker, float damage, float before, float after);
        void OnTickElements(uint32_t frameMs);

      private:
        // Players close enough to share walkers. Each cluster spawns on its
        // own retail cadence.
        struct Cluster {
            std::vector<uint32_t> observers;
            int32_t walkers = 0;
            uint32_t spawnTimerMs = 0;
        };
        struct Selection {
            std::vector<Cluster> clusters;
            uint32_t next = 0;
        };

        void ReportGenerators(const std::vector<SDK::Traffic::NativeTrafficGenerator *> &generators);

        World::WorldService *_world = nullptr;
        std::vector<Observer> _observers;
        // Ids of last update's observers, by index, to carry clusters over.
        std::vector<uint64_t> _previousObserverIds;
        WalkerReporter _reporter;
        Shared::Pedestrian::Control _control;
        bool _reportedGenerators = false;
        // Players: last car impact report per walker.
        static constexpr std::chrono::milliseconds kImpactCooldown {500};
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _lastImpact;
        std::unordered_map<SDK::Traffic::NativeTrafficGenerator *, Selection> _selected;
    };

    // The host's pedestrian counters for the debug view; zero elsewhere.
    PedestrianService::Stats GetHostStats();
    // The installed service, for the host's debug view.
    const PedestrianService *GetService();
} // namespace Mafia1Online::Features::Pedestrian
