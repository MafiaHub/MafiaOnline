#include <utils/safe_win32.h>

#include "npc_replicas.h"

#include "features/player/player_service.h"
#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"
#include "shared/features/npc/npc_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/player/native_human_factory.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

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
            if (replica.lastTransformTime != transformTime) {
                replica.snapshots.Push(snapshot, transformTime);
                replica.locomotion.emplace_back(transformTime, npc->locomotion);
                while (replica.locomotion.size() > 32) {
                    replica.locomotion.pop_front();
                }
                replica.lastTransformTime = transformTime;
            }
            if (!replica.frame && !replica.failed) {
                const auto direction = glm::mat3_cast(snapshot.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
                const std::string name = "Mafia1OnlineNpc_" + std::to_string(id);
                auto *actor = SDK::Player::NativeHumanFactory::CanLoadHumanModel(npc->model)
                    ? SDK::Player::NativeHumanFactory::CreateTemporary(*SDK::Core::Mission::Get(), SDK::Player::NativeActor::Type::Entity, name.c_str(), npc->model.c_str(),
                                                                       {snapshot.position.x, snapshot.position.y, snapshot.position.z}, {direction.x, direction.y, direction.z})
                    : nullptr;
                if (!actor) {
                    replica.failed = true;
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not create NPC replica {} with model '{}'", id, npc->model);
                    return;
                }
                replica.handle = world.NativeObjects().Bind(actor, id);
                replica.frame = actor->Frame();
                _frames[actor] = replica.frame;
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
        const auto direction = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
        replica.frame->SetWorldPosition({pose.position.x, pose.position.y, pose.position.z});
        replica.frame->SetDirection({direction.x, direction.y, direction.z});
        replica.frame->Update();
        actor->SetStoredTransform({pose.position.x, pose.position.y, pose.position.z}, {direction.x, direction.y, direction.z});

        auto *human = static_cast<SDK::Player::NativeHuman *>(native);
        // Leave retail's hit reactions, reloads and timed actions to
        // C_human::Movement, which restores locomotion when they finish.
        const auto melee = human->Melee();
        if (melee.canWork == SDK::Player::kHumanTimedActionWorkState || melee.canWork == SDK::Player::kHumanAnimationSequenceWorkState || melee.weaponChangeTime >= 0) {
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
