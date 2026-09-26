#pragma once

#include <mafia1/sdk/core/game.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Sound {
    // C_game::Play3DSound: int __thiscall(C_game*, I3D_frame* parent, const
    // char* file, I3D_SOUNDTYPE, S_vector position by value, float min, float
    // max, float volume, bool fadeIn, float fadeSpeed, bool loop), ret 0x30.
    // The game owns the sound: C_game::Tick releases it once it stops, and
    // C_game::ClearEffects releases the rest at C_game::Done.
    inline constexpr uintptr_t kPlay3DSound = 0x5aca70;
    // C_game::Stop3DSound(int id, bool fade, float fadeSpeed), ret 0xc.
    inline constexpr uintptr_t kStop3DSound = 0x5acc00;
    // C_I3D_sound_cache::Open(I3D_sound*, const char* file, unsigned, void*,
    // void*, void*), ret 0x18, on sSoundCache. It tries "Sounds\\" + file.
    inline constexpr uintptr_t kSoundCacheOpen = 0x408d90;
    inline constexpr uintptr_t kSoundCache     = 0x647da8;

    inline constexpr int32_t kTypePoint   = 1;
    inline constexpr int32_t kTypeAmbient = 3;
    // Play3DSound's C_GAME_SOUND_RANGE_CURVE and _MIN_VOLUME arguments.
    inline constexpr float kRangeCurve     = 0.4f;
    inline constexpr float kRangeMinVolume = 0.3f;

    // I3D_sound (LS3DF vtable 0x1009ce78) extends I3D_frame; slots proven by
    // Play3DSound's own calls at 0x5acab4..0x5acb7d and PLAYSOUNDEX at 0x472148.
    struct NativeSound;
    struct NativeSoundVTable {
        Scene::NativeFrameVTable frame;
        void *_unused3c[(0x54 - 0x3c) / sizeof(void *)];
        bool(__stdcall *isPlaying)(NativeSound *, bool);
        void(__stdcall *setSoundType)(NativeSound *, int32_t);
        void(__stdcall *setRange)(NativeSound *, float, float, float, float);
        void(__stdcall *setCone)(NativeSound *, float, float);
        void(__stdcall *setOutVolume)(NativeSound *, float);
        void(__stdcall *setVolume)(NativeSound *, float);
        void(__stdcall *setLoop)(NativeSound *, bool);
        void *_unused70[(0x88 - 0x70) / sizeof(void *)];
        void(__stdcall *setOnUpdate)(NativeSound *, bool, bool);
    };
    static_assert(offsetof(NativeSoundVTable, isPlaying) == 0x54);
    static_assert(offsetof(NativeSoundVTable, setSoundType) == 0x58);
    static_assert(offsetof(NativeSoundVTable, setRange) == 0x5c);
    static_assert(offsetof(NativeSoundVTable, setCone) == 0x60);
    static_assert(offsetof(NativeSoundVTable, setOutVolume) == 0x64);
    static_assert(offsetof(NativeSoundVTable, setVolume) == 0x68);
    static_assert(offsetof(NativeSoundVTable, setLoop) == 0x6c);
    static_assert(offsetof(NativeSoundVTable, setOnUpdate) == 0x88);

    struct NativeSound {
        NativeSoundVTable *vtable;

        Scene::NativeFrame *Frame() {
            return reinterpret_cast<Scene::NativeFrame *>(this);
        }
        bool IsPlaying() {
            return vtable->isPlaying(this, false);
        }
        void SetSoundType(int32_t type) {
            vtable->setSoundType(this, type);
        }
        void SetRange(float minimum, float maximum) {
            vtable->setRange(this, minimum, maximum, kRangeCurve, kRangeMinVolume);
        }
        void SetOmnidirectional() {
            constexpr float kTwoPi = 6.2831855f;
            vtable->setCone(this, kTwoPi, kTwoPi);
        }
        void SetOutVolume(float volume) {
            vtable->setOutVolume(this, volume);
        }
        void SetVolume(float volume) {
            vtable->setVolume(this, volume);
        }
        void SetLoop(bool loop) {
            vtable->setLoop(this, loop);
        }
        // STOPSOUND and C_fire use SetOn(false, true) to stop the voice.
        void Stop() {
            vtable->setOnUpdate(this, false, true);
        }
    };

    inline NativeSound *CreateSound() {
        return reinterpret_cast<NativeSound *>(Scene::GetDriver()->vtable->createFrame(Scene::GetDriver(), 4));
    }

    inline bool OpenWave(NativeSound *sound, const char *file) {
        using Call = int(__thiscall *)(void *, NativeSound *, const char *, uint32_t, void *, void *, void *);
        return reinterpret_cast<Call>(kSoundCacheOpen)(reinterpret_cast<void *>(kSoundCache), sound, file, 0, nullptr, nullptr, nullptr) == 0;
    }

    inline int32_t Play3DSound(Core::Game::NativeGame *game, const char *file, int32_t type, Player::Vector3 position, float minimum, float maximum, float volume, bool loop) {
        using Call = int32_t(__thiscall *)(Core::Game::NativeGame *, void *, const char *, int32_t, Player::Vector3, float, float, float, bool, float, bool);
        return reinterpret_cast<Call>(kPlay3DSound)(game, nullptr, file, type, position, minimum, maximum, volume, false, 0.001f, loop);
    }

    inline void Stop3DSound(Core::Game::NativeGame *game, int32_t id) {
        using Call = void(__thiscall *)(Core::Game::NativeGame *, int32_t, bool, float);
        reinterpret_cast<Call>(kStop3DSound)(game, id, false, 0.001f);
    }
} // namespace Mafia1Online::SDK::Sound
