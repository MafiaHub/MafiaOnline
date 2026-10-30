#pragma once

#include "shared/features/combat/damage_event.h"
#include "shared/features/combat/fatal_fall_report.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <unordered_map>
#include <unordered_set>

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
        // The local car just reported running into this NPC's replica: play
        // the impact reaction now (retail C_human::Collision hits at once);
        // the server's damage for that car then only sets health. A player
        // shows the knockdown its own client played (PlayerService).
        void PredictVehicleImpact(uint64_t carId, uint64_t targetId, const SDK::Player::Vector3 &direction);

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
        // The life and native each human was last seen alive on. One that
        // dies on another lies where it fell instead of falling again.
        std::unordered_map<uint64_t, AppliedDeath> _seenAlive;
        // Killed, the death held for its moment (an NPC's replica, a blow
        // still to land): kept protected until it plays.
        std::unordered_set<uint64_t> _dying;
        std::deque<PendingDamage> _pendingDamage;
        std::unordered_map<uint64_t, AppliedDamage> _appliedDamage;
        struct PredictedImpact {
            uint64_t carId = 0;
            uint64_t targetId = 0;
            float directionX = 0.0f;
            float directionY = 0.0f;
            float directionZ = 0.0f;
        };
        // Reported this frame, played on the next Update outside physics.
        std::deque<PredictedImpact> _impactsToPlay;
        // When each (car, target) last played a predicted impact. A health
        // change is never predicted; only the reaction plays early.
        std::map<std::pair<uint64_t, uint64_t>, std::chrono::steady_clock::time_point> _predictedImpacts;
        uint64_t _reportedFallNetworkId  = 0;
        uint64_t _reportedFallGeneration = 0;
        std::chrono::steady_clock::time_point _reportedFallAt;
    };

    bool InstallDeathHooks(DeathService &service);
    void UninstallDeathHooks();
    bool ReplayAuthoritativeDeath(SDK::Player::NativeHuman &human, uint16_t animationId);
    bool ReplayAuthoritativeDamage(SDK::Player::NativeHuman &human, const Shared::Combat::DamageEvent &event);
    // A server-validated hit applied through the retail C_human::Hit with its
    // real damage and attacker; the Hit hook does not suppress it.
    bool ApplyAuthoritativeHit(SDK::Player::NativeHuman &human, SDK::Player::NativeHitType type, const SDK::Player::Vector3 &direction, const SDK::Player::Vector3 &position, float damage,
                               SDK::Player::NativeActor *attacker, uint32_t bodyPart);
} // namespace Mafia1Online::Features::Death
