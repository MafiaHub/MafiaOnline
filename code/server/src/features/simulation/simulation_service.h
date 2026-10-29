#pragma once

#include <mafianet/types.h>

#include <optional>
#include <string>
#include <vector>

namespace Mafia1Online::Shared::Entities {
    class SimulatorEntity;
}

namespace Mafia1Online::Features::Simulation {
    // Admits the one trusted simulation host. It joins with a ticket carrying
    // the configured token and gets a private viewer instead of a player, so
    // it holds no avatar, readiness vote or car control.
    class SimulationService final {
      public:
        // Covers the whole Lost Heaven map from the viewer's origin; the
        // interest grid clamps the query to its world bounds.
        static constexpr float kViewerRange = 20000.0f;

        // An empty token disables the simulation host.
        void Init(std::string token);
        // True when guid joined with a simulation ticket. The host is attached
        // if the token matches and no other host is attached; otherwise the
        // connection is queued for a kick and must not become a player either.
        bool TryAttach(MafiaNet::PeerGuid guid);
        bool IsSimulator(MafiaNet::PeerGuid guid) const;
        void OnDisconnect(MafiaNet::PeerGuid guid);
        // Kicks are deferred to the tick: ClientJoin is still handling the peer.
        void Update();
        void Reset();

        std::optional<MafiaNet::PeerGuid> Host() const {
            return _host;
        }

      private:
        std::string _token;
        std::optional<MafiaNet::PeerGuid> _host;
        Shared::Entities::SimulatorEntity *_viewer = nullptr;
        std::vector<MafiaNet::PeerGuid> _pendingKicks;
    };
} // namespace Mafia1Online::Features::Simulation
