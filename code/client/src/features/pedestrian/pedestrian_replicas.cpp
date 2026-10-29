#include <utils/safe_win32.h>

#include "pedestrian_replicas.h"

#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"
#include "shared/features/pedestrian/pedestrian_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/traffic/native_traffic.h>
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

        SDK::Traffic::CountDownScreamCooldown(frameMs);
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
            auto &replica = _replicas[id];
            Framework::Utils::TransformSnapshot snapshot {walker->position, walker->velocity, walker->rotation};
            if (!Finite(snapshot)) {
                return;
            }
            snapshot.rotation = glm::normalize(snapshot.rotation);
            // Dated by when the host sampled the pose, not the server pass
            // that relayed it.
            const MafiaNet::Time transformTime = walker->GetLastTransformTime();
            if (replica.snapshots.Empty() || replica.lastTransformTime != transformTime) {
                const double clockNow = static_cast<double>(MafiaNet::GetTimeUS()) * 0.001;
                replica.snapshots.Push(snapshot, static_cast<MafiaNet::Time>(std::max(1.0, std::round(replica.clock.Map(walker->sampleClock, clockNow)))), clockNow);
                replica.lastTransformTime = transformTime;
            }
            if (!replica.element && !replica.failed) {
                replica.element = Borrow(generators, *walker, snapshot);
                replica.failed = !replica.element;
                // Streamed in after the host's walker died: it lies where
                // it fell rather than fall again.
                replica.fallen = replica.element && !walker->alive;
            }
            auto *element = replica.element;
            if (!element) {
                return;
            }
            auto *game = SDK::Core::Mission::Get()->Game();
            if (!walker->alive && !replica.dead) {
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
            if (walker->animation != element->CurrentAnimation()) {
                element->PlayAnimation(walker->animation, walker->animationFlag);
                replica.holding = false;
            }
            else if (replica.holding && walker->alive && now - replica.holdingSince > kLivingHoldLimit) {
                element->RestartAnimation(walker->animation, walker->animationFlag);
                replica.holding = false;
            }
            Framework::Utils::TransformSnapshot pose;
            // The buffer's presentation clock slews as the adaptive delay
            // moves with each packet's jitter; subtracting that delay here
            // instead stepped the walker on every late or early packet.
            const bool sampled = replica.snapshots.Sample(replica.snapshots.RenderTimeMs(static_cast<double>(MafiaNet::GetTimeUS()) * 0.001), pose);
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
                const auto direction = glm::mat3_cast(pose.rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
                element->SetPose({pose.position.x, pose.position.y, pose.position.z}, {direction.x, 0.0f, direction.z});
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

        for (auto it = _replicas.begin(); it != _replicas.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }
            Release(world, it->second);
            it = _replicas.erase(it);
        }
    }

    std::optional<Framework::Utils::TransformSnapshot> PedestrianReplicaService::HandOff(World::WorldService &world, uint64_t walkerId) {
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

    void PedestrianReplicaService::Reset(World::WorldService &world) {
        for (auto &[id, replica] : _replicas) {
            Release(world, replica);
        }
        _replicas.clear();
        _handedOff.clear();
        _lastUpdate = {};
    }
} // namespace Mafia1Online::Features::Pedestrian
