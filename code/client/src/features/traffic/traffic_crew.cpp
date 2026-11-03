#include "traffic_crew.h"

#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/car/native_car.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/traffic/native_traffic_car.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <unordered_set>

namespace Mafia1Online::Features::Traffic {
    namespace {
        // GameInit ticks a fresh occupant model 100 ms into its pose.
        constexpr int kPoseTickMs = 100;
        // Longer than the in-car death animation: its last pose holds.
        constexpr int kDeathTickMs = 5000;
        // C_traffic_car::AI ticks a door animation with the frame time; a
        // stall is not replayed in one step.
        constexpr int kMaximumTickMs = 100;
        constexpr auto kStealHide = std::chrono::seconds(2);
    } // namespace

    void TrafficCrewService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (Simulation::IsHost() || !replication || !world.IsReady()) {
            Reset();
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const int frameMs = _lastUpdate == std::chrono::steady_clock::time_point {}
                                ? 0
                                : std::clamp(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastUpdate).count()), 0, kMaximumTickMs);
        _lastUpdate = now;
        std::unordered_set<uint64_t> seen;
        replication->ForEach<Shared::Entities::CarEntity>([&](Shared::Entities::CarEntity *state) {
            if (state->trafficKey == 0 || state->missionGeneration != world.LoadedMissionGeneration()) {
                return;
            }
            const uint64_t id = state->GetNetworkID();
            auto *car = static_cast<SDK::Car::NativeCar *>(world.NativeObjects().Resolve(world.NativeObjects().FindByNetwork(id)));
            if (!car || !car->Frame() || state->terminalState != Shared::Entities::CarEntity::TerminalState::Active) {
                // Unbound means CarService queued the car's removal; the
                // crew goes first, while the car is still whole.
                if (const auto crew = _crews.find(id); crew != _crews.end()) {
                    Release(crew->second);
                    _crews.erase(crew);
                }
                return;
            }
            seen.insert(id);
            auto &crew = _crews[id];
            if (crew.car != car) {
                Release(crew);
                crew.car = car;
            }
            const bool stealing = now < crew.stealUntil;
            Apply(crew, crew.members[0], 0, state->driverModel, stealing ? CrewState::Gone : state->driverState);
            Apply(crew, crew.members[1], 1, state->passengerModel, stealing ? CrewState::Gone : state->passengerState);
            for (auto &member : crew.members) {
                if (member.animating && member.model && frameMs > 0) {
                    member.model->Tick(frameMs);
                }
            }
        });
        for (auto it = _crews.begin(); it != _crews.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }
            Release(it->second);
            it = _crews.erase(it);
        }
    }

    bool TrafficCrewService::Create(Crew &crew, Member &member, int seat, const std::string &model, bool atEntry) {
        if (member.failedName == model) {
            return false;
        }
        auto *frame = SDK::Scene::GetDriver()->CreateModel();
        if (!frame) {
            member.failedName = model;
            return false;
        }
        auto *scene = SDK::Core::Mission::Get()->GetScene();
        SDK::Player::Vector3 position {};
        SDK::Player::Vector3 direction {0.0f, 0.0f, 1.0f};
        const bool placed = atEntry && SDK::Traffic::DriverEntryPlacement(crew.car, position, direction);
        // The entry starts outside the car, in the world; anything else sits
        // in the car frame.
        if (!SDK::Scene::GetModelCache()->OpenModel(frame, model.c_str()) || !frame->LinkTo(placed ? scene->PrimarySector() : crew.car->Frame())) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Could not load traffic crew model '{}'", model);
            frame->Release();
            member.failedName = model;
            return false;
        }
        if (placed) {
            frame->SetWorldPosition(position);
            frame->SetDirection(direction);
        }
        else {
            SDK::Player::Vector3 inPoint {};
            if (SDK::Traffic::CarInPoint(crew.car, seat, inPoint)) {
                frame->SetLocalPosition(inPoint);
            }
            frame->SetLocalRotation(1.0f, 0.0f, 0.0f, 0.0f);
        }
        SDK::Traffic::DisableShadows(frame);
        frame->SetOn(true);
        frame->Update();
        member.model = frame;
        member.modelName = model;
        member.atEntry = placed;
        return true;
    }

    void TrafficCrewService::Apply(Crew &crew, Member &member, int seat, const std::string &model, CrewState state) {
        if (model.empty() || state == CrewState::None || state == CrewState::Gone) {
            Release(member);
            return;
        }
        // A pedestrian who took a parked car brings its own model.
        if (member.model && member.modelName != model) {
            Release(member);
        }
        if (member.model && member.shown == state) {
            return;
        }
        auto *game = SDK::Core::Mission::Get()->Game();
        // The entry starts at the door; every other pose sits in the seat,
        // so a model left outside by an entry is made again in the seat.
        const bool entry = state == CrewState::Entering;
        if (member.model && (entry || member.atEntry)) {
            Release(member);
        }
        if (!member.model && !Create(crew, member, seat, model, entry)) {
            return;
        }
        if (member.driverMan && state != CrewState::Seated) {
            SDK::Traffic::DelDriverMan(game, member.model);
            member.driverMan = false;
        }
        member.animating = false;
        switch (state) {
        case CrewState::Seated:
            if (seat == 0) {
                SDK::Traffic::AddDriverMan(game, member.model, crew.car);
                member.driverMan = true;
            }
            else if (const char *pose = SDK::Traffic::AnimationName(SDK::Traffic::kPassengerStandAnimation);
                     pose && SDK::Scene::ModelPlayAnimation(member.model, pose, false)) {
                member.model->Tick(kPoseTickMs);
                SDK::Scene::ModelStopAnimation(member.model);
            }
            break;
        case CrewState::Dead:
            if (SDK::Scene::ModelPlayAnimation(member.model, SDK::Traffic::kOccupantDeathAnimation, false)) {
                member.model->Tick(kDeathTickMs);
                SDK::Scene::ModelStopAnimation(member.model);
            }
            break;
        case CrewState::Exiting:
        case CrewState::Entering:
            // Only the driver's door is animated by C_traffic_car.
            if (const char *animation = seat == 0 ? SDK::Traffic::DriverDoorAnimation(crew.car, state == CrewState::Exiting) : nullptr;
                animation && SDK::Scene::ModelPlayAnimation(member.model, animation, false)) {
                member.animating = true;
            }
            break;
        default:
            break;
        }
        member.model->Update();
        member.shown = state;
    }

    void TrafficCrewService::Release(Member &member) {
        if (member.model) {
            if (member.driverMan) {
                SDK::Traffic::DelDriverMan(SDK::Core::Mission::Get()->Game(), member.model);
            }
            member.model->SetOn(false);
            member.model->LinkTo(nullptr);
            member.model->Release();
        }
        const std::string failed = std::move(member.failedName);
        member = {};
        member.failedName = failed;
    }

    void TrafficCrewService::Release(Crew &crew) {
        for (auto &member : crew.members) {
            Release(member);
            member.failedName.clear();
        }
        crew.car = nullptr;
    }

    void TrafficCrewService::Reset() {
        for (auto &[id, crew] : _crews) {
            Release(crew);
        }
        _crews.clear();
        _lastUpdate = {};
    }

    void TrafficCrewService::OnNativeCarDestroyed(void *car) {
        for (auto it = _crews.begin(); it != _crews.end(); ++it) {
            if (it->second.car == car) {
                Release(it->second);
                _crews.erase(it);
                return;
            }
        }
    }

    bool TrafficCrewService::IsCrewFrame(const void *car, SDK::Scene::NativeFrame *frame) const {
        for (const auto &[id, crew] : _crews) {
            if (crew.car != car) {
                continue;
            }
            for (unsigned depth = 0; frame && depth < 64; ++depth, frame = frame->Parent()) {
                for (const auto &member : crew.members) {
                    if (member.model && frame == member.model) {
                        return true;
                    }
                }
            }
            return false;
        }
        return false;
    }

    void TrafficCrewService::HideDriverForSteal(uint64_t carId) {
        if (const auto crew = _crews.find(carId); crew != _crews.end()) {
            crew->second.stealUntil = std::chrono::steady_clock::now() + kStealHide;
        }
    }

    void TrafficCrewService::CancelStealHide(uint64_t carId) {
        if (const auto crew = _crews.find(carId); crew != _crews.end()) {
            crew->second.stealUntil = {};
        }
    }
} // namespace Mafia1Online::Features::Traffic
