#pragma once

#include "shared/features/bridge/bridge_report.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>

namespace Mafia1Online::SDK::World {
    struct NativeBridge;
}
namespace Mafia1Online::SDK::Player {
    struct NativeHuman;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}
namespace Mafia1Online::Features::Death {
    class DeathService;
}

namespace Mafia1Online::Features::Bridge {
    // The drawbridges and the steamers they open for. Every client loads the
    // mission's bridges in the same order (GameInit). The simulation host
    // runs them natively and reports them; players make theirs network
    // controlled, so C_actor::Tick runs no AI on them, and replay the host's
    // phases, leaf angles and steamers before each native Update, which moves
    // the collision, blinks the lights and draws the steamers.
    class BridgeService final {
      public:
        bool Install(Death::DeathService &death);
        void Uninstall();
        void RegisterRPC();
        void Update(World::WorldService &world);
        // The mission's bridges are going (mission close, disconnect).
        void Reset();

        void OnGameInit(SDK::World::NativeBridge *bridge);
        void OnGameDone(SDK::World::NativeBridge *bridge);
        // Players: the host's state at the render time, before Update.
        void BeforeUpdate(SDK::World::NativeBridge *bridge);
        // A raised leaf ran into the local player.
        void OnCrushed(SDK::Player::NativeHuman &human);

      private:
        struct Entry {
            SDK::World::NativeBridge *bridge = nullptr;
            uint32_t nameHash = 0;
            bool prepared = false;
        };
        struct Sample {
            uint32_t hostTimeMs = 0;
            Shared::Bridge::Snapshot snapshot;
        };

        void Prepare(Entry &entry);
        void SendSnapshot(World::WorldService &world, bool reliable);
        void CloseWithoutHost();
        [[nodiscard]] int IndexOf(const SDK::World::NativeBridge *bridge) const;
        [[nodiscard]] static double LocalMs();

        std::vector<Entry> _bridges;
        uint64_t _missionGeneration = 0;
        // Players: the host's reports, oldest first.
        std::deque<Sample> _samples;
        // Players: local clock minus host clock, the least over recent
        // packets (the least delayed one), to date the samples here.
        std::deque<double> _clockOffsets;
        double _clockOffset = 0.0;
        bool _hasClockOffset = false;
        // Players: the reporting host's session, and its newest report here.
        uint32_t _hostSession = 0;
        std::chrono::steady_clock::time_point _lastSampleAt {};
        Death::DeathService *_death = nullptr;
        // Host: the phases last reported, to report a change at once.
        std::array<uint8_t, Shared::Bridge::kMaxBridges> _reportedPhases {};
        std::chrono::steady_clock::time_point _lastSnapshot {};
        bool _reportedPlacement = false;
        bool _reportedMismatch = false;
    };
} // namespace Mafia1Online::Features::Bridge
