#include "car_service.h"

#include "features/car/car_hooks.h"
#include "features/car/car_mesh_capture.h"
#include "features/seat/seat_service.h"
#include "features/world/world_service.h"

#include "shared/features/car/car_engine_state.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/car/car_explosion_intent.h"
#include "shared/features/car/car_terminal_intent.h"
#include "shared/features/car/car_load_failure.h"
#include "shared/features/car/car_movement.h"
#include "shared/features/car/car_siren_intent.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/car/native_car.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_death.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <mafia1/sdk/ui/native_indicators.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <string>
#include <unordered_set>

namespace Mafia1Online::Features::Car {
    namespace {
        constexpr auto kMovementSendInterval = std::chrono::milliseconds(33);
        constexpr float kFuelSyncTolerance = 0.01f;

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
    }

    void CarService::Update(World::WorldService &world) {
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

        std::unordered_set<uint64_t> seen;
        const uint64_t myGuid = static_cast<uint64_t>(replication->GetMyGUID());
        replication->ForEach<Shared::Entities::CarEntity>([&](Shared::Entities::CarEntity *car) {
            if (car->missionGeneration != loadedMissionGeneration) {
                return;
            }
            const uint64_t id = car->GetNetworkID();
            seen.insert(id);
            auto &stream                    = _streams[id];
            const bool simulationController = car->simulationControllerGuid != 0 && car->simulationControllerGuid == myGuid;
            if (stream.simulationController != simulationController) {
                stream.simulationController   = simulationController;
                stream.lastSent               = {};
                stream.sequence               = 0;
                stream.engineReportSequence   = 0;
                stream.appliedDynamicsCommandRevision = 0;
                stream.controllerDynamicsInitialized = false;
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
                if (simulationController) {
                    Framework::Utils::TransformSnapshot authoritative {car->position, car->velocity, car->rotation};
                    if (IsFinite(authoritative) && glm::length(authoritative.rotation) >= 0.0001f) {
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
                }
            }
            if (car->lastUpdateTime == 0 && stream.lastPacketTime != 0) {
                return;
            }
            const MafiaNet::Time packetTime = car->lastUpdateTime == 0 ? MafiaNet::GetTime() : car->lastUpdateTime;
            const double now = LocalClockMs();
            double poseTime = static_cast<double>(packetTime);
            double arrival = poseTime;
            bool clockReset = true;
            if (car->poseClockSource != 0) {
                // The server re-sends poses on its own tick, and state-channel
                // updates also advance lastUpdateTime. Date each pose by the
                // controller's physics clock instead, mapped onto the local
                // clock by the least-delayed arrival of the last two seconds.
                // The offset only slews, so one early or late packet never
                // moves a sample's time; a jump resets it.
                if (stream.poseClockSource == car->poseClockSource && stream.poseClockMs == car->poseClockMs) {
                    return;
                }
                arrival = now;
                const double offset = now - static_cast<double>(car->poseClockMs);
                const bool sourceChanged = stream.poseClockSource != car->poseClockSource;
                if (sourceChanged || std::abs(offset - stream.poseClockOffset) > 1000.0) {
                    stream.offsetWindow.clear();
                    stream.poseClockOffset = offset;
                }
                stream.offsetWindow.push_back(offset);
                while (stream.offsetWindow.size() > 60) {
                    stream.offsetWindow.pop_front();
                }
                const double floor = *std::min_element(stream.offsetWindow.begin(), stream.offsetWindow.end());
                stream.poseClockOffset += std::clamp(floor - stream.poseClockOffset, -0.5, 0.5);
                stream.poseClockSource = car->poseClockSource;
                stream.poseClockMs = car->poseClockMs;
                poseTime = static_cast<double>(car->poseClockMs) + stream.poseClockOffset;
                clockReset = sourceChanged;
            }
            if ((!clockReset && poseTime <= stream.lastPoseTime) || (car->poseClockSource == 0 && stream.lastPacketTime == packetTime)) {
                return;
            }
            Framework::Utils::TransformSnapshot snapshot {car->position, car->velocity, car->rotation};
            if (!IsFinite(snapshot) || glm::length(snapshot.rotation) < 0.0001f) {
                return;
            }
            const glm::vec3 angular = IsFiniteVector(car->angularVelocity) && glm::length(car->angularVelocity) <= 30.0f ? car->angularVelocity : glm::vec3(0.0f);
            PushPose(stream, {poseTime, snapshot.position, snapshot.velocity, glm::normalize(snapshot.rotation), angular}, arrival, clockReset);
            stream.lastPoseTime = poseTime;
            stream.lastPacketTime = packetTime;
        });
        for (auto it = _streams.begin(); it != _streams.end();) {
            if (!seen.contains(it->first)) {
                QueueRemoval(world, it->first);
                it = _streams.erase(it);
            }
            else {
                if (!_nativeById.contains(it->first) && !it->second.creationFailed) {
                    if (auto *car = replication->GetEntity<Shared::Entities::CarEntity>(it->first)) {
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
                if (it->second.simulationController) {
                    SyncDamage(world, it->first, it->second);
                    SyncMesh(world, it->first, it->second);
                    ApplyAuthoritativeHits(world, it->first, it->second);
                    SyncDynamics(world, it->first, it->second);
                    ReportMovement(world, it->first, it->second);
                    ReportDamage(world, it->first, it->second);
                    ReportMesh(world, it->first, it->second);
                    DetectTerminal(world, it->first, it->second);
                }
                else {
                    // The pose is written by DriveObserver inside the actor
                    // tick. A car the game did not tick this frame (inactive)
                    // is placed here instead; a terminal car keeps its final
                    // native state.
                    Framework::Utils::TransformSnapshot pose;
                    const auto *state = replication->GetEntity<Shared::Entities::CarEntity>(it->first);
                    if (it->second.drivenSerial + 1 != _updateSerial && state && state->terminalState == Shared::Entities::CarEntity::TerminalState::Active &&
                        Sample(it->first, pose)) {
                        ApplyPose(world, it->first, pose);
                    }
                    SyncDynamics(world, it->first, it->second);
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
        stream.checkpointRevision = 0;
        stream.pendingMeshRevision = 0;
        stream.pendingMeshSourceGuid = 0;
        stream.pendingMeshReceived.clear();
        stream.pendingMeshVertices.clear();
        stream.checkpointVertices.clear();
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
            state->meshRevision != chunk.revision ||
            chunk.revision == 0 ||
            !Shared::Car::ValidateMeshChunkShape(chunk.totalVertices, chunk.chunkIndex,
                                                  chunk.chunkCount, chunk.vertexCount)) {
            return;
        }
        auto &stream = _streams[chunk.networkId];
        if (chunk.revision <= stream.appliedMeshRevision || chunk.revision == stream.checkpointRevision ||
            chunk.revision < stream.pendingMeshRevision) {
            return;
        }
        if (chunk.revision != stream.pendingMeshRevision) {
            stream.pendingMeshRevision = chunk.revision;
            stream.pendingMeshSourceGuid = chunk.sourceGuid;
            stream.pendingMeshChunkCount = chunk.chunkCount;
            stream.pendingMeshTotal = chunk.totalVertices;
            stream.pendingMeshVertices.resize(chunk.totalVertices);
            stream.pendingMeshReceived.assign(chunk.chunkCount, 0);
        }
        if (stream.pendingMeshSourceGuid != chunk.sourceGuid ||
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
            if (!ApplyAuthoritativeCarHit(car, {worldDirection.x, worldDirection.y, worldDirection.z},
                                          {worldHit.x, worldHit.y, worldHit.z}, {normal.x, normal.y, normal.z}, hit.damage,
                                          hitFrame)) {
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
        if (!state || state->meshRevision <= stream.appliedMeshRevision || !state->nativeDamageValid) {
            return;
        }
        const auto native = _nativeById.find(networkId);
        if (native == _nativeById.end() || native->second.removalQueued) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (stream.checkpointRevision != state->meshRevision) {
            if (stream.lastMeshRequest == std::chrono::steady_clock::time_point {} ||
                now - stream.lastMeshRequest >= std::chrono::seconds(1)) {
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
            stream.appliedMeshRevision = state->meshRevision;
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
        stream.appliedMeshRevision = state->meshRevision;
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
        if (stream.lastSent != std::chrono::steady_clock::time_point {} && now - stream.lastSent < kMovementSendInterval) {
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
        movement.clockMs           = stream.simClockMs;
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
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(movement, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
        stream.lastSent = now;
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
            ApplySiren(vehicle, *state);
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
        // Skipped while the local player is entering or seated: Use_Actor
        // sets that driver's gearbox mode and AI_drive owns the pedals.
        const auto *local = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(SDK::Player::CurrentPlayer()));
        const SDK::Player::NativeActor *self = actor;
        if (!local || (local->usedActorEnter != self && local->usedActorLeave != self)) {
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
        if (native == _nativeById.end() || native->second.removalQueued || !state) {
            return;
        }
        auto *car = world.NativeObjects().Resolve(native->second.handle);
        if (!car || native->second.radarColor == state->radarColor) {
            return;
        }
        auto &indicators = SDK::UI::NativeIndicators::Get();
        if (state->radarColor != 0) {
            indicators.RadarAddCar(car, state->radarColor);
        }
        else {
            // Retail lists the local driver's own car in white; keep it.
            const auto *local = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(SDK::Player::CurrentPlayer()));
            if (local && local->usedActorEnter == static_cast<const SDK::Player::NativeActor *>(car) && local->seatId == 0) {
                indicators.RadarAddCar(car, 0xFFFFFFFF);
            }
            else {
                indicators.RadarRemoveCar(car);
            }
        }
        native->second.radarColor = state->radarColor;
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
        else if (state->terminalState == Shared::Entities::CarEntity::TerminalState::Submerged ||
                 state->terminalState == Shared::Entities::CarEntity::TerminalState::OutOfBounds) {
            // Retail has no sink routine: a car below water or in a fall
            // volume ends deactivated and unusable through SetActState(2),
            // C_car::ChangeState and DeactivateCar. Reproduce that end state
            // at the server's final pose; late joiners skip the splash.
            const Framework::Utils::TransformSnapshot final {state->position, glm::vec3(0.0f), state->rotation};
            if (IsFinite(final) && glm::length(final.rotation) >= 0.0001f) {
                ApplyPose(world, networkId, final, true);
            }
            actor->SetEngineOn(false, true);
            using SetActState = void(__thiscall *)(SDK::Player::NativeActor *, int32_t);
            reinterpret_cast<SetActState>(SDK::Player::kActorSetActState)(actor, 2);
        }
    }

    bool CarService::AllowNativeDeactivation(World::WorldService &world, void *car) const {
        // Removal invalidates the registry handle first, so despawn and
        // mission teardown still reach the original.
        const auto handle = world.NativeObjects().FindByNative(car);
        if (handle.networkId == 0) {
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
        // The same checks retail makes: a wheel on water (31) or a fall volume
        // (40) at 0x4216ca, and C_car's -85 falling-velocity cutoff at 0x4211c1.
        using State = Shared::Entities::CarEntity::TerminalState;
        auto terminal = State::Active;
        const auto &vehicle = actor->Vehicle();
        for (int i = 0; i < vehicle.WheelCount() && terminal == State::Active; ++i) {
            if (const auto *wheel = vehicle.Wheel(i)) {
                if (wheel->surfaceMaterial == 31) {
                    terminal = State::Submerged;
                }
                else if (wheel->surfaceMaterial == 40) {
                    terminal = State::OutOfBounds;
                }
            }
        }
        if (terminal == State::Active && vehicle.LinearVelocity().y < -85.0f) {
            terminal = State::OutOfBounds;
        }
        if (terminal == State::Active) {
            return;
        }
        Shared::Car::TerminalIntent intent;
        intent.networkId = networkId;
        intent.missionGeneration = state->missionGeneration;
        intent.state = static_cast<uint8_t>(terminal);
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(intent);
        stream.terminalReported = true;
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
        if (snap) {
            ResetNativePhysics(*actor, networkId);
        }
        else if (const auto stream = _streams.find(networkId); stream != _streams.end() && !stream->second.simulationController) {
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
        auto *frame = record->second.frame;
        _nativeById.erase(record);
        if (auto stream = _streams.find(networkId); stream != _streams.end()) {
            stream->second.appliedDamageRevision = 0;
            stream->second.lastDamageApply = {};
            stream->second.hasReportedDamage = false;
            stream->second.pendingDamageReports.clear();
            stream->second.appliedMeshRevision = 0;
            stream->second.checkpointSourceGuid = 0;
            stream->second.lastMeshRequest = {};
            stream->second.hasReportedMesh = false;
            stream->second.meshDirty = stream->second.simulationController;
            stream->second.pendingHits.clear();
        }
        return frame;
    }

    bool CarService::Sample(uint64_t networkId, Framework::Utils::TransformSnapshot &out) const {
        const auto it = _streams.find(networkId);
        Stream::PoseSample pose;
        if (it == _streams.end() || !SamplePose(it->second, LocalClockMs() - it->second.delayMs, pose)) {
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

    void CarService::PushPose(Stream &stream, const Stream::PoseSample &pose, double arrival, bool reset) {
        if (reset) {
            stream.poses.clear();
        }
        if (!stream.poses.empty()) {
            const auto &last = stream.poses.back();
            if (pose.time <= last.time) {
                return;
            }
            stream.intervalMs = glm::mix(stream.intervalMs, static_cast<float>(pose.time - last.time), 0.1f);
        }
        // How much later than the least-delayed packet this one arrived: the
        // network and server-tick jitter the delay must cover. A decaying
        // peak rather than a mean, since one late packet is an underrun.
        const float lateness = static_cast<float>(std::clamp(arrival - pose.time, 0.0, 500.0));
        stream.latenessPeak  = std::max(lateness, stream.latenessPeak * 0.97f);
        stream.poses.push_back(pose);
        while (stream.poses.size() > 64) {
            stream.poses.pop_front();
        }
    }

    bool CarService::SamplePose(const Stream &stream, double time, Stream::PoseSample &out) {
        const auto &poses = stream.poses;
        if (poses.empty()) {
            return false;
        }
        if (time <= poses.front().time) {
            out = poses.front();
            return true;
        }
        const auto &newest = poses.back();
        if (time >= newest.time) {
            // Underrun or prediction: continue along the last linear and
            // angular velocity, for at most 150 ms.
            const float dt = static_cast<float>(std::min(time - newest.time, 150.0)) * 0.001f;
            out = newest;
            out.position += newest.velocity * dt;
            if (const float spin = glm::length(newest.angularVelocity); spin > 0.0001f) {
                out.rotation = glm::normalize(glm::angleAxis(spin * dt, newest.angularVelocity / spin) * newest.rotation);
            }
            out.time = time;
            return true;
        }
        auto next = std::upper_bound(poses.begin(), poses.end(), time, [](double t, const Stream::PoseSample &p) { return t < p.time; });
        const auto &b = *next;
        const auto &a = *std::prev(next);
        const float span = static_cast<float>(b.time - a.time) * 0.001f;
        const float u = static_cast<float>((time - a.time) / (b.time - a.time));
        out.time = time;
        if (span > 0.3f) {
            out.position = glm::mix(a.position, b.position, u);
            out.velocity = glm::mix(a.velocity, b.velocity, u);
        }
        else {
            // Cubic Hermite through both poses with their velocities, so the
            // path and its derivative stay continuous across samples.
            const float u2 = u * u;
            const float u3 = u2 * u;
            out.position = (2.0f * u3 - 3.0f * u2 + 1.0f) * a.position + (u3 - 2.0f * u2 + u) * span * a.velocity + (-2.0f * u3 + 3.0f * u2) * b.position +
                           (u3 - u2) * span * b.velocity;
            out.velocity = ((6.0f * u2 - 6.0f * u) * a.position + (-6.0f * u2 + 6.0f * u) * b.position) / span + (3.0f * u2 - 4.0f * u + 1.0f) * a.velocity +
                           (3.0f * u2 - 2.0f * u) * b.velocity;
        }
        out.rotation = glm::slerp(a.rotation, glm::dot(a.rotation, b.rotation) < 0.0f ? -b.rotation : b.rotation, u);
        out.angularVelocity = glm::mix(a.angularVelocity, b.angularVelocity, u);
        return true;
    }

    void CarService::OnSimulated(SDK::Car::NativeCar *car, unsigned int frameMs) {
        const auto id = _idByNative.find(car);
        if (id == _idByNative.end()) {
            return;
        }
        if (const auto stream = _streams.find(id->second); stream != _streams.end() && stream->second.simulationController) {
            // C_car::AI steps physics by at most 83 ms per frame.
            stream->second.simClockMs += std::min(frameMs, 83u);
            if (_simulatedSerial != _updateSerial) {
                _simulatedSerial = _updateSerial;
                _simulatedCars.clear();
            }
            const auto &vehicle  = car->Vehicle();
            const auto center    = vehicle.WorldCenter();
            const auto velocity  = vehicle.LinearVelocity();
            _simulatedCars.push_back({glm::vec3(center.x, center.y, center.z), glm::vec3(velocity.x, velocity.y, velocity.z)});
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
        if (streamIt == _streams.end() || streamIt->second.simulationController || nativeCar == _nativeById.end() || nativeCar->second.removalQueued || !state ||
            state->terminalState != Shared::Entities::CarEntity::TerminalState::Active || streamIt->second.poses.empty()) {
            return false;
        }
        auto &stream = streamIt->second;
        if (_clockSerial != _updateSerial) {
            _clockSerial = _updateSerial;
            _frameClock  = LocalClockMs();
        }
        const float dt = static_cast<float>(std::clamp(frameMs, 1u, 83u)) * 0.001f;
        auto *frame = nativeCar->second.frame;
        auto &vehicle = car->Vehicle();
        Stream::PoseSample pose;
        // A car placed from the network is immovable to local physics: a car
        // this client simulates would hit it like a wall. Near one, run it on
        // native physics with correction, so an impact shoves it at once;
        // the owner's result then arrives through the corrected pose.
        {
            const auto center = vehicle.WorldCenter();
            const auto velocity = vehicle.LinearVelocity();
            const glm::vec3 position(center.x, center.y, center.z);
            const glm::vec3 motion(velocity.x, velocity.y, velocity.z);
            for (const auto &[otherPosition, otherVelocity] : _simulatedCars) {
                const glm::vec3 offset = position - otherPosition;
                const float distance = glm::length(offset);
                const float closing = distance > 0.001f ? glm::dot(otherVelocity - motion, offset / distance) : 0.0f;
                if (distance < std::min(20.0f, 7.0f + std::max(0.0f, closing) * 0.5f)) {
                    stream.contactUntil = _frameClock + 1500.0;
                    break;
                }
            }
        }
        if (_predictedSync || stream.contactUntil > _frameClock) {
            // Native physics runs on the replicated pedals and steering, then
            // is pulled toward the pose predicted to the present, still
            // before C_car::Update and the camera read it.
            native(car, frameMs);
            if (!SamplePose(stream, _frameClock, pose)) {
                return true;
            }
            const auto current = frame->WorldPosition();
            const glm::vec3 position(current.x, current.y, current.z);
            const glm::quat rotation = FrameRotation(*frame);
            const glm::vec3 error = pose.position - position;
            const bool snap = glm::length(error) > 5.0f || std::abs(glm::dot(rotation, pose.rotation)) < 0.92f;
            const float blend = snap ? 1.0f : 1.0f - std::exp(-dt / 0.2f);
            const glm::vec3 corrected = position + error * blend;
            const glm::quat turned = glm::slerp(rotation, glm::dot(rotation, pose.rotation) < 0.0f ? -pose.rotation : pose.rotation, blend);
            WriteFrame(*frame, corrected, turned);
            if (snap) {
                ResetNativePhysics(*car, networkId);
            }
            const auto nativeVelocity = vehicle.LinearVelocity();
            const float velocityBlend = snap ? 1.0f : 1.0f - std::exp(-dt / 0.15f);
            const glm::vec3 velocity = glm::mix(glm::vec3(nativeVelocity.x, nativeVelocity.y, nativeVelocity.z), pose.velocity, velocityBlend);
            const auto nativeAngular = vehicle.AngularVelocity();
            const glm::vec3 angular = glm::mix(glm::vec3(nativeAngular.x, nativeAngular.y, nativeAngular.z), pose.angularVelocity, velocityBlend);
            vehicle.SetMotion({velocity.x, velocity.y, velocity.z}, {angular.x, angular.y, angular.z});
            vehicle.UpdateImportantVariables();
            stream.drivenSerial = _updateSerial;
            return true;
        }
        // Move the render delay slowly: every change shifts the sample time,
        // which at speed reads as a jerk. Underruns raise it faster.
        const float target = std::clamp(stream.intervalMs + stream.latenessPeak + 8.0f, 40.0f, 300.0f);
        const double renderTime = _frameClock - stream.delayMs;
        const bool underrun = renderTime > stream.poses.back().time;
        const float step = static_cast<float>(frameMs) * (underrun ? 0.1f : 0.01f);
        stream.delayMs += std::clamp(target - stream.delayMs, -step, underrun ? step * 2.0f : step);
        if (!SamplePose(stream, renderTime, pose)) {
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
        if (std::isfinite(state->steeringInput) && std::abs(state->steeringInput) <= 2.0f) {
            vehicle.SetSteering(state->steeringInput);
        }
        if (std::isfinite(state->engineRotations) && state->engineRotations >= 0.0f) {
            vehicle.SetEngineRotations(state->engineRotations);
        }
        // As UpdateReplayPlayback: the recorded skid bits drive skid marks,
        // tyre smoke and squeal in C_Vehicle::Update.
        for (int i = 0; i < std::min(vehicle.WheelCount(), 8); ++i) {
            if (auto *wheel = vehicle.MutableWheel(i)) {
                if (state->skidMask & (1u << i)) {
                    wheel->flags |= SDK::Car::kWheelHasVisual | SDK::Car::kWheelSkidding;
                }
                else {
                    wheel->flags &= ~(SDK::Car::kWheelHasVisual | SDK::Car::kWheelSkidding);
                }
                wheel->slipValue = 0.0f;
            }
        }
        vehicle.DoWheelsCollision(dt);
        vehicle.EngineFreq(dt);
        stream.drivenSerial = _updateSerial;
        return true;
    }

    void CarService::Reset() {
        _streams.clear();
    }
} // namespace Mafia1Online::Features::Car
