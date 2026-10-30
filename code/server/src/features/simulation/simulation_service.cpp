#include "simulation_service.h"

#include "shared/features/simulation/simulation_ticket.h"
#include "shared/features/simulation/simulator_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <networking/rpc/client_identity.h>

#include <algorithm>
#include <string_view>
#include <utility>

namespace Mafia1Online::Features::Simulation {
    namespace {
        // Constant time in the token's length, so the comparison does not
        // reveal how much of a guessed token was right.
        bool TokensEqual(std::string_view expected, std::string_view supplied) {
            if (expected.size() != supplied.size()) {
                return false;
            }
            unsigned char difference = 0;
            for (size_t i = 0; i < expected.size(); ++i) {
                difference |= static_cast<unsigned char>(expected[i] ^ supplied[i]);
            }
            return difference == 0;
        }
    } // namespace

    void SimulationService::Init(std::string token) {
        _token = std::move(token);
        if (_token.empty()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Simulation host disabled (no simulationToken)");
        }
        else {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Simulation host enabled");
        }
    }

    bool SimulationService::TryAttach(MafiaNet::PeerGuid guid) {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        const auto *identity = network->GetPeerIdentity(MafiaNet::ToGuid(guid));
        if (!identity || !identity->ticket.starts_with(Shared::Simulation::kTicketPrefix)) {
            return false;
        }
        const std::string_view token = std::string_view(identity->ticket).substr(Shared::Simulation::kTicketPrefix.size());
        if (_token.empty() || !TokensEqual(_token, token)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Refused simulation host {}: token does not match", static_cast<uint64_t>(guid));
            _pendingKicks.push_back({guid, "Simulation host refused"});
            return true;
        }
        if (_host || !_standby.empty()) {
            // A host, or one that just left and a standby that follows it.
            if (_standby.size() >= kMaxStandby) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Refused simulation host {}: host {} is attached and {} standby hosts wait", static_cast<uint64_t>(guid),
                                                                           _host ? static_cast<uint64_t>(*_host) : 0, _standby.size());
                _pendingKicks.push_back({guid, "Simulation host refused"});
                return true;
            }
            _standby.push_back(guid);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Simulation host {} waits as standby {} of {}", static_cast<uint64_t>(guid), _standby.size(), kMaxStandby);
            return true;
        }
        Attach(guid, false);
        return true;
    }

    void SimulationService::Attach(MafiaNet::PeerGuid guid, bool promoted) {
        auto *replication = Framework::CoreModules::GetReplication();
        _viewer = replication->CreateEntity<Shared::Entities::SimulatorEntity>();
        // Streamed to the host alone; every other entity the host needs is
        // always visible or owned by it.
        _viewer->streaming.targetGUID = guid;
        // The host drives traffic near every player, so it must receive the
        // range-streamed cars it controls wherever they are on the map.
        _viewer->streaming.range = kViewerRange;
        // It simulates for every player, so it needs the whole set at full
        // rate: no budget cuts and no distance bands from its far origin.
        _viewer->streaming.unthrottled = true;
        _viewer->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        replication->SetViewer(guid, _viewer);
        _host = guid;
        _vacant = false;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Simulation host {} attached as viewer {}{}", static_cast<uint64_t>(guid), _viewer->GetNetworkID(),
                                                                   promoted ? " (standby taking over)" : "");
        if (_attached) {
            _attached(guid, promoted);
        }
    }

    bool SimulationService::IsSimulator(MafiaNet::PeerGuid guid) const {
        const auto listed = [&](const std::vector<MafiaNet::PeerGuid> &list) { return std::find(list.begin(), list.end(), guid) != list.end(); };
        return _host == guid || listed(_standby) || listed(_retiring) ||
               std::any_of(_pendingKicks.begin(), _pendingKicks.end(), [&](const PendingKick &kick) { return kick.guid == guid; });
    }

    void SimulationService::OnDisconnect(MafiaNet::PeerGuid guid) {
        std::erase_if(_pendingKicks, [&](const PendingKick &kick) { return kick.guid == guid; });
        std::erase(_standby, guid);
        std::erase(_retiring, guid);
        if (_host != guid) {
            return;
        }
        // The framework destroys a disconnected peer's viewer.
        _viewer = nullptr;
        _host.reset();
        _vacant = false;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Simulation host {} disconnected; {}", static_cast<uint64_t>(guid),
                                                                   _standby.empty() ? "no traffic, walkers or railways run until a host joins" : "a standby host takes over");
    }

    bool SimulationService::HandOver(std::string_view reason) {
        if (!_host || _standby.empty()) {
            return false;
        }
        const auto replaced = *_host;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Simulation host {} hands over to standby {} ({})", static_cast<uint64_t>(replaced),
                                                                   static_cast<uint64_t>(_standby.front()), reason);
        // Its viewer goes with its connection; until then it is no host to
        // anything (Host() names nobody) and its reports are dropped.
        _viewer = nullptr;
        _host.reset();
        _vacant = false;
        _retiring.push_back(replaced);
        _pendingKicks.push_back({replaced, "Simulation host replaced"});
        return true;
    }

    void SimulationService::Update() {
        if (!_pendingKicks.empty()) {
            auto *network = Framework::CoreModules::GetNetworkPeer();
            for (const auto &kick : std::exchange(_pendingKicks, {})) {
                network->KickPlayer(MafiaNet::ToGuid(kick.guid), Framework::Networking::DisconnectionReason::KICKED_CUSTOM, kick.reason);
            }
        }
        if (_host || _standby.empty()) {
            _vacant = false;
            return;
        }
        // Every service has run one tick without a host and dropped the old
        // host's world; the standby takes over.
        if (!_vacant) {
            _vacant = true;
            return;
        }
        const auto next = _standby.front();
        _standby.erase(_standby.begin());
        Attach(next, true);
    }

    void SimulationService::Reset() {
        _pendingKicks.clear();
        _standby.clear();
        _retiring.clear();
        _vacant = false;
        _viewer = nullptr;
        _host.reset();
    }
} // namespace Mafia1Online::Features::Simulation
