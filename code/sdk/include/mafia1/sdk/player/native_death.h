#pragma once

#include <mafia1/sdk/player/native_human.h>

namespace Mafia1Online::SDK::Player {
    inline constexpr uintptr_t kHumanHit                = 0x5762a0;
    inline constexpr uintptr_t kHumanDeath              = 0x570570;
    inline constexpr uintptr_t kActorSetActState        = 0x406da0;
    inline constexpr uintptr_t kHumanForceDeath         = 0x5878d0;
    inline constexpr uintptr_t kHumanCarAirborneDeath   = 0x58a5a0;
    inline constexpr uintptr_t kHumanMovement           = 0x57a710;
    inline constexpr uintptr_t kDeathAnimationSelection = 0x57a630;

    enum class NativeHitType : int32_t {
        Generic       = 0,
        HardKnockdown = 1,
        Melee         = 2,
        Explosion     = 3,
        Pellet        = 4,
        CarImpact     = 5,
        Fall          = 6,
        Burning       = 7,
        Sniper        = 8,
        Direct        = 9,
    };

    // Borrowed typed view for C_human's damage/death methods. All calls must
    // occur on the game thread while the owning mission is alive.
    class NativeHumanDeath final {
      public:
        explicit NativeHumanDeath(NativeHuman &human): _human(human) {}

        bool Hit(NativeHitType type, const Vector3 &direction, const Vector3 &position, const Vector3 &normal, float damage, NativeActor *attacker, uint32_t bodyPart, Scene::NativeFrame *frame) const {
            using Call = bool(__thiscall *)(NativeHuman *, NativeHitType, const Vector3 &, const Vector3 &, const Vector3 &, float, NativeActor *, uint32_t, Scene::NativeFrame *);
            return reinterpret_cast<Call>(kHumanHit)(&_human, type, direction, position, normal, damage, attacker, bodyPart, frame);
        }

      private:
        NativeHuman &_human;
    };
} // namespace Mafia1Online::SDK::Player
