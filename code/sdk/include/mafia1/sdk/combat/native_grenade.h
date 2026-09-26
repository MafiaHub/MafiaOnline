#pragma once

#include <mafia1/sdk/core/game.h>
#include <mafia1/sdk/player/native_actor.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Combat {
    // C_game::NewGrenade: C_grenade* __thiscall(C_game*, int type, S_vector&
    // position, S_vector& impulse, bool activate, I3D_frame* frame), ret 0x14.
    // Retail throws call it from the throw animation's notify 41 at 0x56de53.
    // Types: 0 grenade on a 5000 ms fuse, 3 Molotov on collision (the item
    // id selects 0 for the grenade, 15, and 3 for everything else).
    inline constexpr uintptr_t kNewGrenade = 0x5AC580;
    // void __thiscall(C_game*, C_actor* source, S_vector& position, float
    // radius, float damage, bool affectWorld, bool largeParticle, bool smoke,
    // int soundId), ret 0x20. C_grenade::AI passes (this, frame position,
    // 15, 400, true, false, true, 18).
    inline constexpr uintptr_t kNewExplosion = 0x5AAE10;
    // void __thiscall(C_game*, S_vector& position, int count), ret 8.
    inline constexpr uintptr_t kNewSmallFires = 0x5ABB90;
    // void __thiscall(C_game*, S_vector& position, I3D_frame* parent, int
    // lifeMs, float radius, float damage, bool damageActors), ret 0x18.
    inline constexpr uintptr_t kNewFire = 0x5ABE10;
    // C_grenade::AI, vtable 0x623d70 slot 13: void __thiscall(C_grenade*,
    // unsigned frameTime), ret 4. Detonates through NewExplosion/NewFire.
    inline constexpr uintptr_t kGrenadeAI = 0x4452C0;
    // Scalar deleting destructor, vtable slot 16: __thiscall(C_grenade*,
    // unsigned flags), ret 4.
    inline constexpr uintptr_t kGrenadeDestructor = 0x445290;

    inline constexpr int32_t kGrenadeType = 0;
    inline constexpr int32_t kMolotovType = 3;
    inline constexpr float kExplosionRadius = 15.0f;
    inline constexpr float kExplosionDamage = 400.0f;
    inline constexpr int32_t kExplosionSound = 18;
    inline constexpr int32_t kSmallFireCount = 4;
    inline constexpr int32_t kFireLifeMs = 5000;
    inline constexpr float kFireRadius = 2.5f;
    inline constexpr float kFireDamage = 50.0f;

    // reM C_grenade (0x160 bytes): detonation triggers after C_bottle.
    struct NativeGrenade {
        Player::NativeActor &Actor() {
            return *reinterpret_cast<Player::NativeActor *>(this);
        }
        // A display copy never detonates; the thrower's client decides.
        void Disarm() {
            _timedDetonation = false;
            _detonateOnCollision = false;
        }
        // reM C_bottle::AI uses this to skip dynamic actors in TestLineV.
        // A display copy must not bounce off an observer's interpolated ped.
        void IgnoreDynamicCollision() {
            _ignoreDynamicCollision = true;
        }

        std::byte _unused00[0x134];
        bool _ignoreDynamicCollision;
        std::byte _unused135[0x138 - 0x135];
        bool _timedDetonation;
        std::byte _unused139[3];
        int32_t _detonationTime;
        bool _detonateOnCollision;
        bool _createExplosion;
        std::byte _unused142[0x14c - 0x142];
        bool _createFire;
        std::byte _unused14d[0x160 - 0x14d];
    };
    static_assert(sizeof(NativeGrenade) == 0x160);
    static_assert(offsetof(NativeGrenade, _ignoreDynamicCollision) == 0x134);
    static_assert(offsetof(NativeGrenade, _timedDetonation) == 0x138);
    static_assert(offsetof(NativeGrenade, _detonationTime) == 0x13c);
    static_assert(offsetof(NativeGrenade, _detonateOnCollision) == 0x140);
    static_assert(offsetof(NativeGrenade, _createExplosion) == 0x141);
    static_assert(offsetof(NativeGrenade, _createFire) == 0x14c);

    inline NativeGrenade *NewGrenade(Core::Game::NativeGame *game, int32_t type, Player::Vector3 position, Player::Vector3 impulse, bool activate) {
        using Call = NativeGrenade *(__thiscall *)(Core::Game::NativeGame *, int32_t, Player::Vector3 *, Player::Vector3 *, bool, void *);
        return reinterpret_cast<Call>(kNewGrenade)(game, type, &position, &impulse, activate, nullptr);
    }
    inline void NewExplosion(Core::Game::NativeGame *game, Player::Vector3 position) {
        using Call = void(__thiscall *)(Core::Game::NativeGame *, void *, Player::Vector3 *, float, float, bool, bool, bool, int32_t);
        reinterpret_cast<Call>(kNewExplosion)(game, nullptr, &position, kExplosionRadius, kExplosionDamage, true, false, true, kExplosionSound);
    }
    // affectWorld false skips every actor, traffic, glass and crash-object
    // hit and keeps only the particles, smoke, sound and light.
    inline void NewExplosion(Core::Game::NativeGame *game, Player::Vector3 position, float radius, float damage, bool affectWorld) {
        using Call = void(__thiscall *)(Core::Game::NativeGame *, void *, Player::Vector3 *, float, float, bool, bool, bool, int32_t);
        reinterpret_cast<Call>(kNewExplosion)(game, nullptr, &position, radius, damage, affectWorld, false, true, kExplosionSound);
    }
    inline void NewFire(Core::Game::NativeGame *game, Player::Vector3 position, int32_t lifeMs, float radius, float damage, bool damageActors) {
        using Fire = void(__thiscall *)(Core::Game::NativeGame *, Player::Vector3 *, void *, int32_t, float, float, bool);
        reinterpret_cast<Fire>(kNewFire)(game, &position, nullptr, lifeMs, radius, damage, damageActors);
    }
    inline void NewMolotovFire(Core::Game::NativeGame *game, Player::Vector3 position) {
        using SmallFires = void(__thiscall *)(Core::Game::NativeGame *, Player::Vector3 *, int32_t);
        using Fire = void(__thiscall *)(Core::Game::NativeGame *, Player::Vector3 *, void *, int32_t, float, float, bool);
        reinterpret_cast<SmallFires>(kNewSmallFires)(game, &position, kSmallFireCount);
        reinterpret_cast<Fire>(kNewFire)(game, &position, nullptr, kFireLifeMs, kFireRadius, kFireDamage, true);
    }
} // namespace Mafia1Online::SDK::Combat
