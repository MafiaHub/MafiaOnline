#include "bridge_service.h"

#include "features/player/player_service.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <networking/network_peer.h>

namespace Mafia1Online::Features::Bridge {
    void BridgeService::RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players) {
        _simulation = &simulation;
        _players = &players;
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Bridge::Snapshot>([this](const Shared::Bridge::Snapshot &received, MafiaNet::Packet *packet) {
            if (_simulation->Host() != MafiaNet::ToPeerGuid(packet->guid) || received.missionGeneration != _missionGeneration) {
                return;
            }
            std::array<bool, Shared::Bridge::kMaxBridges> seen {};
            for (uint8_t i = 0; i < received.count; ++i) {
                const auto &state = received.bridges[i];
                if (!Shared::Bridge::Valid(state) || seen[state.index]) {
                    return;
                }
                seen[state.index] = true;
            }
            for (uint8_t i = 0; i < received.steamerCount; ++i) {
                if (!Shared::Bridge::Valid(received.steamers[i])) {
                    return;
                }
            }
            // An unreliable report can trail a newer reliable phase change;
            // the newest state stands.
            if (_hasReport && received.hostSession == _hostSession && static_cast<int32_t>(received.hostTimeMs - _lastHostTimeMs) <= 0) {
                return;
            }
            _hasReport = true;
            _hostSession = received.hostSession;
            _lastHostTimeMs = received.hostTimeMs;
            // A phase change goes on reliably, as the host sent it.
            bool changed = false;
            for (uint8_t i = 0; i < received.count; ++i) {
                const auto &state = received.bridges[i];
                changed = changed || !_known[state.index] || _latest[state.index].phase != state.phase;
                _latest[state.index] = state;
                _known[state.index] = true;
            }
            const auto reliability = changed ? MafiaNet::Reliability::ReliableOrdered : MafiaNet::Reliability::Unreliable;
            auto snapshot = received;
            auto *peer = Framework::CoreModules::GetNetworkPeer();
            _players->ForEach([&](Shared::Entities::PlayerEntity *player) {
                // Dead or respawning players keep the bridge too: without
                // reports it closes, and opens again in a jump on respawn.
                if (player->missionGeneration == _missionGeneration) {
                    peer->SendRPC(snapshot, MafiaNet::ToGuid(_players->GuidForNetworkId(player->GetNetworkID())), MafiaNet::Priority::High, reliability);
                }
            });
        });
    }

    void BridgeService::SetMissionGeneration(uint64_t generation) {
        _missionGeneration = generation;
        _known.fill(false);
        _hasReport = false;
    }
} // namespace Mafia1Online::Features::Bridge
