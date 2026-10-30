#pragma once

#include <mafia1/sdk/player/native_actor.h>

#include <cstdint>

namespace Mafia1Online::SDK::Collision {
    inline constexpr uintptr_t kCollision          = 0x647F48;
    inline constexpr uintptr_t kTestLineHStatic    = 0x5C74D0;
    // g_collision::DynErase(tDynamicCollObject *), ret 4: takes a dynamic
    // object out of its grid cell; nothing when it has none.
    inline constexpr uintptr_t kDynErase           = 0x5C38B0;

    inline void DynErase(void *object) {
        using Call = void(__thiscall *)(void *, void *);
        reinterpret_cast<Call>(kDynErase)(reinterpret_cast<void *>(kCollision), object);
    }

    // g_collision::DynInsert(tDynamicCollObject *), ret 4: puts a dynamic
    // object into the grid cell of its position; nothing when it has one.
    inline constexpr uintptr_t kDynInsert          = 0x5C36C0;

    inline void DynInsert(void *object) {
        using Call = void(__thiscall *)(void *, void *);
        reinterpret_cast<Call>(kDynInsert)(reinterpret_cast<void *>(kCollision), object);
    }

    // g_collision::TestLineHStatic tests the static world grid only, not cars
    // or humans. __thiscall(g_collision*, const S_vector& start, const
    // S_vector& direction, S_vector* hit, S_vector* normal, unsigned flags),
    // ret 0x14. Police line of sight passes null outputs and flags 0.
    inline bool StaticLineBlocked(const Player::Vector3 &start, const Player::Vector3 &direction) {
        using Call = void *(__thiscall *)(void *, const Player::Vector3 *, const Player::Vector3 *, Player::Vector3 *, Player::Vector3 *, uint32_t);
        return reinterpret_cast<Call>(kTestLineHStatic)(reinterpret_cast<void *>(kCollision), &start, &direction, nullptr, nullptr, 0) != nullptr;
    }
} // namespace Mafia1Online::SDK::Collision
