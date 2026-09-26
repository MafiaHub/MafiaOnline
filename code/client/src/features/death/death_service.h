#pragma once

#include "shared/features/combat/damage_event.h"
#include "shared/features/combat/fatal_fall_report.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>

namespace Mafia1Online::SDK::Player {
    struct NativeActor;
    struct NativeHuman;
    struct Vector3;
    enum class NativeHitType : int32_t;
} // namespace Mafia1Online::SDK::Player

namespace Mafia1Online::Features::Combat {
    class CombatService;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Death {
    class DeathService final {
      public:
        void Update(World::WorldService &world, Combat::CombatService &combat);
        void RegisterRPC();
        void Reset();
        bool Protect(SDK::Player::NativeActor &actor) const;
        void RestoreLiving(SDK::Player::NativeActor &actor) const;
        void OnSuppressedHit(SDK::Player::NativeHuman &target, SDK::Player::NativeHitType type, const SDK::Player::Vector3 &direction, const SDK::Player::Vector3 &hitPosition, SDK::Player::NativeActor *attacker, uint32_t bodyPart);
        void OnSuppressedMelee(SDK::Player::NativeHuman &target, const SDK::Player::Vector3 &direction, const SDK::Player::Vector3 &hitPosition, SDK::Player::NativeActor *attacker, uint32_t bodyPart);
        void OnSuppressedFatalFall(SDK::Player::NativeHuman &human, Shared::Combat::FatalFallReport::Cause cause);

      private:
        struct AppliedDeath {
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration   = 0;
            uint64_t nativeGeneration  = 0;
        };
        struct PendingDamage {
            Shared::Combat::DamageEvent event;
            std::chrono::steady_clock::time_point received;
        };
        struct AppliedDamage {
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration   = 0;
            uint32_t revision          = 0;
        };

        World::WorldService *_world    = nullptr;
        Combat::CombatService *_combat = nullptr;
        std::unordered_map<uint64_t, AppliedDeath> _applied;
        std::deque<PendingDamage> _pendingDamage;
        std::unordered_map<uint64_t, AppliedDamage> _appliedDamage;
        uint64_t _reportedFallNetworkId  = 0;
        uint64_t _reportedFallGeneration = 0;
        std::chrono::steady_clock::time_point _reportedFallAt;
    };

    bool InstallDeathHooks(DeathService &service);
    void UninstallDeathHooks();
    bool ReplayAuthoritativeDeath(SDK::Player::NativeHuman &human, uint16_t animationId);
    bool ReplayAuthoritativeDamage(SDK::Player::NativeHuman &human, const Shared::Combat::DamageEvent &event);
} // namespace Mafia1Online::Features::Death
