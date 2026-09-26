#include <utils/safe_win32.h>

#include "car_hooks.h"

#include "core/application.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/car/car_hit_report.h"
#include "shared/features/combat/vehicle_impact.h"
#include "shared/features/player/player_entity.h"

#include <MinHook.h>
#include <core_modules.h>
#include <mafia1/sdk/car/native_car.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>
#include <networking/replication/replication_manager.h>
#include <networking/network_peer.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <chrono>
#include <unordered_map>

namespace Mafia1Online::Features::Car {
    namespace {
        using CarDestructor = void(__thiscall *)(void *);
        using CarExplosion  = void(__thiscall *)(SDK::Car::NativeCar *, unsigned int);
        using VehicleDeform = void(__thiscall *)(SDK::Car::NativeVehicle *, const SDK::Player::Vector3 *, const SDK::Player::Vector3 *, float, float, unsigned int, const SDK::Player::Vector3 *);
        using VehicleDamage = void(__thiscall *)(SDK::Car::NativeVehicle *, float);
        using CarHit = bool(__thiscall *)(SDK::Car::NativeCar *, int, const SDK::Player::Vector3 *,
                                         const SDK::Player::Vector3 *, const SDK::Player::Vector3 *, float,
                                         SDK::Player::NativeActor *, unsigned int, SDK::Scene::NativeFrame *);
        using CollisionFilterBody = const void *(__fastcall *)(SDK::Car::NativeCar *, const void *, const SDK::Player::Vector3 &, const SDK::Player::Vector3 &);
        CarDestructor gCarDestructorOriginal = nullptr;
        CarExplosion gCarExplosionOriginal   = nullptr;
        VehicleDeform gVehicleDeformOriginal = nullptr;
        VehicleDamage gVehicleDamageOriginal = nullptr;
        CarHit gCarHitOriginal = nullptr;
        CollisionFilterBody gCollisionFilterBodyOriginal = nullptr;
        struct ImpactKey {
            uint64_t carId;
            uint64_t targetId;
            bool operator==(const ImpactKey &) const = default;
        };
        struct ImpactKeyHash {
            size_t operator()(const ImpactKey &key) const {
                return std::hash<uint64_t> {}(key.carId) ^ (std::hash<uint64_t> {}(key.targetId) << 1);
            }
        };
        std::unordered_map<ImpactKey, std::chrono::steady_clock::time_point, ImpactKeyHash> gLastPedestrianImpact;
        using CarAI = void(__thiscall *)(SDK::Car::NativeCar *, unsigned int);
        CarAI gCarAIOriginal = nullptr;

        void NativeCarStep(SDK::Car::NativeCar *car, unsigned int frameMs) {
            gCarAIOriginal(car, frameMs);
        }

        void __fastcall CarAIHook(SDK::Car::NativeCar *car, void *, unsigned int frameMs) {
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            if (application->Cars().DriveObserver(car, frameMs, &NativeCarStep)) {
                return;
            }
            gCarAIOriginal(car, frameMs);
            application->Cars().OnSimulated(car, frameMs);
        }

        bool IsCarBodyFrame(const SDK::Car::NativeCar &car, SDK::Scene::NativeFrame *frame) {
            if (!frame) {
                return true;
            }
            // This follows the same stop conditions as C_car::Hit: a frame
            // with an actor owner or the first model frame. An occupant's
            // frame must reach the human Hit hook, not car damage forwarding.
            for (unsigned depth = 0; frame && depth < 64; ++depth, frame = frame->Parent()) {
                if (auto *actor = frame->ActorOwner()) {
                    return actor == &car;
                }
                if (frame->FrameType() == 9) {
                    return frame == car.Frame();
                }
            }
            return false;
        }

        void ReportLocalCarHit(Core::Application &application, SDK::Car::NativeCar &car, uint64_t carId,
                               const SDK::Player::Vector3 &direction, const SDK::Player::Vector3 &position,
                               const SDK::Player::Vector3 &normal, SDK::Player::NativeActor *attacker) {
            if (!application.World().IsReady() || attacker != SDK::Player::CurrentPlayer() || !car.Frame()) {
                return;
            }
            const auto shooter = application.World().NativeObjects().FindByNative(attacker);
            const auto *shooterState = application.Combat().GetLocalState(application.World());
            if (!shooterState || shooter.networkId == 0 || shooter.networkId != shooterState->networkId ||
                !shooterState->spawned || !shooterState->alive) {
                return;
            }
            auto *frame = car.Frame();
            const auto center = frame->WorldPosition();
            const auto basis = frame->GetWorldBasis();
            const glm::vec3 right(basis.right.x, basis.right.y, basis.right.z);
            const glm::vec3 up(basis.up.x, basis.up.y, basis.up.z);
            const glm::vec3 forward(basis.forward.x, basis.forward.y, basis.forward.z);
            const glm::vec3 hit(position.x - center.x, position.y - center.y, position.z - center.z);
            const glm::vec3 shot(direction.x, direction.y, direction.z);
            if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
                !std::isfinite(hit.x) || !std::isfinite(hit.y) || !std::isfinite(hit.z) ||
                !std::isfinite(shot.x) || !std::isfinite(shot.y) || !std::isfinite(shot.z) ||
                !std::isfinite(right.x) || !std::isfinite(right.y) || !std::isfinite(right.z) ||
                !std::isfinite(up.x) || !std::isfinite(up.y) || !std::isfinite(up.z) ||
                !std::isfinite(forward.x) || !std::isfinite(forward.y) || !std::isfinite(forward.z) ||
                glm::length(right) < 0.8f || glm::length(up) < 0.8f || glm::length(forward) < 0.8f) {
                return;
            }
            const glm::vec3 localHit(glm::dot(hit, right), glm::dot(hit, up), glm::dot(hit, forward));
            const glm::vec3 localDirection(glm::dot(shot, right), glm::dot(shot, up), glm::dot(shot, forward));
            if (glm::length(localHit) > 15.0f || glm::length(localDirection) < 0.8f || glm::length(localDirection) > 1.2f) {
                return;
            }
            const auto pellet = application.Combat().MatchLocalPellet(direction.x, direction.y, direction.z,
                                                                      position.x, position.y, position.z);
            if (!pellet) {
                return;
            }
            Shared::Car::HitReport report;
            report.carId = carId;
            report.missionGeneration = application.World().LoadedMissionGeneration();
            report.shooterId = shooter.networkId;
            report.shooterSpawnGeneration = shooterState->spawnGeneration;
            report.shotSequence = pellet->shotSequence;
            report.pelletIndex = pellet->pelletIndex;
            report.directionX = direction.x;
            report.directionY = direction.y;
            report.directionZ = direction.z;
            report.hitX = position.x;
            report.hitY = position.y;
            report.hitZ = position.z;
            report.normalX = normal.x;
            report.normalY = normal.y;
            report.normalZ = normal.z;
            report.localHitX = localHit.x;
            report.localHitY = localHit.y;
            report.localHitZ = localHit.z;
            report.localDirectionX = localDirection.x;
            report.localDirectionY = localDirection.y;
            report.localDirectionZ = localDirection.z;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
        }

        bool IsRemoteNetworkCar(Core::Application &application, SDK::Car::NativeVehicle *vehicle) {
            auto *car = vehicle->Owner();
            if (!car) {
                return false;
            }
            const auto handle = application.World().NativeObjects().FindByNative(car);
            if (handle.networkId == 0) {
                return false;
            }
            auto *replication = Framework::CoreModules::GetReplication();
            const auto *state = replication->GetEntity<Shared::Entities::CarEntity>(handle.networkId);
            return !state || state->missionGeneration != application.World().LoadedMissionGeneration() ||
                   state->simulationControllerGuid != static_cast<uint64_t>(replication->GetMyGUID());
        }

        void __fastcall CarDestructorHook(void *car, void *) {
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            auto *frame = application->Cars().OnNativeDestroyed(application->World(), car);
            auto *scene = frame ? SDK::Core::Mission::Get()->GetScene() : nullptr;
            application->World().NativeObjects().InvalidateNative(car);
            gCarDestructorOriginal(car);
            if (frame) {
                scene->DeleteFrame(frame);
            }
        }

        void __fastcall CarExplosionHook(SDK::Car::NativeCar *car, void *, unsigned int flags) {
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            if (application->Cars().OnNativeExplosion(application->World(), car)) {
                gCarExplosionOriginal(car, flags);
            }
        }

        void __fastcall VehicleDeformHook(SDK::Car::NativeVehicle *vehicle, void *, const SDK::Player::Vector3 *position,
                                          const SDK::Player::Vector3 *direction, float radius, float force,
                                          unsigned int flags, const SDK::Player::Vector3 *callbackDirection) {
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            if (IsRemoteNetworkCar(*application, vehicle)) {
                return;
            }
            auto *car = vehicle->Owner();
            gVehicleDeformOriginal(vehicle, position, direction, radius, force, flags, callbackDirection);
            if (car) {
                application->Cars().OnNativeDeformed(car);
            }
        }

        void __fastcall VehicleDamageHook(SDK::Car::NativeVehicle *vehicle, void *, float damage) {
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            if (!IsRemoteNetworkCar(*application, vehicle)) {
                gVehicleDamageOriginal(vehicle, damage);
            }
        }

        bool __fastcall CarHitHook(SDK::Car::NativeCar *car, void *, int hitType,
                                   const SDK::Player::Vector3 *direction, const SDK::Player::Vector3 *position,
                                   const SDK::Player::Vector3 *normal, float damage,
                                   SDK::Player::NativeActor *attacker, unsigned int flags,
                                   SDK::Scene::NativeFrame *frame) {
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            const auto handle = application->World().NativeObjects().FindByNative(car);
            if (handle.networkId == 0) {
                return gCarHitOriginal(car, hitType, direction, position, normal, damage, attacker, flags, frame);
            }
            const auto *state = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(handle.networkId);
            if (hitType != 0) {
                if (state && state->missionGeneration == application->World().LoadedMissionGeneration() &&
                    state->simulationControllerGuid == static_cast<uint64_t>(Framework::CoreModules::GetReplication()->GetMyGUID())) {
                    return gCarHitOriginal(car, hitType, direction, position, normal, damage, attacker, flags, frame);
                }
                return true;
            }
            if (frame && !IsCarBodyFrame(*car, frame)) {
                // Retail forwards this hit to a child actor or handles a
                // separate model without changing this car's damage state.
                return gCarHitOriginal(car, hitType, direction, position, normal, damage, attacker, flags, frame);
            }
            if (state && state->missionGeneration == application->World().LoadedMissionGeneration() &&
                direction && position && normal) {
                ReportLocalCarHit(*application, *car, handle.networkId, *direction, *position, *normal, attacker);
            }
            return true;
        }

        const void *__fastcall CollisionFilterBodyHook(SDK::Car::NativeCar *car, const void *collision,
                                                        const SDK::Player::Vector3 &position, const SDK::Player::Vector3 &normal) {
            // reM g_collision_header packs material ID in the high byte.
            // Let the retail body callback create its water particle/sound
            // before reporting the same contact to the server.
            if (collision) {
                const auto packed = *static_cast<const uint32_t *>(collision);
                const auto material = static_cast<uint8_t>(packed >> 24);
                if (material == 31 || material == 40) {
                    const void *result = gCollisionFilterBodyOriginal(car, collision, position, normal);
                    auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
                    application->Cars().OnNativeWorldCollision(application->World(), car, material);
                    return result;
                }
            }
            // reM tDynamicCollObject marks dynamic primitives in the first
            // byte and stores its actor at +0x44. Only remote network humans
            // are ghosted: their replicated pose cannot yield to a car's
            // native impulse, so treating one as a rigid body stops the car.
            const auto *dynamic = static_cast<const SDK::Car::NativeDynamicCollision *>(collision);
            if ((dynamic->packed & SDK::Car::NativeDynamicCollision::kDynamicTypeMask) == 0 || !dynamic->ownerActor ||
                dynamic->ownerActor->GetType() != SDK::Player::NativeActor::Type::Entity) {
                return gCollisionFilterBodyOriginal(car, collision, position, normal);
            }
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            auto &world = application->World();
            if (!world.IsReady()) {
                return gCollisionFilterBodyOriginal(car, collision, position, normal);
            }
            const auto carHandle = world.NativeObjects().FindByNative(car);
            const auto targetHandle = world.NativeObjects().FindByNative(dynamic->ownerActor);
            if (!carHandle.networkId || !targetHandle.networkId) {
                return gCollisionFilterBodyOriginal(car, collision, position, normal);
            }
            auto *replication = Framework::CoreModules::GetReplication();
            const auto *carState = replication->GetEntity<Shared::Entities::CarEntity>(carHandle.networkId);
            const auto *targetState = replication->GetEntity<Shared::Entities::PlayerEntity>(targetHandle.networkId);
            if (!carState || !targetState || carState->missionGeneration != world.LoadedMissionGeneration() ||
                targetState->missionGeneration != world.LoadedMissionGeneration()) {
                return gCollisionFilterBodyOriginal(car, collision, position, normal);
            }
            if (carState->simulationControllerGuid == static_cast<uint64_t>(replication->GetMyGUID()) && targetState->alive) {
                const auto velocity = car->Vehicle().LinearVelocity();
                const float speed = std::hypot(velocity.x, velocity.y, velocity.z);
                if (!std::isfinite(speed) || speed < 3.0f) {
                    // Retail uses this contact for a low speed pedestrian
                    // side jump. Keep the original filter in that case.
                    return gCollisionFilterBodyOriginal(car, collision, position, normal);
                }
                const auto angular = car->Vehicle().AngularVelocity();
                const auto center = car->Vehicle().WorldCenter();
                const glm::vec3 offset(position.x - center.x, position.y - center.y, position.z - center.z);
                const glm::vec3 spin(angular.x, angular.y, angular.z);
                const glm::vec3 linear(velocity.x, velocity.y, velocity.z);
                const glm::vec3 relative = linear - glm::cross(spin, offset);
                const glm::vec3 contactNormal(normal.x, normal.y, normal.z);
                const float approach = glm::dot(relative, contactNormal);
                // C_Vehicle::BodyCollision only handles an approaching
                // contact after its callback filter has returned.
                if (!std::isfinite(approach) || approach >= 0.0f) {
                    return nullptr;
                }
                const ImpactKey key {carHandle.networkId, targetHandle.networkId};
                const auto now = std::chrono::steady_clock::now();
                auto previous = gLastPedestrianImpact.find(key);
                if (previous == gLastPedestrianImpact.end() || now - previous->second >= std::chrono::milliseconds(500)) {
                    if (gLastPedestrianImpact.size() > 1024) {
                        gLastPedestrianImpact.clear();
                    }
                    gLastPedestrianImpact[key] = now;
                    Shared::Combat::VehicleImpact report;
                    report.carId = carHandle.networkId;
                    report.targetId = targetHandle.networkId;
                    report.missionGeneration = world.LoadedMissionGeneration();
                    report.targetSpawnGeneration = targetState->spawnGeneration;
                    report.contactX = position.x;
                    report.contactY = position.y;
                    report.contactZ = position.z;
                    Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
                }
            }
            return nullptr;
        }
    } // namespace

    bool InstallCarHooks() {
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Car::kDestructor), reinterpret_cast<void *>(&CarDestructorHook), reinterpret_cast<void **>(&gCarDestructorOriginal)) != MH_OK) {
            return false;
        }
        if (MH_EnableHook(reinterpret_cast<void *>(SDK::Car::kDestructor)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
            return false;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Car::kExplosion), reinterpret_cast<void *>(&CarExplosionHook), reinterpret_cast<void **>(&gCarExplosionOriginal)) != MH_OK
            || MH_EnableHook(reinterpret_cast<void *>(SDK::Car::kExplosion)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kExplosion));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
            return false;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Car::kDeform), reinterpret_cast<void *>(&VehicleDeformHook), reinterpret_cast<void **>(&gVehicleDeformOriginal)) != MH_OK
            || MH_EnableHook(reinterpret_cast<void *>(SDK::Car::kDeform)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDeform));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kExplosion));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kExplosion));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
            return false;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Car::kDamageVehicle), reinterpret_cast<void *>(&VehicleDamageHook), reinterpret_cast<void **>(&gVehicleDamageOriginal)) != MH_OK
            || MH_EnableHook(reinterpret_cast<void *>(SDK::Car::kDamageVehicle)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDamageVehicle));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDeform));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDeform));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kExplosion));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kExplosion));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
            return false;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Car::kCarHit), reinterpret_cast<void *>(&CarHitHook), reinterpret_cast<void **>(&gCarHitOriginal)) != MH_OK
            || MH_EnableHook(reinterpret_cast<void *>(SDK::Car::kCarHit)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kCarHit));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDamageVehicle));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDamageVehicle));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDeform));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDeform));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kExplosion));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kExplosion));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
            return false;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Car::kCarAI), reinterpret_cast<void *>(&CarAIHook), reinterpret_cast<void **>(&gCarAIOriginal)) != MH_OK
            || MH_EnableHook(reinterpret_cast<void *>(SDK::Car::kCarAI)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kCarAI));
            UninstallCarHooks();
            return false;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Car::kCollisionFilterBody), reinterpret_cast<void *>(&CollisionFilterBodyHook), reinterpret_cast<void **>(&gCollisionFilterBodyOriginal)) != MH_OK
            || MH_EnableHook(reinterpret_cast<void *>(SDK::Car::kCollisionFilterBody)) != MH_OK) {
            UninstallCarHooks();
            return false;
        }
        return true;
    }

    void UninstallCarHooks() {
        MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kCollisionFilterBody));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kCollisionFilterBody));
        MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kCarAI));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kCarAI));
        MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kCarHit));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kCarHit));
        MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDamageVehicle));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDamageVehicle));
        MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDeform));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDeform));
        MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kExplosion));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kExplosion));
        MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDestructor));
        gCollisionFilterBodyOriginal = nullptr;
        gLastPedestrianImpact.clear();
    }

    void ApplyAuthoritativeExplosion(SDK::Car::NativeCar *car) {
        gCarExplosionOriginal(car, 0);
    }

    bool ApplyAuthoritativeCarHit(SDK::Car::NativeCar *car, const SDK::Player::Vector3 &direction,
                                  const SDK::Player::Vector3 &position, const SDK::Player::Vector3 &normal, float damage,
                                  SDK::Scene::NativeFrame *frame) {
        return gCarHitOriginal(car, 0, &direction, &position, &normal, damage, nullptr, 0, frame);
    }
} // namespace Mafia1Online::Features::Car
