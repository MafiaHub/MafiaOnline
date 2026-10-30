#pragma once

#include <mafianet/types.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Mafia1Online::Shared::Entities {
    class SimulatorEntity;
}

namespace Mafia1Online::Features::Simulation {
    // Admits the one trusted simulation host. It joins with a ticket carrying
    // the configured token and gets a private viewer instead of a player, so
    // it holds no avatar, readiness vote or car control.
    //
    // A further host with the token waits as a standby, with no viewer (so
    // nothing replicates to it and it loads no mission) until the host is
    // gone: then the first standby takes over, one full server tick later so
    // every service has dropped the old host's world first.
    class SimulationService final {
      public:
        // Covers the whole Lost Heaven map from the viewer's origin; the
        // interest grid clamps the query to its world bounds.
        static constexpr float kViewerRange = 20000.0f;
        // Standby hosts beyond this many are refused.
        static constexpr size_t kMaxStandby = 2;

        // An empty token disables the simulation host.
        void Init(std::string token);
        // True when guid joined with a simulation ticket. The host is attached
        // if the token matches and no other host is attached; with one
        // attached it waits as a standby. Otherwise the connection is queued
        // for a kick and must not become a player either.
        bool TryAttach(MafiaNet::PeerGuid guid);
        bool IsSimulator(MafiaNet::PeerGuid guid) const;
        void OnDisconnect(MafiaNet::PeerGuid guid);
        // Kicks are deferred to the tick: ClientJoin is still handling the
        // peer. Promotes a standby once the host has been gone a full tick.
        void Update();
        void Reset();
        // Kicks the host so the first standby takes over (Update). False
        // without a host or a standby.
        bool HandOver(std::string_view reason);
        // A host attached (a new connection, or a standby promoted); the
        // second argument is true for a promoted standby.
        void SetAttachedCallback(std::function<void(MafiaNet::PeerGuid, bool)> callback) {
            _attached = std::move(callback);
        }

        std::optional<MafiaNet::PeerGuid> Host() const {
            return _host;
        }
        size_t StandbyCount() const {
            return _standby.size();
        }

      private:
        void Attach(MafiaNet::PeerGuid guid, bool promoted);

        std::string _token;
        std::optional<MafiaNet::PeerGuid> _host;
        Shared::Entities::SimulatorEntity *_viewer = nullptr;
        struct PendingKick {
            MafiaNet::PeerGuid guid {};
            std::string reason;
        };
        std::vector<PendingKick> _pendingKicks;
        // Admitted hosts waiting to take over, in arrival order.
        std::vector<MafiaNet::PeerGuid> _standby;
        // Replaced hosts being kicked: still simulators, never the host.
        std::vector<MafiaNet::PeerGuid> _retiring;
        // Update saw no host once already: the next one may promote.
        bool _vacant = false;
        std::function<void(MafiaNet::PeerGuid, bool)> _attached;
    };
} // namespace Mafia1Online::Features::Simulation
