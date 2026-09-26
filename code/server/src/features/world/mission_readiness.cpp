#include "mission_readiness.h"

#include "shared/features/world/mission_load_result.h"

#include <logging/logger.h>
#include <networking/network_peer.h>

namespace Mafia1Online::Features::World {
    void MissionReadiness::Register(Framework::Networking::NetworkPeer &network) {
        network.RegisterRPC<Shared::World::MissionLoadResult>([this](const Shared::World::MissionLoadResult &result, MafiaNet::Packet *packet) {
            OnResult(MafiaNet::ToPeerGuid(packet->guid), result.generation, static_cast<uint8_t>(result.state));
        });
    }

    void MissionReadiness::SetGeneration(uint64_t generation) {
        _generation = generation;
        for (auto &[guid, loadedGeneration] : _loadedByPeer) {
            loadedGeneration = 0;
        }
    }

    void MissionReadiness::OnConnect(MafiaNet::PeerGuid guid) {
        _loadedByPeer[static_cast<uint64_t>(guid)] = 0;
    }

    void MissionReadiness::OnDisconnect(MafiaNet::PeerGuid guid) {
        _loadedByPeer.erase(static_cast<uint64_t>(guid));
    }

    bool MissionReadiness::IsReady(MafiaNet::PeerGuid guid) const {
        const auto it = _loadedByPeer.find(static_cast<uint64_t>(guid));
        return it != _loadedByPeer.end() && _generation != 0 && it->second == _generation;
    }

    bool MissionReadiness::AllReady() const {
        if (_generation == 0) {
            return false;
        }
        for (const auto &[guid, loadedGeneration] : _loadedByPeer) {
            if (loadedGeneration != _generation) {
                return false;
            }
        }
        return true;
    }

    void MissionReadiness::OnResult(MafiaNet::PeerGuid guid, uint64_t generation, uint8_t state) {
        const auto it = _loadedByPeer.find(static_cast<uint64_t>(guid));
        if (it == _loadedByPeer.end() || generation != _generation || state > static_cast<uint8_t>(Shared::World::MissionLoadState::GameInitFailed)) {
            return;
        }
        if (state == static_cast<uint8_t>(Shared::World::MissionLoadState::Ready)) {
            if (it->second == generation) {
                return;
            }
            it->second = generation;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} loaded Mafia 1 mission generation {}", static_cast<uint64_t>(guid), generation);
        }
        else {
            it->second = 0;
            if (state != static_cast<uint8_t>(Shared::World::MissionLoadState::Unloaded)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Player {} failed Mafia 1 mission generation {} (state {})", static_cast<uint64_t>(guid), generation, state);
            }
        }
        if (_resultCallback) {
            _resultCallback(guid, generation, state);
        }
    }
} // namespace Mafia1Online::Features::World
