#include <utils/safe_win32.h>

#include "npc_replicas.h"

#include "core/application.h"
#include "features/player/player_service.h"
#include "features/seat/seat_service.h"
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
#include <random>
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
        // As PlayerService's kSeizedLimit: the longest an arrest half holds
        // the pose stream off.
        constexpr std::chrono::milliseconds kSeizingLimit {15000};

        // A copy constructed here later than this into its throw-out, or its
        // door exit or entry, stands where the host's is: replaying it would
        // pull it back to the seat while the host's one is already out. The
        // door window stays inside the 2 s SeatService holds an exit for.
        constexpr uint16_t kThrowReplayAgeMs = 1000;
        constexpr uint16_t kDoorReplayAgeMs = 1500;
        // An action waits for its replica at most the snapshot buffer's
        // longest render delay (kSnapshotConfig, 600 ms) and a little. One
        // dated further ahead than a second is from a clock the mapper lost
        // (no pose change for longer than the 16-bit range) and plays now.
        constexpr auto kReachWaitLimit = std::chrono::milliseconds(700);
        constexpr double kReachLeadLimitMs = 1000.0;

        // reM HUMAN_THROW_CAR_SCREAM_BASE and _COUNT.
        constexpr uint32_t kThrownOutFirstLine = 161;
        constexpr uint32_t kThrownOutLines     = 7;

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
                // The Corovacka branch then plays one of the thrown-out
                // lines (161-167) in the new driver's voice; on the host it
                // is a loose sound, never a line of its tracked NPC.
                static std::mt19937 random {std::random_device {}()};
                static_cast<SDK::Player::NativeHuman *>(actor)->Say(kThrownOutFirstLine + std::uniform_int_distribution<uint32_t>(0, kThrownOutLines - 1)(random));
            }
        }
    } // namespace

    void NpcReplicaService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (Simulation::IsHost() || !replication || !world.IsReady()) {
            Reset(world);
            return;
        }
        auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
        std::unordered_set<uint64_t> seen;
        const MafiaNet::Time networkNow = MafiaNet::GetTime();
        const double clockNow           = static_cast<double>(MafiaNet::GetTimeUS()) * 0.001;
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
            const MafiaNet::Time transformTime = npc->GetLastTransformTime();
            const bool newPose                 = replica.snapshots.Empty() || replica.lastTransformTime != transformTime;
            const MafiaNet::Time sampleTime    = newPose ? static_cast<MafiaNet::Time>(std::max(2.0, std::round(replica.clock.Map(npc->sampleClock, clockNow)))) : 0;
            std::optional<Framework::Utils::TransformSnapshot> handedOff;
            if (!replica.frame && !replica.failed && replica.lastTransformTime == 0 && npc->sourcePedestrianId) {
                // The walker this NPC was: start where it stands on this
                // screen, a render delay behind, and ease into the host's
                // stream instead of jumping to its newer pose.
                handedOff = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance())->PedestrianReplicas().HandOff(world, npc->sourcePedestrianId);
                if (handedOff && newPose) {
                    const auto delay = static_cast<MafiaNet::Time>(replica.snapshots.EffectiveDelayMs());
                    const MafiaNet::Time renderTime = networkNow > delay ? networkNow - delay : 1;
                    replica.snapshots.Push(*handedOff, std::min(renderTime, sampleTime - 1));
                }
            }
            if (newPose) {
                replica.snapshots.Push(snapshot, sampleTime, clockNow);
                replica.locomotion.emplace_back(sampleTime, npc->locomotion);
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
                // Lines said before this copy existed are not replayed.
                static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor))->SetVoice(npc->voice);
                replica.speechRevision = npc->speechRevision;
                using CarAction = Shared::Entities::NpcCarAction;
                if (npc->alive && npc->carAction == CarAction::Throw && npc->kind == Shared::Entities::NpcKind::Driver && npc->carActionAgeMs < kThrowReplayAgeMs) {
                    ReplayThrowOut(world, actor, npc->carActionCarId);
                }
                else if (npc->alive && (npc->carAction == CarAction::Exit || npc->carAction == CarAction::Enter) && npc->carActionAgeMs < kDoorReplayAgeMs) {
                    application->Seats().ReplayNpcDoor(world, id, npc->carActionCarId, npc->carActionSeat,
                                                       npc->carAction == CarAction::Exit ? Shared::Car::SeatAction::Exit : Shared::Car::SeatAction::Enter);
                }
            }
            replica.renderTime = replica.snapshots.RenderTimeMs(clockNow);
            auto *actor = world.NativeObjects().Resolve(replica.handle);
            if (actor) {
                // The death reaches the entity and the Combat::State on separate
                // channels; DeathService replays it on the first, and the pose
                // stream stops with it.
                const auto *state = application->Combat().GetState(id);
                const bool alive = npc->alive && (!state || state->alive);
                if (replica.speechRevision != npc->speechRevision) {
                    replica.speechRevision = npc->speechRevision;
                    if (alive) {
                        auto *human = static_cast<SDK::Player::NativeHuman *>(actor);
                        human->SetVoice(npc->voice);
                        human->Say(npc->speechLine);
                    }
                }
                ApplyAnimation(replica, actor, *npc);
                ApplyPose(replica, actor, alive);
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
        // The slewed presentation clock: subtracting the adaptive delay
        // directly stepped the body on every late or early packet.
        const double time = replica.renderTime;
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
        // The officer's arrest half plays from the aligned place.
        if (replica.seizingUntil != std::chrono::steady_clock::time_point {}) {
            if (human->IsPlayingAnimation() && std::chrono::steady_clock::now() < replica.seizingUntil) {
                return;
            }
            replica.seizingUntil = {};
        }
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
        // A script clip owns the body until it ends or is stopped.
        if (std::chrono::steady_clock::now() < replica.animatingUntil) {
            return;
        }
        // The state at the same render time as the pose.
        std::optional<int16_t> state;
        for (const auto &[at, value] : replica.locomotion) {
            if (static_cast<double>(at) > time && state) {
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

    void NpcReplicaService::ApplyAnimation(Replica &replica, void *native, const Shared::Entities::NpcEntity &npc) {
        auto *human = static_cast<SDK::Player::NativeHuman *>(native);
        const int32_t work = human->Melee().canWork;
        if (replica.animationSynced && replica.animationRevision == npc.animationRevision) {
            // A clip with a displacement track plays once whatever its loop
            // flag (Shared::Npc::ScriptClip), as on the host, which starts
            // it again: so does this copy, instead of standing in the stop
            // pose with the host's idle held off.
            if (replica.clip.name.empty()) {
                return;
            }
            // A hit reaction or a move replaced the clip: it no longer holds
            // the host's locomotion off, and a re-sent clip plays again.
            if (!Shared::Npc::ScriptClip::Holds(work)) {
                replica.clip = {};
                replica.animatingUntil = {};
                return;
            }
            if (replica.clip.NeedsReplay(npc.alive && !static_cast<SDK::Player::NativeActor *>(native)->IsDead(), work)) {
                human->PlayScriptAnimation(replica.clip.name.c_str(), true);
            }
            return;
        }
        const bool first = !replica.animationSynced;
        replica.animationSynced = true;
        replica.animationRevision = npc.animationRevision;
        if (!npc.alive) {
            replica.clip = {};
            return;
        }
        if (npc.animation.empty()) {
            if (!first) {
                human->StopAnimation();
            }
            replica.clip = {};
            replica.animatingUntil = {};
            return;
        }
        // A one-shot clip that started before this copy existed is over.
        if (first && !npc.animationLoop) {
            return;
        }
        // A routine refreshing the loop already playing keeps it running.
        if (replica.clip.Continues(npc.animation, npc.animationLoop, work)) {
            return;
        }
        const uint32_t duration = human->PlayScriptAnimation(npc.animation.c_str(), npc.animationLoop);
        replica.clip = {npc.animation, npc.animationLoop, human->IsPlayingAnimation()};
        // A clip this client cannot load holds nothing off: the host's idle
        // shows instead of a frozen pose.
        if (duration == 0) {
            replica.clip = {};
            replica.animatingUntil = {};
            return;
        }
        replica.animatingUntil = npc.animationLoop ? std::chrono::steady_clock::time_point::max()
                                                   : std::chrono::steady_clock::now() + std::chrono::milliseconds(duration);
    }

    void NpcReplicaService::PlaySeizing(World::WorldService &world, uint64_t officerId, const Shared::Npc::Seized::Pose &pose, Shared::Npc::Caught::Outcome outcome) {
        const auto found = officerId ? _replicas.find(officerId) : _replicas.end();
        if (found == _replicas.end() || !found->second.frame) {
            return;
        }
        auto &replica = found->second;
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(replica.handle));
        if (!actor || !actor->IsAlive() || actor->IsDead()) {
            return;
        }
        // Where state 10 stood the host's officer, facing its target; a
        // replica far from it (stale pose) plays where it is.
        const auto current = SDK::Player::WorldPosition(actor);
        if (Shared::Npc::SeizedAlignable({current.x, current.y, current.z}, pose)) {
            const auto place = pose.Position();
            const auto facing = pose.Direction();
            replica.frame->SetWorldPosition({place.x, place.y, place.z});
            replica.frame->SetDirection({facing.x, facing.y, facing.z});
            replica.frame->Update();
            actor->SetStoredTransform({place.x, place.y, place.z}, {facing.x, facing.y, facing.z});
        }
        auto *human = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
        const uint32_t duration = human->PlayAnimationOnce(Shared::Npc::SeizingAnimation(outcome));
        if (duration == 0) {
            return;
        }
        replica.seizingUntil = std::chrono::steady_clock::now() + std::min(std::chrono::milliseconds(duration), kSeizingLimit);
    }

    bool NpcReplicaService::Reached(uint64_t networkId, uint16_t hostClock, std::chrono::steady_clock::time_point received) const {
        const auto found = _replicas.find(networkId);
        if (found == _replicas.end() || !found->second.frame || std::chrono::steady_clock::now() - received >= kReachWaitLimit) {
            return true;
        }
        const auto at = found->second.clock.Project(hostClock);
        if (!at) {
            return true;
        }
        const double lead = *at - found->second.renderTime;
        return lead <= 0.0 || lead > kReachLeadLimitMs;
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
