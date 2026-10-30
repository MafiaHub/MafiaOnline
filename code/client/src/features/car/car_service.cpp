#include "car_service.h"

#include "features/car/car_hooks.h"
#include "features/car/car_mesh_capture.h"
#include "features/debug/sync_trace.h"
#include "features/seat/seat_service.h"
#include "features/simulation/simulation_config.h"
#include "features/world/frame_clock.h"
#include "features/world/world_service.h"

#include "shared/features/car/car_engine_state.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/car/car_explosion_intent.h"
#include "shared/features/car/car_hit_report.h"
#include "shared/features/car/car_terminal_intent.h"
#include "shared/features/car/car_load_failure.h"
#include "shared/features/car/car_movement.h"
#include "shared/features/car/car_siren_intent.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/world/world_state_entity.h"

#include <core_modules.h>
#include <integrations/client/instance.h>
#include <integrations/client/networking/engine.h>
#include <logging/logger.h>
#include <mafia1/sdk/car/native_car.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_death.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <mafia1/sdk/ui/native_indicators.h>
#include <mafia1/sdk/world/native_fire.h>
#include <networking/network_client.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_set>

namespace Mafia1Online::Features::Car {
    namespace {
        constexpr auto kMovementSendInterval = std::chrono::milliseconds(33);
        // A frame ending this close before a scheduled send sends it.
        constexpr auto kMovementSendSlack = std::chrono::milliseconds(2);
        // A car at rest with nothing changing reports only this often, so
        // its pose stops changing on the server and observers stop being
        // sent it; the slow fields refresh as often.
        constexpr auto kIdleHeartbeat = std::chrono::seconds(1);
        // How fast an observer's mapping of a controller's pose clock follows
        // its least-delayed arrivals: the time constant, in ms.
        constexpr double kClockOffsetFollowMs = 2000.0;

        // Nothing an observer could see moved since `last` was sent.
        bool AtRest(const Shared::Car::Movement &now, const Shared::Car::Movement &last) {
            const glm::vec3 position(now.x, now.y, now.z);
            const glm::vec3 lastPosition(last.x, last.y, last.z);
            const float turn = std::abs(glm::dot(glm::quat(now.qw, now.qx, now.qy, now.qz), glm::quat(last.qw, last.qx, last.qy, last.qz)));
            return glm::distance(position, lastPosition) < 0.01f && turn > 0.99999f && glm::length(glm::vec3(now.velocityX, now.velocityY, now.velocityZ)) < 0.05f &&
                   glm::length(glm::vec3(now.angularVelocityX, now.angularVelocityY, now.angularVelocityZ)) < 0.05f && now.gear == last.gear &&
                   now.lightState == last.lightState && now.hornOn == last.hornOn && now.skidMask == 0 && std::abs(now.steeringInput - last.steeringInput) < 0.01f &&
                   std::abs(now.engineRotations - last.engineRotations) < 100.0f && std::abs(now.powerInput - last.powerInput) < 0.02f &&
                   std::abs(now.brakeInput - last.brakeInput) < 0.02f && std::abs(now.handbrakeInput - last.handbrakeInput) < 0.02f &&
                   std::abs(now.clutchInput - last.clutchInput) < 0.02f;
        }

        // A slow field the server would act on changed since `last`.
        bool SlowFieldsChanged(const Shared::Car::Movement &now, const Shared::Car::Movement &last) {
            return now.maximumGear != last.maximumGear || now.speedLimited != last.speedLimited || now.doorTargets != last.doorTargets ||
                   std::abs(now.fuelTankCapacity - last.fuelTankCapacity) > 0.01f || last.fuel - now.fuel >= 0.05f || (now.fuel == 0.0f && last.fuel != 0.0f);
        }
        constexpr float kFuelSyncTolerance = 0.01f;
        // A velocity change faster than this in one physics step (m/s², about
        // 2.5 g) is a collision impulse on a car another client drives.
        constexpr float kImpactAcceleration = 25.0f;
        // Its controller's poses dated this long after the impact carry its
        // own collision (it predicts this car to the present too).
        constexpr double kImpactResponseMs = 150.0;
        // At most this long the local impulse outweighs the controller.
        constexpr double kImpactHoldMs = 1000.0;
        // Car centres this close are touching: two stock cars end to end are
        // about 4.5 m apart, side by side about 2 m.
        constexpr float kImpactContactDistance = 6.0f;
        // Impacts closer together than this belong to one contact.
        constexpr double kImpactEpisodeGapMs = 500.0;
        constexpr float kYieldCorrectionSeconds = 1.0f;
        // How fast an observed car's render clock reaches the present near a
        // car this client simulates, and drops back behind it after: the car
        // runs at most a quarter faster, or 7.5% slower, meanwhile, and the
        // change takes no less than these.
        constexpr float kLeadEntrySeconds = 0.3f;
        constexpr float kLeadExitSeconds  = 1.0f;
        constexpr float kLeadEntryRate    = 0.25f;
        constexpr float kLeadExitRate     = 0.075f;

        glm::quat FrameRotation(SDK::Scene::NativeFrame &frame) {
            const auto basis = frame.GetWorldBasis();
            const glm::vec3 axisZ = glm::normalize(glm::vec3(basis.forward.x, basis.forward.y, basis.forward.z));
            const glm::vec3 axisX = glm::normalize(glm::cross(glm::vec3(basis.up.x, basis.up.y, basis.up.z), axisZ));
            const glm::vec3 axisY = glm::normalize(glm::cross(axisZ, axisX));
            return glm::normalize(glm::quat_cast(glm::mat3(axisX, axisY, axisZ)));
        }

        bool IsFiniteVector(const glm::vec3 &value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFinite(const Framework::Utils::TransformSnapshot &snapshot) {
            return std::isfinite(snapshot.position.x) && std::isfinite(snapshot.position.y) && std::isfinite(snapshot.position.z) && std::isfinite(snapshot.velocity.x) && std::isfinite(snapshot.velocity.y) && std::isfinite(snapshot.velocity.z)
                   && std::isfinite(snapshot.rotation.w) && std::isfinite(snapshot.rotation.x) && std::isfinite(snapshot.rotation.y) && std::isfinite(snapshot.rotation.z);
        }

        float RollForDirection(const glm::quat &rotation, const glm::vec3 &forward) {
            // LS3DF SetDir takes the forward direction and a separate roll;
            // keep the replicated bank instead of flattening cars upright.
            const glm::vec3 targetUp = glm::mat3_cast(rotation) * glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 referenceRight = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward);
            if (glm::length(referenceRight) < 0.001f) {
                referenceRight = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), forward);
            }
            referenceRight = glm::normalize(referenceRight);
            const glm::vec3 referenceUp = glm::normalize(glm::cross(forward, referenceRight));
            // A positive LS3DF roll tilts up toward the frame's right axis.
            return std::atan2(glm::dot(targetUp, referenceRight), glm::dot(targetUp, referenceUp));
        }

        void WriteFrame(SDK::Scene::NativeFrame &frame, const glm::vec3 &position, const glm::quat &rotation) {
            const auto direction = glm::mat3_cast(rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
            frame.SetWorldPosition({position.x, position.y, position.z});
            frame.SetDirection({direction.x, direction.y, direction.z}, RollForDirection(rotation, direction));
            frame.Update();
        }
    } // namespace

    void CarService::RegisterRPC() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::MeshCheckpointChunk>(
            [this](const Shared::Car::MeshCheckpointChunk &chunk, MafiaNet::Packet *) { OnMeshCheckpoint(chunk); });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::AuthoritativeHit>(
            [this](const Shared::Car::AuthoritativeHit &hit, MafiaNet::Packet *) { OnAuthoritativeHit(hit); });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::ContactImpact>(
            [this](const Shared::Car::ContactImpact &impact, MafiaNet::Packet *) { OnContactImpact(impact); });
        // Another player's accepted strike: the sound its own C_human::AI
        // played (the dent arrives with the controller's mesh).
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::MeleeImpact>([this](const Shared::Car::MeleeImpact &impact, MafiaNet::Packet *) {
            auto *mission = SDK::Core::Mission::Get();
            if (!_world || !_world->IsReady() || impact.missionGeneration != _world->LoadedMissionGeneration() || !mission || !mission->Game() ||
                !std::isfinite(impact.x) || !std::isfinite(impact.y) || !std::isfinite(impact.z)) {
                return;
            }
            if (auto *sound = SDK::World::Fire::CreateTableSound(Shared::Car::kMeleeCarSound)) {
                sound->Frame()->SetWorldPosition({impact.x, impact.y, impact.z});
                sound->Frame()->Update();
                SDK::World::Fire::PlayOwnedSound(mission->Game(), sound);
            }
        });
    }

    void CarService::Update(World::WorldService &world) {
        _world = &world;
        if (_simulatedSerial != _updateSerial) {
            _simulatedCars.clear();
        }
        ++_updateSerial;
        auto *replication                      = Framework::CoreModules::GetReplication();
        const uint64_t loadedMissionGeneration = world.LoadedMissionGeneration();
        if (!replication || loadedMissionGeneration == 0) {
            for (const auto &entry : _streams) {
                world.NativeObjects().InvalidateNetwork(entry.first);
            }
            Reset();
            return;
        }
        if (_waterCamera) {
            auto *local = SDK::Player::CurrentPlayer();
            if (local && local != _waterCamera->player.object &&
                world.NativeObjects().Resolve(_waterCamera->player) != local) {
                // A new local life has replaced the drowned occupant. Release
                // only the pose we locked, so a script's later camera lock wins.
                auto *mission = SDK::Core::Mission::Get();
                auto *scene = mission ? mission->GetScene() : nullptr;
                auto *camera = scene ? scene->ActiveCamera() : nullptr;
                if (camera) {
                    const auto eye = camera->WorldPosition();
                    const auto facing = camera->WorldDirection();
                    const glm::vec3 position(eye.x, eye.y, eye.z);
                    const glm::vec3 direction(facing.x, facing.y, facing.z);
                    if (glm::length(position - _waterCamera->position) < 0.01f &&
                        glm::length(direction - _waterCamera->direction) < 0.01f) {
                        mission->Game()->Camera().Unlock();
                        mission->Game()->RecomputeLightCache();
                    }
                }
                _waterCamera.reset();
            }
        }

        std::unordered_set<uint64_t> seen;
        const uint64_t myGuid = static_cast<uint64_t>(replication->GetMyGUID());
        // With the server's to each controller, the round trip that brings a
        // car's poses here. A controller is a player or else the simulation
        // host, whose ping the world state carries.
        const int ownPing       = Framework::CoreModules::GetClientInstance()->GetNetworkingEngine()->GetNetworkClient()->GetPing();
        uint16_t simulationPing = 0;
        std::unordered_map<uint64_t, uint16_t> controllerPings;
        replication->ForEach<Shared::Entities::WorldStateEntity>([&](Shared::Entities::WorldStateEntity *state) {
            simulationPing = state->simulationPing;
        });
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (player->controllerGuid != 0) {
                controllerPings[player->controllerGuid] = player->ping;
            }
        });
        replication->ForEach<Shared::Entities::CarEntity>([&](Shared::Entities::CarEntity *car) {
            if (car->missionGeneration != loadedMissionGeneration) {
                return;
            }
            const uint64_t id = car->GetNetworkID();
            seen.insert(id);
            auto &stream                    = _streams[id];
            const bool simulationController = car->simulationControllerGuid != 0 && car->simulationControllerGuid == myGuid;
            const auto controllerPing       = controllerPings.find(car->simulationControllerGuid);
            stream.transitMs                = CarPoseBuffer::TransitMs(controllerPing != controllerPings.end() ? controllerPing->second : simulationPing, ownPing);
            if (stream.simulationController != simulationController) {
                stream.simulationController   = simulationController;
                stream.lastSent               = {};
                stream.nextSend               = {};
                stream.lastMovement.reset();
                stream.lastSlowMovement.reset();
                stream.sequence               = 0;
                stream.engineReportSequence   = 0;
                stream.appliedDynamicsCommandRevision = 0;
                // A hand-over rebuilds an observed car's physics; an adopted
                // car was never observed and keeps its retail driver's.
                stream.controllerDynamicsInitialized = simulationController && IsAdopted(id);
                stream.hasReportedEngineState = false;
                stream.hasAppliedEngineState  = false;
                stream.explosionReported      = false;
                stream.terminalReported       = false;
                stream.hasReportedDamage      = false;
                stream.pendingDamageReports.clear();
                stream.damageReportSequence   = 0;
                stream.lastDamageReport       = {};
                stream.lastMeshReport         = {};
                stream.meshReportSequence     = 0;
                stream.meshDirty              = simulationController;
                stream.hasReportedMesh        = false;
                // The controller gets no poses of its own (GetTransformSource):
                // as an observer again it waits for the next controller's on
                // native physics, then eases from there (Rebase).
                stream.nativeShown = !simulationController;
                if (simulationController) {
                    stream.poses.Clear();
                }
                // An adopted car is where its own native physics put it; the
                // pose the server holds is older. So, within a snap of it, is a
                // car this client showed: the last pose is ahead of it by its
                // speed times the render delay, a jump at the hand-over.
                if (simulationController && !IsAdopted(id)) {
                    Framework::Utils::TransformSnapshot authoritative {car->position, car->velocity, car->rotation};
                    bool shownNear = false;
                    if (const auto native = _nativeById.find(id);
                        native != _nativeById.end() && !native->second.removalQueued && world.NativeObjects().Resolve(native->second.handle)) {
                        const auto shown = native->second.frame->WorldPosition();
                        shownNear        = glm::distance(glm::vec3(shown.x, shown.y, shown.z), authoritative.position) <= 5.0f;
                    }
                    if (IsFinite(authoritative) && glm::length(authoritative.rotation) >= 0.0001f && !shownNear) {
                        ApplyPose(world, id, authoritative);
                    }
                }
            }
            if (stream.appliedTransformRevision != car->transformRevision) {
                const Framework::Utils::TransformSnapshot forced {car->forcedPosition, car->velocity, car->forcedRotation};
                if (IsFinite(forced) && glm::length(forced.rotation) >= 0.0001f) {
                    ApplyPose(world, id, forced, true);
                    stream.appliedTransformRevision = car->transformRevision;
                    stream.lastSent = {};
                    stream.nextSend = {};
                }
            }
            if (car->lastUpdateTime == 0 && stream.lastPacketTime != 0) {
                return;
            }
            if (car->poseClockSource != 0 || !car->receivedPoses.empty()) {
                // Every pose the transform channel delivered since the last
                // frame, not only the one the entity holds now: two often
                // arrive together, and dropping one left a gap of two send
                // intervals the car was extrapolated across.
                const auto received = std::move(car->receivedPoses);
                car->receivedPoses.clear();
                for (const auto &pose : received) {
                    if (pose.poseClockSource != 0) {
                        ReceivePose(id, stream, pose);
                    }
                }
                return;
            }
            // A pose without a controller clock (none since the clock was
            // added): dated by its packet.
            const MafiaNet::Time packetTime = car->lastUpdateTime == 0 ? MafiaNet::GetTime() : car->lastUpdateTime;
            if (stream.lastPacketTime == packetTime) {
                return;
            }
            Framework::Utils::TransformSnapshot snapshot {car->position, car->velocity, car->rotation};
            if (!IsFinite(snapshot) || glm::length(snapshot.rotation) < 0.0001f) {
                return;
            }
            const glm::vec3 angular = IsFiniteVector(car->angularVelocity) && glm::length(car->angularVelocity) <= 30.0f ? car->angularVelocity : glm::vec3(0.0f);
            const auto poseTime = static_cast<double>(packetTime);
            stream.poses.Push({poseTime, snapshot.position, snapshot.velocity, glm::normalize(snapshot.rotation), angular}, poseTime, true);
            stream.lastPoseTime   = poseTime;
            stream.lastPacketTime = packetTime;
        });
        for (auto it = _streams.begin(); it != _streams.end();) {
            if (!seen.contains(it->first)) {
                QueueRemoval(world, it->first);
                it = _streams.erase(it);
            }
            else {
                if (!_nativeById.contains(it->first) && !it->second.creationFailed) {
                    // The simulation host's own traffic cars are adopted from
                    // the retail world, never created beside it. A script's
                    // car it drives for a scripted NPC (no traffic key) is
                    // created like any other.
                    if (auto *car = replication->GetEntity<Shared::Entities::CarEntity>(it->first);
                        car && !(car->hostDriven && car->trafficKey != 0 && it->second.simulationController)) {
                        // Retail explosion can deactivate and eventually destroy
                        // the original car while keeping its wreckage in the
                        // mission. A terminal replica only needs a native car
                        // once, to replay that explosion on initial stream-in.
                        if (car->terminalState == Shared::Entities::CarEntity::TerminalState::Active ||
                            it->second.appliedTerminalSequence < car->terminalSequence) {
                            Framework::Utils::TransformSnapshot pose {car->position, car->velocity, car->rotation};
                            if (IsFinite(pose) && glm::length(pose.rotation) >= 0.0001f) {
                                pose.rotation = glm::normalize(pose.rotation);
                                CreateNativeCar(world, it->first, car->model.c_str(), pose);
                                it->second.creationFailed = !_nativeById.contains(it->first);
                            }
                        }
                    }
                }
                SyncRepair(world, it->first, it->second);
                const auto *state = replication->GetEntity<Shared::Entities::CarEntity>(it->first);
                if (it->second.simulationController && (!state || state->terminalState == Shared::Entities::CarEntity::TerminalState::Active)) {
                    SyncDamage(world, it->first, it->second);
                    SyncMesh(world, it->first, it->second);
                    ApplyAuthoritativeHits(world, it->first, it->second);
                    ApplyContactImpacts(world, it->first, it->second);
                    SyncDynamics(world, it->first, it->second);
                    ReportMovement(world, it->first, it->second);
                    ReportDamage(world, it->first, it->second);
                    ReportMesh(world, it->first, it->second);
                    DetectTerminal(world, it->first, it->second);
                }
                else if (it->second.simulationController && state &&
                         (state->terminalState == Shared::Entities::CarEntity::TerminalState::Submerged ||
                          state->terminalState == Shared::Entities::CarEntity::TerminalState::FatalFall)) {
                    // The same native car keeps falling under its own physics
                    // until the gamemode removes it or retail deactivates it.
                    ReportMovement(world, it->first, it->second);
                }
                else {
                    // The pose is written by DriveObserver inside the actor
                    // tick. A car the game did not tick this frame (inactive)
                    // is placed here instead; a terminal car keeps its final
                    // native state.
                    Framework::Utils::TransformSnapshot pose;
                    if (it->second.drivenSerial + 1 != _updateSerial && state) {
                        if ((state->terminalState == Shared::Entities::CarEntity::TerminalState::Active ||
                             state->terminalState == Shared::Entities::CarEntity::TerminalState::Submerged ||
                             state->terminalState == Shared::Entities::CarEntity::TerminalState::FatalFall) && Sample(it->first, pose)) {
                            ApplyPose(world, it->first, pose);
                            // Not ticked by the game this frame: placed after
                            // the camera, from the wall clock.
                            if (Debug::SyncTrace::Enabled() && Debug::SyncTrace::Near(pose.position)) {
                                Debug::SyncTrace::Observed("car", it->first).Str("mode", "update").Vec("p", pose.position).Quat("q", pose.rotation);
                            }
                        }
                    }
                    if (!state || (state->terminalState != Shared::Entities::CarEntity::TerminalState::Submerged &&
                                   state->terminalState != Shared::Entities::CarEntity::TerminalState::FatalFall)) {
                        SyncDynamics(world, it->first, it->second);
                    }
                    SyncDamage(world, it->first, it->second);
                    SyncMesh(world, it->first, it->second);
                }
                SyncOpacity(world, it->first);
                SyncEngine(world, it->first, it->second);
                SyncRadar(world, it->first);
                SyncTerminal(world, it->first, it->second);
                ++it;
            }
        }
    }

    void CarService::ReceivePose(uint64_t networkId, Stream &stream, const Shared::Entities::CarEntity::ReceivedPose &pose) {
        // Dated by the controller's frame clock at the step that produced
        // it, mapped onto this one's by the least-delayed arrival of the last
        // two seconds. Both are frame clocks, so the offset holds steady but
        // for the network; it follows the window's floor smoothly (over
        // ~2 s), never in steps, as every change of it is one of the car's
        // speed. One early or late packet never moves a sample's time; a
        // jump resets it.
        if (stream.poseClockSource == pose.poseClockSource && stream.poseClockMs == pose.poseClockMs && stream.poseClockFraction == pose.poseClockFraction) {
            return;
        }
        Framework::Utils::TransformSnapshot snapshot {pose.position, pose.velocity, pose.rotation};
        if (!IsFinite(snapshot) || glm::length(snapshot.rotation) < 0.0001f) {
            return;
        }
        const double now         = World::FrameClock::NowMs();
        const double stampMs     = Shared::Entities::CarEntity::ClockMs(pose.poseClockMs, pose.poseClockFraction);
        const double offset      = now - stampMs;
        const bool sourceChanged = stream.poseClockSource != pose.poseClockSource;
        if (sourceChanged || std::abs(offset - stream.poseClockOffset) > 1000.0) {
            stream.offsetWindow.clear();
            stream.poseClockOffset = offset;
        }
        stream.offsetWindow.push_back(offset);
        while (stream.offsetWindow.size() > 60) {
            stream.offsetWindow.pop_front();
        }
        const double floor = *std::min_element(stream.offsetWindow.begin(), stream.offsetWindow.end());
        const double since = stream.lastOffsetUpdate > 0.0 ? std::clamp(now - stream.lastOffsetUpdate, 0.0, 250.0) : 0.0;
        stream.poseClockOffset += (floor - stream.poseClockOffset) * (1.0 - std::exp(-since / kClockOffsetFollowMs));
        stream.lastOffsetUpdate  = now;
        stream.poseClockSource   = pose.poseClockSource;
        stream.poseClockMs       = pose.poseClockMs;
        stream.poseClockFraction = pose.poseClockFraction;
        const double poseTime    = stampMs + stream.poseClockOffset;
        if (!sourceChanged && poseTime <= stream.lastPoseTime) {
            return;
        }
        const glm::vec3 angular = IsFiniteVector(pose.angularVelocity) && glm::length(pose.angularVelocity) <= 30.0f ? pose.angularVelocity : glm::vec3(0.0f);
        stream.poses.Push({poseTime, snapshot.position, snapshot.velocity, glm::normalize(snapshot.rotation), angular}, now, sourceChanged);
        if (Debug::SyncTrace::Enabled() && Debug::SyncTrace::Near(snapshot.position)) {
            Debug::SyncTrace::Received("car", networkId)
                .Num("clk", stampMs)
                .Num("arrival", now)
                .Num("ptime", poseTime)
                .Num("offset", stream.poseClockOffset)
                .Bool("reset", sourceChanged)
                .Vec("p", snapshot.position);
        }
        stream.lastPoseTime = poseTime;
    }

    void CarService::CreateNativeCar(World::WorldService &world, uint64_t networkId, const char *model, const Framework::Utils::TransformSnapshot &pose) {
        auto *mission = SDK::Core::Mission::Get();
        auto *scene   = mission->GetScene();
        auto *game    = mission->Game();
        auto *frame   = SDK::Scene::GetDriver()->CreateModel();
        if (!frame) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not create frame for network car {}", networkId);
            ReportLoadFailure(world, networkId);
            return;
        }

        const std::string frameName = "Mafia1OnlineCar_" + std::to_string(networkId);
        if (!frame->SetName(frameName.c_str()) || !SDK::Scene::GetModelCache()->OpenModel(frame, model)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not load model '{}' for network car {}", model, networkId);
            frame->Release();
            ReportLoadFailure(world, networkId);
            return;
        }
        if (!frame->LinkTo(scene->PrimarySector())) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not link network car {} to the mission scene", networkId);
            frame->Release();
            ReportLoadFailure(world, networkId);
            return;
        }

        const auto direction = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
        frame->SetWorldPosition({pose.position.x, pose.position.y, pose.position.z});
        frame->SetDirection({direction.x, direction.y, direction.z}, RollForDirection(pose.rotation, direction));
        frame->Update();
        scene->AddFrame(frame);

        auto *actor = mission->CreateActor(SDK::Player::NativeActor::Type::Car);
        if (!actor || !actor->Initialize(frame)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not initialize native car {} (model '{}')", networkId, model);
            if (actor) {
                actor->Release();
            }
            scene->DeleteFrame(frame);
            frame->Release();
            ReportLoadFailure(world, networkId);
            return;
        }

        game->AddTemporaryActor(actor);
        auto *car = static_cast<SDK::Car::NativeCar *>(actor);
        car->SetParticlesActive(true);
        // C_car::NetDirect is empty. The regular AI path calls C_Vehicle::Tick,
        // establishing wheel contact and registering native seat use objects.
        car->SetNetworkControlled(false);
        // A network pose may be above the ground (scripts spawn with a lift):
        // the controller's physics lets it land instead of freezing it there.
        if (const auto stream = _streams.find(networkId); stream != _streams.end() && stream->second.simulationController) {
            car->SkipFirstUpdateFreeze();
        }
        const auto handle = world.NativeObjects().Bind(car, networkId);
        _nativeById.emplace(networkId, NativeCar {handle, frame, false});
        _idByNative.emplace(car, networkId);
        frame->Release();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Created native network car {} with model '{}'", networkId, model);
    }

    void CarService::ReportLoadFailure(World::WorldService &world, uint64_t networkId) {
        Shared::Car::LoadFailure failure;
        failure.networkId         = networkId;
        failure.missionGeneration = world.LoadedMissionGeneration();
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(failure);
    }

    void CarService::ReportDamage(World::WorldService &world, uint64_t networkId, Stream &stream) {
        const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        if (!state || (state->nativeDamageValid && state->nativeDamageRevision > stream.appliedDamageRevision)) {
            // A new controller must first restore the last authoritative
            // damage snapshot before it can submit its local native state.
            return;
        }
        const auto native = _nativeById.find(networkId);
        if (native == _nativeById.end() || native->second.removalQueued) {
            return;
        }
        const auto *car = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!car) {
            return;
        }
        const auto &vehicle = car->Vehicle();
        const auto &lights = vehicle.Lights();
        const auto &zones = vehicle.DeformZones();
        const int wheels = vehicle.WheelCount();
        if (lights.Size() > Shared::Car::DamageState::kMaxLights || zones.Size() > Shared::Car::DamageState::kMaxZones ||
            wheels < 0 || wheels > Shared::Car::DamageState::kMaxWheels) {
            return;
        }
        Shared::Car::DamageState observed;
        observed.lightCount = static_cast<uint8_t>(lights.Size());
        observed.zoneCount = static_cast<uint8_t>(zones.Size());
        observed.wheelCount = static_cast<uint8_t>(wheels);
        for (uint8_t i = 0; i < observed.lightCount; ++i) {
            observed.lights[i] = {lights.begin[i].flags & 1U, lights.begin[i].damage};
        }
        for (uint8_t i = 0; i < observed.zoneCount; ++i) {
            // DEFORMED (2) is cleared every native tick; only BROKEN persists.
            observed.zones[i] = {static_cast<uint16_t>(zones.begin[i].flags & 1U), zones.begin[i].crackLevel};
        }
        for (uint8_t i = 0; i < observed.wheelCount; ++i) {
            const auto *wheel = vehicle.Wheel(i);
            if (!wheel) {
                return;
            }
            observed.wheels[i] = {wheel->flags & Shared::Car::DamageState::kWheelDamageFlags, wheel->health, wheel->deformAngle};
        }
        observed.engineHealth = vehicle.EngineHealth();
        observed.engineDamagePower = vehicle.EngineDamagePower();
        observed.engineDestroyed = vehicle.EngineDestroyed() ? 1 : 0;
        observed.burning = car->Burning() ? 1 : 0;
        observed.burnTimer = car->BurnTimer();
        observed.burnDuration = car->BurnDuration();
        observed.gearboxHealth = vehicle.GearboxHealth();
        observed.bodyDamage = car->BodyDamage();
        observed.fuelTankHealth = car->FuelTankHealth();
        if (!std::isfinite(observed.engineHealth) || !std::isfinite(observed.gearboxHealth) ||
            !std::isfinite(observed.bodyDamage) || !std::isfinite(observed.engineDamagePower)) {
            return;
        }
        for (uint8_t i = 0; i < observed.lightCount; ++i) {
            if (!std::isfinite(observed.lights[i].damage)) { return; }
        }
        for (uint8_t i = 0; i < observed.zoneCount; ++i) {
            if (!std::isfinite(observed.zones[i].crackLevel)) { return; }
        }
        for (uint8_t i = 0; i < observed.wheelCount; ++i) {
            if (!std::isfinite(observed.wheels[i].health) || !std::isfinite(observed.wheels[i].deformAngle)) { return; }
        }
        if (stream.hasReportedDamage && observed == stream.lastReportedDamage) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (stream.lastDamageReport != std::chrono::steady_clock::time_point {} &&
            now - stream.lastDamageReport < std::chrono::milliseconds(100)) {
            return;
        }
        Shared::Car::DamageReport report;
        report.networkId = networkId;
        report.missionGeneration = world.LoadedMissionGeneration();
        report.sequence = ++stream.damageReportSequence;
        report.baseRevision = stream.appliedDamageRevision;
        report.state = observed;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
        stream.lastReportedDamage = observed;
        stream.hasReportedDamage = true;
        stream.pendingDamageReports.push_back(observed);
        while (stream.pendingDamageReports.size() > 16) {
            stream.pendingDamageReports.pop_front();
        }
        stream.lastDamageReport = now;
        stream.meshDirty = true;
    }

    void CarService::SyncRepair(World::WorldService &world, uint64_t networkId, Stream &stream) {
        const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        if (!state || state->repairRevision == 0 || state->repairRevision == stream.appliedRepairRevision ||
            state->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
            return;
        }
        const auto native = _nativeById.find(networkId);
        if (native == _nativeById.end() || native->second.removalQueued) {
            return;
        }
        auto *car = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!car) {
            return;
        }

        // This is the native C_Vehicle::Reset(true) damage/deform sequence,
        // without Reset's drivetrain, seat, pose and wheel-contact changes.
        auto &vehicle = car->Vehicle();
        vehicle.RepairDamage();
        vehicle.RepairDeform();
        // InitDamage restores health but leaves DamageVehicle's one-shot
        // engine/gearbox flags intact; clear them so later impacts can hurt.
        vehicle.ClearDamageFlags();
        vehicle.SetMechanicalDamage(vehicle.EngineHealth(), 1.0f, false, vehicle.GearboxHealth());
        for (int index = 0; index < vehicle.WheelCount(); ++index) {
            if (const auto *wheel = vehicle.Wheel(index)) {
                vehicle.SetWheelDamage(index, 0, wheel->maximumHealth, 0.0f);
            }
        }
        car->SetBodyDamage(car->BodyHealthMaximum(), car->FuelTankHealthMaximum());
        car->SetBurnState(false, 0, 0);

        stream.appliedRepairRevision = state->repairRevision;
        stream.appliedDamageRevision = state->nativeDamageValid ? 0 : state->nativeDamageRevision;
        stream.pendingDamageReports.clear();
        stream.hasReportedDamage = false;
        stream.lastDamageReport = {};
        stream.appliedMeshRevision = state->nativeDamageValid ? 0 : state->meshRevision;
        // A checkpoint of the repaired car may already be in: keep it.
        if (stream.checkpointRepairRevision != state->repairRevision) {
            stream.checkpointRevision = 0;
            stream.checkpointVertices.clear();
        }
        if (stream.pendingMeshRepairRevision != state->repairRevision) {
            stream.pendingMeshRevision = 0;
            stream.pendingMeshSourceGuid = 0;
            stream.pendingMeshReceived.clear();
            stream.pendingMeshVertices.clear();
        }
        stream.lastReportedMesh.clear();
        stream.hasReportedMesh = false;
        stream.meshDirty = stream.simulationController;
        stream.lastMeshReport = {};
    }

    void CarService::SyncOpacity(World::WorldService &world, uint64_t networkId) {
        const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        const auto native = _nativeById.find(networkId);
        if (!state || state->terminalState != Shared::Entities::CarEntity::TerminalState::Active ||
            native == _nativeById.end() || native->second.removalQueued) {
            return;
        }
        auto *car = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!car) {
            return;
        }
        if (!native->second.hasAppliedOpacity || native->second.appliedOpacity != state->opacity ||
            native->second.opacityDamageRevision != state->nativeDamageRevision) {
            car->SetOpacity(state->opacity);
            native->second.appliedOpacity = state->opacity;
            native->second.opacityDamageRevision = state->nativeDamageRevision;
            native->second.hasAppliedOpacity = true;
        }
    }

    void CarService::SyncDamage(World::WorldService &world, uint64_t networkId, Stream &stream) {
        auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        if (!state || !state->nativeDamageValid) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const bool newRevision = stream.appliedDamageRevision != state->nativeDamageRevision;
        if (stream.simulationController && newRevision) {
            const auto acknowledged = std::find(stream.pendingDamageReports.begin(), stream.pendingDamageReports.end(), state->nativeDamage);
            if (acknowledged != stream.pendingDamageReports.end()) {
                // The native car may already have advanced beyond the echoed
                // report. Drop acknowledged reports without applying old data.
                stream.pendingDamageReports.erase(stream.pendingDamageReports.begin(), std::next(acknowledged));
                stream.appliedDamageRevision = state->nativeDamageRevision;
                stream.lastDamageApply = now;
                return;
            }
        }
        if (stream.simulationController && !newRevision) {
            // Reapplying an old checkpoint on the controller can erase an
            // impact before its native change has been reported upstream.
            return;
        }
        if (!newRevision && stream.lastDamageApply != std::chrono::steady_clock::time_point {} &&
            now - stream.lastDamageApply < std::chrono::seconds(1)) {
            return;
        }
        const auto native = _nativeById.find(networkId);
        if (native == _nativeById.end() || native->second.removalQueued) {
            return;
        }
        auto *car = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!car) {
            return;
        }
        auto &vehicle = car->Vehicle();
        const auto &damage = state->nativeDamage;
        if (damage.lightCount != vehicle.Lights().Size() || damage.zoneCount != vehicle.DeformZones().Size() ||
            damage.wheelCount != vehicle.WheelCount()) {
            return;
        }
        for (uint8_t i = 0; i < damage.lightCount; ++i) {
            vehicle.SetLightDamage(i, damage.lights[i].flags, damage.lights[i].damage);
        }
        for (uint8_t i = 0; i < damage.zoneCount; ++i) {
            vehicle.SetZoneDamage(i, damage.zones[i].flags, damage.zones[i].crackLevel,
                                  newRevision && stream.appliedDamageRevision != 0);
        }
        for (uint8_t i = 0; i < damage.wheelCount; ++i) {
            vehicle.SetWheelDamage(i, damage.wheels[i].flags, damage.wheels[i].health, damage.wheels[i].deformAngle);
        }
        // A negative power marks a server-authored engine health change.
        const float power = damage.engineDamagePower < 0.0f ? vehicle.EngineDamagePowerFor(damage.engineHealth) : damage.engineDamagePower;
        vehicle.SetMechanicalDamage(damage.engineHealth, power, damage.engineDestroyed != 0, damage.gearboxHealth);
        car->SetBodyDamage(damage.bodyDamage, damage.fuelTankHealth);
        // A new controller inherits the burn countdown so a burning car still
        // explodes; observers' explosion requests stay suppressed by the hook.
        car->SetBurnState(damage.burning != 0, damage.burnTimer, damage.burnDuration);
        stream.appliedDamageRevision = state->nativeDamageRevision;
        stream.lastDamageApply = now;
    }

    void CarService::ReportMesh(World::WorldService &world, uint64_t networkId, Stream &stream) {
        if (!stream.meshDirty) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (stream.lastMeshReport != std::chrono::steady_clock::time_point {} &&
            now - stream.lastMeshReport < std::chrono::milliseconds(100)) {
            return;
        }
        auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        const auto native = _nativeById.find(networkId);
        if (!state || !state->nativeDamageValid || state->nativeDamageRevision > stream.appliedDamageRevision ||
            state->meshRevision > stream.appliedMeshRevision ||
            native == _nativeById.end() || native->second.removalQueued) {
            // A controller transfer can arrive before the prior owner's mesh
            // checkpoint. Do not replace it with this client's clean mesh.
            return;
        }
        auto *car = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!car) {
            return;
        }
        std::vector<Shared::Car::MeshVertex> vertices;
        if (!CaptureNativeMesh(*car, vertices)) {
            return;
        }
        stream.meshDirty = false;
        stream.lastMeshReport = now;
        if (stream.hasReportedMesh && vertices == stream.lastReportedMesh) {
            return;
        }
        const uint16_t count = std::max<uint16_t>(1, static_cast<uint16_t>((vertices.size() + Shared::Car::kMeshVerticesPerChunk - 1) / Shared::Car::kMeshVerticesPerChunk));
        const uint32_t sequence = ++stream.meshReportSequence;
        for (uint16_t index = 0; index < count; ++index) {
            Shared::Car::MeshReportChunk chunk;
            chunk.networkId = networkId;
            chunk.missionGeneration = world.LoadedMissionGeneration();
            chunk.baseRepairRevision = state->repairRevision;
            chunk.sequence = sequence;
            chunk.chunkIndex = index;
            chunk.chunkCount = count;
            chunk.totalVertices = static_cast<uint16_t>(vertices.size());
            chunk.vertexCount = static_cast<uint16_t>(std::min<size_t>(Shared::Car::kMeshVerticesPerChunk,
                vertices.size() - static_cast<size_t>(index) * Shared::Car::kMeshVerticesPerChunk));
            for (uint16_t i = 0; i < chunk.vertexCount; ++i) {
                chunk.vertices[i] = vertices[static_cast<size_t>(index) * Shared::Car::kMeshVerticesPerChunk + i];
            }
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(chunk);
        }
        stream.lastReportedMesh = std::move(vertices);
        stream.hasReportedMesh = true;
    }

    void CarService::OnMeshCheckpoint(const Shared::Car::MeshCheckpointChunk &chunk) {
        auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(chunk.networkId);
        if (!state || state->missionGeneration != chunk.missionGeneration ||
            !Shared::Car::ValidateMeshChunkShape(chunk.totalVertices, chunk.chunkIndex,
                                                  chunk.chunkCount, chunk.vertexCount)) {
            return;
        }
        auto &stream = _streams[chunk.networkId];
        if (!Shared::Car::WantsMeshCheckpoint(chunk.revision, chunk.repairRevision, state->repairRevision, stream.appliedMeshRevision,
                                              stream.checkpointRevision, stream.pendingMeshRevision)) {
            return;
        }
        if (chunk.revision != stream.pendingMeshRevision) {
            stream.pendingMeshRevision = chunk.revision;
            stream.pendingMeshSourceGuid = chunk.sourceGuid;
            stream.pendingMeshRepairRevision = chunk.repairRevision;
            stream.pendingMeshChunkCount = chunk.chunkCount;
            stream.pendingMeshTotal = chunk.totalVertices;
            stream.pendingMeshVertices.resize(chunk.totalVertices);
            stream.pendingMeshReceived.assign(chunk.chunkCount, 0);
        }
        if (stream.pendingMeshSourceGuid != chunk.sourceGuid ||
            stream.pendingMeshRepairRevision != chunk.repairRevision ||
            stream.pendingMeshChunkCount != chunk.chunkCount ||
            stream.pendingMeshTotal != chunk.totalVertices ||
            stream.pendingMeshReceived[chunk.chunkIndex]) {
            return;
        }
        const size_t offset = static_cast<size_t>(chunk.chunkIndex) * Shared::Car::kMeshVerticesPerChunk;
        for (uint16_t i = 0; i < chunk.vertexCount; ++i) {
            if (chunk.vertices[i].zone >= Shared::Car::DamageState::kMaxZones || chunk.vertices[i].lod > 1) {
                stream.pendingMeshReceived.clear();
                stream.pendingMeshVertices.clear();
                stream.pendingMeshRevision = 0;
                return;
            }
            stream.pendingMeshVertices[offset + i] = chunk.vertices[i];
        }
        stream.pendingMeshReceived[chunk.chunkIndex] = 1;
        if (std::find(stream.pendingMeshReceived.begin(), stream.pendingMeshReceived.end(), 0) != stream.pendingMeshReceived.end()) {
            return;
        }
        stream.checkpointRevision = chunk.revision;
        stream.checkpointSourceGuid = chunk.sourceGuid;
        stream.checkpointRepairRevision = chunk.repairRevision;
        stream.checkpointVertices = std::move(stream.pendingMeshVertices);
        stream.pendingMeshReceived.clear();
        stream.pendingMeshRevision = 0;
    }

    void CarService::OnAuthoritativeHit(const Shared::Car::AuthoritativeHit &hit) {
        const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(hit.carId);
        const glm::vec3 localHit(hit.localHitX, hit.localHitY, hit.localHitZ);
        const glm::vec3 localDirection(hit.localDirectionX, hit.localDirectionY, hit.localDirectionZ);
        if (!state || state->missionGeneration != hit.missionGeneration || hit.controllerGuid == 0 || hit.serverSequence == 0 ||
            !std::isfinite(hit.damage) || hit.damage <= 0.0f || hit.damage > 2000.0f ||
            !std::isfinite(localHit.x) || !std::isfinite(localHit.y) || !std::isfinite(localHit.z) || glm::length(localHit) > 15.0f ||
            !std::isfinite(localDirection.x) || !std::isfinite(localDirection.y) || !std::isfinite(localDirection.z) ||
            glm::length(localDirection) < 0.8f || glm::length(localDirection) > 1.2f) {
            return;
        }
        auto &stream = _streams[hit.carId];
        if (hit.serverSequence <= stream.lastQueuedHitSequence || stream.pendingHits.size() >= 128) {
            return;
        }
        stream.lastQueuedHitSequence = hit.serverSequence;
        stream.pendingHits.push_back({hit, std::chrono::steady_clock::now()});
    }

    void CarService::RecordContact(uint64_t first, uint64_t second) {
        const double now = LocalClockMs();
        auto &times      = _contacts[std::minmax(first, second)];
        std::erase_if(times, [&](double at) { return now - at > Shared::Car::kContactExpiryMs; });
        times.push_back(now);
        if (_contacts.size() > 256) {
            std::erase_if(_contacts, [&](const auto &entry) { return entry.second.empty() || now - entry.second.back() > Shared::Car::kContactExpiryMs; });
        }
    }

    void CarService::OnCarCollision(SDK::Car::NativeCar *car, SDK::Player::NativeActor *other) {
        const auto id      = _idByNative.find(car);
        const auto otherId = _idByNative.find(other);
        if (id == _idByNative.end() || otherId == _idByNative.end()) {
            return;
        }
        if (const auto stream = _streams.find(id->second); stream != _streams.end() && stream->second.simulationController) {
            RecordContact(id->second, otherId->second);
        }
    }

    bool CarService::ContactGhosted(SDK::Car::NativeCar *car, SDK::Player::NativeActor *other) const {
        if (_replayedContacts.empty()) {
            return false;
        }
        const auto id      = _idByNative.find(car);
        const auto otherId = _idByNative.find(other);
        if (id == _idByNative.end() || otherId == _idByNative.end()) {
            return false;
        }
        const auto replayed = _replayedContacts.find(std::minmax(id->second, otherId->second));
        return replayed != _replayedContacts.end() && Shared::Car::ContactReplayed(replayed->second, LocalClockMs());
    }

    void CarService::OnCarHit(SDK::Car::NativeCar *car, SDK::Player::NativeActor *striker, const SDK::Player::Vector3 &position,
                              const SDK::Player::Vector3 &impulse, const SDK::Player::Vector3 &normal, float mass) {
        const auto id        = _idByNative.find(car);
        const auto strikerId = _idByNative.find(striker);
        if (!_world || !_world->IsReady() || id == _idByNative.end() || strikerId == _idByNative.end()) {
            return;
        }
        const auto stream        = _streams.find(id->second);
        const auto strikerStream = _streams.find(strikerId->second);
        if (stream == _streams.end() || strikerStream == _streams.end()) {
            return;
        }
        if (stream->second.simulationController) {
            RecordContact(id->second, strikerId->second);
            return;
        }
        // Here the hit car's dent and damage are dropped (VehicleDeformHook)
        // and its shove lasts only until its controller's poses pull it
        // back: if the controller's physics missed the contact (the cars a
        // little apart there), the hit never happened for anyone.
        const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(id->second);
        auto *frame       = car->Frame();
        const glm::vec3 shove(impulse.x, impulse.y, impulse.z);
        const float strength = glm::length(shove);
        if (!strikerStream->second.simulationController || !state || !frame || state->missionGeneration != _world->LoadedMissionGeneration() ||
            state->terminalState != Shared::Entities::CarEntity::TerminalState::Active || state->simulationControllerGuid == 0 || !std::isfinite(strength) ||
            !(Shared::Car::ContactDeltaV(strength, car->Vehicle().Mass()) >= Shared::Car::kContactMinDeltaV)) {
            return;
        }
        auto &lastAt = _reportedContacts.try_emplace({id->second, strikerId->second}, -1.0e9).first->second;
        if (!Shared::Car::ContactReportDue(LocalClockMs(), lastAt)) {
            return;
        }
        const auto center = frame->WorldPosition();
        const auto basis  = frame->GetWorldBasis();
        const glm::vec3 right(basis.right.x, basis.right.y, basis.right.z);
        const glm::vec3 up(basis.up.x, basis.up.y, basis.up.z);
        const glm::vec3 forward(basis.forward.x, basis.forward.y, basis.forward.z);
        const auto local = [&](const glm::vec3 &vector) { return glm::vec3(glm::dot(vector, right), glm::dot(vector, up), glm::dot(vector, forward)); };
        Shared::Car::ContactReport report;
        report.carId             = id->second;
        report.strikerId         = strikerId->second;
        report.missionGeneration = state->missionGeneration;
        report.position          = {position.x, position.y, position.z};
        report.localPosition     = local(report.position - glm::vec3(center.x, center.y, center.z));
        report.localImpulse      = local(shove);
        report.localNormal       = local(glm::vec3(normal.x, normal.y, normal.z));
        report.mass              = mass;
        if (!IsFiniteVector(report.position) || !IsFiniteVector(report.localPosition) || !IsFiniteVector(report.localImpulse) ||
            !IsFiniteVector(report.localNormal) || !std::isfinite(report.mass)) {
            return;
        }
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
    }

    void CarService::OnContactImpact(const Shared::Car::ContactImpact &impact) {
        const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(impact.carId);
        if (!state || state->missionGeneration != impact.missionGeneration || impact.controllerGuid == 0 || impact.serverSequence == 0 ||
            !IsFiniteVector(impact.localPosition) || glm::length(impact.localPosition) > 6.0f || !IsFiniteVector(impact.localImpulse) ||
            !IsFiniteVector(impact.localNormal) || glm::length(impact.localNormal) < 0.5f || glm::length(impact.localNormal) > 1.5f ||
            !std::isfinite(impact.mass) || impact.mass <= 0.0f || !std::isfinite(impact.maxDeltaV) || impact.maxDeltaV < 0.0f) {
            return;
        }
        auto &stream = _streams[impact.carId];
        if (impact.serverSequence <= stream.lastContactSequence || stream.pendingContacts.size() >= 32) {
            return;
        }
        stream.lastContactSequence = impact.serverSequence;
        const double now           = LocalClockMs();
        stream.pendingContacts.push_back({impact, now, now - static_cast<double>(impact.ageMs)});
    }

    void CarService::ApplyContactImpacts(World::WorldService &world, uint64_t networkId, Stream &stream) {
        const auto myGuid = static_cast<uint64_t>(Framework::CoreModules::GetReplication()->GetMyGUID());
        const double now  = LocalClockMs();
        while (!stream.pendingContacts.empty()) {
            const auto &pending = stream.pendingContacts.front();
            const auto &impact  = pending.impact;
            if (impact.controllerGuid != myGuid || impact.missionGeneration != world.LoadedMissionGeneration() ||
                now - pending.arrival > Shared::Car::kContactExpiryMs) {
                stream.pendingContacts.pop_front();
                continue;
            }
            // Its own physics had this contact, so it is in the car already,
            // or may yet: the striker's replica is predicted to the present,
            // but the estimate of when is only as good as the pings.
            const auto seen   = _contacts.find(std::minmax(networkId, impact.strikerId));
            const auto replay = Shared::Car::ContactReplayDue(seen != _contacts.end() ? std::span<const double>(seen->second) : std::span<const double> {},
                                                              pending.contactAt, now);
            if (replay == Shared::Car::ContactReplay::Drop) {
                stream.pendingContacts.pop_front();
                continue;
            }
            if (replay == Shared::Car::ContactReplay::Wait) {
                return;
            }
            const auto native = _nativeById.find(networkId);
            if (native == _nativeById.end() || native->second.removalQueued || !native->second.frame) {
                return;
            }
            auto *car     = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
            auto *striker = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(world.NativeObjects().FindByNetwork(impact.strikerId)));
            if (!car) {
                return;
            }
            if (!striker || striker->GetType() != SDK::Player::NativeActor::Type::Car) {
                stream.pendingContacts.pop_front();
                continue;
            }
            // Where the reporter had it on this car, in this car's frame now;
            // no faster than the server allows from both cars' speeds.
            auto *frame       = native->second.frame;
            const auto center = frame->WorldPosition();
            const auto basis  = frame->GetWorldBasis();
            const glm::vec3 right(basis.right.x, basis.right.y, basis.right.z);
            const glm::vec3 up(basis.up.x, basis.up.y, basis.up.z);
            const glm::vec3 forward(basis.forward.x, basis.forward.y, basis.forward.z);
            const auto toWorld       = [&](const glm::vec3 &local) { return right * local.x + up * local.y + forward * local.z; };
            const glm::vec3 position = glm::vec3(center.x, center.y, center.z) + toWorld(impact.localPosition);
            const glm::vec3 impulse  = Shared::Car::ClampContactImpulse(toWorld(impact.localImpulse), car->Vehicle().Mass(), impact.maxDeltaV);
            const glm::vec3 normal   = toWorld(impact.localNormal);
            const float mass         = impact.mass;
            stream.pendingContacts.pop_front();
            if (!IsFiniteVector(position) || !IsFiniteVector(impulse) || !IsFiniteVector(normal)) {
                continue;
            }
            ApplyCarContact(car, static_cast<SDK::Car::NativeCar *>(static_cast<void *>(striker)), {position.x, position.y, position.z},
                            {impulse.x, impulse.y, impulse.z}, {normal.x, normal.y, normal.z}, mass);
            stream.meshDirty = true;
            if (_replayedContacts.size() > 256) {
                std::erase_if(_replayedContacts, [&](const auto &entry) { return !Shared::Car::ContactReplayed(entry.second, now); });
            }
            _replayedContacts[std::minmax(networkId, impact.strikerId)] = now;
        }
    }

    void CarService::ApplyAuthoritativeHits(World::WorldService &world, uint64_t networkId, Stream &stream) {
        auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        if (!state || !stream.simulationController ||
            (state->nativeDamageValid && state->nativeDamageRevision > stream.appliedDamageRevision) ||
            state->meshRevision > stream.appliedMeshRevision) {
            return;
        }
        const auto myGuid = static_cast<uint64_t>(Framework::CoreModules::GetReplication()->GetMyGUID());
        const auto now = std::chrono::steady_clock::now();
        while (!stream.pendingHits.empty()) {
            const auto *currentState = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
            if (!currentState || currentState->missionGeneration != world.LoadedMissionGeneration() ||
                currentState->simulationControllerGuid != myGuid) {
                return;
            }
            const auto pending = stream.pendingHits.front();
            const auto &hit = pending.hit;
            if (hit.serverSequence <= stream.lastAppliedHitSequence ||
                hit.missionGeneration != world.LoadedMissionGeneration() ||
                now - pending.received > std::chrono::seconds(2)) {
                stream.pendingHits.pop_front();
                continue;
            }
            if (hit.controllerGuid != myGuid) {
                // A hit for another controller epoch must not stall later ones.
                stream.pendingHits.pop_front();
                continue;
            }
            const auto native = _nativeById.find(networkId);
            if (native == _nativeById.end() || native->second.removalQueued || !native->second.frame) {
                return;
            }
            auto *car = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
            if (!car) {
                return;
            }
            auto *frame = native->second.frame;
            const auto center = frame->WorldPosition();
            const auto basis = frame->GetWorldBasis();
            const glm::vec3 right(basis.right.x, basis.right.y, basis.right.z);
            const glm::vec3 up(basis.up.x, basis.up.y, basis.up.z);
            const glm::vec3 forward(basis.forward.x, basis.forward.y, basis.forward.z);
            const glm::vec3 worldHit = glm::vec3(center.x, center.y, center.z) + right * hit.localHitX + up * hit.localHitY + forward * hit.localHitZ;
            const glm::vec3 worldDirection = glm::normalize(right * hit.localDirectionX + up * hit.localDirectionY + forward * hit.localDirectionZ);
            const glm::vec3 normal(hit.normalX, hit.normalY, hit.normalZ);
            if (!std::isfinite(worldHit.x) || !std::isfinite(worldHit.y) || !std::isfinite(worldHit.z) ||
                !std::isfinite(worldDirection.x) || !std::isfinite(worldDirection.y) || !std::isfinite(worldDirection.z) ||
                !std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z)) {
                stream.pendingHits.pop_front();
                continue;
            }
            // Retail's null-frame fallback rerays exactly one unit up to the
            // impact point, so rounding in the car-local transform can stop
            // it short of the surface. Cross the surface instead and hand the
            // hit frame to Hit, as C_game::TickShoot does for a live bullet.
            const glm::vec3 rayStart = worldHit - worldDirection * 0.5f;
            SDK::Scene::NativeCollision collision;
            auto *hitFrame = SDK::Core::Mission::Get()->GetScene()->TestColHierarchy(
                {rayStart.x, rayStart.y, rayStart.z}, {worldDirection.x, worldDirection.y, worldDirection.z},
                car->Vehicle().ModelFrame(), collision);
            // The simulation host's traffic answers its attacker natively
            // (a rammed police car books the crime, a mafia crew turns on
            // the shooter); a player's own car never needed the attacker.
            SDK::Player::NativeActor *attacker = nullptr;
            if (Simulation::IsHost() && hit.shooterId) {
                attacker = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(world.NativeObjects().FindByNetwork(hit.shooterId)));
            }
            if (!ApplyAuthoritativeCarHit(car, {worldDirection.x, worldDirection.y, worldDirection.z},
                                          {worldHit.x, worldHit.y, worldHit.z}, {normal.x, normal.y, normal.z}, hit.damage,
                                          hitFrame, attacker, Shared::Car::NativeHitType(hit.kind))) {
                return;
            }
            stream.lastAppliedHitSequence = hit.serverSequence;
            // Native Hit can destroy the car and clear this queue through the
            // lifecycle hook. Only remove the record if it is still present.
            if (stream.pendingHits.empty() || stream.pendingHits.front().hit.serverSequence != hit.serverSequence) {
                return;
            }
            stream.pendingHits.pop_front();
            stream.meshDirty = true;
        }
    }

    void CarService::SyncMesh(World::WorldService &world, uint64_t networkId, Stream &stream) {
        auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        if (!state || !state->nativeDamageValid) {
            return;
        }
        const auto native = _nativeById.find(networkId);
        if (native == _nativeById.end() || native->second.removalQueued) {
            return;
        }
        // The newest complete checkpoint goes on at once, whichever revision
        // the replicated meshRevision names: the server pushes each one ahead
        // of that field, and a pile-up raises it every 100 ms, so waiting for
        // a match (or fetching once a second and dropping what was outrun)
        // showed dents seconds late. Revisions only grow, so it converges.
        if (stream.checkpointRevision <= stream.appliedMeshRevision || stream.checkpointRepairRevision != state->repairRevision) {
            const auto now = std::chrono::steady_clock::now();
            // The revision the car names never arrived whole (a restored
            // snapshot, one pushed before this client knew the car): fetch
            // it. A reply fills in an assembly of the same revision.
            if (state->meshRevision > std::max(stream.appliedMeshRevision, stream.checkpointRevision) &&
                (stream.lastMeshRequest == std::chrono::steady_clock::time_point {} || now - stream.lastMeshRequest >= std::chrono::seconds(1))) {
                Shared::Car::MeshCheckpointRequest request;
                request.networkId = networkId;
                request.missionGeneration = world.LoadedMissionGeneration();
                Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(request);
                stream.lastMeshRequest = now;
            }
            return;
        }
        auto *car = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!car) {
            return;
        }
        const uint64_t myGuid = static_cast<uint64_t>(Framework::CoreModules::GetReplication()->GetMyGUID());
        if (stream.simulationController && stream.checkpointSourceGuid == myGuid) {
            stream.appliedMeshRevision = stream.checkpointRevision;
            return;
        }
        auto &vehicle = car->Vehicle();
        if (vehicle.DeformZones().Size() != state->nativeDamage.zoneCount) {
            return;
        }
        if (!Shared::Car::ValidateMeshVertexSet(stream.checkpointVertices, state->nativeDamage.zoneCount)) {
            return;
        }
        std::array<std::array<std::vector<uint32_t>, 2>, Shared::Car::DamageState::kMaxZones> groups;
        for (const auto &vertex : stream.checkpointVertices) {
            if (vertex.zone >= state->nativeDamage.zoneCount || vertex.lod >= vehicle.DeformLODCount(vertex.zone)) {
                return;
            }
            groups[vertex.zone][vertex.lod].push_back(vertex.displacement);
        }
        for (uint8_t zone = 0; zone < state->nativeDamage.zoneCount; ++zone) {
            const int lodCount = vehicle.DeformLODCount(zone);
            if (lodCount <= 0) { return; }
            for (int lod = 0; lod < lodCount; ++lod) {
                if (!vehicle.ValidateDeformCheckpoint(zone, lod, groups[zone][lod])) { return; }
            }
        }
        for (uint8_t zone = 0; zone < state->nativeDamage.zoneCount; ++zone) {
            const int lodCount = vehicle.DeformLODCount(zone);
            for (int lod = 0; lod < lodCount; ++lod) {
                if (!vehicle.ApplyDeformCheckpoint(zone, lod, groups[zone][lod])) { return; }
            }
        }
        stream.appliedMeshRevision = stream.checkpointRevision;
    }

    void CarService::OnNativeDeformed(SDK::Car::NativeCar *car) {
        const auto native = _idByNative.find(car);
        if (native != _idByNative.end()) {
            if (auto stream = _streams.find(native->second); stream != _streams.end() && stream->second.simulationController) {
                stream->second.meshDirty = true;
            }
        }
    }

    void CarService::ReportMovement(World::WorldService &world, uint64_t networkId, Stream &stream) {
        const auto now = std::chrono::steady_clock::now();
        // Against a schedule, not the last send: checked once a frame, "33 ms
        // since the last" turns into 50 or 66 ms gaps whenever a frame ends
        // just short, and the uneven cadence raises every observer's delay.
        if (stream.nextSend != std::chrono::steady_clock::time_point {} && now + kMovementSendSlack < stream.nextSend) {
            return;
        }
        const auto native = _nativeById.find(networkId);
        if (native == _nativeById.end() || native->second.removalQueued) {
            return;
        }
        auto *actor = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!actor) {
            return;
        }
        auto *frame         = native->second.frame;
        const auto position = frame->WorldPosition();
        const auto basis    = frame->GetWorldBasis();
        const glm::vec3 forward(basis.forward.x, basis.forward.y, basis.forward.z);
        const glm::vec3 up(basis.up.x, basis.up.y, basis.up.z);
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(forward.x) || !std::isfinite(forward.y) || !std::isfinite(forward.z) || !std::isfinite(up.x) || !std::isfinite(up.y) || !std::isfinite(up.z)
            || glm::length(forward) < 0.5f || glm::length(up) < 0.5f) {
            return;
        }
        const glm::vec3 axisZ = glm::normalize(forward);
        const glm::vec3 right = glm::cross(up, axisZ);
        if (glm::length(right) < 0.1f) {
            return;
        }
        const glm::vec3 axisX    = glm::normalize(right);
        const glm::vec3 axisY    = glm::normalize(glm::cross(axisZ, axisX));
        const glm::quat rotation = glm::normalize(glm::quat_cast(glm::mat3(axisX, axisY, axisZ)));
        Shared::Car::Movement movement;
        movement.networkId         = networkId;
        movement.missionGeneration = world.LoadedMissionGeneration();
        if (auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId)) {
            movement.transformRevision = state->transformRevision;
        }
        movement.dynamicsCommandRevision = stream.appliedDynamicsCommandRevision;
        movement.sequence          = ++stream.sequence;
        // The physics time this pose was integrated to; wall-clock read
        // times would carry up to a frame of jitter.
        const double stamp         = std::floor(stream.simClock);
        movement.clockMs           = static_cast<uint32_t>(static_cast<int64_t>(stamp));
        movement.clockFraction     = static_cast<uint8_t>(std::clamp((stream.simClock - stamp) * 256.0, 0.0, 255.0));
        movement.x                 = position.x;
        movement.y                 = position.y;
        movement.z                 = position.z;
        movement.qw                = rotation.w;
        movement.qx                = rotation.x;
        movement.qy                = rotation.y;
        movement.qz                = rotation.z;
        auto velocity = actor->Vehicle().LinearVelocity();
        auto angularVelocity = actor->Vehicle().AngularVelocity();
        const float steering = actor->Vehicle().SteeringInput();
        const float fuel = actor->Vehicle().Fuel();
        const float fuelTankCapacity = actor->Vehicle().FuelTankCapacity();
        const float engineRotations = actor->Vehicle().EngineRotations();
        const int gear = actor->Vehicle().Gear();
        const int maximumGear = actor->Vehicle().MaximumGear();
        if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z) ||
            !std::isfinite(angularVelocity.x) || !std::isfinite(angularVelocity.y) || !std::isfinite(angularVelocity.z) ||
            !std::isfinite(steering) || !std::isfinite(fuel) || !std::isfinite(fuelTankCapacity) || !std::isfinite(engineRotations) ||
            fuelTankCapacity < 0.0f || fuelTankCapacity > 500.0f || maximumGear < 0 || maximumGear > 10) {
            return;
        }
        // The pose must keep flowing even when native telemetry leaves the
        // server's accepted range, e.g. engine revs with a driven wheel shot
        // off. Clamp those values instead of dropping the whole report.
        const auto clampLength = [](SDK::Player::Vector3 &value, float maximum) {
            const float length = glm::length(glm::vec3(value.x, value.y, value.z));
            if (length > maximum) {
                value = {value.x * maximum / length, value.y * maximum / length, value.z * maximum / length};
            }
        };
        clampLength(velocity, 119.0f);
        clampLength(angularVelocity, 29.0f);
        movement.velocityX = velocity.x;
        movement.velocityY = velocity.y;
        movement.velocityZ = velocity.z;
        movement.angularVelocityX = angularVelocity.x;
        movement.angularVelocityY = angularVelocity.y;
        movement.angularVelocityZ = angularVelocity.z;
        movement.steeringInput = std::clamp(steering, -2.0f, 2.0f);
        movement.fuel = std::clamp(fuel, 0.0f, fuelTankCapacity);
        movement.fuelTankCapacity = fuelTankCapacity;
        movement.engineRotations = std::clamp(engineRotations, 0.0f, 20000.0f);
        movement.gear = std::clamp(gear, -1, maximumGear);
        movement.maximumGear = maximumGear;
        movement.lightState = actor->Vehicle().LightState();
        movement.hornOn = actor->Vehicle().HornOn();
        const auto inputs = actor->Vehicle().Inputs();
        const auto unit = [](float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; };
        movement.powerInput = unit(inputs.power);
        movement.brakeInput = unit(inputs.brake);
        movement.handbrakeInput = unit(inputs.handbrake);
        movement.clutchInput = std::isfinite(inputs.clutch) ? std::clamp(inputs.clutch, 0.0f, 1.0f) : 1.0f;
        movement.speedLimited = actor->Vehicle().SpeedLimited();
        for (int i = 0; i < std::min(actor->Vehicle().WheelCount(), 8); ++i) {
            // The retail replay record's skid bit: skidding while in contact.
            if (const auto *wheel = actor->Vehicle().Wheel(i);
                wheel && (wheel->flags & SDK::Car::kWheelSkidding) && (wheel->flags & SDK::Car::kWheelInContact)) {
                movement.skidMask |= static_cast<uint8_t>(1u << i);
            }
        }
        for (uint8_t seat = 0; seat < movement.doorTargets.size(); ++seat) {
            const float door = actor->Vehicle().DoorTarget(seat);
            movement.doorTargets[seat] = std::isfinite(door) && door > 0.0f ? static_cast<uint8_t>(std::lround(std::clamp(door, 0.0f, 1.0f) * 255.0f)) : 0;
        }
        auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        movement.slowFields = !stream.lastSlowMovement || SlowFieldsChanged(movement, *stream.lastSlowMovement) || now - stream.lastSlowSent >= kIdleHeartbeat ||
                              (state && !state->dynamicsValid);
        if (!movement.slowFields && stream.lastMovement && AtRest(movement, *stream.lastMovement) && now - stream.lastSent < kIdleHeartbeat) {
            --stream.sequence;
            return;
        }
        // The server withholds this client's own pose (CarEntity::
        // GetTransformSource), so the local replica follows the native car
        // for everything here that reads it.
        if (state) {
            state->position        = {movement.x, movement.y, movement.z};
            state->rotation        = rotation;
            state->velocity        = {movement.velocityX, movement.velocityY, movement.velocityZ};
            state->angularVelocity = {movement.angularVelocityX, movement.angularVelocityY, movement.angularVelocityZ};
        }
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(movement, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
        if (Debug::SyncTrace::Enabled() && Debug::SyncTrace::Near({movement.x, movement.y, movement.z})) {
            Debug::SyncTrace::Truth("car", networkId)
                .Num("clk", Shared::Entities::CarEntity::ClockMs(movement.clockMs, movement.clockFraction))
                .Bool("sent", true)
                .Int("seq", movement.sequence)
                .Vec("p", {movement.x, movement.y, movement.z})
                .Quat("q", rotation)
                .Vec("v", {movement.velocityX, movement.velocityY, movement.velocityZ})
                .Vec("w", {movement.angularVelocityX, movement.angularVelocityY, movement.angularVelocityZ})
                .Num("steer", movement.steeringInput)
                .Int("gear", movement.gear)
                .Num("power", movement.powerInput)
                .Num("brake", movement.brakeInput)
                .Num("hb", movement.handbrakeInput)
                .Num("clutch", movement.clutchInput)
                .Num("rpm", movement.engineRotations)
                .Int("lights", movement.lightState)
                .Int("skid", movement.skidMask);
        }
        // A schedule far behind (a hitch, a car at rest) starts over.
        stream.nextSend     = stream.nextSend != std::chrono::steady_clock::time_point {} && now - stream.nextSend < kMovementSendInterval
                                  ? stream.nextSend + kMovementSendInterval
                                  : now + kMovementSendInterval;
        stream.lastSent     = now;
        stream.lastMovement = movement;
        if (movement.slowFields) {
            stream.lastSlowMovement = movement;
            stream.lastSlowSent     = now;
        }
    }

    void CarService::SyncDynamics(World::WorldService &world, uint64_t networkId, Stream &stream) {
        const auto native = _nativeById.find(networkId);
        if (native == _nativeById.end() || native->second.removalQueued) {
            return;
        }
        auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        auto *actor = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!state || !state->dynamicsValid || !actor) {
            return;
        }
        auto &vehicle = actor->Vehicle();
        if (stream.simulationController) {
            if (!stream.controllerDynamicsInitialized || state->dynamicsCommandRevision > stream.appliedDynamicsCommandRevision) {
                if (!stream.controllerDynamicsInitialized) {
                    // Pedals replayed while observing would otherwise stay
                    // held; a local driver's AI_drive rewrites them each frame.
                    vehicle.ApplyInputs({0.0f, 0.0f, 0.0f, 1.0f});
                    // As an observer this car's physics was forced to replicated
                    // poses every frame, which leaves its wheel and contact
                    // state inconsistent; released as is, it can launch the
                    // car. Rebuild it from the frame, as a teleport does.
                    ResetNativePhysics(*actor, networkId);
                    const auto angular = state->angularVelocity;
                    if (std::isfinite(angular.x) && std::isfinite(angular.y) && std::isfinite(angular.z) && glm::length(angular) <= 30.0f) {
                        vehicle.SetAngularVelocity({angular.x, angular.y, angular.z});
                    }
                    if (std::isfinite(state->steeringInput) && std::abs(state->steeringInput) <= 2.0f) {
                        vehicle.ApplySteeringInput(state->steeringInput);
                    }
                }
                vehicle.SetFuel(std::clamp(state->fuel, 0.0f, vehicle.FuelTankCapacity()));
                vehicle.ApplyLightState(state->lightState);
                vehicle.SetHorn(state->hornOn);
                stream.appliedDynamicsCommandRevision = state->dynamicsCommandRevision;
                stream.controllerDynamicsInitialized = true;
            }
            // Retail pumps can refill the local native car and score without
            // a server purchase. Undo that local increase before reporting a
            // new pose; only server SetFuel may raise the authoritative level.
            if (std::isfinite(state->fuel) && vehicle.Fuel() > state->fuel + kFuelSyncTolerance) {
                vehicle.SetFuel(std::clamp(state->fuel, 0.0f, vehicle.FuelTankCapacity()));
            }
            // The host's police AI switches its sirens itself and reports them.
            if (!state->hostDriven) {
                ApplySiren(vehicle, *state);
            }
            return;
        }
        if (std::isfinite(state->steeringInput) && std::abs(state->steeringInput) <= 2.0f) {
            vehicle.ApplySteeringInput(state->steeringInput);
        }
        if (std::isfinite(state->fuel) && state->fuel >= 0.0f && std::abs(vehicle.Fuel() - state->fuel) > kFuelSyncTolerance) {
            vehicle.SetFuel(state->fuel);
        }
        // Brake and reverse lights, wheel lock, skids and engine revs are all
        // derived by the native tick from the pedals and the engaged gear.
        // Skipped while the local player is entering or in the driver's
        // seat: Use_Actor sets that driver's gearbox mode and AI_drive owns
        // the pedals. A passenger's AI_drive never runs (C_player::AI only
        // drives from seat 0), so in another seat the car keeps the
        // controller's pedals and gear like any observed car; skipping them
        // there froze them at whatever they were when the passenger got in.
        const auto *local = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(SDK::Player::CurrentPlayer()));
        const SDK::Player::NativeActor *self = actor;
        const bool localDriving = local && ((local->usedActorEnter == self && local->seatId == 0) || local->usedActorLeave == self);
        if (!localDriving) {
            vehicle.ApplyInputs({state->powerInput, state->brakeInput, state->handbrakeInput, state->clutchInput});
            vehicle.DisableAutomaticGearbox();
            if (vehicle.Gear() != state->gear && state->gear >= -1 && state->gear <= vehicle.MaximumGear()) {
                (void)vehicle.SetGear(state->gear);
            }
            if (vehicle.SpeedLimited() != state->speedLimited) {
                vehicle.SetSpeedLimited(state->speedLimited);
            }
        }
        SyncDoors(*actor, *state);
        static_assert(Shared::Car::Movement::kReplicatedLightMask == SDK::Car::NativeVehicle::kReplicatedLightMask);
        vehicle.ApplyLightState(SirenLights(state->lightState, state->sirenOn));
        ApplySiren(vehicle, *state);
        if (vehicle.HornOn() != state->hornOn) {
            vehicle.SetHorn(state->hornOn);
        }
    }

    void CarService::SyncRadar(World::WorldService &world, uint64_t networkId) {
        const auto native = _nativeById.find(networkId);
        const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        // An adopted car keeps the blip retail gave it.
        if (native == _nativeById.end() || native->second.removalQueued || !state || native->second.origin != Origin::Created) {
            return;
        }
        auto *car = world.NativeObjects().Resolve(native->second.handle);
        if (!car) {
            return;
        }
        const auto *local      = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(SDK::Player::CurrentPlayer()));
        const bool localDriver = local && local->usedActorEnter == static_cast<const SDK::Player::NativeActor *>(car) && local->seatId == 0;
        const uint32_t color   = state->radarColor != 0 ? state->radarColor : localDriver ? 0xFFFFFFFFu : 0u;
        if (native->second.radarColor == color)
            return;
        auto &indicators = SDK::UI::NativeIndicators::Get();
        if (color != 0) {
            indicators.RadarAddCar(car, color);
        }
        else {
            indicators.RadarRemoveCar(car);
        }
        native->second.radarColor = color;
    }

    uint32_t CarService::SirenLights(uint32_t lightState, bool sirenOn) {
        return sirenOn ? lightState | SDK::Car::NativeVehicle::kSirenLightMask : lightState & ~SDK::Car::NativeVehicle::kSirenLightMask;
    }

    void CarService::ApplySiren(SDK::Car::NativeVehicle &vehicle, const Shared::Entities::CarEntity &state) {
        if (vehicle.SirenOn() != state.sirenOn) {
            vehicle.SetSirenOn(state.sirenOn);
        }
        const uint32_t lights = vehicle.LightState();
        if (lights != SirenLights(lights, state.sirenOn)) {
            vehicle.ApplyLightState(SirenLights(lights, state.sirenOn));
        }
    }

    void CarService::SyncDoors(SDK::Car::NativeCar &car, const Shared::Entities::CarEntity &state) {
        auto &seats = static_cast<SDK::Seat::NativeCar &>(car);
        auto &vehicle = car.Vehicle();
        const auto *local = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(SDK::Player::CurrentPlayer()));
        const SDK::Player::NativeActor *self = &car;
        for (uint8_t seat = 0; seat < state.seatCount && seat < Shared::Entities::CarEntity::kMaxSeats && seat < vehicle.SeatSlotCount(); ++seat) {
            const float current = vehicle.DoorTarget(seat);
            if (current < 0.0f) {
                continue;
            }
            // A door animation that is running here owns its door: the local
            // player's own, or a replayed entry or exit on this client.
            if (local && (local->usedActorEnter == self || local->usedActorLeave == self) && local->seatId == seat) {
                continue;
            }
            if (auto *owner = seats.GetOwner(seat); owner && (owner->GetType() == SDK::Player::NativeActor::Type::Player || owner->GetType() == SDK::Player::NativeActor::Type::Entity) &&
                                                     static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(owner))->usedActorLeave == self) {
                continue;
            }
            const float target = state.doorTargets[seat] / 255.0f;
            if (std::abs(current - target) > 0.02f) {
                vehicle.OpenDoor(seat, target);
            }
        }
    }

    bool CarService::ToggleSiren(World::WorldService &world) {
        if (!world.IsReady()) {
            return false;
        }
        auto *player = SDK::Player::CurrentPlayer();
        const auto *human = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(player));
        if (!player || !human->usedActorEnter || human->seatId != 0) {
            return false;
        }
        const auto car = world.NativeObjects().FindByNative(human->usedActorEnter);
        const auto self = world.NativeObjects().FindByNative(player);
        auto *replication = Framework::CoreModules::GetReplication();
        const auto *state = car.networkId ? replication->GetEntity<Shared::Entities::CarEntity>(car.networkId) : nullptr;
        const auto *entity = self.networkId ? replication->GetEntity<Shared::Entities::PlayerEntity>(self.networkId) : nullptr;
        if (!state || !entity || state->terminalState != Shared::Entities::CarEntity::TerminalState::Active || state->occupantIds[0] != self.networkId) {
            return false;
        }
        Shared::Car::SirenIntent intent;
        intent.networkId = car.networkId;
        intent.missionGeneration = state->missionGeneration;
        intent.spawnGeneration = entity->spawnGeneration;
        intent.on = !state->sirenOn;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(intent);
        return true;
    }

    void CarService::SyncEngine(World::WorldService &world, uint64_t networkId, Stream &stream) {
        const auto native = _nativeById.find(networkId);
        if (native == _nativeById.end() || native->second.removalQueued) {
            return;
        }
        auto *car   = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        auto *actor = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!car || !actor) {
            return;
        }
        if (car->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
            if (actor->EngineOn() != car->engineOn) {
                actor->SetEngineOn(car->engineOn, true);
            }
            return;
        }
        if (stream.simulationController) {
            if (!stream.hasReportedEngineState) {
                if (actor->EngineOn() != car->engineOn) {
                    actor->SetEngineOn(car->engineOn, true);
                }
                stream.lastReportedEngineOn   = car->engineOn;
                stream.hasReportedEngineState = true;
                stream.appliedEngineRevision  = car->engineRevision;
                stream.lastEngineApply        = std::chrono::steady_clock::now();
            }
            else if (stream.appliedEngineRevision != car->engineRevision) {
                // A revision that matches the state we just reported is our
                // acknowledgement. A different state is a server script
                // command and must reach the current simulation controller.
                if (car->engineOn != stream.lastReportedEngineOn) {
                    actor->SetEngineOn(car->engineOn, false);
                    stream.lastReportedEngineOn = car->engineOn;
                    stream.lastEngineApply       = std::chrono::steady_clock::now();
                }
                stream.appliedEngineRevision = car->engineRevision;
            }
            const bool engineOn = actor->EngineRunning();
            if (engineOn != stream.lastReportedEngineOn) {
                Shared::Car::EngineState report;
                report.networkId         = networkId;
                report.missionGeneration = world.LoadedMissionGeneration();
                report.sequence          = ++stream.engineReportSequence;
                report.on                = engineOn;
                Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
                stream.lastReportedEngineOn = engineOn;
            }
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!stream.hasAppliedEngineState) {
            if (actor->EngineOn() != car->engineOn) {
                actor->SetEngineOn(car->engineOn, true);
            }
            stream.hasAppliedEngineState = true;
            stream.appliedEngineRevision = car->engineRevision;
            stream.lastEngineApply       = now;
        }
        else if (stream.appliedEngineRevision != car->engineRevision) {
            actor->SetEngineOn(car->engineOn, false);
            stream.appliedEngineRevision = car->engineRevision;
            stream.lastEngineApply       = now;
        }
        else if (actor->EngineOn() != car->engineOn && now - stream.lastEngineApply > std::chrono::seconds(2)) {
            actor->SetEngineOn(car->engineOn, true);
            stream.lastEngineApply = now;
        }
    }

    void CarService::SyncTerminal(World::WorldService &world, uint64_t networkId, Stream &stream) {
        auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        const auto native = _nativeById.find(networkId);
        if (!state || native == _nativeById.end() || native->second.removalQueued || state->terminalSequence <= stream.appliedTerminalSequence) {
            return;
        }
        auto *actor = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!actor) {
            return;
        }
        // Record before the native call: it may deactivate the original car
        // and create a separate game-owned wreckage actor.
        stream.appliedTerminalSequence = state->terminalSequence;
        if (state->terminalState == Shared::Entities::CarEntity::TerminalState::Exploded) {
            ApplyAuthoritativeExplosion(actor);
        }
        else if (state->terminalState == Shared::Entities::CarEntity::TerminalState::Submerged) {
            CaptureWaterCamera(world, networkId, *actor);
        }
        // Fall hazards also retain the native car's own physics. Retail will
        // deactivate it if its falling velocity becomes invalid.
    }

    bool CarService::AllowNativeDeactivation(World::WorldService &world, void *car) const {
        // Removal invalidates the registry handle first, so despawn and
        // mission teardown still reach the original.
        const auto handle = world.NativeObjects().FindByNative(car);
        if (handle.networkId == 0) {
            return true;
        }
        // The retail game owns an adopted car's lifetime.
        if (IsAdopted(handle.networkId)) {
            return true;
        }
        const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(handle.networkId);
        return !state || state->terminalState != Shared::Entities::CarEntity::TerminalState::Active;
    }

    void CarService::DetectTerminal(World::WorldService &world, uint64_t networkId, Stream &stream) {
        const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        const auto native = _nativeById.find(networkId);
        if (stream.terminalReported || !state || state->terminalState != Shared::Entities::CarEntity::TerminalState::Active ||
            native == _nativeById.end() || native->second.removalQueued) {
            return;
        }
        auto *actor = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(native->second.handle));
        if (!actor) {
            return;
        }
        // A water or fall-volume collision reports directly from the native
        // body callback. The remaining retail invalid-vehicle outcome is a
        // falling velocity below -85, not a coordinate-based map test.
        const auto &vehicle = actor->Vehicle();
        const float fallingVelocity = vehicle.LinearVelocity().y;
        if (!std::isfinite(fallingVelocity) || fallingVelocity >= -85.0f) {
            return;
        }
        Shared::Car::TerminalIntent intent;
        intent.networkId = networkId;
        intent.missionGeneration = state->missionGeneration;
        intent.state = static_cast<uint8_t>(Shared::Entities::CarEntity::TerminalState::FatalFall);
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(intent);
        stream.terminalReported = true;
    }

    void CarService::CaptureWaterCamera(World::WorldService &world, uint64_t networkId, SDK::Car::NativeCar &car) {
        auto *local = SDK::Player::CurrentPlayer();
        auto *scene = SDK::Core::Mission::Get()->GetScene();
        if (!local || !scene || !scene->ActiveCamera() ||
            static_cast<SDK::Seat::NativeHuman *>(static_cast<void *>(local))->usedActorEnter !=
                static_cast<SDK::Player::NativeActor *>(&car)) {
            return;
        }
        const auto handle = world.NativeObjects().FindByNative(local);
        if (!handle.networkId || !world.NativeObjects().Resolve(handle)) {
            return;
        }
        auto *camera = scene->ActiveCamera();
        const auto eye = camera->WorldPosition();
        const auto facing = camera->WorldDirection();
        const glm::vec3 position(eye.x, eye.y, eye.z);
        const glm::vec3 direction(facing.x, facing.y, facing.z);
        if (!IsFiniteVector(position) || !IsFiniteVector(direction) || glm::length(direction) < 0.5f) {
            return;
        }
        _waterCamera = WaterCamera {handle, networkId, position, direction};
        SDK::Core::Mission::Get()->Game()->Camera().LockAt(eye, facing);
    }

    void CarService::OnNativeWorldCollision(World::WorldService &world, SDK::Car::NativeCar *car, uint8_t material) {
        if (!world.IsReady() || (material != 31 && material != 40)) {
            return;
        }
        const auto handle = world.NativeObjects().FindByNative(car);
        auto *replication = Framework::CoreModules::GetReplication();
        const auto *state = handle.networkId && replication ? replication->GetEntity<Shared::Entities::CarEntity>(handle.networkId) : nullptr;
        const auto stream = _streams.find(handle.networkId);
        if (!state || state->missionGeneration != world.LoadedMissionGeneration() ||
            state->terminalState != Shared::Entities::CarEntity::TerminalState::Active ||
            stream == _streams.end() || stream->second.terminalReported || !stream->second.simulationController ||
            state->simulationControllerGuid != static_cast<uint64_t>(replication->GetMyGUID())) {
            return;
        }
        Shared::Car::TerminalIntent intent;
        intent.networkId = handle.networkId;
        intent.missionGeneration = state->missionGeneration;
        intent.state = static_cast<uint8_t>(material == 31 ?
            Shared::Entities::CarEntity::TerminalState::Submerged : Shared::Entities::CarEntity::TerminalState::FatalFall);
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(intent);
        stream->second.terminalReported = true;
        if (material == 31) {
            CaptureWaterCamera(world, handle.networkId, *car);
        }
    }

    void CarService::OnLocalHumanDeath(World::WorldService &world, void *human) {
        if (!_waterCamera || _waterCamera->player.object != human ||
            world.NativeObjects().Resolve(_waterCamera->player) != human || !world.IsReady()) {
            return;
        }
        const auto camera = *_waterCamera;
        SDK::Core::Mission::Get()->Game()->Camera().LockAt(
            {camera.position.x, camera.position.y, camera.position.z},
            {camera.direction.x, camera.direction.y, camera.direction.z});
    }

    bool CarService::OnNativeExplosion(World::WorldService &world, SDK::Car::NativeCar *car) {
        const auto handle = world.NativeObjects().FindByNative(car);
        if (handle.networkId == 0) {
            // Removal invalidates the generation before native destruction.
            // The actor can still receive one native tick on that path.
            return !_idByNative.contains(car);
        }
        auto *replication = Framework::CoreModules::GetReplication();
        auto *state       = replication->GetEntity<Shared::Entities::CarEntity>(handle.networkId);
        if (!state || state->missionGeneration != world.LoadedMissionGeneration() || state->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
            return false;
        }
        auto &stream = _streams[handle.networkId];
        if (!stream.explosionReported && state->simulationControllerGuid != 0 && state->simulationControllerGuid == static_cast<uint64_t>(replication->GetMyGUID())) {
            Shared::Car::ExplosionIntent intent;
            intent.networkId         = handle.networkId;
            intent.missionGeneration = state->missionGeneration;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(intent);
            stream.explosionReported = true;
        }
        return false;
    }

    void CarService::QueueRemoval(World::WorldService &world, uint64_t networkId) {
        auto it = _nativeById.find(networkId);
        if (it == _nativeById.end() || it->second.removalQueued) {
            return;
        }
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(it->second.handle));
        if (it->second.origin == Origin::Pool) {
            // The traffic pool keeps its car; the host only stops driving it.
            ReleaseAdopted(world, networkId);
            return;
        }
        world.NativeObjects().InvalidateNetwork(networkId);
        it->second.removalQueued = true;
        if (actor && world.IsReady()) {
            auto &camera = SDK::Core::Mission::Get()->Game()->Camera();
            if (camera.Car() == actor) {
                camera.SetCar(nullptr);
            }
            // Occupant frames are linked to this car's frame, which the
            // destructor deletes; retail removal does not eject them.
            Seat::ReleaseCarOccupants(static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(actor)));
            SDK::UI::NativeIndicators::Get().ReleaseRadarPlayerCar(actor);
            SDK::Core::Mission::Get()->Game()->RemoveTemporaryActor(actor);
        }
    }

    void CarService::ApplyPose(World::WorldService &world, uint64_t networkId, const Framework::Utils::TransformSnapshot &pose, bool forceSnap) {
        const auto it = _nativeById.find(networkId);
        if (it == _nativeById.end() || it->second.removalQueued || !world.NativeObjects().Resolve(it->second.handle)) {
            return;
        }
        auto *actor        = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(it->second.handle));
        const auto current = it->second.frame->WorldPosition();
        const glm::vec3 currentPosition(current.x, current.y, current.z);
        const float error         = glm::distance(currentPosition, pose.position);
        const bool snap           = forceSnap || error > 5.0f;
        // Poses are interpolated on the controller's sample clock, so follow
        // them exactly; blending with native physics read as judder.
        const glm::vec3 corrected = pose.position;
        const auto direction      = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
        it->second.frame->SetWorldPosition({corrected.x, corrected.y, corrected.z});
        it->second.frame->SetDirection({direction.x, direction.y, direction.z}, RollForDirection(pose.rotation, direction));
        it->second.frame->Update();
        actor->SetStoredTransform({corrected.x, corrected.y, corrected.z}, {direction.x, direction.y, direction.z});
        const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
        if (snap) {
            ResetNativePhysics(*actor, networkId);
        }
        else if (const auto stream = _streams.find(networkId); stream != _streams.end() &&
                     (!stream->second.simulationController || (state && state->terminalState == Shared::Entities::CarEntity::TerminalState::Submerged))) {
            // An observer's native physics would otherwise settle the car on
            // its own, e.g. onto a missing wheel, and bullets test the dynamic
            // collision at that physics pose rather than at the frame.
            actor->Vehicle().SyncPhysicsPose();
        }
        if (IsFinite(pose) && glm::length(pose.velocity) <= 120.0f) {
            const auto currentVelocity = actor->Vehicle().LinearVelocity();
            if (glm::distance(glm::vec3(currentVelocity.x, currentVelocity.y, currentVelocity.z), pose.velocity) > 0.1f) {
                actor->Vehicle().SetLinearVelocity({pose.velocity.x, pose.velocity.y, pose.velocity.z});
            }
        }
    }

    void CarService::ResetNativePhysics(SDK::Car::NativeCar &actor, uint64_t networkId) {
        // An active vehicle writes its frame from its own physics position
        // each tick, and its dynamic collision follows that position rather
        // than the frame. A snap must therefore rebuild the physics state
        // from the frame, as retail script teleports do with C_car::Reset.
        // Reset also clears wheel damage flags, horn and engine; restore them.
        auto &vehicle = actor.Vehicle();
        const int wheelCount = std::clamp(vehicle.WheelCount(), 0, static_cast<int>(Shared::Car::DamageState::kMaxWheels));
        std::array<Shared::Car::DamageState::Wheel, Shared::Car::DamageState::kMaxWheels> wheels {};
        for (int i = 0; i < wheelCount; ++i) {
            if (const auto *wheel = vehicle.Wheel(i)) {
                wheels[i] = {wheel->flags & Shared::Car::DamageState::kWheelDamageFlags, wheel->health, wheel->deformAngle};
            }
        }
        const auto velocity = vehicle.LinearVelocity();
        const bool engineDestroyed = vehicle.EngineDestroyed();
        actor.Reset(0.0f, false);
        // Reset(0.0f) re-arms the first-update freeze, which would zero the
        // velocity restored below and leave the car hanging where it was put.
        actor.SkipFirstUpdateFreeze();
        for (int i = 0; i < wheelCount; ++i) {
            if (vehicle.Wheel(i)) {
                vehicle.SetWheelDamage(i, wheels[i].flags, wheels[i].health, wheels[i].deformAngle);
            }
        }
        if (const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId)) {
            actor.SetEngineOn(state->engineOn, true);
            vehicle.SetHorn(state->hornOn);
        }
        vehicle.SetLinearVelocity(velocity);
        vehicle.SetMechanicalDamage(vehicle.EngineHealth(), vehicle.EngineDamagePower(), engineDestroyed, vehicle.GearboxHealth());
    }

    SDK::Scene::NativeFrame *CarService::OnNativeDestroyed(World::WorldService &world, void *car) {
        const auto it = _idByNative.find(car);
        if (it == _idByNative.end()) {
            return nullptr;
        }
        if (world.IsReady()) {
            auto &camera = SDK::Core::Mission::Get()->Game()->Camera();
            if (camera.Car() == car) {
                camera.SetCar(nullptr);
            }
        }
        const uint64_t networkId = it->second;
        _idByNative.erase(it);
        const auto record = _nativeById.find(networkId);
        if (record == _nativeById.end()) {
            return nullptr;
        }
        // A car the retail game created deletes its own frame.
        auto *frame = record->second.origin == Origin::Created ? record->second.frame : nullptr;
        _nativeById.erase(record);
        if (auto stream = _streams.find(networkId); stream != _streams.end()) {
            ResetNativeState(stream->second);
        }
        return frame;
    }

    void CarService::ResetNativeState(Stream &stream) {
        stream.appliedDamageRevision = 0;
        stream.lastDamageApply = {};
        stream.hasReportedDamage = false;
        stream.pendingDamageReports.clear();
        stream.appliedMeshRevision = 0;
        stream.checkpointSourceGuid = 0;
        stream.lastMeshRequest = {};
        stream.hasReportedMesh = false;
        stream.meshDirty = stream.simulationController;
        stream.pendingHits.clear();
        stream.pendingContacts.clear();
    }

    bool CarService::Adopt(World::WorldService &world, uint64_t networkId, SDK::Car::NativeCar *car, Origin origin) {
        if (!car || !car->Frame() || _nativeById.contains(networkId) || _idByNative.contains(car) || world.NativeObjects().FindByNative(car).networkId != 0) {
            return false;
        }
        const auto handle = world.NativeObjects().Bind(car, networkId);
        NativeCar record {handle, car->Frame(), false};
        record.origin = origin;
        _nativeById.emplace(networkId, record);
        _idByNative.emplace(car, networkId);
        auto &stream = _streams[networkId];
        ResetNativeState(stream);
        // Its pedals and physics are the retail driver's: no hand-over reset.
        stream.controllerDynamicsInitialized = true;
        stream.creationFailed = false;
        return true;
    }

    void CarService::ReleaseAdopted(World::WorldService &world, uint64_t networkId) {
        const auto record = _nativeById.find(networkId);
        if (record == _nativeById.end() || record->second.origin == Origin::Created) {
            return;
        }
        world.NativeObjects().InvalidateNetwork(networkId);
        _idByNative.erase(record->second.handle.object);
        _nativeById.erase(record);
        if (auto stream = _streams.find(networkId); stream != _streams.end()) {
            ResetNativeState(stream->second);
        }
    }

    bool CarService::IsAdopted(uint64_t networkId) const {
        const auto record = _nativeById.find(networkId);
        return record != _nativeById.end() && record->second.origin != Origin::Created;
    }

    bool CarService::Sample(uint64_t networkId, Framework::Utils::TransformSnapshot &out) const {
        const auto it = _streams.find(networkId);
        Stream::PoseSample pose;
        if (it == _streams.end() || !it->second.poses.Sample(World::FrameClock::NowMs() - it->second.poses.DelayMs() + it->second.poses.LeadMs(), pose)) {
            return false;
        }
        out = {pose.position, pose.velocity, pose.rotation};
        return true;
    }

    double CarService::LocalClockMs() {
        // The clock GetTime() counts milliseconds of; whole milliseconds
        // would add up to 6% noise to per-frame motion at 60 fps.
        return static_cast<double>(MafiaNet::GetTimeUS()) * 0.001;
    }

    void CarService::OnSimulated(SDK::Car::NativeCar *car, unsigned int frameMs) {
        const auto id = _idByNative.find(car);
        if (id == _idByNative.end()) {
            return;
        }
        if (const auto found = _streams.find(id->second); found != _streams.end() && found->second.simulationController) {
            // Dated by the frame this step belongs to. The game's frame times
            // are whole milliseconds and run 2-4% behind the wall clock;
            // counting them alone drifted, and pulling the count up to the
            // wall clock once it lagged 40 ms put a 40-110 ms gap between
            // two poses 33 ms apart every few seconds, which observers played
            // as the car stalling. The frame clock leans on the wall clock
            // instead of jumping to it.
            auto &stream    = found->second;
            stream.simClock = World::FrameClock::NowMs();
            if (_simulatedSerial != _updateSerial) {
                _simulatedSerial = _updateSerial;
                _simulatedCars.clear();
            }
            const auto &vehicle  = car->Vehicle();
            const auto center    = vehicle.WorldCenter();
            const auto velocity  = vehicle.LinearVelocity();
            _simulatedCars.push_back({glm::vec3(center.x, center.y, center.z), glm::vec3(velocity.x, velocity.y, velocity.z)});
            if (Debug::SyncTrace::Enabled()) {
                // Every physics step, not only the sent ones: the dense truth
                // an observer's shown pose is measured against.
                const auto native = _nativeById.find(id->second);
                if (native != _nativeById.end() && native->second.frame) {
                    const auto position = native->second.frame->WorldPosition();
                    const auto angular  = vehicle.AngularVelocity();
                    auto *local = reinterpret_cast<SDK::Seat::NativeHuman *>(SDK::Player::CurrentPlayer());
                    if (local && local->usedActorEnter == static_cast<const SDK::Player::NativeActor *>(static_cast<const void *>(car))) {
                        auto &trace              = _traceFrames[id->second];
                        trace.own                = true;
                        ++trace.ticks;
                        trace.frameMs            = frameMs;
                        trace.presented.position = {position.x, position.y, position.z};
                        trace.presented.rotation = FrameRotation(*native->second.frame);
                        trace.written            = trace.presented.position;
                        trace.writtenRotation    = trace.presented.rotation;
                    }
                    if (Debug::SyncTrace::Near({position.x, position.y, position.z})) {
                        Debug::SyncTrace::Truth("car", id->second)
                            .Num("clk", stream.simClock)
                            .Bool("sent", false)
                            .Int("fms", frameMs)
                            .Vec("p", {position.x, position.y, position.z})
                            .Quat("q", FrameRotation(*native->second.frame))
                            .Vec("v", {velocity.x, velocity.y, velocity.z})
                            .Vec("w", {angular.x, angular.y, angular.z});
                    }
                }
            }
        }
    }

    bool CarService::ToggleSyncMode() {
        _predictedSync = !_predictedSync;
        return _predictedSync;
    }

    bool CarService::DriveObserver(SDK::Car::NativeCar *car, unsigned int frameMs, NativeStep native) {
        const auto id = _idByNative.find(car);
        if (id == _idByNative.end()) {
            return false;
        }
        const uint64_t networkId = id->second;
        const auto streamIt = _streams.find(networkId);
        const auto nativeCar = _nativeById.find(networkId);
        auto *replication = Framework::CoreModules::GetReplication();
        const auto *state = replication ? replication->GetEntity<Shared::Entities::CarEntity>(networkId) : nullptr;
        if (_world && state && (state->terminalState == Shared::Entities::CarEntity::TerminalState::Submerged ||
                               state->terminalState == Shared::Entities::CarEntity::TerminalState::FatalFall) &&
            nativeCar != _nativeById.end() && !nativeCar->second.removalQueued && streamIt != _streams.end() &&
            !streamIt->second.simulationController) {
            Framework::Utils::TransformSnapshot pose {state->position, state->velocity, state->rotation};
            Sample(networkId, pose);
            if (IsFinite(pose) && glm::length(pose.rotation) >= 0.0001f) {
                ApplyPose(*_world, networkId, pose);
            }
            streamIt->second.drivenSerial = _updateSerial;
            return true;
        }
        if (streamIt == _streams.end() || streamIt->second.simulationController || nativeCar == _nativeById.end() || nativeCar->second.removalQueued || !state ||
            state->terminalState != Shared::Entities::CarEntity::TerminalState::Active || streamIt->second.poses.Empty()) {
            return false;
        }
        auto &stream = streamIt->second;
        if (_clockSerial != _updateSerial) {
            _clockSerial = _updateSerial;
            // Remote cars step by the game's frame time, as the local car
            // and the camera do (World::FrameClock).
            _frameClock = World::FrameClock::NowMs();
        }
        const float dt = static_cast<float>(std::clamp(frameMs, 1u, 83u)) * 0.001f;
        auto *frame = nativeCar->second.frame;
        auto &vehicle = car->Vehicle();
        const auto massCenter = vehicle.MassCenter();
        stream.poses.SetMassCenter({massCenter.x, massCenter.y, massCenter.z});
        Stream::PoseSample pose;
        // A car placed from the network is immovable to local physics: a car
        // this client simulates would hit it like a wall. Near one, run it on
        // native physics with correction, so an impact shoves it at once;
        // the owner's result then arrives through the corrected pose.
        float nearestSimulated = std::numeric_limits<float>::max();
        {
            // Far enough ahead for the render clock to reach the present
            // first; it takes longer the further that is (kLeadEntryRate).
            const float horizonSeconds = 0.5f + stream.transitMs * 0.002f;
            const auto center = vehicle.WorldCenter();
            const auto velocity = vehicle.LinearVelocity();
            const glm::vec3 position(center.x, center.y, center.z);
            const glm::vec3 motion(velocity.x, velocity.y, velocity.z);
            for (const auto &[otherPosition, otherVelocity] : _simulatedCars) {
                const glm::vec3 offset = position - otherPosition;
                const float distance = glm::length(offset);
                nearestSimulated = std::min(nearestSimulated, distance);
                const float closing = distance > 0.001f ? glm::dot(otherVelocity - motion, offset / distance) : 0.0f;
                if (distance < std::min(25.0f, 7.0f + std::max(0.0f, closing) * horizonSeconds)) {
                    stream.contactUntil = _frameClock + 1500.0;
                }
            }
        }
        // Watched, the car is shown a delay behind; near a car this client
        // simulates, where it is now on its controller's machine, so the two
        // meet where they do there: the newest pose is the poses' transit
        // behind that, so contact time alone put a car hit at speed v that
        // far short (v x the latency). The render clock eases between them:
        // stepping it moved the car by its speed times the change at once, a
        // lurch on entry and a snap back on exit.
        //
        // Nobody watches the simulation host, but its traffic and police read
        // every car's centre and velocity (C_car::IsPossibleColl, reM C_car.
        // cpp:7293; C_actor::IsPossibleCollAll, C_actor.cpp:1416): it runs
        // every player's car at the present.
        const bool contact = _predictedSync || stream.contactUntil > _frameClock;
        const bool predict = contact || Simulation::IsHost();
        // The whole way, not what is left of it: easing the rest faster as it
        // shrinks sped the car up toward the end.
        const float span        = stream.poses.DelayMs() + stream.transitMs;
        const float leadMs      = predict ? span : 0.0f;
        const float leadSeconds = predict ? CarPoseBuffer::LeadSeconds(span, kLeadEntryRate, kLeadEntrySeconds)
                                          : CarPoseBuffer::LeadSeconds(span, kLeadExitRate, kLeadExitSeconds);
        const double renderTime =
            stream.poses.AdvanceDelay(_frameClock, static_cast<float>(frameMs)) + stream.poses.AdvanceLead(leadMs, dt, leadSeconds);
        if (contact) {
            // Native physics runs on the replicated pedals and steering, then
            // is pulled toward the replicated pose at the render time, still
            // before C_car::Update and the camera read it.
            const auto before = vehicle.LinearVelocity();
            native(car, frameMs);
            const auto after = vehicle.LinearVelocity();
            // Pedals and brakes stay near 1 g; a collision impulse does not.
            // Only a car this client simulates touching it counts: a kerb or
            // wall it hits here, it hits on its controller as well.
            if (const float jump = std::hypot(after.x - before.x, after.y - before.y, after.z - before.z);
                std::isfinite(jump) && jump / dt > kImpactAcceleration && nearestSimulated < kImpactContactDistance) {
                // One episode per contact: pushing it keeps impacting, but the
                // controller has its answer for the first one by then.
                if (_frameClock - stream.lastImpactAt > kImpactEpisodeGapMs) {
                    stream.impactAt       = _frameClock;
                    stream.impactRenderAt = renderTime;
                }
                stream.lastImpactAt = _frameClock;
            }
            // The target keeps Present's correction, so it carries on from
            // where the car was shown; the frame is native physics'.
            stream.nativeShown = true;
            if (!stream.poses.Present(renderTime, dt, pose)) {
                return true;
            }
            // Until the controller's poses carry its own answer to the
            // impact, the prediction still runs along the pre-impact path:
            // pulling to it drives the car back into what it hit. Ease
            // toward it slowly and keep the local impulse meanwhile. An answer
            // to this client's report of it (OnCarHit) comes later: the report
            // reaches the controller a transit after it had the contact, and
            // waits out ContactImpact's window there.
            const double answered = stream.impactRenderAt + std::max(Shared::Car::kContactWindowMs, static_cast<double>(stream.transitMs)) + kImpactResponseMs;
            const bool yielding   = stream.impactAt > 0.0 && _frameClock - stream.impactAt < kImpactHoldMs && stream.poses.Newest().time < answered;
            const auto current = frame->WorldPosition();
            const glm::vec3 position(current.x, current.y, current.z);
            const glm::quat rotation = FrameRotation(*frame);
            const glm::vec3 error = pose.position - position;
            const bool snap = glm::length(error) > 5.0f || std::abs(glm::dot(rotation, pose.rotation)) < 0.92f;
            const float blend = snap ? 1.0f : 1.0f - std::exp(-dt / (yielding ? kYieldCorrectionSeconds : 0.2f));
            const glm::vec3 corrected = position + error * blend;
            const glm::quat turned = glm::slerp(rotation, glm::dot(rotation, pose.rotation) < 0.0f ? -pose.rotation : pose.rotation, blend);
            WriteFrame(*frame, corrected, turned);
            if (snap) {
                ResetNativePhysics(*car, networkId);
            }
            const auto nativeVelocity = vehicle.LinearVelocity();
            const float velocityBlend = snap ? 1.0f : yielding ? 0.0f : 1.0f - std::exp(-dt / 0.15f);
            const glm::vec3 velocity = glm::mix(glm::vec3(nativeVelocity.x, nativeVelocity.y, nativeVelocity.z), pose.velocity, velocityBlend);
            const auto nativeAngular = vehicle.AngularVelocity();
            const glm::vec3 angular = glm::mix(glm::vec3(nativeAngular.x, nativeAngular.y, nativeAngular.z), pose.angularVelocity, velocityBlend);
            vehicle.SetMotion({velocity.x, velocity.y, velocity.z}, {angular.x, angular.y, angular.z});
            vehicle.UpdateImportantVariables();
            TraceObserver(networkId, stream, *frame, frameMs, renderTime, pose, true, predict);
            stream.drivenSerial = _updateSerial;
            return true;
        }
        // A late pose that lands behind where the car was shown is closed
        // over ~200 ms (CarPoseBuffer::Present), not jumped to; so is where
        // native physics left it.
        if (stream.nativeShown) {
            stream.nativeShown = false;
            const auto current = frame->WorldPosition();
            stream.poses.Rebase({current.x, current.y, current.z}, FrameRotation(*frame), renderTime - static_cast<double>(frameMs));
        }
        if (!stream.poses.Present(renderTime, dt, pose)) {
            return false;
        }
        const auto current = frame->WorldPosition();
        const bool jumped = glm::distance(glm::vec3(current.x, current.y, current.z), pose.position) > 5.0f;
        WriteFrame(*frame, pose.position, pose.rotation);
        if (jumped) {
            // Rebuilds the wheels' contact state at the new place.
            ResetNativePhysics(*car, networkId);
        }
        vehicle.SetMotion({pose.velocity.x, pose.velocity.y, pose.velocity.z}, {pose.angularVelocity.x, pose.angularVelocity.y, pose.angularVelocity.z});
        vehicle.UpdateImportantVariables();
        vehicle.KeepPhysicsActive();
        // The input only, as retail replay's SetSteer: C_car::Update's
        // C_Vehicle::Update (UpdateSteeringWheels 0x4ddc60) turns the wheels
        // toward it at the car's own steering rates.
        if (std::isfinite(state->steeringInput) && std::abs(state->steeringInput) <= 2.0f) {
            vehicle.ApplySteeringInput(state->steeringInput);
        }
        if (std::isfinite(state->engineRotations) && state->engineRotations >= 0.0f) {
            vehicle.SetEngineRotations(state->engineRotations);
        }
        // As UpdateReplayPlayback: the recorded skid bits drive skid marks,
        // tyre smoke and squeal in C_Vehicle::Update.
        //
        // C_Vehicle::Move clears and recomputes the locked, powered, ABS
        // and ESP bits every physics step; here Move never runs, so they
        // kept whatever the car's last native step left: braking at rest
        // (before the first pose, in contact mode, or as this client's own
        // car before a hand-over) locked every wheel for good. Locked
        // (FULL_SKID) wheels never turn in UpdateRotationWheels, and powered
        // ones turn with an engaged gear this car never engages, so the car
        // slid along on still wheels as if braked. They are cleared, so each
        // wheel turns with its ground travel, and locked only while it skids
        // under the controller's handbrake (on its axle) or full brake.
        const bool locking = state->brakeInput >= 0.99f;
        const bool handbrake = state->handbrakeInput > 0.0f;
        for (int i = 0; i < std::min(vehicle.WheelCount(), 8); ++i) {
            if (auto *wheel = vehicle.MutableWheel(i)) {
                wheel->flags &= ~(SDK::Car::kWheelFullSkid | SDK::Car::kWheelPowered | SDK::Car::kWheelAbsActive | SDK::Car::kWheelEspActive);
                if (state->skidMask & (1u << i)) {
                    wheel->flags |= SDK::Car::kWheelHasVisual | SDK::Car::kWheelSkidding;
                    if (locking || (handbrake && (wheel->flags & SDK::Car::kWheelHandbrakeAxle))) {
                        wheel->flags |= SDK::Car::kWheelFullSkid;
                    }
                }
                else {
                    wheel->flags &= ~(SDK::Car::kWheelHasVisual | SDK::Car::kWheelSkidding);
                }
                wheel->slipValue = 0.0f;
            }
        }
        vehicle.DoWheelsCollision(dt);
        vehicle.EngineFreq(dt);
        TraceObserver(networkId, stream, *frame, frameMs, renderTime, pose, false, predict);
        stream.drivenSerial = _updateSerial;
        return true;
    }

    void CarService::TraceObserver(uint64_t networkId, const Stream &stream, SDK::Scene::NativeFrame &frame, unsigned int frameMs, double renderTime,
                                   const CarPoseSample &presented, bool contact, bool predict) {
        if (!Debug::SyncTrace::Enabled() || !Debug::SyncTrace::Near(presented.position)) {
            return;
        }
        auto &trace = _traceFrames[networkId];
        ++trace.ticks;
        trace.frameMs           = frameMs;
        trace.wall              = LocalClockMs();
        trace.frameClock        = _frameClock;
        trace.renderTime        = renderTime;
        trace.ownerClock        = renderTime - stream.poseClockOffset;
        trace.delayMs           = stream.poses.DelayMs();
        trace.leadMs            = stream.poses.LeadMs();
        trace.transitMs         = stream.transitMs;
        trace.intervalMs        = stream.poses.IntervalMs();
        trace.jitterMs          = stream.poses.JitterMs();
        trace.latenessMs        = stream.poses.LatenessPeakMs();
        trace.sample            = stream.poses.LastSample();
        trace.correctionMeters  = glm::length(stream.poses.CorrectionOffset());
        trace.correctionRadians = stream.poses.CorrectionRadians();
        trace.contact           = contact;
        trace.predict           = predict;
        trace.presented         = presented;
        const auto written      = frame.WorldPosition();
        trace.written           = {written.x, written.y, written.z};
        trace.writtenRotation   = FrameRotation(frame);
        trace.afterTick         = false;
    }

    void CarService::TraceAfterGameTick() {
        if (_traceFrames.empty()) {
            return;
        }
        auto *mission = SDK::Core::Mission::Get();
        auto *scene   = mission ? mission->GetScene() : nullptr;
        auto *camera  = scene ? scene->ActiveCamera() : nullptr;
        auto *local   = reinterpret_cast<SDK::Seat::NativeHuman *>(SDK::Player::CurrentPlayer());
        for (auto &[networkId, trace] : _traceFrames) {
            const auto native = _nativeById.find(networkId);
            auto *car = native != _nativeById.end() && !native->second.removalQueued && _world
                            ? static_cast<SDK::Car::NativeCar *>(_world->NativeObjects().Resolve(native->second.handle))
                            : nullptr;
            if (!car) {
                continue;
            }
            const auto position      = native->second.frame->WorldPosition();
            const auto center        = car->Vehicle().WorldCenter();
            trace.afterTick          = true;
            trace.afterUpdate        = {position.x, position.y, position.z};
            trace.worldCenter        = {center.x, center.y, center.z};
            if (camera) {
                const auto eye = camera->WorldPosition();
                trace.camera   = {eye.x, eye.y, eye.z};
            }
            auto &vehicle = car->Vehicle();
            trace.wheelFlags.clear();
            trace.wheelSpin.clear();
            for (int i = 0; i < std::min(vehicle.WheelCount(), 8); ++i) {
                if (const auto *wheel = vehicle.Wheel(i)) {
                    char text[32];
                    std::snprintf(text, sizeof(text), "%s%x", i ? "/" : "", wheel->flags);
                    trace.wheelFlags += text;
                    std::snprintf(text, sizeof(text), "%s%.3f", i ? "/" : "", wheel->contactAngle);
                    trace.wheelSpin += text;
                }
            }
            const auto inputs = vehicle.Inputs();
            trace.power     = inputs.power;
            trace.brake     = inputs.brake;
            trace.handbrake = inputs.handbrake;
            trace.gear      = vehicle.Gear();
            trace.rpm       = vehicle.EngineRotations();
            trace.lights    = vehicle.LightState();
            trace.localSeat = -1;
            if (local && local->actor.Frame()) {
                const auto at = local->actor.Frame()->WorldPosition();
                trace.local   = {at.x, at.y, at.z};
                if (local->usedActorEnter == static_cast<const SDK::Player::NativeActor *>(static_cast<const void *>(car))) {
                    trace.localSeat = local->seatId;
                }
            }
        }
    }

    void CarService::TraceFrameEnd() {
        for (auto &[networkId, trace] : _traceFrames) {
            auto record = Debug::SyncTrace::Observed("car", networkId);
            if (!record) {
                break;
            }
            const auto native = _nativeById.find(networkId);
            if (native != _nativeById.end() && !native->second.removalQueued && _world && _world->NativeObjects().Resolve(native->second.handle)) {
                const auto shown = native->second.frame->WorldPosition();
                record.Vec("fin", {shown.x, shown.y, shown.z});
            }
            if (trace.afterTick) {
                record.Str("wf", trace.wheelFlags)
                    .Str("ws", trace.wheelSpin)
                    .Num("power", trace.power)
                    .Num("brake", trace.brake)
                    .Num("hb", trace.handbrake)
                    .Int("gear", trace.gear)
                    .Num("rpm", trace.rpm)
                    .Int("lights", trace.lights);
            }
            if (trace.own) {
                record.Str("mode", "own").Int("ticks", trace.ticks).Int("fms", trace.frameMs).Vec("p", trace.presented.position).Quat("q", trace.presented.rotation);
                if (trace.afterTick) {
                    record.Vec("nat", trace.afterUpdate).Vec("wc", trace.worldCenter).Vec("cam", trace.camera).Vec("lp", trace.local).Int("seat", trace.localSeat);
                }
                continue;
            }
            static constexpr const char *kModes[] = {"none", "hermite", "linear", "extrap", "before"};
            record.Str("mode", trace.contact ? "contact" : "interp")
                .Int("ticks", trace.ticks)
                .Int("fms", trace.frameMs)
                .Num("wall", trace.wall)
                .Num("fclk", trace.frameClock)
                .Num("rt", trace.renderTime)
                .Num("oclk", trace.ownerClock)
                .Num("delay", trace.delayMs)
                .Num("lead", trace.leadMs)
                .Num("transit", trace.transitMs)
                .Num("interval", trace.intervalMs)
                .Num("jitter", trace.jitterMs)
                .Num("late", trace.latenessMs)
                .Str("smp", kModes[static_cast<int>(trace.sample.mode)])
                .Num("u", trace.sample.u)
                .Num("span", trace.sample.spanMs)
                .Num("ahead", trace.renderTime - trace.sample.newest)
                .Int("n", trace.sample.count)
                .Num("corr", trace.correctionMeters)
                .Num("corrDeg", glm::degrees(trace.correctionRadians))
                .Bool("predict", trace.predict)
                .Vec("p", trace.presented.position)
                .Quat("q", trace.presented.rotation)
                .Vec("v", trace.presented.velocity)
                .Vec("wr", trace.written)
                .Quat("wq", trace.writtenRotation);
            if (trace.afterTick) {
                record.Vec("nat", trace.afterUpdate).Vec("wc", trace.worldCenter).Vec("cam", trace.camera).Vec("lp", trace.local).Int("seat", trace.localSeat);
            }
        }
        _traceFrames.clear();
    }

    void CarService::Reset() {
        _world = nullptr;
        _waterCamera.reset();
        _streams.clear();
        _contacts.clear();
        _reportedContacts.clear();
        _replayedContacts.clear();
    }
} // namespace Mafia1Online::Features::Car
