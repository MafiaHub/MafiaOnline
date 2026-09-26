#include "player_service.h"

#include "features/seat/seat_service.h"
#include "features/world/world_service.h"

#include "shared/features/player/player_climb.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_movement.h"
#include "shared/features/player/player_native_pose.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/player/native_human_factory.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace Mafia1Online::Features::Player {
    namespace {
        constexpr auto kSendInterval       = std::chrono::milliseconds(50);
        constexpr auto kPoseInterval       = std::chrono::milliseconds(250);
        constexpr const char *kHumanModel  = "Tommy.i3d";
        constexpr float kIdleSpeed         = 0.22f;
        constexpr float kRunSpeed          = 3.0f;
        constexpr float kDiagonalComponent = 0.38f;

        bool IsFinite(const Framework::Utils::TransformSnapshot &snapshot) {
            return std::isfinite(snapshot.position.x) && std::isfinite(snapshot.position.y) && std::isfinite(snapshot.position.z) && std::isfinite(snapshot.velocity.x) && std::isfinite(snapshot.velocity.y) && std::isfinite(snapshot.velocity.z)
                   && std::isfinite(snapshot.rotation.w) && std::isfinite(snapshot.rotation.x) && std::isfinite(snapshot.rotation.y) && std::isfinite(snapshot.rotation.z);
        }

        Framework::Utils::TransformSnapshot PoseFromFrame(SDK::Scene::NativeFrame *frame) {
            const auto position  = frame->WorldPosition();
            const auto direction = frame->WorldDirection();
            const float yaw      = std::atan2(direction.x, direction.z);
            return {{position.x, position.y, position.z}, {0.0f, 0.0f, 0.0f}, glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f))};
        }

        SDK::Player::LocomotionAnimation LocomotionFromPose(const Framework::Utils::TransformSnapshot &pose) {
            using Animation   = SDK::Player::LocomotionAnimation;
            const float speed = std::hypot(pose.velocity.x, pose.velocity.z);
            if (speed < kIdleSpeed) {
                return Animation::Idle;
            }

            const auto forward       = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
            const float facingLength = std::hypot(forward.x, forward.z);
            if (facingLength < 0.001f) {
                return Animation::Idle;
            }
            const float forwardMotion = (pose.velocity.x * forward.x + pose.velocity.z * forward.z) / (speed * facingLength);
            const float rightMotion   = (pose.velocity.x * forward.z - pose.velocity.z * forward.x) / (speed * facingLength);
            const bool running        = speed >= kRunSpeed;

            if (forwardMotion > kDiagonalComponent) {
                if (rightMotion > kDiagonalComponent) {
                    return running ? Animation::RunForwardRight : Animation::WalkForwardRight;
                }
                if (rightMotion < -kDiagonalComponent) {
                    return running ? Animation::RunForwardLeft : Animation::WalkForwardLeft;
                }
                return running ? Animation::RunForward : Animation::WalkForward;
            }
            if (forwardMotion < -kDiagonalComponent) {
                if (rightMotion > kDiagonalComponent) {
                    return Animation::WalkBackwardRight;
                }
                if (rightMotion < -kDiagonalComponent) {
                    return Animation::WalkBackwardLeft;
                }
                return Animation::WalkBackward;
            }
            if (rightMotion >= 0.0f) {
                return running ? Animation::RunRight : Animation::WalkRight;
            }
            return running ? Animation::RunLeft : Animation::WalkLeft;
        }
    } // namespace

    void PlayerService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || !world.IsReady()) {
            Reset(world);
            return;
        }
        ReplayClimbs(world);

        Shared::Entities::PlayerEntity *controlled = nullptr;
        const auto myGuid                          = static_cast<uint64_t>(replication->GetMyGUID());
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (player->controllerGuid == myGuid && player->missionGeneration == world.LoadedMissionGeneration()) {
                controlled = player;
            }
        });
        std::unordered_set<uint64_t> seen;
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (player->missionGeneration != world.LoadedMissionGeneration()) {
                return;
            }
            if (player->controllerGuid == myGuid) {
                return;
            }
            const uint64_t id = player->GetNetworkID();
            if (!controlled || !controlled->spawned || !player->spawned) {
                QueueRemoval(world, id);
                return;
            }
            seen.insert(id);
            auto &stream = _remote[id];
            if (stream.missionGeneration != player->missionGeneration || stream.spawnGeneration != player->spawnGeneration) {
                QueueRemoval(world, id);
                stream                   = {};
                stream.missionGeneration = player->missionGeneration;
                stream.spawnGeneration   = player->spawnGeneration;
            }
            if (stream.creationFailed && _failedModels[id] != player->model) {
                stream.creationFailed = false;
            }
            const MafiaNet::Time packetTime = player->lastUpdateTime == 0 ? MafiaNet::GetTime() : player->lastUpdateTime;
            if (stream.lastPacketTime != packetTime) {
                Framework::Utils::TransformSnapshot snapshot {player->position, player->velocity, player->rotation};
                if (IsFinite(snapshot) && glm::length(snapshot.rotation) >= 0.0001f) {
                    snapshot.rotation = glm::normalize(snapshot.rotation);
                    stream.snapshots.Push(snapshot, packetTime);
                    stream.locomotion.emplace_back(packetTime, player->locomotion);
                    while (stream.locomotion.size() > 32) {
                        stream.locomotion.pop_front();
                    }
                    stream.lastPacketTime = packetTime;
                }
            }
            if (!_currentById.contains(id) && !stream.creationFailed) {
                Framework::Utils::TransformSnapshot pose {player->position, player->velocity, player->rotation};
                if (IsFinite(pose) && glm::length(pose.rotation) >= 0.0001f) {
                    pose.rotation         = glm::normalize(pose.rotation);
                    stream.creationFailed = !CreateNative(world, id, player->spawnGeneration, false, pose, player->model);
                }
            }
            ApplyModel(world, id, player->model);
            Framework::Utils::TransformSnapshot pose;
            if (SampleRemote(id, pose)) {
                ApplyRemotePose(world, id, pose, player->alive, RemoteLocomotion(stream));
            }
        });
        for (auto it = _remote.begin(); it != _remote.end();) {
            if (!seen.contains(it->first)) {
                QueueRemoval(world, it->first);
                it = _remote.erase(it);
            }
            else {
                ++it;
            }
        }

        if (!controlled) {
            ReleaseLocal(world);
            ResetLocal(world);
            return;
        }

        const uint64_t id = controlled->GetNetworkID();
        if (!controlled->spawned) {
            ReleaseLocal(world);
            if (_local.networkId || _spawnGeneration != 0) {
                ResetLocal(world);
            }
            const auto now = std::chrono::steady_clock::now();
            if (_lastSent == std::chrono::steady_clock::time_point {} || now - _lastSent >= kPoseInterval) {
                if (auto pose = SuggestedPose()) {
                    SendNativePose(id, controlled->missionGeneration, *pose);
                    _lastSent = now;
                }
            }
            return;
        }

        const bool changed = _local.networkId != id || _spawnGeneration != controlled->spawnGeneration || _missionGeneration != controlled->missionGeneration || !world.NativeObjects().Resolve(_local);
        if (changed) {
            ReleaseLocal(world);
            ResetLocal(world);
            Framework::Utils::TransformSnapshot pose {controlled->position, controlled->velocity, controlled->rotation};
            if (!IsFinite(pose) || glm::length(pose.rotation) < 0.0001f) {
                return;
            }
            pose.rotation = glm::normalize(pose.rotation);
            auto *stock   = SDK::Player::CurrentPlayer();
            if (SDK::Player::IsPlayerActor(stock) && !stock->IsDead() && !_ownedByNative.contains(stock)) {
                auto *frame          = stock->Frame();
                const auto direction = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
                frame->SetWorldPosition({pose.position.x, pose.position.y, pose.position.z});
                frame->SetDirection({direction.x, direction.y, direction.z});
                frame->Update();
                stock->SetStoredTransform({pose.position.x, pose.position.y, pose.position.z}, {direction.x, direction.y, direction.z});
                _local = world.NativeObjects().Bind(stock, id);
                _appliedModels[id] = kHumanModel;
            }
            else if (!CreateNative(world, id, controlled->spawnGeneration, true, pose, controlled->model)) {
                return;
            }
            _spawnGeneration   = controlled->spawnGeneration;
            _missionGeneration = controlled->missionGeneration;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Bound native local player to replica {} (spawn {})", id, _spawnGeneration);
        }

        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(_local));
        if (!actor) {
            ResetLocal(world);
            return;
        }
        ApplyModel(world, id, controlled->model);
        if (controlled->alive) {
            auto *game = SDK::Core::Mission::Get()->Game();
            // A scripted camera may follow a remote human while the game's
            // controlled player and human pointers must remain local.
            if (!game->HasControlledHuman(actor)) {
                game->SetPlayerForInitialization(actor);
                game->FocusPlayer(actor);
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Restored native player and camera focus to replica {} (spawn {})", id, _spawnGeneration);
            }
            if (changed) {
                // SetHuman assigns the camera's actor but keeps its current
                // mode. A death leaves that mode at 6, so restore the retail
                // pedestrian mode after the new living actor has focus.
                const uint32_t oldMode = game->Camera().Mode();
                game->Camera().ResetForPedestrian();
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Respawn camera reset for replica {} (spawn {}): mode {} -> {}, focus {}", id, _spawnGeneration, oldMode,
                    game->Camera().Mode(), game->HasPlayerFocus(actor));
            }
            static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor))->SetHealth(controlled->health);
        }
        const auto now = std::chrono::steady_clock::now();
        if (!controlled->alive || (_lastSent != std::chrono::steady_clock::time_point {} && now - _lastSent < kSendInterval)) {
            return;
        }
        const auto position  = SDK::Player::WorldPosition(actor);
        const auto direction = SDK::Player::WorldDirection(actor);
        const float yaw      = std::atan2(direction.x, direction.z);
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(yaw)) {
            return;
        }
        Shared::Player::Movement movement;
        movement.networkId         = id;
        movement.missionGeneration = _missionGeneration;
        movement.spawnGeneration   = _spawnGeneration;
        movement.sequence          = ++_sequence;
        movement.x                 = position.x;
        movement.y                 = position.y;
        movement.z                 = position.z;
        movement.yaw               = yaw;
        movement.locomotion        = static_cast<int16_t>(static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor))->AnimationState());
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(movement, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
        _lastSent = now;
    }

    bool PlayerService::CreateNative(World::WorldService &world, uint64_t networkId, uint64_t spawnGeneration, bool local,
                                     const Framework::Utils::TransformSnapshot &pose, const std::string &model) {
        auto *mission          = SDK::Core::Mission::Get();
        const auto direction   = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
        const std::string name = (local ? "Mafia1OnlinePlayer_" : "Mafia1OnlineHuman_") + std::to_string(networkId);
        const bool validModel  = model == kHumanModel || SDK::Player::NativeHumanFactory::CanLoadHumanModel(model);
        const char *selectedModel = validModel ? model.c_str() : kHumanModel;
        auto *actor            = SDK::Player::NativeHumanFactory::CreateTemporary(*mission, local ? SDK::Player::NativeActor::Type::Player : SDK::Player::NativeActor::Type::Entity, name.c_str(), selectedModel, {pose.position.x, pose.position.y, pose.position.z},
            {direction.x, direction.y, direction.z});
        if (!actor && validModel && model != kHumanModel) {
            selectedModel = kHumanModel;
            actor = SDK::Player::NativeHumanFactory::CreateTemporary(*mission, local ? SDK::Player::NativeActor::Type::Player : SDK::Player::NativeActor::Type::Entity, name.c_str(), selectedModel,
                {pose.position.x, pose.position.y, pose.position.z}, {direction.x, direction.y, direction.z});
        }
        if (!actor) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not create native {} replica {}", local ? "player" : "human", networkId);
            _failedModels[networkId] = model;
            return false;
        }
        const auto handle       = world.NativeObjects().Bind(actor, networkId);
        _currentById[networkId] = actor;
        _ownedByNative.emplace(actor, NativeHuman {handle, actor->Frame(), spawnGeneration, local, false});
        _appliedModels[networkId] = selectedModel;
        if (selectedModel != model) {
            _failedModels[networkId] = model;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Could not load human model '{}' for replica {}; showing '{}'", model, networkId, selectedModel);
        }
        if (local) {
            _local = handle;
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Created native {} replica {} (spawn {})", local ? "player" : "human", networkId, spawnGeneration);
        return true;
    }

    void PlayerService::ApplyModel(World::WorldService &world, uint64_t networkId, const std::string &model) {
        if (_appliedModels[networkId] == model || _failedModels[networkId] == model) {
            return;
        }
        const auto handle = world.NativeObjects().FindByNetwork(networkId);
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(handle));
        if (!actor || !actor->IsAlive()) {
            return;
        }
        const auto *seat = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(actor));
        auto *human = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
        // The retail model swap resets the animation machine and weapon
        // attachment. Wait for native action steps and car use to finish.
        const int32_t workState = human->Melee().canWork;
        if (seat->usedActorEnter || seat->usedActorLeave ||
            (workState != SDK::Player::kHumanIdleWorkState && workState != SDK::Player::kHumanLocomotionWorkState)) {
            return;
        }
        if (!SDK::Player::NativeHumanFactory::CanLoadHumanModel(model)) {
            _failedModels[networkId] = model;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Human model '{}' for replica {} is missing or lacks the required skeleton", model, networkId);
            return;
        }
        std::string writableModel = model;
        human->ChangeModel(writableModel.data());
        actor->Frame()->Update();
        _appliedModels[networkId] = model;
        _failedModels.erase(networkId);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Changed human replica {} model to '{}'", networkId, model);
    }

    void PlayerService::QueueRemoval(World::WorldService &world, uint64_t networkId) {
        _appliedModels.erase(networkId);
        _failedModels.erase(networkId);
        const auto current = _currentById.find(networkId);
        if (current == _currentById.end()) {
            return;
        }
        void *native = current->second;
        _currentById.erase(current);
        auto owned = _ownedByNative.find(native);
        if (owned == _ownedByNative.end() || owned->second.removalQueued) {
            world.NativeObjects().InvalidateNetwork(networkId);
            return;
        }
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(owned->second.handle));
        world.NativeObjects().InvalidateNetwork(networkId);
        owned->second.removalQueued = true;
        if (actor && world.IsReady()) {
            auto &camera = SDK::Core::Mission::Get()->Game()->Camera();
            if (camera.Player() == actor && !owned->second.local) {
                camera.SetCar(nullptr);
                camera.SetPlayer(nullptr);
            }
            Seat::ReleaseHumanSeat(static_cast<SDK::Seat::NativeHuman *>(static_cast<void *>(actor)));
            auto *game = SDK::Core::Mission::Get()->Game();
            if (owned->second.local) {
                game->ClearPlayer(actor);
            }
            game->RemoveTemporaryActor(actor);
        }
    }

    SDK::Scene::NativeFrame *PlayerService::OnNativeDestroyed(World::WorldService &world, void *native) {
        auto owned = _ownedByNative.find(native);
        if (owned == _ownedByNative.end()) {
            if (_local.object == native) {
                if (world.IsReady()) {
                    SDK::Core::Mission::Get()->Game()->ClearPlayer(static_cast<SDK::Player::NativeActor *>(native));
                }
                _local = {};
            }
            return nullptr;
        }
        const auto record = owned->second;
        if (world.IsReady() && !record.local) {
            auto &camera = SDK::Core::Mission::Get()->Game()->Camera();
            if (camera.Player() == native) {
                camera.SetCar(nullptr);
                camera.SetPlayer(nullptr);
            }
        }
        if (auto current = _currentById.find(record.handle.networkId); current != _currentById.end() && current->second == native) {
            _currentById.erase(current);
        }
        if (_local.object == native) {
            _local = {};
        }
        if (record.local && world.IsReady()) {
            SDK::Core::Mission::Get()->Game()->ClearPlayer(static_cast<SDK::Player::NativeActor *>(native));
        }
        _ownedByNative.erase(owned);
        return record.frame;
    }

    void PlayerService::RegisterRPC() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Player::Climb>([this](const Shared::Player::Climb &climb, MafiaNet::Packet *) {
            if (climb.networkId == _local.networkId || _climbs.size() >= 32) {
                return;
            }
            _climbs.push_back({climb.networkId, climb.spawnGeneration, glm::vec3(climb.x, climb.y, climb.z), glm::vec3(climb.directionX, 0.0f, climb.directionZ),
                               std::chrono::steady_clock::now()});
        });
    }

    void PlayerService::OnLocalClimb(World::WorldService &world, const glm::vec3 &start, const glm::vec3 &direction) {
        auto *replication = Framework::CoreModules::GetReplication();
        const auto *player = _local.networkId && replication ? replication->GetEntity<Shared::Entities::PlayerEntity>(_local.networkId) : nullptr;
        if (!world.IsReady() || !player || !player->spawned || !player->alive) {
            return;
        }
        Shared::Player::Climb climb;
        climb.networkId         = _local.networkId;
        climb.missionGeneration = player->missionGeneration;
        climb.spawnGeneration   = player->spawnGeneration;
        climb.x                 = start.x;
        climb.y                 = start.y;
        climb.z                 = start.z;
        const glm::vec3 flat    = glm::length(glm::vec3(direction.x, 0.0f, direction.z)) > 0.001f ? glm::normalize(glm::vec3(direction.x, 0.0f, direction.z)) : glm::vec3(0.0f, 0.0f, 1.0f);
        climb.directionX        = flat.x;
        climb.directionZ        = flat.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(climb);
    }

    void PlayerService::ReplayClimbs(World::WorldService &world) {
        const auto now = std::chrono::steady_clock::now();
        while (!_climbs.empty()) {
            const auto climb = _climbs.front();
            _climbs.pop_front();
            if (now - climb.received > std::chrono::seconds(2)) {
                continue;
            }
            const auto current = _currentById.find(climb.networkId);
            const auto owned = current == _currentById.end() ? _ownedByNative.end() : _ownedByNative.find(current->second);
            if (owned == _ownedByNative.end() || owned->second.local || owned->second.spawnGeneration != climb.spawnGeneration) {
                continue;
            }
            auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(owned->second.handle));
            if (!actor || !actor->IsAlive()) {
                continue;
            }
            // Do_Climb probes from the stored pose, so start where the
            // controlling client started; its root motion then lifts the body.
            const SDK::Player::Vector3 position {climb.position.x, climb.position.y, climb.position.z};
            const SDK::Player::Vector3 direction {climb.direction.x, 0.0f, climb.direction.z};
            owned->second.frame->SetWorldPosition(position);
            owned->second.frame->SetDirection(direction);
            owned->second.frame->Update();
            actor->SetStoredTransform(position, direction);
            _replayingClimb = true;
            const bool climbing = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor))->DoClimb();
            _replayingClimb = false;
            if (climbing) {
                _climbingUntil[climb.networkId] = now + std::chrono::milliseconds(2500);
            }
        }
    }

    std::optional<PlayerService::RemoteAnimation> PlayerService::RemoteLocomotion(const RemoteStream &stream) const {
        // The state at the same render time as the interpolated pose, so a
        // stop, turn or jump plays where the body actually does it.
        const MafiaNet::Time now   = MafiaNet::GetTime();
        const auto delay           = static_cast<MafiaNet::Time>(stream.snapshots.EffectiveDelayMs());
        const MafiaNet::Time time  = now > delay ? now - delay : 0;
        std::optional<int16_t> state;
        for (const auto &[at, value] : stream.locomotion) {
            if (at > time && state) {
                break;
            }
            state = value;
        }
        if (!state) {
            return std::nullopt;
        }
        using Animation = SDK::Player::LocomotionAnimation;
        // The upper bits select the weapon/crouch variant, which each human
        // applies from its own replicated state (Do_Jump masks the same way).
        const auto base = static_cast<int16_t>(*state & 0x1ff);
        switch (base) {
        case static_cast<int16_t>(Animation::Idle):
        case static_cast<int16_t>(Animation::RunForward):
        case static_cast<int16_t>(Animation::WalkForward):
        case static_cast<int16_t>(Animation::WalkBackward):
        case static_cast<int16_t>(Animation::WalkLeft):
        case static_cast<int16_t>(Animation::WalkRight):
        case static_cast<int16_t>(Animation::RunLeft):
        case static_cast<int16_t>(Animation::RunRight):
        case static_cast<int16_t>(Animation::RunForwardLeft):
        case static_cast<int16_t>(Animation::RunForwardRight):
        case static_cast<int16_t>(Animation::WalkForwardLeft):
        case static_cast<int16_t>(Animation::WalkForwardRight):
        case static_cast<int16_t>(Animation::WalkBackwardLeft):
        case static_cast<int16_t>(Animation::WalkBackwardRight):
        case 102: return RemoteAnimation {base, false};
        // Jumps and the hard landing play once over the streamed arc.
        case 95:
        case 97:
        case 98:
        case 99:
        case 100:
        case 101:
        case 103: return RemoteAnimation {base, true};
        default: break;
        }
        return RemoteAnimation {static_cast<int16_t>(Animation::Idle), false};
    }

    void PlayerService::ApplyRemotePose(World::WorldService &world, uint64_t networkId, const Framework::Utils::TransformSnapshot &pose, bool alive,
                                        std::optional<RemoteAnimation> locomotion) {
        const auto current = _currentById.find(networkId);
        if (current == _currentById.end()) {
            return;
        }
        if (const auto climbing = _climbingUntil.find(networkId); climbing != _climbingUntil.end()) {
            const auto owned = _ownedByNative.find(current->second);
            auto *human = owned == _ownedByNative.end() ? nullptr : static_cast<SDK::Player::NativeHuman *>(world.NativeObjects().Resolve(owned->second.handle));
            if (human && human->IsClimbing() && std::chrono::steady_clock::now() < climbing->second) {
                return;
            }
            _climbingUntil.erase(climbing);
        }
        const auto owned = _ownedByNative.find(current->second);
        if (owned == _ownedByNative.end()) {
            return;
        }
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(owned->second.handle));
        if (!actor) {
            return;
        }
        const auto *seated = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(actor));
        if ((seated->usedActorEnter && world.NativeObjects().FindByNative(seated->usedActorEnter).networkId != 0) || (seated->usedActorLeave && world.NativeObjects().FindByNative(seated->usedActorLeave).networkId != 0)) {
            return;
        }
        const auto direction = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
        owned->second.frame->SetWorldPosition({pose.position.x, pose.position.y, pose.position.z});
        owned->second.frame->SetDirection({direction.x, direction.y, direction.z});
        owned->second.frame->Update();
        actor->SetStoredTransform({pose.position.x, pose.position.y, pose.position.z}, {direction.x, direction.y, direction.z});
        if (alive) {
            auto *human = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
            // C_human::Do_Shoot owns its melee animation until native
            // Movement/Update clears the action state. Do not replace that
            // animation with a network locomotion sample mid-swing.
            const auto melee = human->Melee();
            if (melee.canWork == SDK::Player::kHumanTimedActionWorkState || melee.weaponChangeTime >= 0) {
                return;
            }
            // A server-accepted hit starts retail's pain/knockdown steps in
            // work state 6. The next pose sample must leave those steps to
            // C_human::Movement; it restores locomotion when they finish.
            if (melee.canWork == SDK::Player::kHumanAnimationSequenceWorkState) {
                return;
            }
            // Do_Shoot(false) starts the retail grenade throw as state 163.
            // Its animation notify releases the held model and the native
            // Update returns to idle when the full track finishes. Replacing
            // it with a streamed movement state here cuts off both the throw
            // and its final frames on remote humans.
            if (human->AnimationState() == SDK::Player::kHumanThrowGrenadeAnimation) {
                return;
            }
            if (!locomotion) {
                human->SetLocomotionAnimation(LocomotionFromPose(pose));
            }
            else if (!locomotion->oneShot) {
                human->SetLocomotionAnimation(static_cast<SDK::Player::LocomotionAnimation>(locomotion->state));
            }
            else if (owned->second.lastAnimation != locomotion->state) {
                // Started once: the native animation returns to idle by
                // itself while the replicated state still names the jump.
                human->SetLocomotionAnimation(static_cast<SDK::Player::LocomotionAnimation>(locomotion->state));
            }
            owned->second.lastAnimation = locomotion ? locomotion->state : 0;
        }
    }


    std::optional<Framework::Utils::TransformSnapshot> PlayerService::SuggestedPose() const {
        auto *mission = SDK::Core::Mission::Get();
        if (auto *stock = SDK::Player::CurrentPlayer(); SDK::Player::IsPlayerActor(stock)) {
            return PoseFromFrame(stock->Frame());
        }
        auto *scene                    = mission->GetScene();
        SDK::Scene::NativeFrame *frame = scene->FindFrame("emeth_1");
        if (!frame) {
            frame = scene->ActiveCamera();
        }
        if (!frame) {
            return std::nullopt;
        }
        return PoseFromFrame(frame);
    }

    void PlayerService::SendNativePose(uint64_t networkId, uint64_t missionGeneration, const Framework::Utils::TransformSnapshot &pose) {
        if (!IsFinite(pose)) {
            return;
        }
        const auto direction = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
        Shared::Player::NativePose report;
        report.networkId         = networkId;
        report.missionGeneration = missionGeneration;
        report.x                 = pose.position.x;
        report.y                 = pose.position.y;
        report.z                 = pose.position.z;
        report.yaw               = std::atan2(direction.x, direction.z);
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
    }

    bool PlayerService::SampleRemote(uint64_t networkId, Framework::Utils::TransformSnapshot &out) const {
        const auto it = _remote.find(networkId);
        if (it == _remote.end()) {
            return false;
        }
        const MafiaNet::Time now = MafiaNet::GetTime();
        const auto delay         = static_cast<MafiaNet::Time>(it->second.snapshots.EffectiveDelayMs());
        bool held                = false;
        const bool sampled       = it->second.snapshots.Sample(now > delay ? now - delay : 0, out, &held);
        if (sampled && held) {
            out.velocity = glm::vec3(0.0f);
        }
        return sampled;
    }

    void PlayerService::ReleaseLocal(World::WorldService &world) {
        if (!_local.networkId) {
            return;
        }
        if (_currentById.contains(_local.networkId)) {
            QueueRemoval(world, _local.networkId);
            return;
        }
        // The mission's stock player is borrowed, not a temporary actor.
        // Clear its native player, human and camera references before the
        // registry handle is invalidated for a new spawn.
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(_local));
        if (actor && world.IsReady()) {
            SDK::Core::Mission::Get()->Game()->ClearPlayer(actor);
        }
    }

    void PlayerService::ResetLocal(World::WorldService &world) {
        if (_local.networkId) {
            _appliedModels.erase(_local.networkId);
            _failedModels.erase(_local.networkId);
            world.NativeObjects().InvalidateNetwork(_local.networkId);
        }
        _local             = {};
        _spawnGeneration   = 0;
        _missionGeneration = 0;
        _sequence          = 0;
        _lastSent          = {};
    }

    void PlayerService::Reset(World::WorldService &world) {
        _climbs.clear();
        _climbingUntil.clear();
        std::vector<uint64_t> current;
        current.reserve(_currentById.size());
        for (const auto &[id, native] : _currentById) {
            current.push_back(id);
        }
        for (const uint64_t id : current) {
            QueueRemoval(world, id);
        }
        ReleaseLocal(world);
        ResetLocal(world);
        for (const auto &[id, stream] : _remote) {
            world.NativeObjects().InvalidateNetwork(id);
        }
        _remote.clear();
        _appliedModels.clear();
        _failedModels.clear();
    }
} // namespace Mafia1Online::Features::Player
