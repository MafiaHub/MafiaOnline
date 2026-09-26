#include "debris_service.h"

#include "debris_hooks.h"
#include "features/world/world_service.h"
#include "shared/features/car/car_debris_entity.h"
#include "shared/features/car/car_entity.h"

#include <core_modules.h>
#include <mafia1/sdk/car/native_car.h>
#include <mafia1/sdk/core/mission.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Mafia1Online::Features::Car {
    namespace {
        constexpr auto kMovementInterval = std::chrono::milliseconds(50);

        glm::quat FrameRotation(SDK::Scene::NativeFrame *frame) {
            const auto basis = frame->GetWorldBasis();
            const glm::vec3 right(basis.right.x, basis.right.y, basis.right.z);
            const glm::vec3 up(basis.up.x, basis.up.y, basis.up.z);
            const glm::vec3 forward(basis.forward.x, basis.forward.y, basis.forward.z);
            return glm::normalize(glm::quat_cast(glm::mat3(right, up, forward)));
        }

        float RollForDirection(const glm::quat &rotation, const glm::vec3 &forward) {
            const glm::vec3 targetUp = glm::mat3_cast(rotation) * glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 referenceRight = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward);
            if (glm::length(referenceRight) < 0.001f) {
                referenceRight = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), forward);
            }
            referenceRight = glm::normalize(referenceRight);
            const glm::vec3 referenceUp = glm::normalize(glm::cross(forward, referenceRight));
            return std::atan2(-glm::dot(targetUp, referenceRight), glm::dot(targetUp, referenceUp));
        }

        SDK::Scene::NativeFrame *SourceFrame(SDK::Car::NativeCar *car, uint8_t type, uint8_t partIndex) {
            auto &vehicle = car->Vehicle();
            if (type == 1) {
                const auto *wheel = vehicle.Wheel(partIndex);
                return wheel ? wheel->frame : nullptr;
            }
            const auto &zones = vehicle.DeformZones();
            if (partIndex >= zones.Size()) {
                return nullptr;
            }
            const auto &zone = zones.begin[partIndex];
            const uint16_t expectedZoneType = type == 2 ? 2 : type == 3 ? 5 : type == 4 ? 7 :
                                              type == 5 ? 4 : type == 6 ? 6 : type == 7 ? 8 : 0;
            return zone.type == expectedZoneType ? zone.frame : nullptr;
        }
    } // namespace

    bool DebrisService::AllowNativeDropOut(World::WorldService &world, SDK::Car::NativeCar *car) const {
        const auto handle = world.NativeObjects().FindByNative(car);
        if (handle.networkId == 0) {
            return true;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        const auto *state = replication->GetEntity<Shared::Entities::CarEntity>(handle.networkId);
        return state && state->missionGeneration == world.LoadedMissionGeneration() &&
               state->simulationControllerGuid == static_cast<uint64_t>(replication->GetMyGUID());
    }

    void DebrisService::OnNativeDropOut(World::WorldService &world, SDK::Car::NativeCar *car,
                                        SDK::Player::NativeActor *actor, int type, int partIndex,
                                        const void *parameters) {
        const auto handle = world.NativeObjects().FindByNative(car);
        auto *replication = Framework::CoreModules::GetReplication();
        const auto *state = replication->GetEntity<Shared::Entities::CarEntity>(handle.networkId);
        if (!state || state->missionGeneration != world.LoadedMissionGeneration() ||
            state->simulationControllerGuid != static_cast<uint64_t>(replication->GetMyGUID()) ||
            !actor || !actor->Frame() || !parameters || type < 1 || type > 7 || partIndex < 0 || partIndex > 255) {
            return;
        }
        Shared::Car::DebrisSpawnReport report;
        report.carId = handle.networkId;
        report.missionGeneration = state->missionGeneration;
        report.localSequence = ++_localSequence;
        report.type = static_cast<uint8_t>(type);
        report.partIndex = static_cast<uint8_t>(partIndex);
        auto &v = report.parameters.values;
        if (type == 1) {
            const auto &native = *static_cast<const SDK::Car::NativeDropOutWheel *>(parameters);
            v = {native.radius, native.scale, native.weight,
                 native.linearVelocity.x, native.linearVelocity.y, native.linearVelocity.z,
                 native.lateralForceMaximum, native.rollingResistance};
        }
        else {
            const auto &native = *static_cast<const SDK::Car::NativeDropOutBox *>(parameters);
            v = {native.weight, native.direction.x, native.direction.y, native.direction.z,
                 native.friction, 0.0f, 0.0f, 0.0f};
        }
        const auto position = actor->Frame()->WorldPosition();
        const auto rotation = FrameRotation(actor->Frame());
        report.x = position.x; report.y = position.y; report.z = position.z;
        report.qw = rotation.w; report.qx = rotation.x;
        report.qy = rotation.y; report.qz = rotation.z;
        _pending[report.localSequence] = actor;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
    }

    void DebrisService::CreateNative(World::WorldService &world, uint64_t networkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        const auto *debris = replication->GetEntity<Shared::Entities::CarDebrisEntity>(networkId);
        if (!debris || debris->missionGeneration != world.LoadedMissionGeneration()) {
            return;
        }
        const auto carHandle = world.NativeObjects().FindByNetwork(debris->carId);
        auto *car = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(carHandle));
        auto *source = car ? SourceFrame(car, debris->type, debris->partIndex) : nullptr;
        if (!source) {
            return;
        }
        SDK::Player::NativeActor *actor = nullptr;
        const auto &v = debris->parameters.values;
        const auto type = static_cast<SDK::Car::NativeDropOutType>(debris->type);
        if (debris->type == 1) {
            SDK::Car::NativeDropOutWheel native {v[0], v[1], v[2], {v[3], v[4], v[5]}, v[6], v[7]};
            actor = ApplyAuthoritativeDropOut(car, source, &native, type, debris->partIndex);
        }
        else {
            SDK::Car::NativeDropOutBox native {v[0], {v[1], v[2], v[3]}, v[4], type};
            actor = ApplyAuthoritativeDropOut(car, source, &native, type, debris->partIndex);
        }
        if (!actor) {
            return;
        }
        LiveActor live;
        live.handle = world.NativeObjects().Bind(actor, networkId);
        _live[networkId] = live;
        ApplyPose(world, networkId, _live[networkId]);
    }

    void DebrisService::ReportMovement(World::WorldService &world, uint64_t networkId, LiveActor &live) {
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(live.handle));
        if (!actor || !actor->Frame()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (live.lastMovement.time_since_epoch().count() && now - live.lastMovement < kMovementInterval) {
            return;
        }
        Shared::Car::DebrisMovement movement;
        movement.debrisId = networkId;
        movement.missionGeneration = world.LoadedMissionGeneration();
        movement.sequence = ++live.movementSequence;
        const auto position = actor->Frame()->WorldPosition();
        const auto rotation = FrameRotation(actor->Frame());
        movement.x = position.x; movement.y = position.y; movement.z = position.z;
        movement.qw = rotation.w; movement.qx = rotation.x;
        movement.qy = rotation.y; movement.qz = rotation.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(movement, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
        live.lastMovement = now;
    }

    void DebrisService::ApplyPose(World::WorldService &world, uint64_t networkId, LiveActor &live) {
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(live.handle));
        const auto *debris = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarDebrisEntity>(networkId);
        if (!actor || !actor->Frame() || !debris) {
            return;
        }
        const glm::vec3 direction = glm::normalize(debris->rotation * glm::vec3(0.0f, 0.0f, 1.0f));
        actor->Frame()->SetWorldPosition({debris->position.x, debris->position.y, debris->position.z});
        actor->Frame()->SetDirection({direction.x, direction.y, direction.z}, RollForDirection(debris->rotation, direction));
        actor->Frame()->Update();
    }

    void DebrisService::QueueRemoval(World::WorldService &world, uint64_t networkId) {
        auto it = _live.find(networkId);
        if (it == _live.end()) {
            return;
        }
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(it->second.handle));
        world.NativeObjects().InvalidateNetwork(networkId);
        if (actor && SDK::Core::Mission::Get() && SDK::Core::Mission::Get()->Game()) {
            SDK::Core::Mission::Get()->Game()->RemoveTemporaryActor(actor);
        }
        _live.erase(it);
    }

    void DebrisService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || world.LoadedMissionGeneration() == 0) {
            Reset(world);
            return;
        }
        const uint64_t myGuid = static_cast<uint64_t>(replication->GetMyGUID());
        std::unordered_set<uint64_t> seen;
        replication->ForEach<Shared::Entities::CarDebrisEntity>([&](auto *debris) {
            if (debris->missionGeneration != world.LoadedMissionGeneration()) {
                return;
            }
            const uint64_t id = debris->GetNetworkID();
            seen.insert(id);
            if (_goneRequested.find(id) != _goneRequested.end()) {
                if (debris->controllerGuid == myGuid) { return; }
                _goneRequested.erase(id);
            }
            if (_live.find(id) == _live.end() && _evicted.find(id) == _evicted.end()) {
                if (debris->creatorGuid == myGuid) {
                    const auto pending = _pending.find(debris->creatorSequence);
                    if (pending != _pending.end()) {
                        _live[id].handle = world.NativeObjects().Bind(pending->second, id);
                        _pending.erase(pending);
                    }
                    else if (_expired.erase(debris->creatorSequence) != 0 && debris->controllerGuid == myGuid) {
                        Shared::Car::DebrisGone gone {id, debris->missionGeneration};
                        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(gone);
                        _goneRequested.insert(id);
                    }
                    else {
                        CreateNative(world, id);
                    }
                }
                else {
                    CreateNative(world, id);
                }
            }
            const auto live = _live.find(id);
            if (live == _live.end()) {
                return;
            }
            if (debris->controllerGuid == myGuid) {
                ReportMovement(world, id, live->second);
            }
            else {
                ApplyPose(world, id, live->second);
            }
        });
        for (auto it = _live.begin(); it != _live.end();) {
            if (seen.find(it->first) == seen.end()) {
                const uint64_t id = it->first;
                ++it;
                QueueRemoval(world, id);
            }
            else { ++it; }
        }
        for (auto it = _goneRequested.begin(); it != _goneRequested.end();) {
            if (seen.find(*it) == seen.end()) { it = _goneRequested.erase(it); }
            else { ++it; }
        }
        for (auto it = _evicted.begin(); it != _evicted.end();) {
            if (seen.find(*it) == seen.end()) { it = _evicted.erase(it); }
            else { ++it; }
        }
    }

    void DebrisService::OnNativeDestroyed(World::WorldService &world, void *actor) {
        for (auto it = _pending.begin(); it != _pending.end();) {
            if (it->second == actor) {
                _expired.insert(it->first);
                it = _pending.erase(it);
            }
            else { ++it; }
        }
        const auto handle = world.NativeObjects().FindByNative(actor);
        auto live = _live.find(handle.networkId);
        if (handle.networkId == 0 || live == _live.end() || live->second.handle.generation != handle.generation) {
            return;
        }
        const auto *debris = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarDebrisEntity>(handle.networkId);
        if (debris && debris->controllerGuid == static_cast<uint64_t>(Framework::CoreModules::GetReplication()->GetMyGUID())) {
            Shared::Car::DebrisGone gone {handle.networkId, debris->missionGeneration};
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(gone);
            _goneRequested.insert(handle.networkId);
        }
        else if (!live->second.removalQueued) {
            _evicted.insert(handle.networkId);
        }
        _live.erase(live);
    }

    void DebrisService::Reset(World::WorldService &world) {
        for (auto it = _live.begin(); it != _live.end();) {
            const uint64_t id = it->first;
            ++it;
            QueueRemoval(world, id);
        }
        if (auto *mission = SDK::Core::Mission::Get(); mission && mission->Game()) {
            for (auto &[sequence, actor] : _pending) {
                if (actor) { mission->Game()->RemoveTemporaryActor(actor); }
            }
        }
        _pending.clear();
        _expired.clear();
        _goneRequested.clear();
        _evicted.clear();
        _localSequence = 0;
    }
} // namespace Mafia1Online::Features::Car
