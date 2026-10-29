#include <utils/safe_win32.h>

#include "car_hooks.h"

#include "core/application.h"
#include "features/simulation/simulation_config.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/car/car_hit_report.h"
#include "shared/features/combat/melee_rules.h"
#include "shared/features/combat/vehicle_impact.h"
#include "shared/features/npc/npc_entity.h"
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
#include <optional>
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
        using CallCollision = void(__fastcall *)(SDK::Car::NativeCar *, const void *, const SDK::Player::Vector3 &, const SDK::Player::Vector3 &,
                                                 const SDK::Player::Vector3 &, float);
        CarDestructor gCarDestructorOriginal = nullptr;
        CarExplosion gCarExplosionOriginal   = nullptr;
        VehicleDeform gVehicleDeformOriginal = nullptr;
        VehicleDamage gVehicleDamageOriginal = nullptr;
        CarHit gCarHitOriginal = nullptr;
        CollisionFilterBody gCollisionFilterBodyOriginal = nullptr;
        CallCollision gCallCollisionOriginal = nullptr;
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
        struct LastContact {
            std::chrono::steady_clock::time_point at;
            Shared::Combat::VehicleContact kind;
        };
        std::unordered_map<ImpactKey, LastContact, ImpactKeyHash> gLastPedestrianImpact;
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

        struct CarLocalHit {
            glm::vec3 hit;
            glm::vec3 direction;
        };

        // The impact in the car's own frame, which the controller replays on
        // its copy; nothing for a broken basis or a shot that is not a unit
        // vector near the car.
        std::optional<CarLocalHit> LocalCarHit(SDK::Car::NativeCar &car, const SDK::Player::Vector3 &direction,
                                               const SDK::Player::Vector3 &position) {
            auto *frame = car.Frame();
            if (!frame) {
                return std::nullopt;
            }
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
                return std::nullopt;
            }
            const glm::vec3 localHit(glm::dot(hit, right), glm::dot(hit, up), glm::dot(hit, forward));
            const glm::vec3 localDirection(glm::dot(shot, right), glm::dot(shot, up), glm::dot(shot, forward));
            if (glm::length(localHit) > 15.0f || glm::length(localDirection) < 0.8f || glm::length(localDirection) > 1.2f) {
                return std::nullopt;
            }
            return CarLocalHit {localHit, localDirection};
        }

        void ReportLocalCarHit(Core::Application &application, SDK::Car::NativeCar &car, uint64_t carId,
                               const SDK::Player::Vector3 &direction, const SDK::Player::Vector3 &position,
                               const SDK::Player::Vector3 &normal, SDK::Player::NativeActor *attacker) {
            if (!application.World().IsReady() || attacker != SDK::Player::CurrentPlayer()) {
                return;
            }
            const auto shooter = application.World().NativeObjects().FindByNative(attacker);
            const auto *shooterState = application.Combat().GetLocalState(application.World());
            if (!shooterState || shooter.networkId == 0 || shooter.networkId != shooterState->networkId ||
                !shooterState->spawned || !shooterState->alive) {
                return;
            }
            const auto local = LocalCarHit(car, direction, position);
            if (!local) {
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
            report.localHitX = local->hit.x;
            report.localHitY = local->hit.y;
            report.localHitZ = local->hit.z;
            report.localDirectionX = local->direction.x;
            report.localDirectionY = local->direction.y;
            report.localDirectionZ = local->direction.z;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
        }

        // The local player's melee strike on a network car, against the
        // swing the server accepted (one car report per swing).
        void ReportLocalMeleeCarHit(Core::Application &application, SDK::Car::NativeCar &car, uint64_t carId,
                                    const SDK::Player::Vector3 &direction, const SDK::Player::Vector3 &position,
                                    const SDK::Player::Vector3 &normal, SDK::Player::NativeActor *attacker) {
            static Shared::Combat::MeleeReportGate gate;
            if (!application.World().IsReady()) {
                return;
            }
            const auto attackerHandle = application.World().NativeObjects().FindByNative(attacker);
            const auto *attackerState = application.Combat().GetLocalState(application.World());
            const auto swing = application.Combat().OpenLocalMelee();
            if (!attackerState || attackerHandle.networkId == 0 || attackerHandle.networkId != attackerState->networkId || !attackerState->spawned ||
                !attackerState->alive || !swing) {
                return;
            }
            // Retail's swing direction is not a unit vector; the server checks
            // the hit point, so a unit one does.
            const glm::vec3 swingDirection(direction.x, direction.y, direction.z);
            const float length = glm::length(swingDirection);
            if (!(length > 0.0001f)) {
                return;
            }
            const SDK::Player::Vector3 unit {swingDirection.x / length, swingDirection.y / length, swingDirection.z / length};
            const auto local = LocalCarHit(car, unit, position);
            if (!local || !gate.Admit(*swing)) {
                return;
            }
            Shared::Car::HitReport report;
            report.kind = Shared::Car::HitKind::Melee;
            report.carId = carId;
            report.missionGeneration = application.World().LoadedMissionGeneration();
            report.shooterId = attackerHandle.networkId;
            report.shooterSpawnGeneration = attackerState->spawnGeneration;
            report.shotSequence = *swing;
            report.directionX = unit.x;
            report.directionY = unit.y;
            report.directionZ = unit.z;
            report.hitX = position.x;
            report.hitY = position.y;
            report.hitZ = position.z;
            report.normalX = normal.x;
            report.normalY = normal.y;
            report.normalZ = normal.z;
            report.localHitX = local->hit.x;
            report.localHitY = local->hit.y;
            report.localHitZ = local->hit.z;
            report.localDirectionX = local->direction.x;
            report.localDirectionY = local->direction.y;
            report.localDirectionZ = local->direction.z;
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
            // A host traffic car's network car moves on or goes; a player's
            // crew models leave before the frame they hang from.
            if (Simulation::IsHost()) {
                application->Traffic().OnNativeCarDestroyed(car);
            }
            else {
                application->TrafficCrew().OnNativeCarDestroyed(car);
            }
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
            // A host traffic car not yet a network car explodes once it is,
            // through the server, so players see it: a burning car asks again
            // each tick.
            if (Simulation::IsHost() && application->Traffic().AwaitsBinding(car)) {
                return;
            }
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
                const bool current = state && state->missionGeneration == application->World().LoadedMissionGeneration();
                const bool controller = current && state->simulationControllerGuid == static_cast<uint64_t>(Framework::CoreModules::GetReplication()->GetMyGUID());
                if (hitType == Shared::Car::NativeHitType(Shared::Car::HitKind::Melee) && current) {
                    if (attacker && attacker == SDK::Player::CurrentPlayer()) {
                        // The server sends it on to the controller and has
                        // everyone else hear it.
                        if (direction && position && normal) {
                            ReportLocalMeleeCarHit(*application, *car, handle.networkId, *direction, *position, *normal, attacker);
                        }
                    }
                    else if (attacker && Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::PlayerEntity>(
                                             application->World().NativeObjects().FindByNative(attacker).networkId)) {
                        // A remote player's replayed swing: that player
                        // reports its own strike.
                        return true;
                    }
                }
                if (controller) {
                    return gCarHitOriginal(car, hitType, direction, position, normal, damage, attacker, flags, frame);
                }
                return true;
            }
            if (frame && !Simulation::IsHost() && application->TrafficCrew().IsCrewFrame(car, frame)) {
                // A traffic crew model is the host's to kill: its re-ray of
                // the reported impact hits the host's own crew model, and
                // retail's DostalJsemRanuKachlou does the rest.
                if (state && state->missionGeneration == application->World().LoadedMissionGeneration() && direction && position && normal) {
                    ReportLocalCarHit(*application, *car, handle.networkId, *direction, *position, *normal, attacker);
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
                if (!Simulation::IsHost()) {
                    ReportLocalCarHit(*application, *car, handle.networkId, *direction, *position, *normal, attacker);
                }
                else if (attacker && application->Npcs().Owns(attacker)) {
                    // The host has no player of its own; its NPCs' bullets
                    // reach a player's car only through the server.
                    if (const auto local = LocalCarHit(*car, *direction, *position)) {
                        application->Npcs().OnHitCar(handle.networkId, *direction, *position, *normal, damage, attacker,
                                                     {local->hit.x, local->hit.y, local->hit.z},
                                                     {local->direction.x, local->direction.y, local->direction.z});
                    }
                }
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
            if ((dynamic->packed & SDK::Car::NativeDynamicCollision::kDynamicTypeMask) != 0 && dynamic->ownerActor &&
                dynamic->ownerActor->GetType() == SDK::Player::NativeActor::Type::Barrier) {
                // A road block's spike strip: Collision_Filter_Body flattens a
                // tyre it crosses (C_irenka::ZapichameSi, VEHICLE_WHEEL_SPARE).
                // Only a car's simulation controller rolls over it; the flat
                // reaches every other copy with the car's damage state.
                auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
                if (IsRemoteNetworkCar(*application, &car->Vehicle())) {
                    return nullptr;
                }
                return gCollisionFilterBodyOriginal(car, collision, position, normal);
            }
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
            // A simulation host NPC is a network human too.
            const auto *player = replication->GetEntity<Shared::Entities::PlayerEntity>(targetHandle.networkId);
            const auto *npc = player ? nullptr : replication->GetEntity<Shared::Entities::NpcEntity>(targetHandle.networkId);
            const uint64_t targetMission = player ? player->missionGeneration : npc ? npc->missionGeneration : 0;
            const bool targetAlive = player ? player->alive : npc && npc->alive;
            const uint64_t targetSpawn = player ? player->spawnGeneration : Shared::Entities::NpcEntity::kSpawnGeneration;
            if (!carState || (!player && !npc) || carState->missionGeneration != world.LoadedMissionGeneration() ||
                targetMission != world.LoadedMissionGeneration()) {
                return gCollisionFilterBodyOriginal(car, collision, position, normal);
            }
            const bool controlled = carState->simulationControllerGuid == static_cast<uint64_t>(replication->GetMyGUID());
            const auto velocity = car->Vehicle().LinearVelocity();
            const float speed = std::hypot(velocity.x, velocity.y, velocity.z);
            // C_human::Collision 0x5784a0 hits only from 3 m/s (damage
            // (|v| - 2.5) * 4.2); slower it pushes the human out and starts
            // a side jump. Below 3 the original filter keeps the body rigid,
            // as before; the dodge itself is the target's owner's to play.
            const bool slow = !std::isfinite(speed) || speed < Shared::Combat::kVehicleImpactMinSpeed;
            if (controlled && targetAlive) {
                const auto angular = car->Vehicle().AngularVelocity();
                const auto center = car->Vehicle().WorldCenter();
                const glm::vec3 offset(position.x - center.x, position.y - center.y, position.z - center.z);
                const glm::vec3 spin(angular.x, angular.y, angular.z);
                const glm::vec3 linear(velocity.x, velocity.y, velocity.z);
                const glm::vec3 relative = linear - glm::cross(spin, offset);
                const glm::vec3 contactNormal(normal.x, normal.y, normal.z);
                // C_Vehicle::BodyCollision only handles an approaching
                // contact after its callback filter has returned.
                const float approach = glm::dot(relative, contactNormal);
                Shared::Combat::VehicleContact kind;
                if (Shared::Combat::ClassifyVehicleContact(speed, approach, kind)) {
                    const ImpactKey key {carHandle.networkId, targetHandle.networkId};
                    const auto now = std::chrono::steady_clock::now();
                    const auto previous = gLastPedestrianImpact.find(key);
                    if (previous == gLastPedestrianImpact.end() ||
                        Shared::Combat::VehicleContactDue(now - previous->second.at, previous->second.kind, kind)) {
                        if (gLastPedestrianImpact.size() > 1024) {
                            gLastPedestrianImpact.clear();
                        }
                        gLastPedestrianImpact[key] = {now, kind};
                        Shared::Combat::VehicleImpact report;
                        report.carId = carHandle.networkId;
                        report.targetId = targetHandle.networkId;
                        report.missionGeneration = world.LoadedMissionGeneration();
                        report.targetSpawnGeneration = targetSpawn;
                        report.contactX = position.x;
                        report.contactY = position.y;
                        report.contactZ = position.z;
                        report.kind = kind;
                        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
                        if (kind == Shared::Combat::VehicleContact::Impact) {
                            // Retail C_human::Collision hits the human in this
                            // same contact; the server's answer is a round
                            // trip away.
                            application->Death().PredictVehicleImpact(carHandle.networkId, targetHandle.networkId, velocity);
                        }
                    }
                }
                if (slow) {
                    return gCollisionFilterBodyOriginal(car, collision, position, normal);
                }
            }
            return nullptr;
        }

        void __fastcall CallCollisionHook(SDK::Car::NativeCar *car, const void *collision, const SDK::Player::Vector3 &position,
                                          const SDK::Player::Vector3 &impulse, const SDK::Player::Vector3 &normal, float mass) {
            // Retail reads the owner of a dynamic collision only (its sweep
            // reports an actor's hit through no other); the ram reaction
            // comes before the hit car's own Collision, as in retail.
            const auto *dynamic = static_cast<const SDK::Car::NativeDynamicCollision *>(collision);
            if (Simulation::IsHost() && (dynamic->packed & SDK::Car::NativeDynamicCollision::kDynamicTypeMask) != 0 && dynamic->ownerActor) {
                auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
                application->Traffic().OnCarCollision(car, dynamic->ownerActor, normal, true);
            }
            gCallCollisionOriginal(car, collision, position, impulse, normal, mass);
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
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Car::kCallCollision), reinterpret_cast<void *>(&CallCollisionHook), reinterpret_cast<void **>(&gCallCollisionOriginal)) != MH_OK
            || MH_EnableHook(reinterpret_cast<void *>(SDK::Car::kCallCollision)) != MH_OK) {
            UninstallCarHooks();
            return false;
        }
        return true;
    }

    void UninstallCarHooks() {
        MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kCallCollision));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kCallCollision));
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
                                  SDK::Scene::NativeFrame *frame, SDK::Player::NativeActor *attacker, int hitType) {
        return gCarHitOriginal(car, hitType, &direction, &position, &normal, damage, attacker, 0, frame);
    }
} // namespace Mafia1Online::Features::Car
