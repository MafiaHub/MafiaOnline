#include "road_barriers.h"

#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"
#include "shared/features/traffic/road_barrier_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/traffic/native_traffic_car.h>
#include <networking/replication/replication_manager.h>

#include <glm/glm.hpp>

namespace Mafia1Online::Features::Traffic {
    void RoadBarrierService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        // The host's police place their own native barriers.
        if (Simulation::IsHost() || !replication || !world.IsReady()) {
            Reset(world);
            return;
        }
        if (_missionGeneration != world.LoadedMissionGeneration()) {
            OnMissionClosing();
            _missionGeneration = world.LoadedMissionGeneration();
        }
        std::unordered_set<uint64_t> seen;
        replication->ForEach<Shared::Entities::RoadBarrierEntity>([&](Shared::Entities::RoadBarrierEntity *barrier) {
            if (barrier->missionGeneration != _missionGeneration) {
                return;
            }
            const uint64_t id = barrier->GetNetworkID();
            seen.insert(id);
            if (!_live.contains(id) && !_failed.contains(id)) {
                Create(id);
            }
        });
        for (auto it = _live.begin(); it != _live.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }
            Remove(world, it->second);
            it = _live.erase(it);
        }
        std::erase_if(_failed, [&](uint64_t id) { return !seen.contains(id); });
    }

    void RoadBarrierService::Create(uint64_t networkId) {
        const auto *barrier = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::RoadBarrierEntity>(networkId);
        auto *mission = SDK::Core::Mission::Get();
        auto *frame = SDK::Scene::GetDriver()->CreateModel();
        if (!frame) {
            _failed.insert(networkId);
            return;
        }
        // C_shvestky's pool: the model, named as retail names it.
        if (!SDK::Scene::GetModelCache()->OpenModel(frame, Shared::Entities::RoadBarrierEntity::kModel)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Could not load road block barrier model");
            frame->Release();
            _failed.insert(networkId);
            return;
        }
        frame->SetName("road_bl_I");
        // GameInit takes the collision from the world matrix, so the frame is
        // posed first. Retail barriers stand upright (SetDir, roll 0).
        const glm::vec3 forward = glm::normalize(barrier->placedRotation * glm::vec3(0.0f, 0.0f, 1.0f));
        frame->SetWorldPosition({barrier->placedPosition.x, barrier->placedPosition.y, barrier->placedPosition.z});
        frame->SetDirection({forward.x, forward.y, forward.z});
        frame->Update();
        auto *actor = mission->CreateActor(SDK::Player::NativeActor::Type::Barrier);
        if (!actor || !actor->Initialize(frame)) {
            if (actor) {
                actor->Release();
            }
            frame->Release();
            _failed.insert(networkId);
            return;
        }
        // The actor owns the creation reference from here; ~C_actor releases
        // it once GameDone has unlinked the frame.
        actor->SetRemoveFrame(true);
        // GameInit runs here with the frame still unlinked, so GameDone
        // returns it to no parent. The road block spawn then deactivates and
        // activates it into its sector.
        mission->Game()->AddTemporaryActor(actor);
        SDK::Traffic::DeactivateBarrier(actor);
        SDK::Traffic::ActivateBarrier(actor);
        _live[networkId] = actor;
    }

    void RoadBarrierService::Remove(World::WorldService &world, SDK::Player::NativeActor *actor) {
        // GameDone erases the collision and C_game frees the actor on its tick.
        if (world.IsReady() && _missionGeneration == world.LoadedMissionGeneration()) {
            SDK::Core::Mission::Get()->Game()->RemoveTemporaryActor(actor);
        }
    }

    void RoadBarrierService::OnMissionClosing() {
        _live.clear();
        _failed.clear();
    }

    void RoadBarrierService::Reset(World::WorldService &world) {
        for (const auto &[id, actor] : _live) {
            Remove(world, actor);
        }
        _live.clear();
        _failed.clear();
    }

    void RoadBarrierService::OnNativeDestroyed(void *actor) {
        std::erase_if(_live, [actor](const auto &entry) { return entry.second == actor; });
    }
} // namespace Mafia1Online::Features::Traffic
