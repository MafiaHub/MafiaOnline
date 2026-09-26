#include "car_service.h"

#include "shared/features/car/car_engine_state.h"
#include "shared/features/car/car_damage_state.h"
#include "shared/features/car/car_movement.h"
#include "shared/features/car/car_mesh_checkpoint.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <utility>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace Mafia1Online::Features::Car {
    namespace {
        constexpr float kFuelReportStep = 0.05f;

        bool IsFinite(glm::vec3 value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFinite(glm::quat value) {
            return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool UnitInput(float value) {
            return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
        }

        bool IsModelIdentifier(std::string_view model) {
            if (model.size() < 5 || model.size() > 64 || model.substr(model.size() - 4) != ".i3d") {
                return false;
            }
            return std::all_of(model.begin(), model.end(), [](unsigned char character) {
                return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
            });
        }
    } // namespace

    Shared::Entities::CarEntity *CarService::Spawn(std::string_view model, glm::vec3 position, glm::quat rotation, uint64_t missionGeneration, uint64_t controllerGuid) {
        if (!IsModelIdentifier(model) || !IsFinite(position) || !IsFinite(rotation) || glm::length(rotation) < 0.0001f || missionGeneration == 0) {
            return nullptr;
        }
        auto *replication             = Framework::CoreModules::GetReplication();
        auto *car                     = replication->CreateEntity<Shared::Entities::CarEntity>();
        car->model                    = std::string(model);
        car->position                 = position;
        car->rotation                 = glm::normalize(rotation);
        car->missionGeneration        = missionGeneration;
        car->simulationControllerGuid = controllerGuid;
        // Until native player positions drive the viewer's interest area, keep
        // every server car visible so a late join receives the full state.
        car->streaming.alwaysVisible = true;
        car->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        _ids.insert(car->GetNetworkID());
        RecordHitPose(car->GetNetworkID(), *car);
        return car;
    }

    Shared::Entities::CarEntity *CarService::Find(uint64_t networkId) const {
        if (_ids.find(networkId) == _ids.end()) {
            return nullptr;
        }
        return Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(networkId);
    }

    bool CarService::Despawn(uint64_t networkId) {
        auto *car = Find(networkId);
        _motion.erase(networkId);
        _mesh.erase(networkId);
        _hitPoses.erase(networkId);
        _authoredDamageRevision.erase(networkId);
        if (!car) {
            _ids.erase(networkId);
            return false;
        }
        _ids.erase(networkId);
        Framework::CoreModules::GetReplication()->DestroyEntity(car);
        return true;
    }

    void CarService::ResetForMission() {
        auto *replication = Framework::CoreModules::GetReplication();
        for (uint64_t id : _ids) {
            if (auto *car = replication->GetEntity<Shared::Entities::CarEntity>(id)) {
                replication->DestroyEntity(car);
            }
        }
        _ids.clear();
        _motion.clear();
        _mesh.clear();
        _hitPoses.clear();
        _lastSeatIntent.clear();
        _authoredDamageRevision.clear();
    }

    bool CarService::ApplyMovement(const Shared::Car::Movement &movement, uint64_t senderGuid) {
        auto *car = Find(movement.networkId);
        if (!car || senderGuid == 0 || car->simulationControllerGuid != senderGuid || car->missionGeneration != movement.missionGeneration ||
            car->transformRevision != movement.transformRevision || movement.dynamicsCommandRevision > car->dynamicsCommandRevision ||
            (car->terminalState != Shared::Entities::CarEntity::TerminalState::Active &&
             car->terminalState != Shared::Entities::CarEntity::TerminalState::Submerged &&
             car->terminalState != Shared::Entities::CarEntity::TerminalState::FatalFall)) {
            return false;
        }
        const glm::vec3 position(movement.x, movement.y, movement.z);
        const glm::quat rotation(movement.qw, movement.qx, movement.qy, movement.qz);
        const glm::vec3 velocity(movement.velocityX, movement.velocityY, movement.velocityZ);
        const glm::vec3 angularVelocity(movement.angularVelocityX, movement.angularVelocityY, movement.angularVelocityZ);
        const float rotationLength = glm::length(rotation);
        if (!IsFinite(position) || !IsFinite(rotation) || !IsFinite(velocity) || !IsFinite(angularVelocity) || !std::isfinite(rotationLength) || rotationLength < 0.5f || rotationLength > 2.0f ||
            glm::length(velocity) > 120.0f || glm::length(angularVelocity) > 30.0f || !std::isfinite(movement.steeringInput) || std::abs(movement.steeringInput) > 2.0f ||
            !std::isfinite(movement.fuel) || !std::isfinite(movement.fuelTankCapacity) || movement.fuel < 0.0f ||
            movement.fuelTankCapacity < 0.0f || movement.fuelTankCapacity > 500.0f || movement.fuel > movement.fuelTankCapacity + 0.01f ||
            !std::isfinite(movement.engineRotations) || movement.engineRotations < 0.0f || movement.engineRotations > 20000.0f ||
            movement.maximumGear < 0 || movement.maximumGear > 10 || movement.gear < -1 || movement.gear > movement.maximumGear ||
            (movement.lightState & ~Shared::Car::Movement::kReplicatedLightMask) != 0 || !UnitInput(movement.powerInput) || !UnitInput(movement.brakeInput) ||
            !UnitInput(movement.handbrakeInput) || !UnitInput(movement.clutchInput) ||
            std::abs(position.x) > 50000.0f || std::abs(position.y) > 50000.0f || std::abs(position.z) > 50000.0f) {
            return false;
        }
        auto &state = _motion[movement.networkId];
        if (state.hasSequence && (std::abs(car->fuelTankCapacity - movement.fuelTankCapacity) > 0.1f || car->maximumGear != movement.maximumGear)) {
            return false;
        }
        if (state.hasSequence && static_cast<int32_t>(movement.sequence - state.lastSequence) <= 0) {
            return false;
        }
        const auto now      = std::chrono::steady_clock::now();
        const float elapsed = state.hasSequence ? std::clamp(std::chrono::duration<float>(now - state.lastMovement).count(), 0.0f, 3.0f) : 0.0f;
        const float allowed = state.hasSequence ? 3.0f + 100.0f * elapsed : 10.0f;
        if (glm::distance(position, car->position) > allowed) {
            return false;
        }
        const bool initialDynamics = !car->dynamicsValid;
        car->velocity      = velocity;
        car->position      = position;
        car->poseClockMs     = movement.clockMs;
        car->poseClockSource = senderGuid;
        car->rotation      = glm::normalize(rotation);
        car->dynamicsValid = true;
        car->angularVelocity = angularVelocity;
        car->steeringInput = movement.steeringInput;
        car->fuelTankCapacity = movement.fuelTankCapacity;
        if (car->fuel > car->fuelTankCapacity) {
            car->fuel = car->fuelTankCapacity;
            ++car->dynamicsCommandRevision;
        }
        car->engineRotations = movement.engineRotations;
        car->gear = movement.gear;
        car->maximumGear = movement.maximumGear;
        car->powerInput = movement.powerInput;
        car->brakeInput = movement.brakeInput;
        car->handbrakeInput = movement.handbrakeInput;
        car->clutchInput = movement.clutchInput;
        car->speedLimited = movement.speedLimited;
        car->skidMask = movement.skidMask;
        for (uint8_t seat = 0; seat < Shared::Entities::CarEntity::kMaxSeats; ++seat) {
            car->doorTargets[seat] = seat < car->seatCount ? movement.doorTargets[seat] : 0;
        }
        if (movement.dynamicsCommandRevision == car->dynamicsCommandRevision) {
            // The first report establishes this model's native tank level.
            // Thereafter only consumption may come from the controller;
            // refilling requires the server's SetFuel command.
            if (initialDynamics || car->fuel - movement.fuel >= kFuelReportStep || (movement.fuel == 0.0f && car->fuel != 0.0f)) {
                car->fuel = movement.fuel;
            }
            car->lightState = movement.lightState;
            car->hornOn = movement.hornOn;
        }
        state.lastSequence = movement.sequence;
        state.hasSequence  = true;
        state.lastMovement = now;
        RecordHitPose(movement.networkId, *car);
        return true;
    }

    void CarService::RecordHitPose(uint64_t carId, const Shared::Entities::CarEntity &car) {
        auto &poses = _hitPoses[carId];
        const auto now = std::chrono::steady_clock::now();
        poses.push_back({now, car.position, car.rotation, car.velocity, car.simulationControllerGuid});
        while (poses.size() > 64 || (!poses.empty() && now - poses.front().observedAt > std::chrono::seconds(2))) {
            poses.pop_front();
        }
    }

    bool CarService::ValidateHitGeometry(const Shared::Car::HitReport &report,
                                         std::chrono::steady_clock::time_point acceptedAt) const {
        const auto *car = Find(report.carId);
        const auto poses = _hitPoses.find(report.carId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active ||
            car->missionGeneration != report.missionGeneration || poses == _hitPoses.end()) {
            return false;
        }
        const glm::vec3 hit(report.hitX, report.hitY, report.hitZ);
        const glm::vec3 direction(report.directionX, report.directionY, report.directionZ);
        const glm::vec3 normal(report.normalX, report.normalY, report.normalZ);
        const glm::vec3 localHit(report.localHitX, report.localHitY, report.localHitZ);
        const glm::vec3 localDirection(report.localDirectionX, report.localDirectionY, report.localDirectionZ);
        if (!IsFinite(hit) || !IsFinite(direction) || !IsFinite(normal) || !IsFinite(localHit) || !IsFinite(localDirection) ||
            glm::length(localHit) > 15.0f || glm::length(direction) < 0.8f || glm::length(direction) > 1.2f ||
            glm::length(localDirection) < 0.8f || glm::length(localDirection) > 1.2f ||
            glm::length(normal) < 0.2f || glm::length(normal) > 1.5f) {
            return false;
        }
        // Match the car pose visible to the shooter, including its remote
        // interpolation delay. A teleport replaces the history entirely.
        for (const auto &pose : poses->second) {
            if (pose.observedAt + std::chrono::milliseconds(500) < acceptedAt ||
                pose.observedAt > acceptedAt + std::chrono::milliseconds(250)) {
                continue;
            }
            const glm::vec3 expectedHit = pose.position + pose.rotation * localHit;
            const glm::vec3 expectedDirection = pose.rotation * glm::normalize(localDirection);
            if (glm::distance(expectedHit, hit) <= 2.5f &&
                glm::dot(expectedDirection, glm::normalize(direction)) >= 0.95f) {
                return true;
            }
        }
        return false;
    }

    std::optional<float> CarService::ValidateVehicleImpactGeometry(uint64_t carId, uint64_t missionGeneration,
                                                                    const glm::vec3 &contact, const glm::vec3 &targetPosition,
                                                                    std::chrono::steady_clock::time_point receivedAt) const {
        const auto *car = Find(carId);
        const auto poses = _hitPoses.find(carId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active ||
            car->missionGeneration != missionGeneration || car->simulationControllerGuid == 0 ||
            poses == _hitPoses.end() || !IsFinite(contact) || !IsFinite(targetPosition)) {
            return std::nullopt;
        }
        // C_Vehicle's body sphere sweep reports a point on the pedestrian,
        // while the server has only the ped origin. Allow the actor's height
        // and a small amount of replication delay without accepting a hit on
        // another pedestrian several metres away.
        const glm::vec3 fromTarget = contact - targetPosition;
        if (glm::length(glm::vec2(fromTarget.x, fromTarget.z)) > 2.5f ||
            fromTarget.y < -1.5f || fromTarget.y > 3.0f) {
            return std::nullopt;
        }

        std::optional<float> acceptedSpeed;
        float bestBodyDistanceSquared = std::numeric_limits<float>::max();
        for (const auto &pose : poses->second) {
            if (pose.controllerGuid != car->simulationControllerGuid ||
                pose.observedAt + std::chrono::milliseconds(500) < receivedAt ||
                pose.observedAt > receivedAt + std::chrono::milliseconds(150) ||
                !IsFinite(pose.velocity)) {
                continue;
            }
            const float speed = glm::length(pose.velocity);
            if (!std::isfinite(speed) || speed < 3.0f || speed > 120.0f) {
                continue;
            }
            // Server car models do not expose native collision bounds. These
            // conservative stock-car extents admit the front, side and rear
            // sweep while excluding the much larger old radius-only check.
            const glm::vec3 local = glm::inverse(pose.rotation) * (contact - pose.position);
            if (!IsFinite(local) || std::abs(local.x) > 2.4f || local.y < -1.5f || local.y > 2.8f ||
                std::abs(local.z) > 4.5f) {
                continue;
            }
            const float bodyDistanceSquared = (local.x * local.x) / (2.4f * 2.4f) +
                                              (local.z * local.z) / (4.5f * 4.5f);
            if (bodyDistanceSquared < bestBodyDistanceSquared) {
                bestBodyDistanceSquared = bodyDistanceSquared;
                acceptedSpeed = speed;
            }
        }
        return acceptedSpeed;
    }

    void CarService::ResetMeshSender(uint64_t networkId) {
        // The client restarts its mesh sequence whenever it gains control, so
        // A -> B -> A would otherwise be rejected until A passed its old number.
        if (auto mesh = _mesh.find(networkId); mesh != _mesh.end()) {
            mesh->second.ownerGuid = 0;
        }
    }

    bool CarService::ApplyEngineState(const Shared::Car::EngineState &report, uint64_t senderGuid) {
        auto *car = Find(report.networkId);
        if (!car || senderGuid == 0 || car->simulationControllerGuid != senderGuid || car->missionGeneration != report.missionGeneration || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
            return false;
        }
        auto &state = _motion[report.networkId];
        if (state.hasEngineSequence && static_cast<int32_t>(report.sequence - state.lastEngineSequence) <= 0) {
            return false;
        }
        state.lastEngineSequence = report.sequence;
        state.hasEngineSequence  = true;
        if (car->engineOn != report.on) {
            car->engineOn = report.on;
            ++car->engineRevision;
        }
        return true;
    }

    bool CarService::ApplyDamageReport(const Shared::Car::DamageReport &report, uint64_t senderGuid) {
        auto *car = Find(report.networkId);
        if (!car || senderGuid == 0 || car->simulationControllerGuid != senderGuid ||
            car->missionGeneration != report.missionGeneration ||
            car->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
            return false;
        }
        if (const auto authored = _authoredDamageRevision.find(report.networkId);
            authored != _authoredDamageRevision.end() && report.baseRevision < authored->second) {
            return false;
        }
        auto damage = report.state;
        if (damage.lightCount > Shared::Car::DamageState::kMaxLights ||
            damage.zoneCount > Shared::Car::DamageState::kMaxZones ||
            damage.wheelCount > Shared::Car::DamageState::kMaxWheels ||
            !std::isfinite(damage.engineHealth) || !std::isfinite(damage.gearboxHealth) ||
            !std::isfinite(damage.bodyDamage) ||
            damage.engineHealth < 0.0f || damage.engineHealth > 100000.0f ||
            damage.gearboxHealth < 0.0f || damage.gearboxHealth > 100000.0f ||
            damage.bodyDamage < 0.0f || damage.bodyDamage > 100000.0f ||
            damage.fuelTankHealth < 0 || damage.fuelTankHealth > 100000 ||
            !std::isfinite(damage.engineDamagePower) || damage.engineDamagePower < 0.0f || damage.engineDamagePower > 1.5f ||
            damage.engineDestroyed > 1 || damage.burning > 1 ||
            damage.burnTimer > 600000 || damage.burnDuration > 600000) {
            return false;
        }
        if (car->nativeDamageValid &&
            (car->nativeDamage.lightCount != damage.lightCount || car->nativeDamage.zoneCount != damage.zoneCount ||
             car->nativeDamage.wheelCount != damage.wheelCount)) {
            return false;
        }
        for (uint8_t i = 0; i < damage.lightCount; ++i) {
            if ((damage.lights[i].flags & ~1U) || !std::isfinite(damage.lights[i].damage) ||
                damage.lights[i].damage < 0.0f || damage.lights[i].damage > 100000.0f) {
                return false;
            }
        }
        for (uint8_t i = 0; i < damage.zoneCount; ++i) {
            if ((damage.zones[i].flags & ~3U) || !std::isfinite(damage.zones[i].crackLevel) ||
                damage.zones[i].crackLevel < 0.0f || damage.zones[i].crackLevel > 100000.0f) {
                return false;
            }
        }
        for (uint8_t i = 0; i < damage.wheelCount; ++i) {
            if ((damage.wheels[i].flags & ~Shared::Car::DamageState::kWheelDamageFlags) ||
                !std::isfinite(damage.wheels[i].health) || damage.wheels[i].health < 0.0f || damage.wheels[i].health > 100000.0f ||
                !std::isfinite(damage.wheels[i].deformAngle) || std::abs(damage.wheels[i].deformAngle) > 3.2f) {
                return false;
            }
        }
        // Unused fixed slots have no native counterpart. Discard their wire
        // values so a controller cannot advance revisions by changing them.
        for (uint8_t i = damage.lightCount; i < Shared::Car::DamageState::kMaxLights; ++i) {
            damage.lights[i] = {};
        }
        for (uint8_t i = damage.zoneCount; i < Shared::Car::DamageState::kMaxZones; ++i) {
            damage.zones[i] = {};
        }
        for (uint8_t i = damage.wheelCount; i < Shared::Car::DamageState::kMaxWheels; ++i) {
            damage.wheels[i] = {};
        }
        auto &sequence = _motion[report.networkId];
        if (sequence.hasDamageSequence && static_cast<int32_t>(report.sequence - sequence.lastDamageSequence) <= 0) {
            return false;
        }
        sequence.lastDamageSequence = report.sequence;
        sequence.hasDamageSequence = true;
        if (car->nativeDamageValid && car->nativeDamage == damage) {
            return false;
        }
        car->nativeDamage = damage;
        car->nativeDamageValid = true;
        ++car->nativeDamageRevision;
        return true;
    }

    bool CarService::ApplyMeshReportChunk(const Shared::Car::MeshReportChunk &chunk, uint64_t senderGuid) {
        auto *car = Find(chunk.networkId);
        if (!car || !car->nativeDamageValid || senderGuid == 0 || car->simulationControllerGuid != senderGuid ||
            car->missionGeneration != chunk.missionGeneration ||
            car->repairRevision != chunk.baseRepairRevision ||
            car->terminalState != Shared::Entities::CarEntity::TerminalState::Active ||
            !Shared::Car::ValidateMeshChunkShape(chunk.totalVertices, chunk.chunkIndex,
                                                  chunk.chunkCount, chunk.vertexCount)) {
            return false;
        }
        auto &mesh = _mesh[chunk.networkId];
        if (mesh.ownerGuid != senderGuid) {
            mesh.ownerGuid = senderGuid;
            mesh.lastSequence = 0;
            mesh.hasSequence = false;
            mesh.pending.clear();
            mesh.received.clear();
        }
        if (mesh.hasSequence && static_cast<int32_t>(chunk.sequence - mesh.lastSequence) <= 0) {
            return false;
        }
        if (mesh.pending.empty() && mesh.received.empty() ||
            static_cast<int32_t>(chunk.sequence - mesh.pendingSequence) > 0) {
            mesh.pendingSequence = chunk.sequence;
            mesh.pendingCount = chunk.chunkCount;
            mesh.pendingTotal = chunk.totalVertices;
            mesh.pending.resize(chunk.totalVertices);
            mesh.received.assign(chunk.chunkCount, 0);
        }
        if (chunk.sequence != mesh.pendingSequence || chunk.chunkCount != mesh.pendingCount ||
            chunk.totalVertices != mesh.pendingTotal || mesh.received[chunk.chunkIndex]) {
            return false;
        }
        const size_t offset = static_cast<size_t>(chunk.chunkIndex) * Shared::Car::kMeshVerticesPerChunk;
        for (uint16_t i = 0; i < chunk.vertexCount; ++i) {
            const auto &vertex = chunk.vertices[i];
            if (vertex.zone >= car->nativeDamage.zoneCount || vertex.lod > 1) {
                mesh.pending.clear();
                mesh.received.clear();
                return false;
            }
            mesh.pending[offset + i] = vertex;
        }
        mesh.received[chunk.chunkIndex] = 1;
        if (std::find(mesh.received.begin(), mesh.received.end(), 0) != mesh.received.end()) {
            return false;
        }
        if (!Shared::Car::ValidateMeshVertexSet(mesh.pending, car->nativeDamage.zoneCount)) {
            mesh.pending.clear();
            mesh.received.clear();
            return false;
        }
        mesh.lastSequence = mesh.pendingSequence;
        mesh.hasSequence = true;
        std::sort(mesh.pending.begin(), mesh.pending.end(), [](const auto &a, const auto &b) {
            if (a.zone != b.zone) { return a.zone < b.zone; }
            if (a.lod != b.lod) { return a.lod < b.lod; }
            return (a.displacement >> 18) < (b.displacement >> 18);
        });
        const bool changed = mesh.checkpoint != mesh.pending;
        if (changed) {
            mesh.checkpoint = mesh.pending;
            mesh.sourceGuid = senderGuid;
            ++car->meshRevision;
        }
        mesh.pending.clear();
        mesh.received.clear();
        if (!changed) {
            return false;
        }
        const uint16_t count = std::max<uint16_t>(1, static_cast<uint16_t>((mesh.checkpoint.size() + Shared::Car::kMeshVerticesPerChunk - 1) / Shared::Car::kMeshVerticesPerChunk));
        for (uint16_t index = 0; index < count; ++index) {
            Shared::Car::MeshCheckpointChunk output;
            output.networkId = chunk.networkId;
            output.missionGeneration = chunk.missionGeneration;
            output.sequence = static_cast<uint32_t>(car->meshRevision);
            output.chunkIndex = index;
            output.chunkCount = count;
            output.totalVertices = static_cast<uint16_t>(mesh.checkpoint.size());
            output.vertexCount = static_cast<uint16_t>(std::min<size_t>(Shared::Car::kMeshVerticesPerChunk,
                mesh.checkpoint.size() - static_cast<size_t>(index) * Shared::Car::kMeshVerticesPerChunk));
            output.revision = car->meshRevision;
            output.sourceGuid = senderGuid;
            for (uint16_t i = 0; i < output.vertexCount; ++i) {
                output.vertices[i] = mesh.checkpoint[static_cast<size_t>(index) * Shared::Car::kMeshVerticesPerChunk + i];
            }
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(output);
        }
        return true;
    }

    void CarService::SendMeshCheckpoint(uint64_t networkId, uint64_t recipientGuid) const {
        const auto *car = Find(networkId);
        const auto stored = _mesh.find(networkId);
        if (!car || stored == _mesh.end() || car->meshRevision == 0 || recipientGuid == 0) {
            return;
        }
        const auto &mesh = stored->second;
        const uint16_t count = std::max<uint16_t>(1, static_cast<uint16_t>((mesh.checkpoint.size() + Shared::Car::kMeshVerticesPerChunk - 1) / Shared::Car::kMeshVerticesPerChunk));
        for (uint16_t index = 0; index < count; ++index) {
            Shared::Car::MeshCheckpointChunk output;
            output.networkId = networkId;
            output.missionGeneration = car->missionGeneration;
            output.sequence = static_cast<uint32_t>(car->meshRevision);
            output.chunkIndex = index;
            output.chunkCount = count;
            output.totalVertices = static_cast<uint16_t>(mesh.checkpoint.size());
            output.vertexCount = static_cast<uint16_t>(std::min<size_t>(Shared::Car::kMeshVerticesPerChunk,
                mesh.checkpoint.size() - static_cast<size_t>(index) * Shared::Car::kMeshVerticesPerChunk));
            output.revision = car->meshRevision;
            output.sourceGuid = mesh.sourceGuid;
            for (uint16_t i = 0; i < output.vertexCount; ++i) {
                output.vertices[i] = mesh.checkpoint[static_cast<size_t>(index) * Shared::Car::kMeshVerticesPerChunk + i];
            }
            Framework::CoreModules::GetNetworkPeer()->SendRPC(output, MafiaNet::ToGuid(static_cast<MafiaNet::PeerGuid>(recipientGuid)));
        }
    }

    void CarService::TransferController(uint64_t formerGuid, uint64_t replacementGuid) {
        if (formerGuid == 0 || formerGuid == replacementGuid) {
            return;
        }
        for (uint64_t id : _ids) {
            if (auto *car = Find(id); car && car->simulationControllerGuid == formerGuid) {
                car->simulationControllerGuid = replacementGuid;
                _motion.erase(id);
                ResetMeshSender(id);
            }
        }
    }

    void CarService::SetController(uint64_t networkId, uint64_t controllerGuid) {
        if (auto *car = Find(networkId); car && car->simulationControllerGuid != controllerGuid) {
            car->simulationControllerGuid = controllerGuid;
            _motion.erase(networkId);
            ResetMeshSender(networkId);
        }
    }

    void CarService::AssignUncontrolled(uint64_t controllerGuid) {
        if (controllerGuid == 0) {
            return;
        }
        for (uint64_t id : _ids) {
            if (auto *car = Find(id); car && car->simulationControllerGuid == 0) {
                car->simulationControllerGuid = controllerGuid;
                _motion.erase(id);
                ResetMeshSender(id);
            }
        }
    }

    bool CarService::SetTransform(uint64_t networkId, glm::vec3 position, glm::vec3 velocity, glm::quat rotation) {
        auto *car = Find(networkId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active || !IsFinite(position) || !IsFinite(velocity) || !IsFinite(rotation) || glm::length(rotation) < 0.0001f) {
            return false;
        }
        car->position = position;
        car->velocity = velocity;
        car->rotation = glm::normalize(rotation);
        car->forcedPosition = position;
        car->forcedRotation = car->rotation;
        ++car->transformRevision;
        _motion.erase(networkId);
        _hitPoses.erase(networkId);
        RecordHitPose(networkId, *car);
        return true;
    }

    bool CarService::SetMechanicalDamage(uint64_t networkId, float engineHealth, float gearboxHealth, float bodyDamage, int32_t fuelTankHealth) {
        auto *car = Find(networkId);
        if (!car || !car->nativeDamageValid || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active ||
            !std::isfinite(engineHealth) || !std::isfinite(gearboxHealth) || !std::isfinite(bodyDamage) ||
            engineHealth < 0.0f || engineHealth > 100000.0f || gearboxHealth < 0.0f || gearboxHealth > 100000.0f ||
            bodyDamage < 0.0f || bodyDamage > 100000.0f || fuelTankHealth < 0 || fuelTankHealth > 100000) {
            return false;
        }
        auto damage = car->nativeDamage;
        damage.engineHealth = engineHealth;
        damage.gearboxHealth = gearboxHealth;
        damage.bodyDamage = bodyDamage;
        damage.fuelTankHealth = fuelTankHealth;
        // The controller recomputes power from its model's engine limits.
        damage.engineDamagePower = -1.0f;
        if (engineHealth > 0.0f && fuelTankHealth > 0) {
            // A repair must also stop the native burn countdown, otherwise
            // the car still explodes.
            damage.engineDestroyed = 0;
            damage.burning = 0;
            damage.burnTimer = 0;
            damage.burnDuration = 0;
        }
        if (damage == car->nativeDamage) {
            return true;
        }
        car->nativeDamage = damage;
        ++car->nativeDamageRevision;
        _authoredDamageRevision[networkId] = car->nativeDamageRevision;
        return true;
    }

    bool CarService::SetDamage(uint64_t networkId, float health, uint32_t damageFlags, uint32_t detachedParts) {
        auto *car = Find(networkId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active || !std::isfinite(health) || health < 0.0f) {
            return false;
        }
        car->health        = health;
        car->damageFlags   = damageFlags;
        car->detachedParts = detachedParts;
        return true;
    }

    bool CarService::SetOpacity(uint64_t networkId, float opacity) {
        auto *car = Find(networkId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active ||
            !std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f) {
            return false;
        }
        car->opacity = opacity;
        return true;
    }

    bool CarService::Repair(uint64_t networkId) {
        auto *car = Find(networkId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
            return false;
        }
        // A repair is a fresh native damage/deform baseline. Do not retain
        // the old snapshot while the controller is reporting the repaired car.
        ++car->repairRevision;
        car->health = 100.0f;
        car->damageFlags = 0;
        car->detachedParts = 0;
        car->nativeDamage = {};
        car->nativeDamageValid = false;
        ++car->nativeDamageRevision;
        _authoredDamageRevision[networkId] = car->nativeDamageRevision;

        auto &mesh = _mesh[networkId];
        mesh.pending.clear();
        mesh.received.clear();
        mesh.checkpoint.clear();
        mesh.sourceGuid = 0;
        mesh.ownerGuid = 0;
        mesh.hasSequence = false;
        ++car->meshRevision;
        return true;
    }

    bool CarService::SetEngineOn(uint64_t networkId, bool on) {
        auto *car = Find(networkId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
            return false;
        }
        if (car->engineOn != on) {
            car->engineOn = on;
            ++car->engineRevision;
        }
        return true;
    }

    bool CarService::SetFuel(uint64_t networkId, float fuel) {
        auto *car = Find(networkId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active || !car->dynamicsValid ||
            !std::isfinite(fuel) || fuel < 0.0f || fuel > car->fuelTankCapacity) {
            return false;
        }
        if (car->fuel != fuel) {
            car->fuel = fuel;
            ++car->dynamicsCommandRevision;
        }
        return true;
    }

    bool CarService::SetLights(uint64_t networkId, uint32_t lightState) {
        auto *car = Find(networkId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active || !car->dynamicsValid ||
            (lightState & ~Shared::Car::Movement::kReplicatedLightMask) != 0) {
            return false;
        }
        if (car->lightState != lightState) {
            car->lightState = lightState;
            ++car->dynamicsCommandRevision;
        }
        return true;
    }

    bool CarService::SetHorn(uint64_t networkId, bool on) {
        auto *car = Find(networkId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active || !car->dynamicsValid) {
            return false;
        }
        if (car->hornOn != on) {
            car->hornOn = on;
            ++car->dynamicsCommandRevision;
        }
        return true;
    }

    bool CarService::SetSiren(uint64_t networkId, bool on) {
        auto *car = Find(networkId);
        if (!car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
            return false;
        }
        car->sirenOn = on;
        return true;
    }

    bool CarService::SetRadarColor(uint64_t networkId, uint32_t argb) {
        auto *car = Find(networkId);
        if (!car) {
            return false;
        }
        car->radarColor = argb;
        return true;
    }

    bool CarService::SetTerminalState(uint64_t networkId, Shared::Entities::CarEntity::TerminalState state) {
        auto *car = Find(networkId);
        if (!car || state > Shared::Entities::CarEntity::TerminalState::FatalFall || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active || state == Shared::Entities::CarEntity::TerminalState::Active) {
            return false;
        }
        car->terminalState = state;
        ++car->terminalSequence;
        if (state == Shared::Entities::CarEntity::TerminalState::Exploded && car->engineOn) {
            car->engineOn = false;
            ++car->engineRevision;
        }
        if (state == Shared::Entities::CarEntity::TerminalState::Exploded) {
            car->health = 0.0f;
        }
        if (state == Shared::Entities::CarEntity::TerminalState::Exploded) {
            car->sirenOn = false;
        }
        // Terminal cars have no enterable seats. The native explosion ejects
        // the corresponding humans when each client applies this revision.
        for (uint8_t seat = 0; state == Shared::Entities::CarEntity::TerminalState::Exploded &&
                               seat < car->seatCount && seat < Shared::Entities::CarEntity::kMaxSeats; ++seat) {
            if (car->occupantIds[seat] == 0) {
                continue;
            }
            car->seatActorId              = car->occupantIds[seat];
            car->seatIndex                = seat;
            car->seatResult               = Shared::Entities::CarEntity::SeatResult::Exited;
            car->occupantIds[seat]         = 0;
            car->occupantGenerations[seat] = 0;
            ++car->seatSequence;
        }
        return true;
    }

    bool CarService::SetSeatCount(uint64_t networkId, uint8_t count) {
        auto *car = Find(networkId);
        if (!car || count == 0 || count > Shared::Entities::CarEntity::kMaxSeats) {
            return false;
        }
        for (uint8_t seat = count; seat < car->seatCount; ++seat) {
            if (car->occupantIds[seat] != 0) {
                return false;
            }
        }
        car->seatCount = count;
        return true;
    }

    bool CarService::RecordSeatOutcome(uint64_t networkId, uint8_t seat, uint64_t playerId, uint64_t playerGeneration, Shared::Entities::CarEntity::SeatResult result) {
        auto *car = Find(networkId);
        if (!car || seat >= car->seatCount || seat >= Shared::Entities::CarEntity::kMaxSeats || playerId == 0 || playerGeneration == 0 || result == Shared::Entities::CarEntity::SeatResult::None || result > Shared::Entities::CarEntity::SeatResult::ExitBlocked) {
            return false;
        }
        if (car->terminalState != Shared::Entities::CarEntity::TerminalState::Active && result != Shared::Entities::CarEntity::SeatResult::Exited) {
            return false;
        }
        const bool occupiedByPlayer = car->occupantIds[seat] == playerId && car->occupantGenerations[seat] == playerGeneration;
        switch (result) {
        case Shared::Entities::CarEntity::SeatResult::Entered:
            if (car->occupantIds[seat] != 0) {
                return false;
            }
            break;
        case Shared::Entities::CarEntity::SeatResult::Stolen:
            // An accepted StealBegin already evicted the victim, so the
            // thief usually completes into a free seat.
            if (occupiedByPlayer) {
                return false;
            }
            break;
        case Shared::Entities::CarEntity::SeatResult::Exited:
        case Shared::Entities::CarEntity::SeatResult::ExitBlocked:
            if (!occupiedByPlayer) {
                return false;
            }
            break;
        case Shared::Entities::CarEntity::SeatResult::None: return false;
        }
        if (result == Shared::Entities::CarEntity::SeatResult::Entered || result == Shared::Entities::CarEntity::SeatResult::Stolen) {
            // Resolve a prior occupancy before adding the new one. A player is
            // never allowed to appear in two seats or two cars at once.
            for (uint64_t id : _ids) {
                if (auto *other = Find(id)) {
                    for (uint8_t otherSeat = 0; otherSeat < other->seatCount && otherSeat < Shared::Entities::CarEntity::kMaxSeats; ++otherSeat) {
                        if (other->occupantIds[otherSeat] == playerId && other->occupantGenerations[otherSeat] == playerGeneration) {
                            other->occupantIds[otherSeat]         = 0;
                            other->occupantGenerations[otherSeat] = 0;
                        }
                    }
                }
            }
            car->occupantIds[seat]         = playerId;
            car->occupantGenerations[seat] = playerGeneration;
            // The driver simulates the car, including a script-seated driver.
            const auto *driver = seat == 0 ? Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::PlayerEntity>(playerId) : nullptr;
            if (driver && driver->controllerGuid != 0 && car->simulationControllerGuid != driver->controllerGuid) {
                car->simulationControllerGuid = driver->controllerGuid;
                _motion.erase(networkId);
                ResetMeshSender(networkId);
            }
        }
        else if (result == Shared::Entities::CarEntity::SeatResult::Exited) {
            car->occupantIds[seat]         = 0;
            car->occupantGenerations[seat] = 0;
        }
        ++car->seatSequence;
        car->seatActorId = playerId;
        car->seatIndex   = seat;
        car->seatResult  = result;
        return true;
    }

    std::optional<CarService::SeatLocation> CarService::SeatForPlayer(uint64_t playerId, uint64_t playerGeneration) const {
        for (uint64_t id : _ids) {
            if (const auto *car = Find(id)) {
                for (uint8_t seat = 0; seat < car->seatCount && seat < Shared::Entities::CarEntity::kMaxSeats; ++seat) {
                    if (car->occupantIds[seat] == playerId && car->occupantGenerations[seat] == playerGeneration) {
                        return SeatLocation{id, seat};
                    }
                }
            }
        }
        return std::nullopt;
    }

    std::optional<CarService::Eviction> CarService::TakeEviction() {
        return std::exchange(_lastEviction, std::nullopt);
    }

    std::optional<Shared::Car::SeatEvent> CarService::ApplySeatIntent(const Shared::Car::SeatIntent &intent, Shared::Entities::PlayerEntity &player, uint64_t missionGeneration) {
        using Action = Shared::Car::SeatAction;
        using Result = Shared::Entities::CarEntity::SeatResult;
        _lastEviction.reset();
        auto *car = Find(intent.carId);
        if (!car || (car->terminalState != Shared::Entities::CarEntity::TerminalState::Active &&
                     !(car->terminalState == Shared::Entities::CarEntity::TerminalState::Submerged &&
                       (intent.action == Action::Exit || intent.action == Action::ExitBlocked))) ||
            intent.seat >= car->seatCount || intent.seat >= Shared::Entities::CarEntity::kMaxSeats ||
            intent.playerId != player.GetNetworkID() || intent.spawnGeneration != player.spawnGeneration || intent.missionGeneration != missionGeneration || car->missionGeneration != missionGeneration ||
            player.missionGeneration != missionGeneration || !player.spawned || !player.alive || intent.sequence == 0) {
            return std::nullopt;
        }
        auto previous = _lastSeatIntent.find(intent.playerId);
        if (previous != _lastSeatIntent.end() && static_cast<int32_t>(intent.sequence - previous->second) <= 0) {
            return std::nullopt;
        }
        const bool seatedHere = car->occupantIds[intent.seat] == intent.playerId && car->occupantGenerations[intent.seat] == intent.spawnGeneration;
        const bool entering = intent.action == Action::Enter || intent.action == Action::EnterBegin || intent.action == Action::Steal || intent.action == Action::StealBegin;
        if (entering && glm::distance(player.position, car->position) > 12.0f) {
            return std::nullopt;
        }
        if (intent.action == Action::Move) {
            // reM Do_ClimbInCarLR transfers native ownership before the
            // climb animation. Move the durable seat in one server update.
            const uint8_t from = static_cast<uint8_t>(intent.seat ^ 1);
            if (from >= car->seatCount || car->occupantIds[from] != intent.playerId ||
                car->occupantGenerations[from] != intent.spawnGeneration || car->occupantIds[intent.seat] != 0 ||
                (intent.seat & ~1u) != (from & ~1u)) {
                return std::nullopt;
            }
            car->occupantIds[from] = 0;
            car->occupantGenerations[from] = 0;
            car->occupantIds[intent.seat] = intent.playerId;
            car->occupantGenerations[intent.seat] = intent.spawnGeneration;
            car->seatActorId = intent.playerId;
            car->seatIndex = intent.seat;
            car->seatResult = Result::Moved;
            ++car->seatSequence;
        } else if (intent.action == Action::EnterBegin) {
            if (car->occupantIds[intent.seat] != 0 || SeatForPlayer(intent.playerId, intent.spawnGeneration)) {
                return std::nullopt;
            }
        } else if (intent.action == Action::StealBegin) {
            if (car->occupantIds[intent.seat] == 0 || seatedHere || SeatForPlayer(intent.playerId, intent.spawnGeneration)) {
                return std::nullopt;
            }
            // Retail's throw (Do_ThrowCocotFromCar -> intern_ThrowMeFromCar)
            // frees the seat at once and the attacker enters afterwards, so
            // the victim leaves the seat now rather than on a later Steal.
            const uint64_t victim = car->occupantIds[intent.seat];
            const uint64_t victimGeneration = car->occupantGenerations[intent.seat];
            if (!RecordSeatOutcome(intent.carId, intent.seat, victim, victimGeneration, Result::Exited)) {
                return std::nullopt;
            }
            _lastEviction = {victim, victimGeneration};
        } else {
            Result result;
            switch (intent.action) {
            case Action::Enter: result = Result::Entered; break;
            case Action::Steal: result = Result::Stolen; break;
            case Action::Exit: result = Result::Exited; break;
            case Action::ExitBlocked: result = Result::ExitBlocked; break;
            default: return std::nullopt;
            }
            if (!RecordSeatOutcome(intent.carId, intent.seat, intent.playerId, intent.spawnGeneration, result)) {
                return std::nullopt;
            }
        }
        _lastSeatIntent[intent.playerId] = intent.sequence;
        Shared::Car::SeatEvent event;
        static_cast<Shared::Car::SeatIntent &>(event) = intent;
        event.serverSequence = ++_seatEventSequence;
        return event;
    }

    void CarService::ClearOccupant(uint64_t playerId, uint64_t playerGeneration) {
        _lastSeatIntent.erase(playerId);
        for (uint64_t id : _ids) {
            auto *car = Find(id);
            if (!car) {
                continue;
            }
            for (uint8_t seat = 0; seat < car->seatCount && seat < Shared::Entities::CarEntity::kMaxSeats; ++seat) {
                if (car->occupantIds[seat] == playerId && car->occupantGenerations[seat] == playerGeneration) {
                    car->occupantIds[seat]         = 0;
                    car->occupantGenerations[seat] = 0;
                    ++car->seatSequence;
                    car->seatActorId = playerId;
                    car->seatIndex   = seat;
                    // Life end/disconnect removes the human without a native
                    // exit animation on clients still simulating this car.
                    car->seatResult  = Shared::Entities::CarEntity::SeatResult::Cleared;
                }
            }
        }
    }
} // namespace Mafia1Online::Features::Car
