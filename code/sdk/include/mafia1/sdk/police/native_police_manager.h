#pragma once

#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Police {
    // C_game +0x2da0 is the police manager C_shvestky (0x459d94-0x459d97).
    inline constexpr uintptr_t kGamePoliceManager = 0x2DA0;
    // char __thiscall C_shvestky::TickDoRiciActors(bool force), ret 4. Its
    // retirement pass reads the active camera's world position once and,
    // unless forced, retires an entity whose XZ distance from it is at least
    // 150 (22500 squared): it removes the temporary actor, and C_human::
    // GameDone reactivates the walker it came from at its last pose.
    // C_shvestky::Tick calls it unforced every m_uDoRiciPeriod. Gangsters
    // (CreateMafianos) and recruited police (FizlosNamakatDoAkce) both join
    // this list.
    inline constexpr uintptr_t kTickDoRiciActors = 0x49EA80;
    inline constexpr float kDoRiciKeepDistanceSq = 22500.0f;
    // void __thiscall C_shvestky::AddDoRiciActor(C_actor *), ret 4: appends
    // the actor unless the list already holds it.
    inline constexpr uintptr_t kAddDoRiciActor = 0x49E820;

    // std::vector<C_actor *> m_DoRiciActors at +0x7c: first +0x80, last
    // +0x84, end of storage +0x88 (AddDoRiciActor 0x49e820-0x49e85f).
    struct NativePoliceManager {
        [[nodiscard]] Player::NativeActor **RetirementBegin() const { return _retirementBegin; }
        [[nodiscard]] Player::NativeActor **RetirementEnd() const { return _retirementEnd; }
        [[nodiscard]] size_t RetirementCount() const {
            return _retirementBegin ? static_cast<size_t>(_retirementEnd - _retirementBegin) : 0;
        }
        // Shortens the list to its first count entries without freeing
        // storage; the actors past it stay alive and are re-added with
        // AddRetirement.
        void TruncateRetirement(size_t count) {
            _retirementEnd = _retirementBegin + count;
        }
        void AddRetirement(Player::NativeActor *actor) {
            using Call = void(__thiscall *)(NativePoliceManager *, Player::NativeActor *);
            reinterpret_cast<Call>(kAddDoRiciActor)(this, actor);
        }

        std::byte _unused00[0x80];
        Player::NativeActor **_retirementBegin;
        Player::NativeActor **_retirementEnd;
        Player::NativeActor **_retirementStorageEnd;
    };
    static_assert(offsetof(NativePoliceManager, _retirementBegin) == 0x80);
    static_assert(offsetof(NativePoliceManager, _retirementEnd) == 0x84);
    static_assert(offsetof(NativePoliceManager, _retirementStorageEnd) == 0x88);

    inline NativePoliceManager *PoliceManager(Core::Game::NativeGame *game) {
        return reinterpret_cast<NativePoliceManager *>(reinterpret_cast<std::byte *>(game) + kGamePoliceManager);
    }
} // namespace Mafia1Online::SDK::Police
