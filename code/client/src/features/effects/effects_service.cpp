#include "effects_service.h"

#include "features/world/world_service.h"
#include "shared/features/world/world_effects.h"

#include <core_modules.h>
#include <mafia1/sdk/combat/native_grenade.h>
#include <mafia1/sdk/core/mission.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Effects {
    namespace {
        bool Finite(float x, float y, float z) {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
        }
    } // namespace

    void EffectsService::RegisterRPC(World::WorldService &world) {
        _world       = &world;
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Shared::World::Explosion>([this](const Shared::World::Explosion &explosion, MafiaNet::Packet *) {
            auto *mission = SDK::Core::Mission::Get();
            if (!_world->IsReady() || explosion.missionGeneration != _world->LoadedMissionGeneration() || !mission || !mission->Game()
                || !Finite(explosion.x, explosion.y, explosion.z)) {
                return;
            }
            const float radius = std::clamp(explosion.radius, 0.5f, Shared::World::Explosion::kMaxRadius);
            const float damage = std::clamp(explosion.damage, 0.0f, Shared::World::Explosion::kMaxDamage);
            SDK::Combat::NewExplosion(mission->Game(), {explosion.x, explosion.y, explosion.z}, radius, damage, damage > 0.0f);
        });
        network->RegisterRPC<Shared::World::Fire>([this](const Shared::World::Fire &fire, MafiaNet::Packet *) {
            auto *mission = SDK::Core::Mission::Get();
            if (!_world->IsReady() || fire.missionGeneration != _world->LoadedMissionGeneration() || !mission || !mission->Game() || !Finite(fire.x, fire.y, fire.z)) {
                return;
            }
            const auto life    = static_cast<int32_t>(std::clamp<uint32_t>(fire.lifeMs, 500, Shared::World::Fire::kMaxLifeMs));
            const float radius = std::clamp(fire.radius, 0.5f, Shared::World::Fire::kMaxRadius);
            const float damage = std::clamp(fire.damage, 0.0f, Shared::World::Fire::kMaxDamage);
            SDK::Combat::NewFire(mission->Game(), {fire.x, fire.y, fire.z}, life, radius, damage, damage > 0.0f);
        });
    }
} // namespace Mafia1Online::Features::Effects
