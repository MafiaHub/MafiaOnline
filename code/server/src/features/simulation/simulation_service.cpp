#include "simulation_service.h"

#include "shared/features/simulation/simulation_ticket.h"
#include "shared/features/simulation/simulator_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <networking/rpc/client_identity.h>

#include <algorithm>

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
            _pendingKicks.push_back(guid);
            return true;
        }
        if (_host) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Refused simulation host {}: host {} is already attached", static_cast<uint64_t>(guid), static_cast<uint64_t>(*_host));
            _pendingKicks.push_back(guid);
            return true;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        _viewer = replication->CreateEntity<Shared::Entities::SimulatorEntity>();
        // Streamed to the host alone; every other entity the host needs is
        // always visible or owned by it.
        _viewer->streaming.targetGUID = guid;
        // The host drives traffic near every player, so it must receive the
        // range-streamed cars it controls wherever they are on the map.
        _viewer->streaming.range = kViewerRange;
        _viewer->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        replication->SetViewer(guid, _viewer);
        _host = guid;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Simulation host {} attached as viewer {}", static_cast<uint64_t>(guid), _viewer->GetNetworkID());
        return true;
    }

    bool SimulationService::IsSimulator(MafiaNet::PeerGuid guid) const {
        return _host == guid || std::find(_pendingKicks.begin(), _pendingKicks.end(), guid) != _pendingKicks.end();
    }

    void SimulationService::OnDisconnect(MafiaNet::PeerGuid guid) {
        std::erase(_pendingKicks, guid);
        if (_host != guid) {
            return;
        }
        // The framework destroys a disconnected peer's viewer.
        _viewer = nullptr;
        _host.reset();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Simulation host {} disconnected", static_cast<uint64_t>(guid));
    }

    void SimulationService::Update() {
        if (_pendingKicks.empty()) {
            return;
        }
        auto *network = Framework::CoreModules::GetNetworkPeer();
        for (const auto guid : std::exchange(_pendingKicks, {})) {
            network->KickPlayer(MafiaNet::ToGuid(guid), Framework::Networking::DisconnectionReason::KICKED_CUSTOM, "Simulation host refused");
        }
    }

    void SimulationService::Reset() {
        _pendingKicks.clear();
        _viewer = nullptr;
        _host.reset();
    }
} // namespace Mafia1Online::Features::Simulation
