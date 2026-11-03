#pragma once

#include <mafia1/sdk/core/game.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/sound/native_sound.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::World::Fire {
    // C_ParticleManager, C_mission::m_pParticleManager (+0x6c), as
    // C_game::AddHydrant reads it (0x5b9c87).
    inline constexpr size_t kMissionParticleManagerOffset = 0x6c;
    // int __thiscall C_ParticleManager::CreateParticle(int effectId, const
    // S_vector& position, const S_vector& direction, int typeSelector,
    // I3D_frame* parent), ret 0x14: -1 for an effect id outside
    // tables\effects.tbl. With a parent, an effect whose transform flags are
    // set takes position in the parent's space, and a type flagged
    // ROTATE_DIRECTION (8) or ROTATE_SPAWN_OFFSET (0x10) turns its velocity
    // or spawn spread by the parent's world matrix, scale included
    // (S_vector::RotateByMatrix is the plain 3x3 product).
    inline constexpr uintptr_t kCreateParticle = 0x449690;
    // void __thiscall C_ParticleManager::DestroyParticle(int guid), ret 4;
    // an unknown guid is ignored (FindGuid).
    inline constexpr uintptr_t kDestroyParticle = 0x44a240;
    // C_sound_table::ConstructSound(I3D_sound*, unsigned id) on g_SoundManager.
    inline constexpr uintptr_t kConstructSound = 0x55f820;
    inline constexpr uintptr_t kSoundManager   = 0x646f50;
    // int __thiscall C_game::Play3DSound(I3D_sound*), ret 4: switches the
    // sound on and hands it to the game, which releases it once it stopped
    // (C_game::NewFire plays its ignition, sound table entry 21, this way).
    inline constexpr uintptr_t kPlayOwnedSound = 0x5accb0;
    // void __thiscall C_game::SpawnFireFrameSlot(const S_vector& position,
    // float size, int timeMs, short type), ret 0x10. Takes the highest of
    // the ten pooled lights (C_game +0x2a48, 0xc each) whose time ran out,
    // or none; C_game::NewFire passes (position, 10, lifeTime, 1). Type
    // bit 1 flickers the light and fades it over its last 500 ms; C_game::Tick
    // switches it off at 0.
    inline constexpr uintptr_t kSpawnFireFrameSlot = 0x5b5b40;
    inline constexpr size_t kFireFrameSlotsOffset = 0x2a48;
    inline constexpr int kFireFrameSlotCount      = 10;
    inline constexpr float kFireLightSize         = 10.0f;
    inline constexpr int32_t kFireLightFadeMs     = 500;

    // tables\effects.tbl, in EFFECTS.TXT order (LoadData appends them in
    // file order). C_fire uses 21, C_game::AddHydrant 87.
    inline constexpr int32_t kEffectFire          = 6;  // "Fire": flames and smoke, no decay
    inline constexpr int32_t kEffectFireSmallFade = 22; // "fire_small_fading": dies down by itself
    inline constexpr int32_t kEffectSteam         = 28; // "smoke_parnik": the steamer's steam
    inline constexpr int32_t kEffectHitWater      = 43; // "hit_water": a splash
    inline constexpr int32_t kEffectSlowSmoke     = 55; // "slow_smoke"
    inline constexpr int32_t kEffectFireNoSmoke   = 57; // "Fire_no_smoke"
    inline constexpr int32_t kEffectHose          = 89; // "hadice": 5 m/s along the parent's x axis, gravity, 2 s
    // The hose effect's own speed (its type direction (5, 0, 0)).
    inline constexpr float kHoseEffectSpeed = 5.0f;
    // Sound table: C_fire's loop (C_FIRE_LOOP_SOUND_ID), NewFire's ignition
    // (ConstructSound(sound, 21) before Play3DSound), the hydrant jet's loop (AddHydrant pushes 0x147).
    inline constexpr uint32_t kSoundFireLoop  = 20;
    inline constexpr uint32_t kSoundIgnition  = 21;
    inline constexpr uint32_t kSoundWaterLoop = 327;

    struct NativeFireFrameSlot {
        Scene::NativeFrame *light;
        int32_t timeRemaining;
        int16_t type;
        uint16_t padding;
    };
    static_assert(sizeof(NativeFireFrameSlot) == 0x0c);
    static_assert(offsetof(NativeFireFrameSlot, timeRemaining) == 0x04);

    inline void *ParticleManager() {
        return *reinterpret_cast<void **>(reinterpret_cast<std::byte *>(Core::Mission::Get()) + kMissionParticleManagerOffset);
    }

    inline int32_t CreateParticle(int32_t effect, const Player::Vector3 &position, const Player::Vector3 &direction, Scene::NativeFrame *parent) {
        using Call = int32_t(__thiscall *)(void *, int32_t, const Player::Vector3 *, const Player::Vector3 *, int32_t, Scene::NativeFrame *);
        return reinterpret_cast<Call>(kCreateParticle)(ParticleManager(), effect, &position, &direction, 0, parent);
    }

    inline void DestroyParticle(int32_t guid) {
        if (guid < 0) {
            return;
        }
        reinterpret_cast<void(__thiscall *)(void *, int32_t)>(kDestroyParticle)(ParticleManager(), guid);
    }

    // A sound frame with a sound table entry loaded; the caller owns it.
    inline Sound::NativeSound *CreateTableSound(uint32_t id) {
        auto *sound = Sound::CreateSound();
        if (!sound) {
            return nullptr;
        }
        using Call = bool(__thiscall *)(void *, Sound::NativeSound *, uint32_t);
        if (!reinterpret_cast<Call>(kConstructSound)(reinterpret_cast<void *>(kSoundManager), sound, id)) {
            sound->Frame()->Release();
            return nullptr;
        }
        return sound;
    }

    inline void PlayOwnedSound(Core::Game::NativeGame *game, Sound::NativeSound *sound) {
        reinterpret_cast<int32_t(__thiscall *)(Core::Game::NativeGame *, Sound::NativeSound *)>(kPlayOwnedSound)(game, sound);
    }

    inline NativeFireFrameSlot *FireFrameSlots(Core::Game::NativeGame *game) {
        return reinterpret_cast<NativeFireFrameSlot *>(reinterpret_cast<std::byte *>(game) + kFireFrameSlotsOffset);
    }

    // Retail's own fires and explosions take a slot when they start and get
    // no light when none is free; script fires leave them this many.
    inline constexpr int kFireFrameSlotsKeptForRetail = 4;

    // The retail fire light at position for timeMs: the slot it took, or
    // -1 when no more than the retail share is unlit.
    inline int SpawnFireLight(Core::Game::NativeGame *game, const Player::Vector3 &position, int32_t timeMs) {
        auto *slots = FireFrameSlots(game);
        int free    = -1;
        int unlit   = 0;
        for (int index = kFireFrameSlotCount - 1; index >= 0; --index) {
            if (slots[index].timeRemaining < 1) {
                free = free < 0 ? index : free;
                ++unlit;
            }
        }
        if (free < 0 || unlit <= kFireFrameSlotsKeptForRetail) {
            return -1;
        }
        using Call = void(__thiscall *)(Core::Game::NativeGame *, const Player::Vector3 *, float, int32_t, int16_t);
        reinterpret_cast<Call>(kSpawnFireFrameSlot)(game, &position, kFireLightSize, timeMs, 1);
        return slots[free].timeRemaining == timeMs ? free : -1;
    }
} // namespace Mafia1Online::SDK::World::Fire
