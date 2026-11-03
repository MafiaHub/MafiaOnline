#include "props_service.h"

#include "features/player/player_service.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <glm/geometric.hpp>

#include <algorithm>

namespace Mafia1Online::Features::World {
    namespace Wire = Shared::World::Props;

    void PropsService::RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players) {
        _simulation = &simulation;
        _players = &players;
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Wire::BreakReport>([this](const Wire::BreakReport &received, MafiaNet::Packet *packet) {
            OnBreak(received, MafiaNet::ToPeerGuid(packet->guid));
        });
        network->RegisterRPC<Wire::SignBreakReport>([this](const Wire::SignBreakReport &received, MafiaNet::Packet *packet) {
            OnSignBreak(received, MafiaNet::ToPeerGuid(packet->guid));
        });
        network->RegisterRPC<Wire::PiecesReport>([this](const Wire::PiecesReport &received, MafiaNet::Packet *packet) {
            OnPieces(received, MafiaNet::ToPeerGuid(packet->guid));
        });
        network->RegisterRPC<Wire::RestReport>([this](const Wire::RestReport &received, MafiaNet::Packet *packet) {
            OnRest(received, MafiaNet::ToPeerGuid(packet->guid));
        });
        network->RegisterRPC<Wire::SnapshotRequest>([this](const Wire::SnapshotRequest &request, MafiaNet::Packet *packet) {
            OnSnapshotRequest(request, packet);
        });
    }

    std::optional<uint64_t> PropsService::SenderKey(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const {
        if (missionGeneration == 0 || missionGeneration != _missionGeneration) {
            return std::nullopt;
        }
        if (_simulation->Host() == sender) {
            return 0;
        }
        auto *player = _players->FindByGuid(sender);
        if (!player || player->missionGeneration != _missionGeneration) {
            return std::nullopt;
        }
        return player->GetNetworkID();
    }

    bool PropsService::Accept(MafiaNet::PeerGuid sender, uint64_t key, const Wire::Vector &place) {
        if (key != 0) {
            auto *player = _players->FindByGuid(sender);
            if (!player || !(glm::distance(player->position, glm::vec3(place.x, place.y, place.z)) <= Wire::kReportReach)) {
                return false;
            }
        }
        auto &budget = _budgets[key];
        const auto now = std::chrono::steady_clock::now();
        if (now - budget.window >= std::chrono::seconds(1)) {
            budget.window = now;
            budget.count = 0;
        }
        return ++budget.count <= kReportsPerSecond;
    }

    void PropsService::OnBreak(const Wire::BreakReport &received, MafiaNet::PeerGuid sender) {
        const auto key = SenderKey(sender, received.missionGeneration);
        if (!key || !received.prop.Valid() || !Accept(sender, *key, received.prop.position)) {
            return;
        }
        // The first break stands; the others' are the same prop again.
        auto &standing = _brokenBy[*key];
        if (_broken.size() >= Wire::kMaxBroken || (*key != 0 && standing >= Wire::kMaxBrokenPerPlayer) || !_brokenNames.insert(received.prop.name).second) {
            return;
        }
        ++standing;
        auto report = received;
        report.prop.Clamp();
        report.prop.pieces.clear();
        _broken.push_back(report.prop);
        _brokenOwners.push_back(*key);
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void PropsService::OnSignBreak(const Wire::SignBreakReport &received, MafiaNet::PeerGuid sender) {
        // Struck where the native code finds that sign, by a player near it
        // (the simulation host anywhere); the clients check the placement
        // and the primitive against their own cache.bin and tree.
        const auto key = SenderKey(sender, received.missionGeneration);
        if (!key || !received.sign.Valid() || !Accept(sender, *key, received.sign.position)) {
            return;
        }
        auto &standing = _brokenBy[*key];
        if (_signs.size() >= Wire::kMaxSigns || (*key != 0 && standing >= Wire::kMaxBrokenPerPlayer) || !_signKeys.insert(received.sign.Key()).second) {
            return;
        }
        ++standing;
        auto report = received;
        report.sign.Clamp();
        report.sign.pieces.clear();
        _signs.push_back(report.sign);
        _signOwners.push_back(*key);
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void PropsService::OnPieces(const Wire::PiecesReport &received, MafiaNet::PeerGuid sender) {
        // Once, from the client whose break stood, near where it broke.
        const auto key = SenderKey(sender, received.missionGeneration);
        if (!key) {
            return;
        }
        std::vector<Wire::Pose> *pieces = nullptr;
        const Wire::Vector *place = nullptr;
        if (received.sign) {
            for (size_t i = 0; i < _signs.size(); ++i) {
                if (_signs[i].Key() == received.signKey && _signOwners[i] == *key) {
                    pieces = &_signs[i].pieces;
                    place = &_signs[i].position;
                }
            }
        } else {
            for (size_t i = 0; i < _broken.size(); ++i) {
                if (_broken[i].name == received.name && _brokenOwners[i] == *key) {
                    pieces = &_broken[i].pieces;
                    place = &_broken[i].position;
                }
            }
        }
        if (!pieces || !pieces->empty() || !Wire::ValidPieces(received.pieces, *place) || !Accept(sender, *key, *place)) {
            return;
        }
        *pieces = received.pieces;
        auto report = received;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report, MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
    }

    void PropsService::OnRest(const Wire::RestReport &received, MafiaNet::PeerGuid sender) {
        const auto key = SenderKey(sender, received.missionGeneration);
        if (!key || !received.pose.Valid()) {
            return;
        }
        // Load indices of this mission's props agree on every client, and
        // an index keeps the name hash it first came with.
        const auto &pose = received.pose;
        if (const auto hash = _nameHashes.find(pose.index); hash != _nameHashes.end() && hash->second != pose.nameHash) {
            return;
        }
        // The reporter is near where the prop was, and one rest moves it
        // only so far. A prop's first move is checked by the clients, which
        // know where the mission placed it.
        const auto last = _poses.find(pose.index);
        if (last != _poses.end() && Wire::Distance(last->second.pose.position, pose.position) > Wire::kMaxPropTravel) {
            return;
        }
        if (!Accept(sender, *key, last != _poses.end() ? last->second.pose.position : pose.position)) {
            return;
        }
        _nameHashes.emplace(pose.index, pose.nameHash);
        _poses[pose.index] = {pose, received.home};
        auto report = received;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report, MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
    }

    void PropsService::OnSnapshotRequest(const Wire::SnapshotRequest &request, MafiaNet::Packet *packet) {
        const auto sender = MafiaNet::ToPeerGuid(packet->guid);
        const bool host = _simulation->Host() == sender;
        if (request.missionGeneration != _missionGeneration || (!host && !_players->FindByGuid(sender))) {
            return;
        }
        // A whole snapshot is up to some 350 KB, reliable.
        auto &budget = _snapshots[static_cast<uint64_t>(sender)];
        const auto now = std::chrono::steady_clock::now();
        if (budget.count >= kSnapshotsPerMission || (budget.count != 0 && now - budget.last < kSnapshotRetry)) {
            return;
        }
        ++budget.count;
        budget.last = now;
        Wire::Snapshot snapshot;
        snapshot.missionGeneration = _missionGeneration;
        snapshot.broken = _broken;
        snapshot.signs = _signs;
        snapshot.moved.reserve(_poses.size());
        for (const auto &[index, resting] : _poses) {
            // A late joiner has the mission's own pose already.
            if (!resting.home) {
                snapshot.moved.push_back(resting.pose);
            }
        }
        Framework::CoreModules::GetNetworkPeer()->SendRPC(snapshot, packet->guid, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    size_t PropsService::MovedCount() const {
        return static_cast<size_t>(std::count_if(_poses.begin(), _poses.end(), [](const auto &entry) { return !entry.second.home; }));
    }

    void PropsService::SetMissionGeneration(uint64_t generation) {
        _missionGeneration = generation;
        _broken.clear();
        _brokenOwners.clear();
        _brokenNames.clear();
        _signs.clear();
        _signOwners.clear();
        _signKeys.clear();
        _brokenBy.clear();
        _poses.clear();
        _nameHashes.clear();
        _budgets.clear();
        _snapshots.clear();
    }

    size_t PropsService::Repair() {
        const size_t count = BrokenCount() + MovedCount();
        _broken.clear();
        _brokenOwners.clear();
        _brokenNames.clear();
        _signs.clear();
        _signOwners.clear();
        _signKeys.clear();
        _brokenBy.clear();
        _poses.clear();
        // Clients put back what they have; one still loading the mission
        // asks for the (now empty) state when it is done.
        Wire::Repair repair;
        repair.missionGeneration = _missionGeneration;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(repair, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("World props repaired ({} broken or moved)", count);
        return count;
    }
} // namespace Mafia1Online::Features::World
