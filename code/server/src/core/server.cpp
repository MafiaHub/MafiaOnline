#include "server.h"

#include "shared/features/car/car_engine_state.h"
#include "shared/features/car/car_damage_state.h"
#include "shared/features/car/car_debris.h"
#include "shared/features/car/car_explosion_intent.h"
#include "shared/features/car/car_siren_intent.h"
#include "shared/features/car/car_terminal_intent.h"
#include "shared/features/car/car_hit_report.h"
#include "shared/features/car/car_load_failure.h"
#include "shared/features/car/car_movement.h"
#include "shared/features/car/car_mesh_checkpoint.h"
#include "shared/features/car/seat_action.h"
#include "shared/features/chat/text_policy.h"
#include "shared/features/door/door_intent.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/world/mission_catalog.h"
#include "shared/features/world/mission_entity.h"
#include "shared/register_entities.h"

#include "features/script/script_events.h"
#include "features/script/script_runtime.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <glm/geometric.hpp>

#include <limits>
#include <networking/replication/replication_manager.h>
#include <networking/rpc/chat_message.h>

#include <cmath>
#include <limits>

namespace Mafia1Online::Core {
    void Server::PostInit() {
        Shared::Entities::RegisterEntities();
        _mission                               = GetModConfig().Get<std::string>("mission");
        _missionState                          = Framework::CoreModules::GetReplication()->CreateEntity<Shared::Entities::MissionEntity>();
        _missionState->mission                 = _mission;
        _missionState->streaming.alwaysVisible = true;
        _missionState->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        _players.RegisterRPC();
        _players.ResetForMission(_missionState->generation);
        _worldScript.Init(_players, _cars);
        _worldScript.ResetForMission(_missionState->generation);
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Door::DoorIntent>([this](const Shared::Door::DoorIntent &intent, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            if (_missionReadiness.IsReady(sender) && _worldScript.ApplyDoorIntent(intent, static_cast<uint64_t>(sender), MissionGeneration())) {
                if (auto *door = _worldScript.FindDoor(intent.frameName)) {
                    Features::Script::EmitDoorEvent(*this, "doorStateChange", *door, intent.playerId);
                }
            }
        });
        _combat.RegisterRPC(_players, _cars);
        _pickups.RegisterRPC(_players, _combat, _cars);
        _combat.SetDropCallback([this](uint64_t playerId, uint8_t weaponId, uint16_t loaded, uint16_t reserve, const glm::vec3 &position, float yaw) {
            return _pickups.Create(weaponId, position, yaw, loaded, reserve, MissionGeneration(), std::chrono::seconds(120));
        });
        _combat.SetDroppedCallback([this](uint64_t pickupId, uint64_t playerId) {
            Features::Script::EmitPickupEvent(*this, "weaponDropped", pickupId, playerId);
        });
        _pickups.SetTakenCallback([this](uint64_t pickupId, uint64_t playerId, uint8_t) {
            Features::Script::EmitPickupEvent(*this, "pickupTaken", pickupId, playerId);
        });
        _combat.SetTransitionCallback([this](uint64_t playerId, uint64_t sourceId, std::optional<uint8_t> weaponId,
                                             Features::Combat::DamageCause cause, float oldHealth, float newHealth, uint16_t deathAnimation) {
            if (newHealth <= 0.0f && oldHealth > 0.0f) {
                if (auto *player = _players.FindByNetworkId(playerId)) {
                    _cars.ClearOccupant(playerId, player->spawnGeneration);
                }
            }
            Features::Script::EmitPlayerHealthEvent(*this, playerId, sourceId, weaponId, cause, oldHealth, newHealth, deathAnimation);
        });
        _combat.SetActionCallback([this](const Shared::Combat::Event &event) {
            Features::Script::EmitCombatActionEvent(*this, event);
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::LoadFailure>([this](const Shared::Car::LoadFailure &failure, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            auto *car         = _cars.Find(failure.networkId);
            if (!_players.FindByGuid(sender) || !_missionReadiness.IsReady(sender) || !car || failure.missionGeneration != MissionGeneration() || car->missionGeneration != failure.missionGeneration) {
                return;
            }
            auto &failed = _carLoadFailures[failure.networkId];
            failed.insert(static_cast<uint64_t>(sender));
            if (car->simulationControllerGuid == static_cast<uint64_t>(sender)) {
                uint64_t replacementGuid = 0;
                _players.ForEach([&](Shared::Entities::PlayerEntity *candidate) {
                    const auto candidateGuid = static_cast<uint64_t>(_players.GuidForNetworkId(candidate->GetNetworkID()));
                    if (replacementGuid == 0 && !failed.contains(candidateGuid) &&
                        _missionReadiness.IsReady(static_cast<MafiaNet::PeerGuid>(candidateGuid))) {
                        replacementGuid = candidateGuid;
                    }
                });
                _cars.SetController(failure.networkId, replacementGuid);
            }
            if (_reportedCarLoadFailures.insert(failure.networkId).second) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Client {} could not load network car {} (model '{}'); car remains server-owned", static_cast<uint64_t>(sender), failure.networkId, car->model);
                if (auto *player = _players.FindByGuid(sender)) {
                    SendNotice(player->GetNetworkID(), "Car model could not load on this client; see client log");
                }
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::Movement>([this](const Shared::Car::Movement &movement, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            if (_players.FindByGuid(sender) && _missionReadiness.IsReady(sender) && movement.missionGeneration == MissionGeneration()) {
                auto *car = _cars.Find(movement.networkId);
                const int32_t oldGear = car ? car->gear : 0;
                const uint32_t oldLights = car ? car->lightState : 0;
                const bool oldHorn = car && car->hornOn;
                const float oldFuel = car ? car->fuel : 0.0f;
                const bool hadDynamics = car && car->dynamicsValid;
                if (_cars.ApplyMovement(movement, static_cast<uint64_t>(sender)) && car && hadDynamics) {
                    if (car->gear != oldGear) {
                        Features::Script::EmitCarEvent(*this, "vehicleGearChange", *car);
                    }
                    if (car->lightState != oldLights) {
                        Features::Script::EmitCarEvent(*this, "vehicleLightsChange", *car);
                    }
                    if (car->hornOn != oldHorn) {
                        Features::Script::EmitCarEvent(*this, "vehicleHornChange", *car);
                    }
                    // Fuel changes continuously in native physics. Notify
                    // scripts at quarter-unit boundaries instead of 20 Hz.
                    if (std::floor(car->fuel * 4.0f) != std::floor(oldFuel * 4.0f)) {
                        Features::Script::EmitCarEvent(*this, "vehicleFuelChange", *car);
                    }
                }
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::EngineState>([this](const Shared::Car::EngineState &state, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            if (_players.FindByGuid(sender) && _missionReadiness.IsReady(sender) && state.missionGeneration == MissionGeneration()) {
                auto *car        = _cars.Find(state.networkId);
                const bool oldOn = car && car->engineOn;
                if (_cars.ApplyEngineState(state, static_cast<uint64_t>(sender)) && car && oldOn != car->engineOn) {
                    Features::Script::EmitCarEvent(*this, "vehicleEngineChange", *car);
                }
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::DamageReport>([this](const Shared::Car::DamageReport &report, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            if (_players.FindByGuid(sender) && _missionReadiness.IsReady(sender) && report.missionGeneration == MissionGeneration() &&
                _cars.ApplyDamageReport(report, static_cast<uint64_t>(sender))) {
                if (auto *car = _cars.Find(report.networkId)) {
                    Features::Script::EmitCarEvent(*this, "vehicleDamage", *car);
                }
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::DebrisSpawnReport>([this](const Shared::Car::DebrisSpawnReport &report, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            if (_players.FindByGuid(sender) && _missionReadiness.IsReady(sender) &&
                report.missionGeneration == MissionGeneration()) {
                if (auto *debris = _debris.ApplySpawn(report, static_cast<uint64_t>(sender), MissionGeneration(), _cars)) {
                    Features::Script::EmitCarDebrisEvent(*this, "vehiclePartDetached", *debris);
                }
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::DebrisMovement>([this](const Shared::Car::DebrisMovement &movement, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            if (_players.FindByGuid(sender) && _missionReadiness.IsReady(sender) &&
                movement.missionGeneration == MissionGeneration()) {
                _debris.ApplyMovement(movement, static_cast<uint64_t>(sender));
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::DebrisGone>([this](const Shared::Car::DebrisGone &gone, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            if (_players.FindByGuid(sender) && _missionReadiness.IsReady(sender) &&
                gone.missionGeneration == MissionGeneration()) {
                _debris.ApplyGone(gone, static_cast<uint64_t>(sender));
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::HitReport>([this](const Shared::Car::HitReport &report, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            auto *car = _cars.Find(report.carId);
            if (!_players.FindByGuid(sender) || !_missionReadiness.IsReady(sender) ||
                report.missionGeneration != MissionGeneration() || !car ||
                car->missionGeneration != report.missionGeneration || car->simulationControllerGuid == 0 ||
                car->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
                return;
            }
            const auto evidence = _combat.InspectCarImpact(report, sender);
            if (!evidence || evidence->damage <= 0.0f ||
                !_cars.ValidateHitGeometry(report, evidence->acceptedAt) ||
                !_combat.ConsumeCarImpact(report, sender)) {
                return;
            }
            Shared::Car::AuthoritativeHit hit;
            hit.carId = report.carId;
            hit.missionGeneration = report.missionGeneration;
            hit.controllerGuid = car->simulationControllerGuid;
            hit.serverSequence = ++_carHitSequence;
            hit.damage = evidence->damage;
            hit.directionX = report.directionX;
            hit.directionY = report.directionY;
            hit.directionZ = report.directionZ;
            hit.hitX = report.hitX;
            hit.hitY = report.hitY;
            hit.hitZ = report.hitZ;
            hit.normalX = report.normalX;
            hit.normalY = report.normalY;
            hit.normalZ = report.normalZ;
            hit.localHitX = report.localHitX;
            hit.localHitY = report.localHitY;
            hit.localHitZ = report.localHitZ;
            hit.localDirectionX = report.localDirectionX;
            hit.localDirectionY = report.localDirectionY;
            hit.localDirectionZ = report.localDirectionZ;
            Framework::CoreModules::GetNetworkPeer()->SendRPC(hit, MafiaNet::ToGuid(static_cast<MafiaNet::PeerGuid>(hit.controllerGuid)));
            Features::Script::EmitCarHitEvent(*this, report, hit.damage);
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::MeshReportChunk>([this](const Shared::Car::MeshReportChunk &chunk, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            if (_players.FindByGuid(sender) && _missionReadiness.IsReady(sender) &&
                chunk.missionGeneration == MissionGeneration() &&
                _cars.ApplyMeshReportChunk(chunk, static_cast<uint64_t>(sender))) {
                if (auto *car = _cars.Find(chunk.networkId)) {
                    Features::Script::EmitCarEvent(*this, "vehicleDamage", *car);
                }
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::MeshCheckpointRequest>([this](const Shared::Car::MeshCheckpointRequest &request, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            if (_players.FindByGuid(sender) && _missionReadiness.IsReady(sender) &&
                request.missionGeneration == MissionGeneration()) {
                _cars.SendMeshCheckpoint(request.networkId, static_cast<uint64_t>(sender));
            }
        });
        // Only the current simulation controller may report a native terminal
        // outcome. Occupants die through server combat before the terminal
        // state clears their seats.
        const auto applyTerminal = [this](uint64_t networkId, uint64_t missionGeneration, MafiaNet::PeerGuid sender,
                                          Shared::Entities::CarEntity::TerminalState terminal) {
            auto *car = _cars.Find(networkId);
            if (!_players.FindByGuid(sender) || !_missionReadiness.IsReady(sender) || !car || car->missionGeneration != MissionGeneration() || missionGeneration != car->missionGeneration || car->simulationControllerGuid != static_cast<uint64_t>(sender)
                || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
                return;
            }
            // Capture occupant identities before committing the terminal state,
            // which clears all seats. CombatService chooses and publishes each
            // victim's authoritative death animation.
            const auto occupantIds         = car->occupantIds;
            const auto occupantGenerations = car->occupantGenerations;
            const uint8_t seatCount        = car->seatCount;
            for (uint8_t seat = 0; seat < seatCount; ++seat) {
                if (auto *player = _players.FindByNetworkId(occupantIds[seat]); player && occupantIds[seat] != 0 && player->spawnGeneration == occupantGenerations[seat]) {
                    _combat.SetHealth(player->GetNetworkID(), 0.0f, 0, std::nullopt, Features::Combat::DamageCause::Vehicle);
                }
            }
            if (_cars.SetTerminalState(networkId, terminal)) {
                if (terminal == Shared::Entities::CarEntity::TerminalState::Exploded) {
                    _debris.NoteExploded(networkId);
                }
                Features::Script::EmitCarEvent(*this, "vehicleTerminal", *_cars.Find(networkId));
            }
        };
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::ExplosionIntent>([applyTerminal](const Shared::Car::ExplosionIntent &intent, MafiaNet::Packet *packet) {
            applyTerminal(intent.networkId, intent.missionGeneration, MafiaNet::ToPeerGuid(packet->guid), Shared::Entities::CarEntity::TerminalState::Exploded);
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::TerminalIntent>([applyTerminal](const Shared::Car::TerminalIntent &intent, MafiaNet::Packet *packet) {
            using State = Shared::Entities::CarEntity::TerminalState;
            const auto state = static_cast<State>(intent.state);
            if (state == State::Submerged || state == State::OutOfBounds) {
                applyTerminal(intent.networkId, intent.missionGeneration, MafiaNet::ToPeerGuid(packet->guid), state);
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::SirenIntent>([this](const Shared::Car::SirenIntent &intent, MafiaNet::Packet *packet) {
            auto *player = _players.FindByGuid(MafiaNet::ToPeerGuid(packet->guid));
            auto *car    = _cars.Find(intent.networkId);
            if (!player || !car || !player->alive || !_missionReadiness.IsReady(MafiaNet::ToPeerGuid(packet->guid)) || intent.missionGeneration != MissionGeneration() ||
                car->missionGeneration != MissionGeneration() || intent.spawnGeneration != player->spawnGeneration || car->occupantIds[0] != player->GetNetworkID() ||
                car->occupantGenerations[0] != player->spawnGeneration || car->sirenOn == intent.on) {
                return;
            }
            if (_cars.SetSiren(intent.networkId, intent.on)) {
                Features::Script::EmitCarEvent(*this, "vehicleSirenChange", *car);
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::SeatIntent>([this](const Shared::Car::SeatIntent &intent, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            auto *player      = _players.FindByGuid(sender);
            if (!player || !_missionReadiness.IsReady(sender)) {
                return;
            }
            if (auto event = _cars.ApplySeatIntent(intent, *player, MissionGeneration())) {
                Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(*event);
                if (const auto evicted = _cars.TakeEviction()) {
                    // Scripts see the thrown occupant leave before the steal.
                    auto exit            = *event;
                    exit.playerId        = evicted->playerId;
                    exit.spawnGeneration = evicted->playerGeneration;
                    exit.action          = Shared::Car::SeatAction::Exit;
                    Features::Script::EmitSeatEvent(*this, exit);
                }
                Features::Script::EmitSeatEvent(*this, *event);
            }
        });
        _missionReadiness.Register(*Framework::CoreModules::GetNetworkPeer());
        _missionReadiness.SetResultCallback([this](MafiaNet::PeerGuid guid, uint64_t generation, uint8_t state) {
            if (auto *player = _players.FindByGuid(guid)) {
                Features::Script::EmitMissionLoadEvent(*this, player->GetNetworkID(), generation, state);
            }
        });
        _missionReadiness.SetGeneration(_missionState->generation);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Configured Mafia 1 mission: {}", _mission);
    }

    void Server::PostUpdate() {
        _players.Update();
        _combat.Update();
        _worldScript.Update();
        _pickups.Update();
        UpdateCarControllers();
        _debris.PruneMissingCars(_cars);
        if (!_allReadyNotified && AllPlayersReady()) {
            bool hasPlayers = false;
            _players.ForEach([&](Shared::Entities::PlayerEntity *) {
                hasPlayers = true;
            });
            if (hasPlayers) {
                _allReadyNotified = true;
                Features::Script::EmitMissionEvent(*this, "missionReady");
            }
        }
    }

    void Server::UpdateCarControllers() {
        // The simulation controller is the client whose native physics is
        // authoritative for a car: its driver, otherwise the first occupied
        // passenger seat, otherwise the nearest mission-ready player. An empty
        // car only moves to another player who is clearly closer, and not more
        // often than every two seconds, so a hand-off never flaps.
        constexpr auto kInterval         = std::chrono::milliseconds(250);
        constexpr auto kEmptyHandoffHold = std::chrono::seconds(2);
        constexpr float kEmptyHysteresis = 10.0f;
        const auto now                   = std::chrono::steady_clock::now();
        if (now - _lastControllerUpdate < kInterval) {
            return;
        }
        _lastControllerUpdate = now;
        const auto usable = [&](uint64_t carId, uint64_t guid) {
            if (guid == 0 || !_missionReadiness.IsReady(static_cast<MafiaNet::PeerGuid>(guid)) || !_players.FindByGuid(static_cast<MafiaNet::PeerGuid>(guid))) {
                return false;
            }
            const auto failed = _carLoadFailures.find(carId);
            return failed == _carLoadFailures.end() || !failed->second.contains(guid);
        };
        _cars.ForEach([&](Shared::Entities::CarEntity *car) {
            const uint64_t carId = car->GetNetworkID();
            if (car->missionGeneration != MissionGeneration() || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
                return;
            }
            uint64_t occupantGuid = 0;
            for (uint8_t seat = 0; occupantGuid == 0 && seat < car->seatCount && seat < Shared::Entities::CarEntity::kMaxSeats; ++seat) {
                auto *player = car->occupantIds[seat] ? _players.FindByNetworkId(car->occupantIds[seat]) : nullptr;
                if (player && player->spawned && player->alive && player->spawnGeneration == car->occupantGenerations[seat] && usable(carId, player->controllerGuid)) {
                    occupantGuid = player->controllerGuid;
                }
            }
            if (occupantGuid != 0) {
                _emptyCarHandoff.erase(carId);
                if (car->simulationControllerGuid != occupantGuid) {
                    _cars.SetController(carId, occupantGuid);
                }
                return;
            }
            uint64_t nearestGuid  = 0;
            float nearest         = std::numeric_limits<float>::max();
            float currentDistance = std::numeric_limits<float>::max();
            _players.ForEach([&](Shared::Entities::PlayerEntity *player) {
                if (!player->spawned || player->missionGeneration != MissionGeneration() || !usable(carId, player->controllerGuid)) {
                    return;
                }
                const float distance = glm::distance(player->position, car->position);
                if (player->controllerGuid == car->simulationControllerGuid) {
                    currentDistance = distance;
                }
                if (distance < nearest) {
                    nearest     = distance;
                    nearestGuid = player->controllerGuid;
                }
            });
            const bool currentUsable = usable(carId, car->simulationControllerGuid) && currentDistance != std::numeric_limits<float>::max();
            if (nearestGuid == 0 || nearestGuid == car->simulationControllerGuid) {
                if (!currentUsable && car->simulationControllerGuid != nearestGuid) {
                    _cars.SetController(carId, nearestGuid);
                }
                return;
            }
            const auto last = _emptyCarHandoff.find(carId);
            const bool held = last != _emptyCarHandoff.end() && now - last->second < kEmptyHandoffHold;
            if (!currentUsable || (!held && currentDistance - nearest > kEmptyHysteresis)) {
                _cars.SetController(carId, nearestGuid);
                _emptyCarHandoff[carId] = now;
            }
        });
    }

    void Server::PreShutdown() {
        _reportedCarLoadFailures.clear();
        _carLoadFailures.clear();
        _emptyCarHandoff.clear();
        _combat.SetTransitionCallback({});
        _combat.SetActionCallback({});
        _missionReadiness.SetResultCallback({});
        _debris.ResetForMission();
        _pickups.ResetForMission();
        _cars.ResetForMission();
        _combat.ResetForMission();
        _worldScript.Shutdown();
        _players.Reset();
        _missionState = nullptr;
    }

    void Server::OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &info) {
        _allReadyNotified = false;
        _missionReadiness.OnConnect(info.guid);
        _players.OnConnect(info);
        _cars.AssignUncontrolled(static_cast<uint64_t>(info.guid));
        _debris.AssignUncontrolled(static_cast<uint64_t>(info.guid));
        if (auto *player = _players.FindByGuid(info.guid)) {
            _combat.OnPlayerConnect(player->GetNetworkID());
            Features::Script::EmitPlayerEvent(*this, "playerConnect", player->GetNetworkID());
        }
    }

    void Server::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        uint64_t replacementGuid = 0;
        _players.ForEach([&](Shared::Entities::PlayerEntity *candidate) {
            const auto candidateGuid = _players.GuidForNetworkId(candidate->GetNetworkID());
            if (replacementGuid == 0 && candidateGuid != guid && _missionReadiness.IsReady(candidateGuid)) {
                replacementGuid = static_cast<uint64_t>(candidateGuid);
            }
        });
        _cars.TransferController(static_cast<uint64_t>(guid), replacementGuid);
        _debris.TransferController(static_cast<uint64_t>(guid), replacementGuid);
        if (auto *player = _players.FindByGuid(guid)) {
            Features::Script::EmitPlayerEvent(*this, "playerDisconnect", player->GetNetworkID());
            _cars.ClearOccupant(player->GetNetworkID(), player->spawnGeneration);
            _combat.OnPlayerDisconnect(player->GetNetworkID());
            _chatBuckets.erase(player->GetNetworkID());
        }
        _players.OnDisconnect(guid);
        _missionReadiness.OnDisconnect(guid);
    }

    bool Server::AllowChat(uint64_t senderNetworkId) {
        constexpr float kBurst       = 5.0f;
        constexpr float kRefillPerSecond = 1.0f;
        const auto now = std::chrono::steady_clock::now();
        auto &bucket   = _chatBuckets[senderNetworkId];
        if (bucket.last == std::chrono::steady_clock::time_point {}) {
            bucket.tokens = kBurst;
        }
        else {
            bucket.tokens = std::min(kBurst, bucket.tokens + std::chrono::duration<float>(now - bucket.last).count() * kRefillPerSecond);
        }
        bucket.last = now;
        if (bucket.tokens < 1.0f) {
            if (now - bucket.lastWarning > std::chrono::seconds(2)) {
                bucket.lastWarning = now;
                SendNotice(senderNetworkId, "You are sending messages too quickly.", 0xffa050ff);
            }
            return false;
        }
        bucket.tokens -= 1.0f;
        return true;
    }

    void Server::OnChatMessage(uint64_t senderNetworkId, const std::string &text) {
        auto *player = _players.FindByNetworkId(senderNetworkId);
        if (!player) {
            return;
        }
        const std::string line = Shared::Chat::SanitizeLine(text, Shared::Chat::kMaxMessageCodePoints);
        if (line.empty() || !AllowChat(senderNetworkId)) {
            return;
        }
        Framework::Networking::RPC::ChatMessage message;
        message.text            = line;
        message.author          = player->nickname;
        // The author's name takes their nametag color (0xAARRGGBB on the
        // replica, 0xRRGGBBAA on the wire).
        message.color           = (player->nametag.color << 8) | 0xff;
        message.senderNetworkId = senderNetworkId;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(message);
        Features::Script::EmitPlayerChat(*this, senderNetworkId, line);
    }

    void Server::OnChatCommand(uint64_t senderNetworkId, const std::string &text, const std::string &command, const std::vector<std::string> &args) {
        (void)text;
        if (_players.FindByNetworkId(senderNetworkId) && AllowChat(senderNetworkId)) {
            Features::Script::EmitPlayerCommand(*this, senderNetworkId, command, args);
        }
    }

    void Server::BroadcastNotice(std::string_view text, uint32_t color) {
        Framework::Networking::RPC::ChatMessage message;
        message.text  = Shared::Chat::SanitizeLine(text, Shared::Chat::kMaxMessageCodePoints * 2);
        message.color = color;
        if (!message.text.empty()) {
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(message);
        }
    }

    void Server::ModuleRegister(Framework::Scripting::Engine *engine) {
        Features::Script::RegisterScripting(engine, *this);
    }

    v8::Local<v8::Value> Server::WrapScriptPlayer(v8::Isolate *isolate, uint64_t networkId) {
        return Scripting::WrapPlayer(isolate, networkId);
    }

    bool Server::ChangeMission(std::string_view name) {
        if (!Shared::World::IsStockMission(name) || _missionState->generation == std::numeric_limits<uint64_t>::max()) {
            return false;
        }
        _mission               = name;
        _missionState->mission = _mission;
        ++_missionState->generation;
        _missionReadiness.SetGeneration(_missionState->generation);
        _allReadyNotified = false;
        _reportedCarLoadFailures.clear();
        _debris.ResetForMission();
        _pickups.ResetForMission();
        _cars.ResetForMission();
        _combat.ResetForMission();
        _worldScript.ResetForMission(_missionState->generation);
        _players.ResetForMission(_missionState->generation);
        Features::Script::EmitMissionEvent(*this, "missionChange");
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Requested Mafia 1 mission '{}' (generation {})", _mission, _missionState->generation);
        return true;
    }

    uint64_t Server::MissionGeneration() const {
        return _missionState->generation;
    }

    bool Server::SendNotice(uint64_t playerNetworkId, std::string_view text, uint32_t color) {
        if (!_players.FindByNetworkId(playerNetworkId) || text.empty() || text.size() > 400) {
            return false;
        }
        Framework::Networking::RPC::ChatMessage message;
        message.text  = Shared::Chat::SanitizeLine(text, Shared::Chat::kMaxMessageCodePoints * 2);
        message.color = color;
        if (message.text.empty()) {
            return false;
        }
        Framework::CoreModules::GetNetworkPeer()->SendRPC(message, MafiaNet::ToGuid(_players.GuidForNetworkId(playerNetworkId)));
        return true;
    }
} // namespace Mafia1Online::Core
