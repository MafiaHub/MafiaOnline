#include "railway_service.h"

#include "features/combat/combat_service.h"
#include "features/player/player_service.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <networking/network_peer.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Railway {
    namespace {
        bool Finite(const Shared::Railway::Vector &vector) {
            return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
        }

        glm::vec3 Vec(const Shared::Railway::Vector &vector) {
            return {vector.x, vector.y, vector.z};
        }

        bool Valid(const Shared::Railway::State &state) {
            if (state.index >= Shared::Railway::kMaxRailways || state.wagonCount > Shared::Railway::kMaxWagons || !std::isfinite(state.speed) || !Finite(state.position) ||
                !Finite(state.direction)) {
                return false;
            }
            for (uint8_t i = 0; i < state.wagonCount; ++i) {
                if (!Finite(state.wagonPositions[i]) || !Finite(state.wagonDirections[i])) {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    void RailwayService::RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Combat::CombatService &combat) {
        _simulation = &simulation;
        _players = &players;
        _combat = &combat;
        RegisterBoarding();
        RegisterPassengers();
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Shared::Railway::Snapshot>([this](const Shared::Railway::Snapshot &snapshot, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), snapshot.missionGeneration)) {
                return;
            }
            for (uint8_t i = 0; i < snapshot.count; ++i) {
                const auto &state = snapshot.railways[i];
                if (!Valid(state)) {
                    return;
                }
            }
            for (uint8_t i = 0; i < snapshot.count; ++i) {
                const auto &state = snapshot.railways[i];
                _latest[state.index] = state;
                _known[state.index] = true;
            }
            Forward(snapshot);
        });
        network->RegisterRPC<Shared::Railway::PlayerHit>([this](const Shared::Railway::PlayerHit &hit, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), hit.missionGeneration) || hit.index >= Shared::Railway::kMaxRailways || !_known[hit.index] ||
                !std::isfinite(hit.damage) || hit.damage <= 0.0f || !Finite(hit.direction)) {
                return;
            }
            const auto *target = _players->FindByNetworkId(hit.targetNetworkId);
            // A rider stands inside it; the host's replica is not in the
            // wagon's collision list, so its front sweep can still reach it.
            if (!target || target->railway != Shared::Railway::kNoRailway) {
                return;
            }
            // The body or any wagon must be at the player.
            if (!(DistanceTo(hit.index, target->position) <= kHitReach)) {
                return;
            }
            if (_combat->ApplyRailwayHit(hit.targetNetworkId, hit.missionGeneration, hit.targetSpawnGeneration, hit.damage, Vec(hit.direction)) && _hit) {
                _hit(hit.targetNetworkId, hit.index, hit.damage);
            }
        });
        // A player's bullet on a railway part, as its screen drew it: to the
        // host, which applies it to its passengers at the same place.
        network->RegisterRPC<Shared::Railway::ShotHit>([this](const Shared::Railway::ShotHit &received, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            const auto host = _simulation->Host();
            if (!host || *host == sender || !ShotHitPlausible(received)) {
                return;
            }
            const Combat::CombatService::ShotClaim claim {received.shooterId, received.missionGeneration, received.shooterSpawnGeneration, received.shotSequence,
                                                         received.pelletIndex, Vec(received.direction), Vec(received.hit)};
            const auto evidence = _combat->InspectShot(claim, sender);
            if (!evidence || !_combat->ConsumeShot(claim, sender)) {
                return;
            }
            auto forwarded = received;
            forwarded.damage = evidence->damage;
            Framework::CoreModules::GetNetworkPeer()->SendRPC(forwarded, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        });
    }

    bool RailwayService::ShotHitPlausible(const Shared::Railway::ShotHit &shot) const {
        if (shot.missionGeneration != _missionGeneration || shot.index >= Shared::Railway::kMaxRailways || !_known[shot.index] ||
            shot.part > _latest[shot.index].wagonCount || !Finite(shot.direction) || !Finite(shot.hit) || !Finite(shot.localDirection) || !Finite(shot.localHit)) {
            return false;
        }
        // The local spot must be where the validated world hit is on the
        // part as the host last reported it, give or take how far it ran in
        // the shooter's render delay and trip (the second a rider's ride
        // allows): a real shot at the hull cannot be moved onto a rider.
        const auto &railway = _latest[shot.index];
        const auto &origin = shot.part == 0 ? railway.position : railway.wagonPositions[shot.part - 1];
        const auto &direction = shot.part == 0 ? railway.direction : railway.wagonDirections[shot.part - 1];
        return Shared::Railway::ShotHitMatchesPart(Shared::Railway::YawFrame(origin, direction), shot, railway.speed, kRiderLagSeconds);
    }

    float RailwayService::DistanceTo(uint8_t index, const glm::vec3 &position) const {
        const auto &railway = _latest[index];
        float nearest = glm::distance(Vec(railway.position), position);
        for (uint8_t i = 0; i < railway.wagonCount; ++i) {
            nearest = std::min(nearest, glm::distance(Vec(railway.wagonPositions[i]), position));
        }
        return nearest;
    }

    void RailwayService::RegisterPassengers() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Railway::Passengers>([this](const Shared::Railway::Passengers &passengers, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), passengers.missionGeneration)) {
                return;
            }
            for (uint8_t i = 0; i < passengers.railwayCount; ++i) {
                if (passengers.railways[i] >= Shared::Railway::kMaxRailways) {
                    return;
                }
            }
            for (uint8_t i = 0; i < passengers.count; ++i) {
                const auto &passenger = passengers.passengers[i];
                if (passenger.index >= Shared::Railway::kMaxRailways || passenger.part > Shared::Railway::kMaxWagons || !Finite(passenger.local) || !std::isfinite(passenger.yaw) ||
                    passenger.model.size() > Shared::Railway::Passengers::kMaxModelLength) {
                    return;
                }
            }
            auto *peer = Framework::CoreModules::GetNetworkPeer();
            Shared::Railway::Passengers nearby;
            nearby.missionGeneration = passengers.missionGeneration;
            _players->ForEach([&](Shared::Entities::PlayerEntity *player) {
                if (!player->spawned || player->missionGeneration != _missionGeneration) {
                    return;
                }
                std::array<bool, Shared::Railway::kMaxRailways> covered {};
                nearby.railwayCount = 0;
                for (uint8_t i = 0; i < passengers.railwayCount; ++i) {
                    const uint8_t index = passengers.railways[i];
                    if (!_known[index]) {
                        continue;
                    }
                    const glm::vec3 offset = Vec(_latest[index].position) - player->position;
                    if (offset.x * offset.x + offset.z * offset.z <= Shared::Railway::kStreamRange * Shared::Railway::kStreamRange) {
                        covered[index] = true;
                        nearby.railways[nearby.railwayCount++] = index;
                    }
                }
                nearby.count = 0;
                for (uint8_t i = 0; i < passengers.count; ++i) {
                    if (covered[passengers.passengers[i].index]) {
                        nearby.passengers[nearby.count++] = passengers.passengers[i];
                    }
                }
                if (nearby.railwayCount) {
                    peer->SendRPC(nearby, MafiaNet::ToGuid(_players->GuidForNetworkId(player->GetNetworkID())), MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
                }
            });
        });
    }

    void RailwayService::RegisterBoarding() {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Shared::Railway::BoardRequest>([this](const Shared::Railway::BoardRequest &received, MafiaNet::Packet *packet) {
            auto *player = _players->FindByGuid(MafiaNet::ToPeerGuid(packet->guid));
            const auto host = _simulation->Host();
            // Each request holds the host's railway: at most one a second.
            if (player) {
                const auto now = std::chrono::steady_clock::now();
                auto &last = _lastBoardRequest[player->GetNetworkID()];
                if (now - last < std::chrono::milliseconds(Shared::Railway::kBoardRequestIntervalMs)) {
                    return;
                }
                last = now;
            }
            if (!player || !host || player->GetNetworkID() != received.playerId || received.missionGeneration != _missionGeneration || !player->spawned || !player->alive ||
                received.index >= Shared::Railway::kMaxRailways || !_known[received.index] || !(DistanceTo(received.index, player->position) <= Shared::Railway::kBoardReach)) {
                Shared::Railway::BoardAnswer refused;
                refused.missionGeneration = received.missionGeneration;
                refused.index = received.index;
                refused.playerId = received.playerId;
                Framework::CoreModules::GetNetworkPeer()->SendRPC(refused, packet->guid, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
                return;
            }
            auto request = received;
            Framework::CoreModules::GetNetworkPeer()->SendRPC(request, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        });
        network->RegisterRPC<Shared::Railway::BoardAnswer>([this](const Shared::Railway::BoardAnswer &received, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), received.missionGeneration) || !_players->FindByNetworkId(received.playerId)) {
                return;
            }
            auto answer = received;
            Framework::CoreModules::GetNetworkPeer()->SendRPC(answer, MafiaNet::ToGuid(_players->GuidForNetworkId(received.playerId)), MafiaNet::Priority::High,
                                                              MafiaNet::Reliability::ReliableOrdered);
        });
        network->RegisterRPC<Shared::Railway::BoardDone>([this](const Shared::Railway::BoardDone &received, MafiaNet::Packet *packet) {
            auto *player = _players->FindByGuid(MafiaNet::ToPeerGuid(packet->guid));
            const auto host = _simulation->Host();
            if (!player || !host || player->GetNetworkID() != received.playerId || received.missionGeneration != _missionGeneration ||
                received.index >= Shared::Railway::kMaxRailways) {
                return;
            }
            auto done = received;
            Framework::CoreModules::GetNetworkPeer()->SendRPC(done, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        });
    }

    void RailwayService::SetMissionGeneration(uint64_t generation) {
        _missionGeneration = generation;
        _known.fill(false);
    }

    bool RailwayService::FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const {
        return _simulation && _simulation->Host() == sender && missionGeneration == _missionGeneration;
    }

    void RailwayService::Forward(const Shared::Railway::Snapshot &snapshot) {
        auto *peer = Framework::CoreModules::GetNetworkPeer();
        Shared::Railway::Snapshot nearby;
        nearby.missionGeneration = snapshot.missionGeneration;
        nearby.hostSession = snapshot.hostSession;
        nearby.hostTimeMs = snapshot.hostTimeMs;
        // Far railways go a slice at a time, each in one snapshot of
        // kFarSnapshotDivider as before, so no snapshot carries them all.
        const uint32_t farSlice = _forwarded++ % Shared::Railway::kFarSnapshotDivider;
        _players->ForEach([&](Shared::Entities::PlayerEntity *player) {
            if (!player->spawned || player->missionGeneration != _missionGeneration) {
                return;
            }
            nearby.count = 0;
            for (uint8_t i = 0; i < snapshot.count; ++i) {
                const auto &state = snapshot.railways[i];
                const glm::vec3 offset = Vec(state.position) - player->position;
                if (state.index % Shared::Railway::kFarSnapshotDivider == farSlice ||
                    offset.x * offset.x + offset.z * offset.z <= Shared::Railway::kStreamRange * Shared::Railway::kStreamRange) {
                    nearby.railways[nearby.count++] = state;
                }
            }
            if (nearby.count) {
                peer->SendRPC(nearby, MafiaNet::ToGuid(_players->GuidForNetworkId(player->GetNetworkID())), MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
            }
        });
    }
} // namespace Mafia1Online::Features::Railway
