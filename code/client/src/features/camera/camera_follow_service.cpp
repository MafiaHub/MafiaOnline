#include "camera_follow_service.h"

#include "features/world/world_service.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/player/player_camera_view.h"
#include "shared/features/player/player_entity.h"

#include <MinHook.h>
#include <core_modules.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Mafia1Online::Features::Camera {
    namespace {
        using PlayerEntity = Shared::Entities::PlayerEntity;
        using CarEntity = Shared::Entities::CarEntity;
        using NativeActor = SDK::Player::NativeActor;
        using NativeCamera = SDK::Player::NativeCamera;
        using CameraTick = void(__thiscall *)(NativeCamera *, uint32_t);
        constexpr uintptr_t kCameraTick = 0x5ed4c0; // reM G_Camera::Tick
        constexpr uint32_t kPedestrianBehindMode = 1; // reM E_G_CameraMode
        constexpr uint32_t kPedestrianMouseMode = 2; // reM E_G_CameraMode
        constexpr float kPi = 3.14159265358979323846f;
        constexpr std::size_t kCameraMouseSampleCount = 30;
        // reM G_Camera.cpp globals. The 30-sample inertia ring belongs only
        // to mouse camera mode and is restored after the spectator tick.
        constexpr uintptr_t kCameraMouseSamples = 0x6bd918;
        constexpr uintptr_t kCameraMouseSampleIndex = 0x6bdb28;
        constexpr uintptr_t kCameraMouseYaw = 0x6bdb38;
        constexpr uintptr_t kCameraMousePitch = 0x6bdb3c;
        constexpr uintptr_t kLs3dfMouseStateOffset = 0x1c56f0; // reM g_mouseState
        struct CameraMouseSample {
            float horizontal;
            float vertical;
            float deltaTime;
        };
        static_assert(sizeof(CameraMouseSample) == 12);
        CameraTick gCameraTickOriginal = nullptr;
        CameraFollowService *gFollowService = nullptr;

        void __fastcall CameraTickHook(NativeCamera *camera, void *, uint32_t milliseconds) {
            if (!gFollowService->OnNativeTick(*camera)) {
                gCameraTickOriginal(camera, milliseconds);
                return;
            }
            auto *samples = reinterpret_cast<CameraMouseSample *>(kCameraMouseSamples);
            auto &sampleIndex = *reinterpret_cast<int *>(kCameraMouseSampleIndex);
            auto &mouseYaw = *reinterpret_cast<float *>(kCameraMouseYaw);
            auto &mousePitch = *reinterpret_cast<float *>(kCameraMousePitch);
            auto *mouseDelta = reinterpret_cast<int32_t *>(SDK::Graphics::Ls3dfBase() + kLs3dfMouseStateOffset);
            std::array<CameraMouseSample, kCameraMouseSampleCount> oldSamples;
            std::memcpy(oldSamples.data(), samples, sizeof(oldSamples));
            const int oldSampleIndex = sampleIndex;
            const float oldYaw = mouseYaw;
            const float oldPitch = mousePitch;
            const int32_t oldDeltaX = mouseDelta[0];
            const int32_t oldDeltaY = mouseDelta[1];
            const auto oldActorDirection = camera->Player()->Direction();
            std::memset(samples, 0, sizeof(oldSamples));
            mouseDelta[0] = 0;
            mouseDelta[1] = 0;
            mouseYaw = gFollowService->FollowYaw();
            mousePitch = gFollowService->FollowPitch();
            gCameraTickOriginal(camera, milliseconds);
            // Mouse camera mode writes its final view heading into m_pPlayer.
            // The observed C_entity must retain its replicated direction.
            camera->Player()->_direction = oldActorDirection;
            mouseYaw = oldYaw;
            mousePitch = oldPitch;
            mouseDelta[0] = oldDeltaX;
            mouseDelta[1] = oldDeltaY;
            sampleIndex = oldSampleIndex;
            std::memcpy(samples, oldSamples.data(), sizeof(oldSamples));
        }

        NativeActor *ResolveActor(World::WorldService &world, uint64_t id, NativeActor::Type type) {
            const auto handle = world.NativeObjects().FindByNetwork(id);
            auto *actor = static_cast<NativeActor *>(world.NativeObjects().Resolve(handle));
            return actor && actor->GetType() == type ? actor : nullptr;
        }

        NativeActor *SeatedCar(World::WorldService &world, PlayerEntity &player) {
            NativeActor *native = nullptr;
            auto *replication = Framework::CoreModules::GetReplication();
            replication->ForEach<CarEntity>([&](CarEntity *car) {
                if (native || car->missionGeneration != player.missionGeneration ||
                    car->terminalState != CarEntity::TerminalState::Active) {
                    return;
                }
                for (uint8_t seat = 0; seat < car->seatCount && seat < CarEntity::kMaxSeats; ++seat) {
                    if (car->occupantIds[seat] == player.GetNetworkID() &&
                        car->occupantGenerations[seat] == player.spawnGeneration) {
                        native = ResolveActor(world, car->GetNetworkID(), NativeActor::Type::Car);
                        return;
                    }
                }
            });
            return native;
        }

        NativeActor *LocalCar(World::WorldService &world, NativeActor *local, PlayerEntity &observer) {
            if (local) {
                const auto *human = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(local));
                if (human->usedActorEnter) {
                    const auto handle = world.NativeObjects().FindByNative(human->usedActorEnter);
                    if (auto *car = static_cast<NativeActor *>(world.NativeObjects().Resolve(handle));
                        car && car->GetType() == NativeActor::Type::Car) {
                        return car;
                    }
                }
            }
            return SeatedCar(world, observer);
        }
    } // namespace

    void CameraFollowService::RegisterRPC() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Player::CameraView>([this](const Shared::Player::CameraView &view, MafiaNet::Packet *) {
            OnView(view);
        });
    }

    bool CameraFollowService::InstallTickHook() {
        gFollowService = this;
        if (MH_CreateHook(reinterpret_cast<void *>(kCameraTick), reinterpret_cast<void *>(&CameraTickHook),
                reinterpret_cast<void **>(&gCameraTickOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(kCameraTick)) != MH_OK) {
            UninstallTickHook();
            return false;
        }
        return true;
    }

    void CameraFollowService::UninstallTickHook() {
        MH_DisableHook(reinterpret_cast<void *>(kCameraTick));
        MH_RemoveHook(reinterpret_cast<void *>(kCameraTick));
        gCameraTickOriginal = nullptr;
        gFollowService = nullptr;
    }

    bool CameraFollowService::OnNativeTick(NativeCamera &camera) {
        // C_game::Update writes this from its *controlled* C_player directly
        // before G_Camera::Tick. Retain it for clients following us, then put
        // the watched player's pitch into the camera used by this tick.
        _sourcePitch = camera.Pitch();
        if (!_followingRemote || camera.Car() || !camera.Player() || camera.Mode() != kPedestrianMouseMode) {
            _lastTick = {};
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!_hasView || now - _lastViewReceived > std::chrono::seconds(1)) {
            const auto &direction = camera.Player()->Direction();
            _smoothedYaw = std::atan2(direction.x, direction.z);
            _smoothedPitch = std::clamp(_sourcePitch, 0.0f, 1.0f);
            _lastTick = now;
            return true;
        }
        const float delta = _lastTick == std::chrono::steady_clock::time_point {} ? 0.05f :
            std::clamp(std::chrono::duration<float>(now - _lastTick).count(), 0.0f, 0.1f);
        // One short local filter removes the 20 Hz view steps. The shortest
        // angular difference keeps camera yaw smooth across the +/-pi wrap.
        const float blend = 1.0f - std::exp(-delta / 0.035f);
        _smoothedPitch += (_targetPitch - _smoothedPitch) * blend;
        const float yawDelta = std::atan2(std::sin(_targetYaw - _smoothedYaw), std::cos(_targetYaw - _smoothedYaw));
        _smoothedYaw += yawDelta * blend;
        camera.SetPitch(_smoothedPitch);
        _lastTick = now;
        return true;
    }

    void CameraFollowService::OnView(const Shared::Player::CameraView &view) {
        if (!_followingRemote || view.networkId != _targetId || view.spawnGeneration != _targetSpawnGeneration ||
            !std::isfinite(view.pitch) || view.pitch < 0.0f || view.pitch > 1.0f ||
            !std::isfinite(view.yaw) || std::abs(view.yaw) > kPi ||
            (_lastViewSequence != 0 && static_cast<int32_t>(view.sequence - _lastViewSequence) <= 0)) {
            return;
        }
        _lastViewSequence = view.sequence;
        _targetPitch = view.pitch;
        _targetYaw = view.yaw;
        _lastViewReceived = std::chrono::steady_clock::now();
        if (!_hasView) {
            _smoothedPitch = view.pitch;
            _smoothedYaw = view.yaw;
            _hasView = true;
        }
    }

    void CameraFollowService::Reset() {
        _followingRemote = false;
        _targetId = 0;
        _targetSpawnGeneration = 0;
        _restorePedestrianMode = 0;
        _hasView = false;
        _lastViewSequence = 0;
        _lastTick = {};
        _lastSent = {};
    }

    void CameraFollowService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || !world.IsReady()) {
            Reset();
            return;
        }

        PlayerEntity *observer = nullptr;
        const uint64_t myGuid = static_cast<uint64_t>(replication->GetMyGUID());
        replication->ForEach<PlayerEntity>([&](PlayerEntity *player) {
            if (player->controllerGuid == myGuid && player->missionGeneration == world.LoadedMissionGeneration()) {
                observer = player;
            }
        });
        if (!observer) {
            return;
        }

        auto *game = SDK::Core::Mission::Get()->Game();
        auto &camera = game->Camera();
        auto *local = observer->spawned ? ResolveActor(world, observer->GetNetworkID(), NativeActor::Type::Player) : nullptr;
        const auto now = std::chrono::steady_clock::now();
        if (local && observer->alive && (_lastSent == std::chrono::steady_clock::time_point {} ||
            now - _lastSent >= std::chrono::milliseconds(50))) {
            float pitch = _sourcePitch;
            const auto &bodyDirection = local->Direction();
            float yaw = std::atan2(bodyDirection.x, bodyDirection.z);
            if (!_followingRemote) {
                if (auto *frame = SDK::Core::Mission::Get()->GetScene()->ActiveCamera()) {
                    const auto direction = frame->WorldDirection();
                    if (std::isfinite(direction.y)) {
                        pitch = std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / kPi;
                    }
                    if (std::isfinite(direction.x) && std::isfinite(direction.z) &&
                        direction.x * direction.x + direction.z * direction.z > 0.0001f) {
                        yaw = std::atan2(direction.x, direction.z);
                    }
                }
            }
            Shared::Player::CameraView view;
            view.networkId = observer->GetNetworkID();
            view.missionGeneration = observer->missionGeneration;
            view.spawnGeneration = observer->spawnGeneration;
            view.sequence = ++_sourceSequence;
            view.pitch = std::clamp(pitch, 0.0f, 1.0f);
            view.yaw = yaw;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(view, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
            _lastSent = now;
        }
        PlayerEntity *target = observer->cameraTargetId && observer->cameraTargetId != observer->GetNetworkID()
                                   ? replication->GetEntity<PlayerEntity>(observer->cameraTargetId)
                                   : nullptr;
        NativeActor *remote = nullptr;
        if (target && target->spawned && target->alive && target->missionGeneration == observer->missionGeneration) {
            remote = ResolveActor(world, target->GetNetworkID(), NativeActor::Type::Entity);
        }

        if (remote) {
            NativeActor *car = SeatedCar(world, *target);
            if (!_followingRemote) {
                const uint32_t currentMode = camera.Mode();
                _restorePedestrianMode = currentMode >= 1 && currentMode <= 3 ? currentMode : kPedestrianBehindMode;
            }
            if (_targetId != target->GetNetworkID() || _targetSpawnGeneration != target->spawnGeneration) {
                _targetId = target->GetNetworkID();
                _targetSpawnGeneration = target->spawnGeneration;
                _lastViewSequence = 0;
                _hasView = false;
            }
            const bool playerChanged = camera.Player() != remote;
            const bool carChanged = camera.Car() != car;
            if (playerChanged && camera.Car()) {
                camera.SetCar(nullptr);
            }
            if (playerChanged) {
                camera.SetPlayer(remote);
            }
            if (carChanged || (playerChanged && !car) || (!observer->alive && camera.Mode() == 6)) {
                camera.SetCar(car);
            }
            // The native mouse profile places the camera from the source
            // view yaw, including free look, and handles world collision.
            // The Tick hook replaces mouse input just for this camera tick
            // and restores its write to the remote actor's direction.
            if (!car && (playerChanged || carChanged || camera.Mode() != kPedestrianMouseMode)) {
                // Retail has no parameter row for mode 2; it carries over
                // the previous row. Select pedestrian behind once first so
                // a prior car camera cannot lend it car spacing values.
                camera.SetMode(kPedestrianBehindMode);
                camera.SetMode(kPedestrianMouseMode);
            }
            _followingRemote = true;
            return;
        }

        if (!_followingRemote && (!local || camera.Player() == local)) {
            if (_restorePedestrianMode && !camera.Car()) {
                camera.SetMode(_restorePedestrianMode);
                _restorePedestrianMode = 0;
            }
            return;
        }
        // The target may have despawned, disconnected or left this mission.
        // Keep the server's target ID so its next life is followed again.
        if (camera.Car()) {
            camera.SetCar(nullptr);
        }
        if (camera.Player() != local) {
            camera.SetPlayer(local);
        }
        // SetPlayer preserves the old mode. Select the local pedestrian or
        // car profile once, including when an old death mode was active.
        camera.SetCar(local ? LocalCar(world, local, *observer) : nullptr);
        if (_restorePedestrianMode && !camera.Car()) {
            camera.SetMode(_restorePedestrianMode);
            _restorePedestrianMode = 0;
        }
        _followingRemote = false;
        _targetId = 0;
        _targetSpawnGeneration = 0;
        _hasView = false;
        _lastViewSequence = 0;
    }
} // namespace Mafia1Online::Features::Camera
