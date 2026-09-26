#include "death_service.h"

#include "features/combat/combat_service.h"
#include "features/world/world_service.h"
#include "shared/features/combat/fatal_fall_report.h"
#include "shared/features/combat/hit_report.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <mafia1/sdk/player/native_death.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>

namespace Mafia1Online::Features::Death {
    using SDK::Player::NativeActor;
    using SDK::Player::NativeHitType;
    using SDK::Player::NativeHuman;
    using SDK::Player::Vector3;

    void DeathService::RegisterRPC() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Combat::DamageEvent>([this](const Shared::Combat::DamageEvent &event, MafiaNet::Packet *) {
            if (event.targetNetworkId == 0 || event.missionGeneration == 0 || event.targetSpawnGeneration == 0 || event.targetRevision == 0 || event.bodyPart < 1 ||
                (event.bodyPart > 6 && event.bodyPart != Shared::Combat::HitReport::kInCarBodyPart)) {
                return;
            }
            _pendingDamage.push_back({event, std::chrono::steady_clock::now()});
            if (_pendingDamage.size() > 256) {
                _pendingDamage.pop_front();
            }
        });
    }

    void DeathService::Update(World::WorldService &world, Combat::CombatService &combat) {
        _world  = &world;
        _combat = &combat;
        if (!world.IsReady()) {
            _applied.clear();
            _pendingDamage.clear();
            _appliedDamage.clear();
            return;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        for (auto it = _pendingDamage.begin(); it != _pendingDamage.end();) {
            const auto &event = it->event;
            if (event.missionGeneration < world.LoadedMissionGeneration() || now - it->received > std::chrono::seconds(2)) {
                it = _pendingDamage.erase(it);
                continue;
            }
            const auto *state = combat.GetState(event.targetNetworkId);
            if (!state || state->missionGeneration < event.missionGeneration || (state->missionGeneration == event.missionGeneration && state->spawnGeneration < event.targetSpawnGeneration)
                || (state->missionGeneration == event.missionGeneration && state->spawnGeneration == event.targetSpawnGeneration && state->revision < event.targetRevision)) {
                ++it;
                continue;
            }
            if (state->missionGeneration != event.missionGeneration || state->spawnGeneration != event.targetSpawnGeneration || !state->spawned || !state->alive) {
                it = _pendingDamage.erase(it);
                continue;
            }
            const auto handle = world.NativeObjects().FindByNetwork(event.targetNetworkId);
            auto *actor       = static_cast<NativeActor *>(world.NativeObjects().Resolve(handle));
            if (!actor) {
                ++it;
                continue;
            }
            if (actor->GetType() != NativeActor::Type::Player && actor->GetType() != NativeActor::Type::Entity) {
                it = _pendingDamage.erase(it);
                continue;
            }
            auto &applied = _appliedDamage[event.targetNetworkId];
            if (applied.missionGeneration == event.missionGeneration && applied.spawnGeneration == event.targetSpawnGeneration && applied.revision >= event.targetRevision) {
                it = _pendingDamage.erase(it);
                continue;
            }
            auto *human = static_cast<NativeHuman *>(static_cast<void *>(actor));
            if (ReplayAuthoritativeDamage(*human, event)) {
                human->SetHealth(state->health);
                actor->RestoreLivingState();
                applied = {event.missionGeneration, event.targetSpawnGeneration, event.targetRevision};
                it      = _pendingDamage.erase(it);
            }
            else {
                ++it;
            }
        }
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            const uint64_t networkId = player->GetNetworkID();
            const auto *state        = combat.GetState(networkId);
            if (!state || !state->spawned || state->missionGeneration != world.LoadedMissionGeneration() || state->spawnGeneration != player->spawnGeneration) {
                _applied.erase(networkId);
                return;
            }
            const auto handle = world.NativeObjects().FindByNetwork(networkId);
            auto *actor       = static_cast<NativeActor *>(world.NativeObjects().Resolve(handle));
            if (!actor || (actor->GetType() != NativeActor::Type::Player && actor->GetType() != NativeActor::Type::Entity)) {
                return;
            }
            if (state->alive) {
                _applied.erase(networkId);
                if (!actor->IsAlive() || actor->IsDead()) {
                    RestoreLiving(*actor);
                }
                return;
            }
            const AppliedDeath applied {state->missionGeneration, state->spawnGeneration, handle.generation};
            const auto it = _applied.find(networkId);
            if (it != _applied.end() && it->second.missionGeneration == applied.missionGeneration && it->second.spawnGeneration == applied.spawnGeneration && it->second.nativeGeneration == applied.nativeGeneration) {
                return;
            }
            auto *human = static_cast<NativeHuman *>(static_cast<void *>(actor));
            if (!actor->IsDead()) {
                human->SetHealth(std::max(human->Health(), 1.0f));
                actor->RestoreLivingState();
                if (ReplayAuthoritativeDeath(*human, state->deathAnimation == 0 ? 134 : state->deathAnimation)) {
                    _applied[networkId] = applied;
                }
            }
            else {
                _applied[networkId] = applied;
            }
        });
    }

    void DeathService::Reset() {
        _applied.clear();
        _pendingDamage.clear();
        _appliedDamage.clear();
        _world                  = nullptr;
        _combat                 = nullptr;
        _reportedFallNetworkId  = 0;
        _reportedFallGeneration = 0;
        _reportedFallAt         = {};
    }

    bool DeathService::Protect(NativeActor &actor) const {
        if (!_world || !_world->IsReady() || (actor.GetType() != NativeActor::Type::Player && actor.GetType() != NativeActor::Type::Entity)) {
            return false;
        }
        const auto handle = _world->NativeObjects().FindByNative(&actor);
        // Once the server has killed this spawn, the replayed Hit must finish
        // natively: C_actor::Tick calls Death on the next frame and the death
        // animation ends in SetActState(2). Respawn creates a new actor.
        if (const auto *state = handle.networkId && _combat ? _combat->GetState(handle.networkId) : nullptr; state && state->spawned && !state->alive) {
            return false;
        }
        return handle.networkId != 0 || SDK::Player::CurrentPlayer() == &actor;
    }

    void DeathService::RestoreLiving(NativeActor &actor) const {
        if (!Protect(actor)) {
            return;
        }
        const auto handle = _world->NativeObjects().FindByNative(&actor);
        const auto *state = _combat && handle.networkId ? _combat->GetState(handle.networkId) : nullptr;
        auto *human       = static_cast<NativeHuman *>(static_cast<void *>(&actor));
        human->SetHealth(state && state->alive ? std::max(state->health, 1.0f) : std::max(human->Health(), 1.0f));
        actor.RestoreLivingState();
    }

    void DeathService::OnSuppressedHit(NativeHuman &target, NativeHitType type, const Vector3 &direction, const Vector3 &hitPosition, NativeActor *attacker, uint32_t bodyPart) {
        if (!_world || !_combat || attacker != SDK::Player::CurrentPlayer() || bodyPart > 6) {
            return;
        }
        if (type == NativeHitType::HardKnockdown) {
            // C_human::AI's melee sweep: torso (5) or head (6), hit type 1.
            OnSuppressedMelee(target, direction, hitPosition, attacker, bodyPart);
            return;
        }
        if (type != NativeHitType::Generic) {
            return;
        }
        auto reportedPart = static_cast<uint8_t>(bodyPart);
        if (bodyPart == 0) {
            // A seated human's collision is off; bullets reach it only through
            // the car's Hit, which passes zero flags as the body part.
            const auto *seated = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(&target.Actor()));
            if (!seated->usedActorEnter || _world->NativeObjects().FindByNative(seated->usedActorEnter).networkId == 0) {
                return;
            }
            reportedPart = Shared::Combat::HitReport::kInCarBodyPart;
        }
        const auto shooter = _world->NativeObjects().FindByNative(attacker);
        const auto victim  = _world->NativeObjects().FindByNative(&target.Actor());
        if (shooter.networkId == 0 || victim.networkId == 0 || shooter.networkId == victim.networkId) {
            return;
        }
        const auto *shooterState = _combat->GetLocalState(*_world);
        const auto *targetState  = _combat->GetState(victim.networkId);
        if (!shooterState || !targetState || shooterState->networkId != shooter.networkId || !shooterState->alive || !targetState->alive) {
            return;
        }
        const auto pellet = _combat->MatchLocalPellet(direction.x, direction.y, direction.z, hitPosition.x, hitPosition.y, hitPosition.z);
        if (!pellet) {
            return;
        }
        Shared::Combat::HitReport report;
        report.shooterNetworkId       = shooter.networkId;
        report.targetNetworkId        = victim.networkId;
        report.missionGeneration      = shooterState->missionGeneration;
        report.shooterSpawnGeneration = shooterState->spawnGeneration;
        report.targetSpawnGeneration  = targetState->spawnGeneration;
        report.shotSequence           = pellet->shotSequence;
        report.pelletIndex            = pellet->pelletIndex;
        report.bodyPart               = reportedPart;
        report.directionX             = direction.x;
        report.directionY             = direction.y;
        report.directionZ             = direction.z;
        report.hitX                  = hitPosition.x;
        report.hitY                  = hitPosition.y;
        report.hitZ                  = hitPosition.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
    }

    void DeathService::OnSuppressedMelee(NativeHuman &target, const Vector3 &direction, const Vector3 &hitPosition, NativeActor *attacker, uint32_t bodyPart) {
        const auto sequence = _combat->OpenLocalMelee();
        if (!sequence || (bodyPart != 5 && bodyPart != 6)) {
            return;
        }
        const auto attackerHandle = _world->NativeObjects().FindByNative(attacker);
        const auto victim         = _world->NativeObjects().FindByNative(&target.Actor());
        const auto *attackerState = _combat->GetLocalState(*_world);
        const auto *targetState   = victim.networkId ? _combat->GetState(victim.networkId) : nullptr;
        if (attackerHandle.networkId == 0 || victim.networkId == 0 || attackerHandle.networkId == victim.networkId || !attackerState ||
            !targetState || attackerState->networkId != attackerHandle.networkId || !attackerState->alive || !targetState->alive) {
            return;
        }
        // The sweep direction is the swing segment, not a unit vector.
        const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        Shared::Combat::HitReport report;
        report.kind                   = Shared::Combat::HitReport::Kind::Melee;
        report.shooterNetworkId       = attackerHandle.networkId;
        report.targetNetworkId        = victim.networkId;
        report.missionGeneration      = attackerState->missionGeneration;
        report.shooterSpawnGeneration = attackerState->spawnGeneration;
        report.targetSpawnGeneration  = targetState->spawnGeneration;
        report.shotSequence           = *sequence;
        report.bodyPart               = static_cast<uint8_t>(bodyPart);
        report.directionX             = length > 0.0001f ? direction.x / length : 0.0f;
        report.directionY             = length > 0.0001f ? direction.y / length : 0.0f;
        report.directionZ             = length > 0.0001f ? direction.z / length : 1.0f;
        report.hitX                   = hitPosition.x;
        report.hitY                   = hitPosition.y;
        report.hitZ                   = hitPosition.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
    }

    void DeathService::OnSuppressedFatalFall(NativeHuman &human, Shared::Combat::FatalFallReport::Cause cause) {
        if (!_world || !_combat || !_world->IsReady() || SDK::Player::CurrentPlayer() != &human.Actor()) {
            return;
        }
        const auto handle = _world->NativeObjects().FindByNative(&human.Actor());
        const auto *state = _combat->GetLocalState(*_world);
        const auto now    = std::chrono::steady_clock::now();
        if (!state || handle.networkId != state->networkId || !state->spawned || !state->alive || (_reportedFallNetworkId == state->networkId && _reportedFallGeneration == state->spawnGeneration && now - _reportedFallAt < std::chrono::seconds(3))) {
            return;
        }
        _reportedFallNetworkId  = state->networkId;
        _reportedFallGeneration = state->spawnGeneration;
        _reportedFallAt         = now;
        Shared::Combat::FatalFallReport report;
        report.networkId         = state->networkId;
        report.missionGeneration = state->missionGeneration;
        report.spawnGeneration   = state->spawnGeneration;
        report.cause             = cause;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
    }
} // namespace Mafia1Online::Features::Death
