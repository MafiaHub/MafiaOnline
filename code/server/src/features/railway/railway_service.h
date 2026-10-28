#pragma once

#include "shared/features/railway/railway_report.h"

#include <mafianet/types.h>

#include <glm/vec3.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace Mafia1Online::Features::Simulation {
    class SimulationService;
}
namespace Mafia1Online::Features::Player {
    class PlayerService;
}
namespace Mafia1Online::Features::Combat {
    class CombatService;
}

namespace Mafia1Online::Features::Railway {
    // Trams and metro trains run on the simulation host, which loads them
    // with every player and reports them all ten times a second. Each player
    // gets the ones near it and places its own copies from them; the host's
    // railway hits on players' replicas become server damage.
    class RailwayService final {
      public:
        // A hit on a player farther than this from the railway that reported
        // it is refused: the body is about 14 m, a metro twice that.
        static constexpr float kHitReach = 25.0f;

        void RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Combat::CombatService &combat);
        void SetMissionGeneration(uint64_t generation);

        // The host's newest report of a railway, by load index; null before
        // the first report of this mission.
        [[nodiscard]] const Shared::Railway::State *Find(uint32_t index) const {
            return index < Shared::Railway::kMaxRailways && _known[index] ? &_latest[index] : nullptr;
        }
        template <typename F>
        void ForEach(F &&visit) const {
            for (uint32_t index = 0; index < Shared::Railway::kMaxRailways; ++index) {
                if (_known[index]) {
                    visit(_latest[index]);
                }
            }
        }
        // A position within kBoardReach of the body or a wagon of a railway
        // the host reported.
        [[nodiscard]] bool IsNear(uint8_t index, const glm::vec3 &position) const {
            return index < Shared::Railway::kMaxRailways && _known[index] && DistanceTo(index, position) <= Shared::Railway::kBoardReach;
        }
        // An applied hit of a railway on a player: playerId, index, damage.
        void SetHitCallback(std::function<void(uint64_t, uint8_t, float)> callback) {
            _hit = std::move(callback);
        }

      private:
        bool FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const;
        // Player <-> host: stepping aboard locks the host's railway.
        void RegisterBoarding();
        // Host -> players near: each railway's passengers.
        void RegisterPassengers();
        // From the body or the nearest wagon of a reported railway.
        [[nodiscard]] float DistanceTo(uint8_t index, const glm::vec3 &position) const;
        void Forward(const Shared::Railway::Snapshot &snapshot);

        const Simulation::SimulationService *_simulation = nullptr;
        Player::PlayerService *_players = nullptr;
        Combat::CombatService *_combat = nullptr;
        uint64_t _missionGeneration = 0;
        // Snapshots forwarded, for the far railways' share.
        uint32_t _forwarded = 0;
        // The host's newest state of each railway, for the hit check.
        std::array<Shared::Railway::State, Shared::Railway::kMaxRailways> _latest {};
        std::array<bool, Shared::Railway::kMaxRailways> _known {};
        std::function<void(uint64_t, uint8_t, float)> _hit;
        // When each player last asked to step aboard or off.
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _lastBoardRequest;
    };
} // namespace Mafia1Online::Features::Railway
