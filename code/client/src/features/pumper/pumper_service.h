#pragma once

#include "shared/features/pumper/pumper_report.h"

#include <mafia1/sdk/player/native_actor.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

namespace Mafia1Online::SDK::World {
    struct NativePumper;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Pumper {
    // The gas station attendants (C_pumper), loaded by every client in the
    // same order. The simulation host runs them with the retail AI and is
    // told by the server which network car to serve; players replay the
    // host's state, animation and position through the native animation
    // machine, so the notify events play the footsteps, the fuel loop, the
    // cigarettes and the smoke here, and speak the dialogue lines on each
    // state change. The customer's own client runs the retail Start and the
    // retail release on its car, between the server's Accepted and Release.
    class PumperService final {
      public:
        bool Install(World::WorldService &world);
        void Uninstall();
        void RegisterRPC();
        void Update(World::WorldService &world);
        // The mission's attendants are going (mission close).
        void Reset();

        void OnGameInit(SDK::World::NativePumper *pumper);
        void OnGameDone(SDK::World::NativePumper *pumper);
        // C_pumper::AI. The host runs it; players replay the host instead.
        void AI(SDK::World::NativePumper *pumper, unsigned int frameTimeMs);
        // C_pumper::ChooseNextAction on the host, whose dialogue answers
        // would read C_game::m_pPlayer (it has none) and whose release would
        // drive a car another client simulates.
        void ChooseNextAction(SDK::World::NativePumper *pumper);
        // The retail "use the pump" choice of the local player.
        void RequestService(SDK::World::NativePumper *pumper, void *car);
        // The fuel animation's event 43: the host tells the server, which
        // fills the tank.
        void OnFuelled(SDK::World::NativePumper *pumper);

      private:
        struct Entry {
            SDK::World::NativePumper *pumper = nullptr;
            uint32_t nameHash = 0;
            // GameInit's route start (current) and first step (next).
            void *startNode = nullptr;
            void *firstNode = nullptr;
            // Players: the host's newest state and the one replayed.
            bool hasTarget = false;
            Shared::Pumper::State target;
            bool hasShown = false;
            uint8_t shown = Shared::Pumper::kIdle;
            SDK::Player::Vector3 position {};
            SDK::Player::Vector3 direction {};
            // Customer: the car the local native Start took.
            uint64_t customerCarId = 0;
            std::chrono::steady_clock::time_point greetedAt {};
            // Host: the car being served, and whether the attendant is still
            // on his way to it.
            uint64_t serviceCarId = 0;
            bool outbound = false;
            // Host: what was last reported.
            uint8_t reportedState = 0xff;
            int32_t reportedAnimation = -2;
            uint64_t reportedCarId = 0;
        };

        Entry *Find(const SDK::World::NativePumper *pumper);
        void Replay(Entry &entry, unsigned int frameTimeMs);
        void OnReplayedTransition(Entry &entry, uint8_t from, uint8_t to);
        SDK::Scene::NativeFrame *CustomerModel(const Entry &entry, uint64_t carId) const;
        void *ResolveCar(uint64_t carId) const;
        void OnCommand(const Shared::Pumper::Command &command);
        void HostServe(Entry &entry, uint64_t carId);
        void HostAbandon(Entry &entry);
        void HostReport(Entry &entry, Shared::Pumper::Progress::Kind kind, uint64_t carId);
        void CustomerStart(Entry &entry, uint64_t carId);
        void CustomerRelease(Entry &entry);
        void SendSnapshot(bool reliable);

        World::WorldService *_world = nullptr;
        std::vector<Entry> _pumpers;
        uint64_t _missionGeneration = 0;
        // Players: the reporting host's session and its newest report.
        uint32_t _hostSession = 0;
        uint32_t _lastHostTimeMs = 0;
        bool _hasReport = false;
        // Customer: the one request waiting for the server.
        bool _pending = false;
        std::chrono::steady_clock::time_point _pendingAt {};
        std::chrono::steady_clock::time_point _lastSnapshot {};
        bool _reportedMismatch = false;
    };
} // namespace Mafia1Online::Features::Pumper
