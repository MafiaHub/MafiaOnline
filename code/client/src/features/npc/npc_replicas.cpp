#include <utils/safe_win32.h>

#include "npc_replicas.h"

#include "core/application.h"
#include "features/player/player_service.h"
#include "features/player/stalled_gait.h"
#include "features/seat/seat_service.h"
#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"
#include "shared/features/combat/melee_rules.h"
#include "shared/features/npc/npc_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_death.h>
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
        // A corpse whose fall ended away from the host's (a pose eased for a
        // melee contact, a death the wait limit let through) is drawn to it
        // within this time constant, no faster than this, while it falls.
        constexpr float kCorpseSettleSeconds = 0.25f;
        constexpr float kCorpseSettleSpeed = 1.0f;
        constexpr float kCorpseSettledDistance = 0.02f;

        // The host's NPC struck from within reach of where it saw its
        // target, the target itself a pose behind; on the target's screen
        // the replica trails both. Within the server's melee reach, widened
        // by how far the replica moves in its render delay, it closes to
        // just outside the metre inside which retail's fighter steps away
        // (C_entity::actionTick, ENTITY_ACTION_FIGHT_CLOSE_EVADE_DISTANCE_SQ).
        constexpr float kContactDistance = 1.1f;
        constexpr float kContactLagLimit = 1.5f;
        constexpr float kContactEaseInSeconds = 0.2f;
        constexpr float kContactEaseOutSeconds = 0.3f;

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
        AdvanceFades();
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
                replica.locomotion.push_back({sampleTime, npc->locomotion, npc->aiming, npc->crouching});
                replica.newestVelocity = snapshot.velocity;
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
            if (const auto *gait = GaitAt(replica, replica.renderTime)) {
                replica.stance = Stance {gait->aiming, gait->crouching};
            }
            auto *actor = world.NativeObjects().Resolve(replica.handle);
            if (actor) {
                // The death reaches the entity and the Combat::State on separate
                // channels; either one is final.
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
                if (replica.deathShown) {
                    SettleCorpse(replica, actor);
                }
                else {
                    ApplyPose(replica, actor);
                }
                replica.seenAlive = replica.seenAlive || alive;
                if (!alive && !replica.deathShown) {
                    // The host's body ran on until it died; DeathService
                    // replays the death once this copy draws the body at that
                    // moment (NpcEntity::deathClock), where the host's fell.
                    // Only the State said so yet: the entity's word, with the
                    // clock, follows within a send. One never seen alive lies
                    // as it streamed in; a lost clock waits no longer than
                    // any event.
                    const auto now = std::chrono::steady_clock::now();
                    if (replica.killedAt == std::chrono::steady_clock::time_point {}) {
                        replica.killedAt = now;
                    }
                    replica.deathShown = !replica.seenAlive || now - replica.killedAt >= Shared::Net::kEventWaitLimit ||
                                         (!npc->alive && replica.clock.Shows(npc->deathClock, replica.renderTime));
                }
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
            Remove(world, id, node.mapped(), true);
        }
    }

    void NpcReplicaService::ApplyPose(Replica &replica, void *native) {
        // The slewed presentation clock: subtracting the adaptive delay
        // directly stepped the body on every late or early packet.
        const double time = replica.renderTime;
        Framework::Utils::TransformSnapshot pose;
        if (!replica.snapshots.Sample(time, pose)) {
            return;
        }
        auto *actor = static_cast<SDK::Player::NativeActor *>(native);
        // A corpse lies where its death clip takes it (SettleCorpse).
        if (actor->IsDead()) {
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
        auto position = pose.position;
        auto direction = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
        ApplyContact(replica, pose, position, direction);
        replica.frame->SetWorldPosition({position.x, position.y, position.z});
        replica.frame->SetDirection({direction.x, direction.y, direction.z});
        replica.frame->Update();
        actor->SetStoredTransform({position.x, position.y, position.z}, {direction.x, direction.y, direction.z});

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
        const auto *gait = GaitAt(replica, time);
        if (!gait) {
            return;
        }
        std::optional<int16_t> state = gait->locomotion;
        // Held at the extrapolation bound: the stop pose until the stream
        // resumes, not a run on the spot.
        if (Player::MovingGait(*state) &&
            Player::Stalled(time, static_cast<double>(replica.locomotion.back().at), kSnapshotConfig.maxExtrapolationMs, replica.newestVelocity)) {
            state = Player::kStopState;
        }
        const auto animation = Player::PlayerService::ClassifyLocomotion(*state);
        if (!animation.oneShot || replica.lastAnimation != animation.state) {
            human->SetLocomotionAnimation(static_cast<SDK::Player::LocomotionAnimation>(animation.state));
        }
        replica.lastAnimation = animation.state;
    }

    void NpcReplicaService::SettleCorpse(Replica &replica, void *native) {
        const auto now = std::chrono::steady_clock::now();
        const float seconds = replica.settledAt == std::chrono::steady_clock::time_point {} ? 0.0f : std::min(std::chrono::duration<float>(now - replica.settledAt).count(), 0.1f);
        replica.settledAt = now;
        Framework::Utils::TransformSnapshot pose;
        const auto *seated = static_cast<const SDK::Seat::NativeHuman *>(native);
        if (!replica.snapshots.Sample(replica.renderTime, pose) || seated->usedActorEnter || seated->usedActorLeave) {
            return;
        }
        // The host's body at the moment this copy shows: falling along the
        // same death clip, then where it rests. The fall started from the
        // host's place at the host's moment, so they part only by a pose
        // interval of the clip's track; the gap closes while the body falls.
        // One never seen alive lies where the host's does from the start.
        const auto at = SDK::Player::WorldPosition(native);
        const glm::vec3 error = pose.position - glm::vec3(at.x, at.y, at.z);
        const float distance = glm::length(error);
        if (!std::isfinite(distance) || distance < kCorpseSettledDistance) {
            return;
        }
        glm::vec3 step = error;
        if (replica.seenAlive) {
            step *= std::min(seconds / kCorpseSettleSeconds, 1.0f);
            const float limit = kCorpseSettleSpeed * seconds;
            if (glm::length(step) > limit) {
                step *= limit / glm::length(step);
            }
        }
        SDK::Player::ShiftBody(*static_cast<SDK::Player::NativeHuman *>(native), {step.x, step.y, step.z});
    }

    void NpcReplicaService::ApplyContact(Replica &replica, const Framework::Utils::TransformSnapshot &pose, glm::vec3 &position, glm::vec3 &direction) {
        const auto now = std::chrono::steady_clock::now();
        const float seconds = replica.contactAt == std::chrono::steady_clock::time_point {} ? 0.0f : std::min(std::chrono::duration<float>(now - replica.contactAt).count(), 0.1f);
        replica.contactAt = now;
        auto *player = SDK::Player::CurrentPlayer();
        if (!player || (now >= replica.engagedUntil && replica.contact <= 0.0f)) {
            replica.contact = 0.0f;
            return;
        }
        const auto at = SDK::Player::WorldPosition(player);
        const glm::vec3 target(at.x, at.y, at.z);
        const glm::vec3 flat(position.x - target.x, 0.0f, position.z - target.z);
        const float distance = glm::length(flat);
        const float speed = glm::length(glm::vec3(pose.velocity.x, 0.0f, pose.velocity.z));
        const float lag = std::isfinite(speed) ? std::min(speed * replica.snapshots.EffectiveDelayMs() * 0.001f, kContactLagLimit) : 0.0f;
        const bool closing = now < replica.engagedUntil && distance > kContactDistance && distance <= Shared::Combat::kMeleeReach + lag &&
                             std::abs(position.y - target.y) <= Shared::Combat::kMeleeHeightDifference;
        if (closing) {
            replica.contactSpot = {target.x + flat.x / distance * kContactDistance, position.y, target.z + flat.z / distance * kContactDistance};
        }
        replica.contact = std::clamp(replica.contact + (closing ? seconds / kContactEaseInSeconds : -seconds / kContactEaseOutSeconds), 0.0f, 1.0f);
        if (replica.contact <= 0.0f) {
            return;
        }
        position = glm::mix(position, replica.contactSpot, replica.contact);
        // Facing the player, as the host's fighter turned to strike.
        const glm::vec3 facing(target.x - position.x, 0.0f, target.z - position.z);
        if (glm::length(facing) > 0.01f) {
            const glm::vec3 turned = glm::mix(direction, glm::normalize(facing), replica.contact);
            if (glm::length(turned) > 0.01f) {
                direction = glm::normalize(turned);
            }
        }
    }

    void NpcReplicaService::Engage(uint64_t networkId, std::chrono::steady_clock::time_point until) {
        const auto found = _replicas.find(networkId);
        if (found != _replicas.end()) {
            found->second.engagedUntil = std::max(found->second.engagedUntil, until);
        }
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
        if (found == _replicas.end() || !found->second.frame || std::chrono::steady_clock::now() - received >= Shared::Net::kEventWaitLimit) {
            return true;
        }
        return found->second.clock.Shows(hostClock, found->second.renderTime);
    }

    const NpcReplicaService::Replica::Gait *NpcReplicaService::GaitAt(const Replica &replica, double time) {
        const Replica::Gait *gait = nullptr;
        for (const auto &entry : replica.locomotion) {
            if (static_cast<double>(entry.at) > time && gait) {
                break;
            }
            gait = &entry;
        }
        return gait;
    }

    std::optional<NpcReplicaService::Stance> NpcReplicaService::DisplayedStance(uint64_t networkId) const {
        const auto found = _replicas.find(networkId);
        return found == _replicas.end() || !found->second.frame ? std::nullopt : found->second.stance;
    }

    bool NpcReplicaService::DeathShown(uint64_t networkId) const {
        const auto found = _replicas.find(networkId);
        return found == _replicas.end() || !found->second.frame || found->second.deathShown;
    }

    void NpcReplicaService::Remove(World::WorldService &world, uint64_t networkId, const Replica &replica, bool fade) {
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(replica.handle));
        world.NativeObjects().InvalidateNetwork(networkId);
        if (!actor || !world.IsReady()) {
            return;
        }
        // Retail fades a walker leaving its despawn radius only while it is
        // drawn, through its "base" mesh, and drops it at once otherwise
        // (C_traffic_element::Tick, Create's m_pTransparencyObject); a
        // C_player fades the same mesh (m_pAlphaObject). A seated copy goes
        // with its seat at once.
        const auto *seated = static_cast<const SDK::Seat::NativeHuman *>(static_cast<void *>(actor));
        auto *base = actor->Frame()->FindChildFrame("base");
        if (fade && !seated->usedActorEnter && !seated->usedActorLeave && base && base->SupportsTransparency() &&
            actor->Frame()->RenderStamp() == SDK::Core::Mission::Get()->RenderStamp()) {
            // Nothing moves it any more: it stands, not runs on the spot.
            auto *human = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
            if (!actor->IsDead() && Player::MovingGait(human->AnimationState())) {
                human->SetLocomotionAnimation(SDK::Player::LocomotionAnimation::Idle);
            }
            _fading.push_back({actor, base, 1.0f});
            return;
        }
        SDK::Core::Mission::Get()->Game()->RemoveTemporaryActor(actor);
    }

    void NpcReplicaService::AdvanceFades() {
        const auto now = std::chrono::steady_clock::now();
        const float seconds = _fadedAt == std::chrono::steady_clock::time_point {} ? 0.0f : std::min(std::chrono::duration<float>(now - _fadedAt).count(), 0.1f);
        _fadedAt = now;
        // Retail's rate: the whole opacity in a second (frameTime * 0.001).
        std::vector<SDK::Player::NativeActor *> faded;
        for (auto it = _fading.begin(); it != _fading.end();) {
            it->alpha -= seconds;
            if (it->alpha > 0.0f) {
                it->base->SetTransparency(it->alpha);
                ++it;
                continue;
            }
            faded.push_back(static_cast<SDK::Player::NativeActor *>(it->actor));
            it = _fading.erase(it);
        }
        // Out of the list first: removal runs the actor destructor now.
        for (auto *actor : faded) {
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
        std::erase_if(_fading, [actor](const Fading &fading) { return fading.actor == actor; });
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
            Remove(world, id, replica, false);
        }
        auto fading = std::move(_fading);
        _fading.clear();
        _fadedAt = {};
        if (world.IsReady()) {
            for (const auto &copy : fading) {
                SDK::Core::Mission::Get()->Game()->RemoveTemporaryActor(static_cast<SDK::Player::NativeActor *>(copy.actor));
            }
        }
    }
} // namespace Mafia1Online::Features::Npc
