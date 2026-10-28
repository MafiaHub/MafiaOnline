#pragma once

#include "shared/features/bridge/bridge_report.h"

#include <mafianet/types.h>

#include <array>
#include <cstdint>

namespace Mafia1Online::Features::Simulation {
    class SimulationService;
}
namespace Mafia1Online::Features::Player {
    class PlayerService;
}

namespace Mafia1Online::Features::Bridge {
    // The drawbridges run on the simulation host, which loads them with every
    // player and reports them five times a second and on each phase change.
    // Every spawned player gets every report: there are only two bridges, and
    // their steamers sail between them.
    class BridgeService final {
      public:
        void RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players);
        void SetMissionGeneration(uint64_t generation);

        // The host's newest report of a bridge, by load index; null before
        // the first report of this mission.
        [[nodiscard]] const Shared::Bridge::State *Find(uint32_t index) const {
            return index < Shared::Bridge::kMaxBridges && _known[index] ? &_latest[index] : nullptr;
        }
        template <typename F>
        void ForEach(F &&visit) const {
            for (uint32_t index = 0; index < Shared::Bridge::kMaxBridges; ++index) {
                if (_known[index]) {
                    visit(_latest[index]);
                }
            }
        }

      private:
        const Simulation::SimulationService *_simulation = nullptr;
        Player::PlayerService *_players = nullptr;
        uint64_t _missionGeneration = 0;
        std::array<Shared::Bridge::State, Shared::Bridge::kMaxBridges> _latest {};
        std::array<bool, Shared::Bridge::kMaxBridges> _known {};
        // The newest report taken: its host session and host clock.
        bool _hasReport = false;
        uint32_t _hostSession = 0;
        uint32_t _lastHostTimeMs = 0;
    };
} // namespace Mafia1Online::Features::Bridge
