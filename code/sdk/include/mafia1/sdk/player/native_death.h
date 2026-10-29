#pragma once

#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <cstddef>

namespace Mafia1Online::SDK::Player {
    inline constexpr uintptr_t kHumanHit                = 0x5762a0;
    inline constexpr uintptr_t kHumanDeath              = 0x570570;
    inline constexpr uintptr_t kActorSetActState        = 0x406da0;
    inline constexpr uintptr_t kHumanForceDeath         = 0x5878d0;
    inline constexpr uintptr_t kHumanCarAirborneDeath   = 0x58a5a0;
    inline constexpr uintptr_t kHumanMovement           = 0x57a710;
    inline constexpr uintptr_t kDeathAnimationSelection = 0x57a630;

    // C_human's animation machine (+0xbc): its current track (+0x2c, -1
    // for none) and active stage (+0x1c). GetActiveAnimTime 0x40e890,
    // __thiscall(machine, unsigned &time, unsigned &duration), ret 8:
    // the duration is the active stage's animation end time.
    inline constexpr uintptr_t kHumanAnimationMachine             = 0xBC;
    inline constexpr uintptr_t kAnimationMachineCurrentTrack      = 0x2C;
    inline constexpr uintptr_t kAnimationMachineActiveStage       = 0x1C;
    inline constexpr uintptr_t kAnimationMachineGetActiveAnimTime = 0x40E890;

    // Hit's sniper death (0x5762a0) sets the model's active stage ahead
    // (to 80 %) once the death clip plays, unless the machine runs a
    // track. This sets it to the clip's end, for a body that died before
    // this copy of it existed. False with a track, whose displacement the
    // jump would leave behind.
    inline bool SkipDeathAnimationToEnd(NativeHuman &human) {
        auto *machine = reinterpret_cast<std::byte *>(&human) + kHumanAnimationMachine;
        if (*reinterpret_cast<const int32_t *>(machine + kAnimationMachineCurrentTrack) != -1) {
            return false;
        }
        uint32_t time     = 0;
        uint32_t duration = 0;
        using GetActiveAnimTime = void(__thiscall *)(void *, uint32_t &, uint32_t &);
        reinterpret_cast<GetActiveAnimTime>(kAnimationMachineGetActiveAnimTime)(machine, time, duration);
        auto *model = human.Actor().Frame();
        reinterpret_cast<Scene::NativeModelVTable *>(model->vtable)->setAnimTime(model, *reinterpret_cast<const uint32_t *>(machine + kAnimationMachineActiveStage), static_cast<int>(duration));
        return true;
    }

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
