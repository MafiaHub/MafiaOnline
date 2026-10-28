#include <utils/safe_win32.h>

#include "npc_replicas.h"

#include "core/application.h"
#include "features/player/player_service.h"
#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"
#include "shared/features/npc/npc_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/player/native_human_factory.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Mafia1Online::Features::Npc {
    namespace {
        bool Finite(const Framework::Utils::TransformSnapshot &pose) {
            return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) && std::isfinite(pose.position.z) && std::isfinite(pose.rotation.w) &&
                std::isfinite(pose.rotation.x) && std::isfinite(pose.rotation.y) && std::isfinite(pose.rotation.z) && glm::length(pose.rotation) >= 0.0001f;
        }
        // Holds the pose stream off a pull-out whose track never ends.
        constexpr auto kPullLimit = std::chrono::seconds(5);

        // A driver constructed here later than this into its throw-out stands
        // where the host's is: replaying the throw would pull it back to the
        // seat while the host's one is already out.
        constexpr uint16_t kThrowReplayAgeMs = 1000;

        // A stolen traffic car's driver, constructed while its throw-out runs.
        // Retail's Corovacka branch of Do_ThrowCocotFromCar seats the new
        // C_entity (Intern_UseCar) and throws it out (intern_ThrowMeFromCar).
        void ReplayThrowOut(World::WorldService &world, void *actor, uint64_t carId) {
            auto *native = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(world.NativeObjects().FindByNetwork(carId)));
            if (!native || native->GetType() != SDK::Player::NativeActor::Type::Car) {
                return;
            }
            auto *car = static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(native));
            auto *human = static_cast<SDK::Seat::NativeHuman *>(actor);
            if (!car->GetOwner(0) && human->PlaceInCar(car, 0)) {
                human->ThrowOutOfSeat();
            }
        }
    } // namespace

    void NpcReplicaService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (Simulation::IsHost() || !replication || !world.IsReady()) {
            Reset(world);
            return;
        }
        std::unordered_set<uint64_t> seen;
        const MafiaNet::Time networkNow = MafiaNet::GetTime();
        replication->ForEach<Shared::Entities::NpcEntity>([&](Shared::Entities::NpcEntity *npc) {
            if (npc->missionGeneration != world.LoadedMissionGeneration() || npc->model.empty()) {
                return;
            }
            const uint64_t id = npc->GetNetworkID();
            Framework::Utils::TransformSnapshot snapshot {npc->position, npc->velocity, npc->rotation};
            if (!Finite(snapshot)) {
                return;
            }
            seen.insert(id);
            snapshot.rotation = glm::normalize(snapshot.rotation);
            auto &replica = _replicas[id];
            const MafiaNet::Time transformTime = npc->GetLastTransformTime() ? npc->GetLastTransformTime() : networkNow;
            std::optional<Framework::Utils::TransformSnapshot> handedOff;
            if (!replica.frame && !replica.failed && replica.lastTransformTime == 0 && npc->sourcePedestrianId) {
                // The walker this NPC was: start where it stands on this
                // screen, a render delay behind, and ease into the host's
                // stream instead of jumping to its newer pose.
                handedOff = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance())->PedestrianReplicas().HandOff(world, npc->sourcePedestrianId);
                if (handedOff) {
                    const auto delay = static_cast<MafiaNet::Time>(replica.snapshots.EffectiveDelayMs());
                    const MafiaNet::Time renderTime = networkNow > delay ? networkNow - delay : 1;
                    replica.snapshots.Push(*handedOff, std::min(renderTime, transformTime > 1 ? transformTime - 1 : 1));
                }
            }
            if (replica.lastTransformTime != transformTime) {
                replica.snapshots.Push(snapshot, transformTime);
                replica.locomotion.emplace_back(transformTime, npc->locomotion);
                while (replica.locomotion.size() > 32) {
                    replica.locomotion.pop_front();
                }
                replica.lastTransformTime = transformTime;
            }
            if (!replica.frame && !replica.failed) {
                const auto &spawn = handedOff ? *handedOff : snapshot;
                const auto direction = glm::mat3_cast(spawn.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
                const std::string name = "Mafia1OnlineNpc_" + std::to_string(id);
                auto *actor = SDK::Player::NativeHumanFactory::CanLoadHumanModel(npc->model)
                    ? SDK::Player::NativeHumanFactory::CreateTemporary(*SDK::Core::Mission::Get(), SDK::Player::NativeActor::Type::Entity, name.c_str(), npc->model.c_str(),
                                                                       {spawn.position.x, spawn.position.y, spawn.position.z}, {direction.x, direction.y, direction.z})
                    : nullptr;
                if (!actor) {
                    replica.failed = true;
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not create NPC replica {} with model '{}'", id, npc->model);
                    return;
                }
                replica.handle = world.NativeObjects().Bind(actor, id);
                replica.frame = actor->Frame();
                _frames[actor] = replica.frame;
                if (npc->kind == Shared::Entities::NpcKind::Driver && npc->thrownFromCarId && npc->alive && npc->throwAgeMs < kThrowReplayAgeMs) {
                    ReplayThrowOut(world, actor, npc->thrownFromCarId);
                }
            }
            auto *actor = world.NativeObjects().Resolve(replica.handle);
            if (actor) {
                ApplyPose(replica, actor, npc->alive);
            }
        });
        std::vector<uint64_t> gone;
        for (const auto &[id, replica] : _replicas) {
            if (!seen.contains(id)) {
                gone.push_back(id);
            }
        }
        // Out of the map first: removal can run the actor destructor now.
        for (const uint64_t id : gone) {
            auto node = _replicas.extract(id);
            Remove(world, id, node.mapped());
        }
    }

    void NpcReplicaService::ApplyPose(Replica &replica, void *native, bool alive) {
        const MafiaNet::Time now = MafiaNet::GetTime();
        const auto delay = static_cast<MafiaNet::Time>(replica.snapshots.EffectiveDelayMs());
        const MafiaNet::Time time = now > delay ? now - delay : 0;
        Framework::Utils::TransformSnapshot pose;
        bool held = false;
        if (!replica.snapshots.Sample(time, pose, &held)) {
            return;
        }
        auto *actor = static_cast<SDK::Player::NativeActor *>(native);
        // A corpse stays where the death animation left it.
        if (!alive) {
            return;
        }
        // Seated, or in a door animation: the car carries it and the seat
        // replay animates it.
        if (const auto *seated = static_cast<const SDK::Seat::NativeHuman *>(native); seated->usedActorEnter || seated->usedActorLeave) {
            return;
        }
        auto *human = static_cast<SDK::Player::NativeHuman *>(native);
        // A replayed throw placed the NPC at the door and plays its pull-out;
        // the stream would stand it idle there before the player is out.
        if (human->IsPullingFromCar()) {
            const auto now = std::chrono::steady_clock::now();
            if (replica.pullingUntil == std::chrono::steady_clock::time_point {}) {
                replica.pullingUntil = now + kPullLimit;
            }
            if (now < replica.pullingUntil) {
                return;
            }
        }
        else {
            replica.pullingUntil = {};
        }
        const auto direction = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
        replica.frame->SetWorldPosition({pose.position.x, pose.position.y, pose.position.z});
        replica.frame->SetDirection({direction.x, direction.y, direction.z});
        replica.frame->Update();
        actor->SetStoredTransform({pose.position.x, pose.position.y, pose.position.z}, {direction.x, direction.y, direction.z});

        // Leave retail's hit reactions, reloads and timed actions to
        // C_human::Movement, which restores locomotion when they finish.
        const auto melee = human->Melee();
        if (melee.canWork == SDK::Player::kHumanTimedActionWorkState || melee.canWork == SDK::Player::kHumanAnimationSequenceWorkState || melee.weaponChangeTime >= 0) {
            return;
        }
        // Do_Shoot(false) throws as state 163 until the track ends, as on a
        // player's replica (PlayerService::ApplyRemotePose).
        if (human->AnimationState() == SDK::Player::kHumanThrowGrenadeAnimation) {
            return;
        }
        // The state at the same render time as the pose.
        std::optional<int16_t> state;
        for (const auto &[at, value] : replica.locomotion) {
            if (at > time && state) {
                break;
            }
            state = value;
        }
        if (!state) {
            return;
        }
        const auto animation = Player::PlayerService::ClassifyLocomotion(*state);
        if (!animation.oneShot || replica.lastAnimation != animation.state) {
            human->SetLocomotionAnimation(static_cast<SDK::Player::LocomotionAnimation>(animation.state));
        }
        replica.lastAnimation = animation.state;
    }

    void NpcReplicaService::Remove(World::WorldService &world, uint64_t networkId, const Replica &replica) {
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(replica.handle));
        world.NativeObjects().InvalidateNetwork(networkId);
        if (actor && world.IsReady()) {
            SDK::Core::Mission::Get()->Game()->RemoveTemporaryActor(actor);
        }
    }

    SDK::Scene::NativeFrame *NpcReplicaService::OnNativeDestroyed(World::WorldService &world, void *actor) {
        const auto found = _frames.find(actor);
        if (found == _frames.end()) {
            return nullptr;
        }
        SDK::Scene::NativeFrame *frame = found->second;
        _frames.erase(found);
        // Destroyed natively while still streamed (mission close): the next
        // Update creates a new one if it is still here.
        for (auto it = _replicas.begin(); it != _replicas.end(); ++it) {
            if (it->second.handle.object == actor) {
                world.NativeObjects().InvalidateNetwork(it->first);
                _replicas.erase(it);
                break;
            }
        }
        return frame;
    }

    void NpcReplicaService::Reset(World::WorldService &world) {
        auto replicas = std::move(_replicas);
        _replicas.clear();
        for (const auto &[id, replica] : replicas) {
            Remove(world, id, replica);
        }
    }
} // namespace Mafia1Online::Features::Npc
