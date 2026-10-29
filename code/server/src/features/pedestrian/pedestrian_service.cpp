#include "pedestrian_service.h"

#include "features/car/car_service.h"
#include "features/player/player_service.h"
#include "shared/features/car/car_entity.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/pedestrian/pedestrian_control.h"
#include "shared/features/pedestrian/pedestrian_entity.h"
#include "shared/features/pedestrian/pedestrian_report.h"
#include "shared/features/pedestrian/pedestrian_scream.h"
#include "shared/features/player/player_entity.h"
#include "shared/net/sample_clock.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace Mafia1Online::Features::Pedestrian {
    namespace {
        bool Finite(float x, float y, float z, float yaw) {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(yaw);
        }

        void SetPose(Shared::Entities::PedestrianEntity &entity, float x, float y, float z, float yaw, float speed) {
            entity.position = {x, y, z};
            entity.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            entity.velocity = {std::sin(yaw) * speed, 0.0f, std::cos(yaw) * speed};
        }
    } // namespace

    void PedestrianService::RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Car::CarService &cars) {
        _simulation = &simulation;
        _players = &players;
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Shared::Pedestrian::CarImpact>([this, &cars](const Shared::Pedestrian::CarImpact &impact, MafiaNet::Packet *packet) {
            const auto host = _simulation->Host();
            const auto key = _keyByNetworkId.find(impact.walkerId);
            auto *car = cars.Find(impact.carId);
            if (!host || impact.missionGeneration != _missionGeneration || key == _keyByNetworkId.end() || !car ||
                car->terminalState != Shared::Entities::CarEntity::TerminalState::Active || car->missionGeneration != _missionGeneration ||
                car->simulationControllerGuid == 0 || car->simulationControllerGuid != static_cast<uint64_t>(MafiaNet::ToPeerGuid(packet->guid))) {
                return;
            }
            const auto *walker = _walkers.at(key->second).entity;
            const glm::vec3 direction(impact.directionX, impact.directionY, impact.directionZ);
            const float speed = glm::length(direction);
            const float dx = walker->position.x - car->position.x;
            const float dz = walker->position.z - car->position.z;
            if (!walker->alive || !std::isfinite(speed) || speed > kMaxImpactSpeed || dx * dx + dz * dz > kCarImpactReach * kCarImpactReach) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (const auto last = _lastCarImpact.find(impact.walkerId); last != _lastCarImpact.end() && now - last->second < kCarImpactCooldown) {
                return;
            }
            _lastCarImpact[impact.walkerId] = now;
            Shared::Pedestrian::Impact forwarded;
            forwarded.missionGeneration = _missionGeneration;
            forwarded.key = key->second;
            forwarded.carId = impact.carId;
            forwarded.directionX = direction.x;
            forwarded.directionY = direction.y;
            forwarded.directionZ = direction.z;
            Framework::CoreModules::GetNetworkPeer()->SendRPC(forwarded, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        });
        network->RegisterRPC<Shared::Pedestrian::Scream>([this](const Shared::Pedestrian::Scream &received, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), received.missionGeneration) || !Finite(received.x, received.y, received.z, 0.0f) ||
                !std::isfinite(received.maximumRange) || received.maximumRange < 0.0f) {
                return;
            }
            auto scream = received;
            const float reach = scream.maximumRange + kScreamRangeMargin;
            auto *peer = Framework::CoreModules::GetNetworkPeer();
            _players->ForEach([&](Shared::Entities::PlayerEntity *player) {
                const float dx = player->position.x - scream.x;
                const float dz = player->position.z - scream.z;
                if (dx * dx + dz * dz <= reach * reach) {
                    peer->SendRPC(scream, MafiaNet::ToGuid(_players->GuidForNetworkId(player->GetNetworkID())), MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
                }
            });
        });
        network->RegisterRPC<Shared::Pedestrian::Spawned>([this](const Shared::Pedestrian::Spawned &spawned, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), spawned.missionGeneration) || _walkers.contains(spawned.key) ||
                _walkers.size() >= kMaxWalkers || spawned.model.empty() || spawned.model.size() > Shared::Entities::PedestrianEntity::kMaxModelLength ||
                !Finite(spawned.x, spawned.y, spawned.z, spawned.yaw)) {
                return;
            }
            auto *entity = Framework::CoreModules::GetReplication()->CreateEntity<Shared::Entities::PedestrianEntity>();
            entity->model = spawned.model;
            entity->missionGeneration = spawned.missionGeneration;
            entity->generator = spawned.generator;
            entity->walkStyle = spawned.walkStyle;
            entity->umbrella = spawned.umbrella;
            entity->streaming.range = kStreamRange;
            entity->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
            SetPose(*entity, spawned.x, spawned.y, spawned.z, spawned.yaw, 0.0f);
            _walkers[spawned.key] = {entity, std::chrono::steady_clock::now()};
            _keyByNetworkId[entity->GetNetworkID()] = spawned.key;
            if (_events.spawn) {
                _events.spawn(*entity);
            }
        });
        network->RegisterRPC<Shared::Pedestrian::Snapshot>([this](const Shared::Pedestrian::Snapshot &snapshot, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), snapshot.missionGeneration)) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            for (uint8_t i = 0; i < snapshot.count; ++i) {
                const auto &pose = snapshot.walkers[i];
                const auto walker = _walkers.find(pose.key);
                if (walker == _walkers.end() || !Finite(pose.x, pose.y, pose.z, pose.yaw) || !std::isfinite(pose.speed)) {
                    continue;
                }
                auto &entity = *walker->second.entity;
                // An unchanged pose keeps its clock, so a standing walker or
                // a corpse is not sent again every snapshot.
                Shared::Net::RelayPose(entity, snapshot.hostClockMs, [&] {
                    SetPose(entity, pose.x, pose.y, pose.z, pose.yaw, entity.alive && !pose.dead ? pose.speed : 0.0f);
                });
                entity.poseSource = MafiaNet::ToPeerGuid(packet->guid);
                entity.animation = pose.animation;
                entity.animationFlag = pose.animationFlag;
                // A dropped umbrella never comes back to that life.
                entity.umbrella = entity.umbrella && pose.umbrella;
                entity.alpha = pose.alpha;
                walker->second.lastSeen = now;
                if (pose.dead && entity.alive) {
                    entity.alive = false;
                    if (_events.death) {
                        _events.death(entity, walker->second.lastAttackerId);
                    }
                }
            }
        });
        network->RegisterRPC<Shared::Pedestrian::Damaged>([this](const Shared::Pedestrian::Damaged &damaged, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), damaged.missionGeneration) || !std::isfinite(damaged.damage) ||
                !std::isfinite(damaged.healthBefore) || !std::isfinite(damaged.healthAfter)) {
                return;
            }
            const auto walker = _walkers.find(damaged.key);
            if (walker == _walkers.end()) {
                return;
            }
            if (damaged.attackerId) {
                walker->second.lastAttackerId = damaged.attackerId;
            }
            if (_events.damage) {
                _events.damage(*walker->second.entity, damaged.attackerId, damaged.hitType, damaged.damage, damaged.healthBefore, damaged.healthAfter);
            }
        });
        network->RegisterRPC<Shared::Pedestrian::Retired>([this](const Shared::Pedestrian::Retired &retired, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), retired.missionGeneration)) {
                return;
            }
            if (const auto walker = _walkers.find(retired.key); walker != _walkers.end()) {
                Retire(walker);
            }
        });
    }

    bool PedestrianService::FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const {
        return _simulation->Host() == sender && missionGeneration == _missionGeneration;
    }

    void PedestrianService::SetMissionGeneration(uint64_t generation) {
        if (generation != _missionGeneration) {
            Clear();
            _missionGeneration = generation;
        }
    }

    void PedestrianService::Update() {
        if (!_simulation->Host()) {
            Clear();
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        for (auto it = _walkers.begin(); it != _walkers.end();) {
            auto current = it++;
            if (now - current->second.lastSeen > kStaleAfter) {
                Retire(current);
            }
        }
    }

    void PedestrianService::Retire(std::unordered_map<uint32_t, Walker>::iterator walker) {
        auto *entity = walker->second.entity;
        if (_events.retire) {
            _events.retire(*entity);
        }
        _keyByNetworkId.erase(entity->GetNetworkID());
        _lastCarImpact.erase(entity->GetNetworkID());
        _walkers.erase(walker);
        Framework::CoreModules::GetReplication()->DestroyEntity(entity);
    }

    void PedestrianService::Clear() {
        if (_walkers.empty()) {
            return;
        }
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            for (const auto &[key, walker] : _walkers) {
                replication->DestroyEntity(walker.entity);
            }
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Removed {} pedestrians", _walkers.size());
        _walkers.clear();
        _keyByNetworkId.clear();
        _lastCarImpact.clear();
    }

    void PedestrianService::OnHostAttached() {
        SendSettings();
    }

    bool PedestrianService::SetSettings(const Settings &settings) {
        if (!(settings.density >= 0.0f && settings.density <= 1.0f) || settings.corpseLifetimeMs > kMaxCorpseLifetimeMs) {
            return false;
        }
        _settings = settings;
        SendSettings();
        return true;
    }

    void PedestrianService::SendSettings() const {
        const auto host = _simulation->Host();
        if (!host) {
            return;
        }
        Shared::Pedestrian::Control control;
        control.enabled = _settings.enabled;
        control.density = _settings.density;
        control.corpseLifetimeMs = _settings.corpseLifetimeMs;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(control, MafiaNet::ToGuid(*host));
    }

    bool PedestrianService::Remove(uint64_t networkId) {
        const auto key = _keyByNetworkId.find(networkId);
        const auto host = _simulation->Host();
        if (key == _keyByNetworkId.end() || !host) {
            return false;
        }
        Shared::Pedestrian::Remove remove;
        remove.missionGeneration = _missionGeneration;
        remove.key = key->second;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(remove, MafiaNet::ToGuid(*host));
        return true;
    }

    uint64_t PedestrianService::NetworkIdOfKey(uint32_t key) const {
        const auto walker = _walkers.find(key);
        return walker == _walkers.end() ? 0 : walker->second.entity->GetNetworkID();
    }

    Shared::Entities::PedestrianEntity *PedestrianService::Find(uint64_t networkId) const {
        const auto key = _keyByNetworkId.find(networkId);
        if (key == _keyByNetworkId.end()) {
            return nullptr;
        }
        const auto walker = _walkers.find(key->second);
        return walker == _walkers.end() ? nullptr : walker->second.entity;
    }

    void PedestrianService::ForEach(const std::function<void(Shared::Entities::PedestrianEntity &)> &visit) const {
        for (const auto &[key, walker] : _walkers) {
            visit(*walker.entity);
        }
    }
} // namespace Mafia1Online::Features::Pedestrian
