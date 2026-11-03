#include <utils/safe_win32.h>

#include "pedestrian_replicas.h"

#include "core/application.h"
#include "features/car/car_service.h"
#include "features/player/stalled_gait.h"
#include "features/simulation/simulation_config.h"
#include "features/world/fading_copies.h"
#include "features/world/frame_clock.h"
#include "features/world/world_service.h"
#include "shared/features/pedestrian/pedestrian_entity.h"
#include "shared/features/pedestrian/pedestrian_knockdown.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/car/native_car.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/traffic/native_traffic.h>
#include <mafia1/sdk/world/native_fire.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>
#include <unordered_set>

namespace Mafia1Online::Features::Pedestrian {
    namespace {
        // Retail elements cap a frame at the game loop's 83 ms.
        constexpr uint32_t kMaximumFrameMs = 83;
        // A living walker whose next clip has the same id as the last one
        // reports no change; restart it rather than hold forever.
        constexpr std::chrono::milliseconds kLivingHoldLimit {500};
        // A dead walker's clip ended: the host may still chain the queued
        // end of a knockdown (Hit 0x4588d0), whose id comes a moment after
        // this copy's fall ends. Then the corpse settles.
        constexpr std::chrono::milliseconds kCorpseSettleDelay {500};
        // A reaction this copy played waits this long for the host's walker
        // to be past it: the controller's report, the server, the host and
        // back, the render delay, and the clips themselves. One the host
        // never applied (the server refused it) then gives the walker back.
        constexpr std::chrono::milliseconds kReactionConfirmLimit {3000};
        // Tick's threat test reaches no further than a horn's 50 m watch
        // plus a car length.
        constexpr float kThreatScanDistance = 60.0f;
        // Snapshot's own warp distance at a standstill.
        constexpr float kHandOverSnapDistance = 4.0f;
        // How far a walker streamed in dead runs its clip at once.
        constexpr uint32_t kFallenLimitMs = 10000;

        bool Finite(const Framework::Utils::TransformSnapshot &pose) {
            return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) && std::isfinite(pose.position.z) && glm::length(pose.rotation) >= 0.0001f;
        }

        // The pool's file names carry the "Models\" directory the host strips.
        bool SameModel(const char *poolName, std::string_view model) {
            std::string_view name = poolName;
            if (const auto slash = name.find_last_of("\\/"); slash != std::string_view::npos) {
                name.remove_prefix(slash + 1);
            }
            return name.size() == model.size() && std::equal(name.begin(), name.end(), model.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            });
        }

        // Update's frames, back to back, until retail Tick would end the
        // clip; a looping clip stops at kFallenLimitMs.
        void RunToEnd(SDK::Traffic::NativeTrafficElement &element) {
            for (uint32_t elapsed = 0; elapsed < kFallenLimitMs; elapsed += kMaximumFrameMs) {
                if (element.TickAnimation(kMaximumFrameMs)) {
                    return;
                }
                element.Model()->Tick(static_cast<int>(kMaximumFrameMs));
                if (element.CountDownClip(kMaximumFrameMs)) {
                    return;
                }
            }
        }

        std::vector<SDK::Traffic::NativeTrafficGenerator *> MissionGenerators() {
            std::vector<SDK::Traffic::NativeTrafficGenerator *> generators;
            SDK::Core::Mission::Get()->ForEachActor([&](SDK::Player::NativeActor *actor) {
                if (static_cast<uint32_t>(actor->GetType()) == SDK::Traffic::kGeneratorActorType) {
                    generators.push_back(reinterpret_cast<SDK::Traffic::NativeTrafficGenerator *>(actor));
                }
            });
            return generators;
        }
    } // namespace

    uint64_t PedestrianReplicaService::WalkerOfModel(const SDK::Scene::NativeFrame *model) const {
        for (const auto &[id, replica] : _replicas) {
            if (replica.element && replica.element->Model() == model) {
                return id;
            }
        }
        return 0;
    }

    void PedestrianReplicaService::OnCarContact(World::WorldService &world, const SDK::Scene::NativeFrame *model, uint64_t carId, SDK::Car::NativeCar &car,
                                                const glm::vec3 &velocity) {
        namespace Pedestrian = Shared::Pedestrian;
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || !Pedestrian::FiniteVector(velocity) || !(glm::length(velocity) > 0.0f)) {
            return;
        }
        for (auto &[id, replica] : _replicas) {
            auto *element = replica.element;
            if (!element || element->Model() != model) {
                continue;
            }
            const auto *walker = replication->GetEntity<Shared::Entities::PedestrianEntity>(id);
            // Retail Hit returns at once for a dead walker, and a fallen one
            // has no collision a car could meet.
            if (!walker || !walker->alive || replica.dead || replica.settled || replica.fallen ||
                (replica.reaction && replica.reaction->kind == Pedestrian::ReactionKind::Knockdown) ||
                Pedestrian::KnockdownCollisionDelay(element->CurrentAnimation()) != 0) {
                return;
            }
            // Collision's Hit repeats every physics step of the contact; one
            // decides it, as the server accepts one in kReactionGapMs.
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double, std::milli>(now - replica.lastContact).count() < Pedestrian::kReactionGapMs) {
                return;
            }
            const auto position = element->Position();
            const auto direction = element->Direction();
            const glm::vec3 at(position.x, position.y, position.z);
            const glm::vec3 facing(direction.x, direction.y, direction.z);
            Pedestrian::Reaction reaction;
            reaction.walkerId = id;
            reaction.carId = carId;
            reaction.position = at;
            reaction.facing = facing;
            reaction.impact = velocity;
            if (Pedestrian::KnocksDown(velocity)) {
                // Hit's knockdown: dead below 0.5 health, with its two random
                // picks for a fatal fall.
                reaction.kind = Pedestrian::ReactionKind::Knockdown;
                reaction.fatal = Pedestrian::CarImpactKills(walker->health, Pedestrian::CarImpactDamage(velocity));
                const auto knockdown = Pedestrian::CarKnockdown(velocity, facing, reaction.fatal, (_random() & 1) != 0);
                reaction.animation = static_cast<int16_t>(knockdown.animation);
                reaction.queued = static_cast<int16_t>(knockdown.queued);
                reaction.facing = knockdown.facing;
            }
            else {
                // Hit's slow contact dodges only with no clip track playing
                // (0x458ab6); the car just pushes a walker mid-clip.
                if (!element->AnimationFree() || Pedestrian::IsMegaPanic(element->CurrentAnimation())) {
                    return;
                }
                reaction.kind = Pedestrian::ReactionKind::Sidestep;
                const auto carAt = car.Position();
                reaction.animation = static_cast<int16_t>(Pedestrian::CarDodge(at, facing, velocity, {carAt.x, carAt.y, carAt.z}));
            }
            replica.lastContact = now;
            Decide(world, replica, reaction);
            return;
        }
    }

    void PedestrianReplicaService::OnReaction(const Shared::Pedestrian::Reaction &reaction) {
        if (_pendingReactions.size() >= 64) {
            _pendingReactions.pop_front();
        }
        _pendingReactions.push_back({reaction, std::chrono::steady_clock::now()});
    }

    void PedestrianReplicaService::Decide(World::WorldService &world, Replica &replica, Shared::Pedestrian::Reaction &reaction) {
        auto *replication = Framework::CoreModules::GetReplication();
        const auto *walker = replication ? replication->GetEntity<Shared::Entities::PedestrianEntity>(reaction.walkerId) : nullptr;
        if (!walker) {
            return;
        }
        reaction.missionGeneration = world.LoadedMissionGeneration();
        // Its own car's poses carry this clock: each player's copy of the
        // car is at this moment when it shows the pose of this frame.
        reaction.SetCarClock(World::FrameClock::NowMs());
        if (!Shared::Pedestrian::ReactionWellFormed(reaction)) {
            return;
        }
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(reaction, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        StartReaction(replica, reaction, walker->voice);
    }

    void PedestrianReplicaService::StartReaction(Replica &replica, const Shared::Pedestrian::Reaction &reaction, uint8_t voice) {
        namespace Pedestrian = Shared::Pedestrian;
        auto *element = replica.element;
        if (!element || !element->IsActive() || replica.settled) {
            return;
        }
        const bool knockdown = reaction.kind == Pedestrian::ReactionKind::Knockdown;
        // Hit's knockdown steps (0x458eea-0x45916f): the umbrella dropped,
        // the collision out until the fall and its queued clip are over, and
        // the body turned to the impact; Tick's SetAnim plays the fall with
        // the flag it had. A dodge plays as Tick's, with the flag clear.
        bool flag = false;
        glm::vec3 facing = reaction.facing;
        if (knockdown) {
            if (element->HasUmbrella()) {
                element->DropUmbrella();
            }
            element->KnockDown(Pedestrian::KnockdownCollisionDelay(reaction.animation));
            flag = element->AnimationFlag();
        }
        else {
            const auto direction = element->Direction();
            facing = {direction.x, direction.y, direction.z};
        }
        const auto position = element->Position();
        element->SetPose(position, {facing.x, 0.0f, facing.z});
        element->RestartAnimation(reaction.animation, flag);
        replica.holding = false;
        replica.handOver.reset();
        replica.reaction = Replica::Reacting {reaction.kind, reaction.animation, reaction.queued, reaction.fatal, facing, Replica::Reacting::Phase::Clip, false,
                                              std::chrono::steady_clock::now()};
        // Tick's dodge scream and Collision's thud, where the walker is.
        if (reaction.scream >= 0) {
            SDK::Traffic::PlayScream(voice, reaction.scream, position, Pedestrian::kDodgeScreamMinimumRange, Pedestrian::kDodgeScreamMaximumRange,
                                     Pedestrian::kDodgeScreamCurve, Pedestrian::kDodgeScreamMinimumVolume);
        }
        auto *mission = SDK::Core::Mission::Get();
        if (reaction.kind != Pedestrian::ReactionKind::Dodge && glm::dot(reaction.impact, reaction.impact) > Pedestrian::kImpactSoundSpeedSq && mission && mission->Game()) {
            if (auto *sound = SDK::World::Fire::CreateTableSound(Pedestrian::kImpactSound)) {
                sound->Frame()->SetWorldPosition({position.x, position.y + Pedestrian::kImpactSoundHeight, position.z});
                sound->Frame()->Update();
                SDK::World::Fire::PlayOwnedSound(mission->Game(), sound);
            }
        }
    }

    bool PedestrianReplicaService::PlayReaction(Replica &replica, int32_t shownAnimation, bool shownAlive, uint32_t frameMs, std::chrono::steady_clock::time_point now) {
        namespace Pedestrian = Shared::Pedestrian;
        auto &reaction = *replica.reaction;
        auto *element = replica.element;
        const bool knockdown = reaction.kind == Pedestrian::ReactionKind::Knockdown;
        // Killed by something else mid-dodge: the host's death plays.
        if (replica.dead && !knockdown) {
            replica.reaction.reset();
            replica.holding = false;
            return false;
        }
        // The host's walker shows it (its Hit may have picked another fall
        // where its outcome differed).
        if (!reaction.confirmed && (shownAnimation == reaction.animation || (reaction.queued && shownAnimation == reaction.queued) ||
                                    (knockdown && (!shownAlive || Pedestrian::KnockdownCollisionDelay(shownAnimation) != 0)))) {
            reaction.confirmed = true;
        }
        const auto advance = [&] {
            if (replica.holding) {
                return;
            }
            // Moved by the clip's root motion alone, from where it reacted.
            replica.holding = element->TickAnimation(frameMs);
            replica.holdingSince = now;
            element->SetPose(element->Position(), {reaction.facing.x, 0.0f, reaction.facing.z});
            if (!replica.holding) {
                element->Model()->Tick(static_cast<int>(frameMs));
                replica.holding = element->CountDownClip(frameMs);
            }
        };
        if (reaction.phase != Replica::Reacting::Phase::Done) {
            advance();
            if (replica.holding) {
                if (reaction.phase == Replica::Reacting::Phase::Clip && reaction.queued) {
                    // Tick chains Hit's queued clip with the loaded flag; a
                    // walker the host's Hit killed after all settles instead
                    // of getting up.
                    if (replica.dead && reaction.queued == Pedestrian::kFallRecover) {
                        reaction.queued = Pedestrian::kFallEndB;
                        reaction.fatal = true;
                    }
                    element->PlayAnimation(reaction.queued, true);
                    reaction.phase = Replica::Reacting::Phase::Queued;
                    replica.holding = false;
                }
                else {
                    reaction.phase = Replica::Reacting::Phase::Done;
                    // A corpse lies as its last clip left it; a living
                    // walker breathes where it stands.
                    if (!reaction.fatal && !replica.dead) {
                        element->PlayAnimation(Player::kStopState, false);
                        replica.holding = false;
                    }
                }
            }
            return true;
        }
        if (replica.dead) {
            // Tick's end of a dead walker's last clip.
            replica.settled = true;
            element->SettleCorpse(SDK::Core::Mission::Get()->Game());
            replica.reaction.reset();
            return true;
        }
        const bool past = reaction.confirmed && shownAlive && shownAnimation != reaction.animation && shownAnimation != reaction.queued &&
                          Pedestrian::KnockdownCollisionDelay(shownAnimation) == 0;
        if (past || now - reaction.since > kReactionConfirmLimit) {
            const auto position = element->Position();
            const auto direction = element->Direction();
            replica.handOver = Replica::HandOver {{position.x, position.y, position.z}, {direction.x, direction.y, direction.z}, false, glm::vec3(0.0f), now};
            replica.reaction.reset();
            replica.holding = false;
            if (knockdown && !element->HasCollisions()) {
                element->RestoreCollision();
            }
            return false;
        }
        advance();
        return true;
    }

    void PedestrianReplicaService::DetectThreats(World::WorldService &world, Car::CarService &cars) {
        namespace Pedestrian = Shared::Pedestrian;
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        // The local horn's cooldown, which this client's AI_drive sets.
        const bool horn = SDK::Traffic::HornCooldownActive();
        cars.ForEachSimulated([&](uint64_t carId, SDK::Car::NativeCar &car) {
            auto *frame = car.Frame();
            if (!frame) {
                return;
            }
            const auto framePosition = frame->WorldPosition();
            const auto frameDirection = frame->WorldDirection();
            const glm::vec3 carFrameAt(framePosition.x, framePosition.y, framePosition.z);
            const glm::vec3 heading(frameDirection.x, frameDirection.y, frameDirection.z);
            const auto velocity = car.Vehicle().LinearVelocity();
            const auto carAt = car.Position();
            const float watch = Pedestrian::ThreatWatchDistance(car.Vehicle().DriverSpeed(), horn);
            for (auto &[id, replica] : _replicas) {
                auto *element = replica.element;
                if (!element || !element->IsActive() || !element->Model() || replica.dead || replica.settled || replica.fallen) {
                    continue;
                }
                // A reaction still playing keeps its clip track; one done
                // and breathing may react again, as retail's walker would.
                if (replica.reaction && replica.reaction->phase != Replica::Reacting::Phase::Done) {
                    continue;
                }
                const auto *walker = replication->GetEntity<Shared::Entities::PedestrianEntity>(id);
                // Tick's earlier returns: a corpse, a taxi wave, a mega panic;
                // then no clip track and no one-shot clip.
                if (!walker || !walker->alive || walker->taxiWave || Pedestrian::IsMegaPanic(element->CurrentAnimation()) || !element->AnimationFree()) {
                    continue;
                }
                const auto position = element->Position();
                const glm::vec3 at(position.x, position.y, position.z);
                if (glm::distance(at, carFrameAt) > kThreatScanDistance || !car.WouldRunOver(position, watch)) {
                    continue;
                }
                const auto direction = element->Direction();
                const glm::vec3 facing(direction.x, direction.y, direction.z);
                if (!Pedestrian::ThreatClose(horn, heading, carFrameAt, facing, at)) {
                    continue;
                }
                Pedestrian::Reaction reaction;
                reaction.walkerId = id;
                reaction.carId = carId;
                reaction.kind = Pedestrian::ReactionKind::Dodge;
                reaction.position = at;
                reaction.facing = facing;
                reaction.animation = static_cast<int16_t>(Pedestrian::CarDodge(at, facing, {velocity.x, velocity.y, velocity.z}, {carAt.x, carAt.y, carAt.z}));
                reaction.scream = static_cast<int16_t>(Pedestrian::kDodgeScreamFirst + static_cast<int32_t>(_random() % Pedestrian::kDodgeScreamCount));
                Decide(world, replica, reaction);
            }
        });
    }

    void PedestrianReplicaService::PlayDueReactions(Car::CarService &cars) {
        auto *replication = Framework::CoreModules::GetReplication();
        const auto now = std::chrono::steady_clock::now();
        for (auto it = _pendingReactions.begin(); it != _pendingReactions.end();) {
            const auto &reaction = it->reaction;
            const double waited = std::chrono::duration<double, std::milli>(now - it->received).count();
            const auto shown = cars.ShownControllerClock(reaction.carId);
            if (!Shared::Pedestrian::ReactionDue(shown ? &*shown : nullptr, reaction.CarClock(), waited)) {
                ++it;
                continue;
            }
            const auto replica = _replicas.find(reaction.walkerId);
            const auto *walker = replication ? replication->GetEntity<Shared::Entities::PedestrianEntity>(reaction.walkerId) : nullptr;
            if (replica != _replicas.end() && walker && !replica->second.dead) {
                StartReaction(replica->second, reaction, walker->voice);
            }
            it = _pendingReactions.erase(it);
        }
    }

    void PedestrianReplicaService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (Simulation::IsHost() || !replication || !world.IsReady()) {
            Reset(world);
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = _lastUpdate == std::chrono::steady_clock::time_point {} ? 0 : std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastUpdate).count();
        _lastUpdate = now;
        const auto frameMs = static_cast<uint32_t>(std::clamp<int64_t>(elapsed, 1, kMaximumFrameMs));
        AdvanceFades(frameMs);

        SDK::Traffic::CountDownScreamCooldown(frameMs);
        // TickElements never runs here: the horn's cooldown counts down
        // as it would there.
        SDK::Traffic::CountDownCollisionCooldown(frameMs);
        const auto generators = MissionGenerators();
        std::unordered_set<uint64_t> seen;
        std::unordered_set<uint64_t> handedOff;
        const MafiaNet::Time networkNow = MafiaNet::GetTime();
        replication->ForEach<Shared::Entities::PedestrianEntity>([&](Shared::Entities::PedestrianEntity *walker) {
            if (walker->missionGeneration != world.LoadedMissionGeneration() || walker->model.empty()) {
                return;
            }
            const uint64_t id = walker->GetNetworkID();
            if (_handedOff.contains(id)) {
                handedOff.insert(id);
                return;
            }
            seen.insert(id);
            // Back before its element faded out: that element goes on.
            if (!_replicas.contains(id)) {
                Reclaim(id);
            }
            auto &replica = _replicas[id];
            Framework::Utils::TransformSnapshot snapshot {walker->position, walker->velocity, walker->rotation};
            if (!Finite(snapshot)) {
                return;
            }
            snapshot.rotation = glm::normalize(snapshot.rotation);
            // Dated by when the host sampled the pose, not the server pass
            // that relayed it.
            const MafiaNet::Time transformTime = walker->GetLastTransformTime();
            const double clockNow = static_cast<double>(MafiaNet::GetTimeUS()) * 0.001;
            if (replica.snapshots.Empty() || replica.lastTransformTime != transformTime) {
                const auto sampleTime = static_cast<MafiaNet::Time>(std::max(1.0, std::round(replica.clock.Map(walker->sampleClock, clockNow))));
                replica.snapshots.Push(snapshot, sampleTime, clockNow);
                replica.lastTransformTime = transformTime;
                replica.newestTime = static_cast<double>(sampleTime);
                replica.newestVelocity = snapshot.velocity;
            }
            // The host played each animation from the pose it sampled with
            // it, while this copy draws a render delay behind: a clip plays,
            // and a death lays the walker down, once the body gets there.
            replica.animation.Observe({walker->animation, walker->animationFlag, walker->alive}, walker->animationClock, now);
            // The buffer's presentation clock slews as the adaptive delay
            // moves with each packet's jitter; subtracting that delay here
            // instead stepped the walker on every late or early packet.
            const double renderTime = replica.snapshots.RenderTimeMs(clockNow);
            const auto shown = replica.animation.Shown(replica.clock, renderTime, now);
            if (!replica.element && !replica.failed) {
                replica.element = Borrow(generators, *walker, snapshot);
                replica.failed = !replica.element;
                // Streamed in after the host's walker died: it lies where
                // it fell rather than fall again.
                replica.fallen = replica.element && !shown.alive;
            }
            auto *element = replica.element;
            if (!element) {
                return;
            }
            auto *game = SDK::Core::Mission::Get()->Game();
            if (!shown.alive && !replica.dead) {
                replica.dead = true;
                element->Die(game);
            }

            if (replica.appliedAlpha != walker->alpha) {
                replica.appliedAlpha = walker->alpha;
                element->SetAlpha(static_cast<float>(walker->alpha) / 255.0f);
            }
            if (element->HasUmbrella() && !walker->umbrella) {
                element->DropUmbrella();
            }
            // The host's walker waves for a taxi: retail's wave as this
            // replica's extended animation, faded out as EndMavaniNaTaxi
            // fades it. The turns and the breath under it come with the
            // host's animation like any other.
            if (replica.appliedWave != walker->taxiWave) {
                if (replica.appliedWave) {
                    element->EndTaxiWave();
                }
                replica.appliedWave = walker->taxiWave;
                element->ShowTaxiWave(walker->taxiWave);
            }
            // Retail Tick returns at once for a settled corpse (0x452cd0):
            // a later Hit on it there sets a clip it never plays.
            if (replica.settled) {
                return;
            }
            if (replica.reaction && PlayReaction(replica, shown.id, shown.alive, frameMs, now)) {
                return;
            }
            // Held at the extrapolation bound: retail's idle (SetAnim
            // blends to it) until the stream resumes, not a walk on the spot.
            const bool stopped = shown.alive && Player::MovingGait(shown.id) &&
                                 Player::Stalled(renderTime, replica.newestTime, kSnapshotConfig.maxExtrapolationMs, replica.newestVelocity);
            const int32_t animation = stopped ? Player::kStopState : shown.id;
            const bool animationFlag = stopped ? false : shown.flag;
            if (animation != element->CurrentAnimation()) {
                // The host's knockdown takes the collision out as its Hit did
                // there; SetAnim puts it back after the clip queued behind.
                if (const uint32_t delay = Shared::Pedestrian::KnockdownCollisionDelay(animation)) {
                    element->KnockDown(delay);
                }
                element->PlayAnimation(animation, animationFlag);
                replica.holding = false;
            }
            // A fall that ended waits for the host's next clip: restarting
            // it falls again.
            else if (replica.holding && shown.alive && Shared::Pedestrian::KnockdownCollisionDelay(animation) == 0 &&
                     now - replica.holdingSince > kLivingHoldLimit) {
                element->RestartAnimation(animation, animationFlag);
                replica.holding = false;
            }
            Framework::Utils::TransformSnapshot pose;
            const bool sampled = replica.snapshots.Sample(renderTime, pose);
            if (replica.fallen) {
                replica.fallen = false;
                RunToEnd(*element);
                const auto direction = glm::mat3_cast(snapshot.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
                element->SetPose({snapshot.position.x, snapshot.position.y, snapshot.position.z}, {direction.x, 0.0f, direction.z});
                replica.holding = true;
                replica.settled = true;
                element->SettleCorpse(game);
                return;
            }
            if (sampled && !replica.holding) {
                // Root motion moves the element; the host's pose, which
                // already carries the host's root motion, replaces it.
                replica.holding = element->TickAnimation(frameMs);
                replica.holdingSince = now;
                glm::vec3 direction = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
                glm::vec3 position = pose.position;
                // Back from a reaction it played itself: from where that
                // left it onto the host's poses.
                if (auto &handOver = replica.handOver) {
                    if (!handOver->measured) {
                        handOver->measured = true;
                        handOver->offset = handOver->from - pose.position;
                        handOver->since = now;
                        // Too far to slide (the host never applied it): the
                        // host's walker is where it is.
                        if (glm::length(handOver->offset) > kHandOverSnapDistance) {
                            handOver->offset = glm::vec3(0.0f);
                        }
                    }
                    const float remaining = Shared::Pedestrian::HandOverRemaining(std::chrono::duration<float>(now - handOver->since).count());
                    position += handOver->offset * remaining;
                    const glm::vec3 blended = glm::mix(glm::vec3(direction.x, 0.0f, direction.z), glm::vec3(handOver->fromDirection.x, 0.0f, handOver->fromDirection.z), remaining);
                    if (glm::length(blended) > 0.0001f) {
                        direction = blended;
                    }
                    if (remaining <= 0.0f) {
                        handOver.reset();
                    }
                }
                element->SetPose({position.x, position.y, position.z}, {direction.x, 0.0f, direction.z});
                if (!replica.holding) {
                    element->Model()->Tick(static_cast<int>(frameMs));
                    replica.holding = element->CountDownClip(frameMs);
                }
            }
            if (replica.dead && replica.holding && now - replica.holdingSince >= kCorpseSettleDelay) {
                replica.settled = true;
                element->SettleCorpse(game);
            }
        });

        _handedOff = std::move(handedOff);

        auto &cars = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance())->Cars();
        PlayDueReactions(cars);
        DetectThreats(world, cars);

        for (auto it = _replicas.begin(); it != _replicas.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }
            Retire(world, it->first, it->second);
            it = _replicas.erase(it);
        }
    }

    std::optional<Framework::Utils::TransformSnapshot> PedestrianReplicaService::HandOff(World::WorldService &world, uint64_t walkerId) {
        // Gone from the stream a moment before its NPC came: the NPC takes
        // its place instead of fading beside it.
        if (const auto fading = std::find_if(_fading.begin(), _fading.end(), [walkerId](const Fading &copy) { return copy.walkerId == walkerId; }); fading != _fading.end()) {
            Framework::Utils::TransformSnapshot pose {};
            pose.position = {fading->position.x, fading->position.y, fading->position.z};
            pose.rotation = glm::angleAxis(std::atan2(fading->direction.x, fading->direction.z), glm::vec3(0.0f, 1.0f, 0.0f));
            if (world.IsReady() && fading->element->IsActive()) {
                fading->element->Deactivate();
            }
            _fading.erase(fading);
            _handedOff.insert(walkerId);
            return pose;
        }
        const auto found = _replicas.find(walkerId);
        if (found == _replicas.end() || !found->second.element || !found->second.element->IsActive() || !found->second.element->Model()) {
            return std::nullopt;
        }
        auto *model = found->second.element->Model();
        const auto position = model->WorldPosition();
        const auto direction = model->WorldDirection();
        Framework::Utils::TransformSnapshot pose {};
        pose.position = {position.x, position.y, position.z};
        pose.rotation = glm::angleAxis(std::atan2(direction.x, direction.z), glm::vec3(0.0f, 1.0f, 0.0f));
        Release(world, found->second);
        _replicas.erase(found);
        _handedOff.insert(walkerId);
        return pose;
    }

    SDK::Traffic::NativeTrafficElement *PedestrianReplicaService::Borrow(const std::vector<SDK::Traffic::NativeTrafficGenerator *> &generators,
                                                                       Shared::Entities::PedestrianEntity &walker, const Framework::Utils::TransformSnapshot &pose) {
        // The host's own generator first; any pool with the model otherwise.
        std::vector<SDK::Traffic::NativeTrafficGenerator *> order;
        if (walker.generator < generators.size()) {
            order.push_back(generators[walker.generator]);
        }
        for (auto *generator : generators) {
            if (order.empty() || generator != order.front()) {
                order.push_back(generator);
            }
        }
        for (auto *generator : order) {
            for (int32_t index = 0; index < generator->PoolSize(); ++index) {
                auto *element = generator->Element(index);
                if (element->IsActive() || !element->Model() || !SameModel(element->ModelFileName(), walker.model)) {
                    continue;
                }
                const auto direction = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
                auto *model = element->Model();
                model->SetWorldPosition({pose.position.x, pose.position.y, pose.position.z});
                model->SetDirection({direction.x, 0.0f, direction.z});
                model->Update();
                if (!element->ActivateFree()) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Pedestrian replica {} ('{}') found no walkable node at ({:.0f}, {:.0f}, {:.0f})",
                        walker.GetNetworkID(), walker.model, pose.position.x, pose.position.y, pose.position.z);
                    return nullptr;
                }
                element->SetWalkStyle(walker.walkStyle);
                if (walker.umbrella) {
                    element->AttachUmbrella();
                }
                return element;
            }
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("No idle '{}' walker in any pool for pedestrian replica {}", walker.model, walker.GetNetworkID());
        return nullptr;
    }

    void PedestrianReplicaService::Release(World::WorldService &world, Replica &replica) {
        // Deactivate unregisters what ActivateFree registered and returns the
        // element to its pool; after mission teardown there is nothing left.
        if (replica.element && world.IsReady() && replica.element->IsActive()) {
            if (replica.appliedWave) {
                replica.element->EndTaxiWave();
            }
            replica.element->Deactivate();
        }
        replica.element = nullptr;
    }

    void PedestrianReplicaService::Retire(World::WorldService &world, uint64_t walkerId, Replica &replica) {
        auto *element = replica.element;
        // Retail Tick fades a walker leaving its despawn radius only while
        // it is drawn this pass (the model's render stamp is the mission's,
        // 0x45345f), over a second, and deactivates one out of view at once;
        // 359bef9 fades NPC replicas the same way.
        if (!element || !world.IsReady() || !element->IsActive() || !element->Model() ||
            element->Model()->RenderStamp() != SDK::Core::Mission::Get()->RenderStamp()) {
            Release(world, replica);
            return;
        }
        if (replica.appliedWave) {
            element->EndTaxiWave();
        }
        // Nothing moves it any more: a living walker stands rather than
        // walks on the spot (SetAnim blends to the breath).
        const bool living = !replica.dead && !replica.settled;
        if (living && Player::MovingGait(element->CurrentAnimation())) {
            element->PlayAnimation(Player::kStopState, false);
            replica.holding = false;
        }
        const auto position = element->Model()->WorldPosition();
        const auto direction = element->Model()->WorldDirection();
        const bool animating = !replica.settled && !replica.holding;
        replica.element = nullptr;
        _fading.push_back({walkerId, element, std::clamp(element->Alpha(), 0.0f, 1.0f), {position.x, position.y, position.z}, {direction.x, 0.0f, direction.z},
                           animating, std::move(replica)});
    }

    void PedestrianReplicaService::Reclaim(uint64_t walkerId) {
        auto copy = World::TakeFading(_fading, &Fading::walkerId, walkerId);
        if (!copy || !copy->element->IsActive()) {
            return;
        }
        auto &replica = _replicas.emplace(walkerId, std::move(copy->replica)).first->second;
        replica.element = copy->element;
        // Update applies the walker's own opacity again, and its taxi wave,
        // which Retire ended.
        replica.appliedAlpha = -1;
        replica.appliedWave = 0;
    }

    void PedestrianReplicaService::AdvanceFades(uint32_t frameMs) {
        // Retail's rate: the whole opacity in a second (frameTime * 0.001).
        for (auto it = _fading.begin(); it != _fading.end();) {
            auto *element = it->element;
            it->alpha -= static_cast<float>(frameMs) * 0.001f;
            if (!element->IsActive()) {
                it = _fading.erase(it);
                continue;
            }
            if (it->alpha <= 0.0f) {
                element->Deactivate();
                it = _fading.erase(it);
                continue;
            }
            element->SetAlpha(it->alpha);
            // Its breath goes on where it stood, as Update ticks a replica.
            if (it->animating && !element->TickAnimation(frameMs)) {
                element->SetPose({it->position.x, it->position.y, it->position.z}, {it->direction.x, it->direction.y, it->direction.z});
                element->Model()->Tick(static_cast<int>(frameMs));
            }
            ++it;
        }
    }

    void PedestrianReplicaService::Reset(World::WorldService &world) {
        for (auto &[id, replica] : _replicas) {
            Release(world, replica);
        }
        _replicas.clear();
        if (world.IsReady()) {
            for (const auto &copy : _fading) {
                if (copy.element->IsActive()) {
                    copy.element->Deactivate();
                }
            }
        }
        _fading.clear();
        _handedOff.clear();
        _pendingReactions.clear();
        _lastUpdate = {};
    }
} // namespace Mafia1Online::Features::Pedestrian
