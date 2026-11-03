#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Profile {
    inline constexpr uintptr_t kLoadSave = 0x6d4628;
    inline constexpr uintptr_t kSelect = 0x605570;
    inline constexpr uintptr_t kCreate = 0x6055c0;
    inline constexpr uintptr_t kDelete = 0x605740;
    inline constexpr uintptr_t kSave = 0x6059f0;
    inline constexpr uintptr_t kSaveGame = 0x606ea0;
    // G_LoadSaveClass::PushBindingsToInput, thiscall: C_Input::Reset, then
    // BindKey for each of the 60 m_Bindings slots (0x607a60).
    inline constexpr uintptr_t kPushBindingsToInput = 0x607a60;
    // G_LoadSaveClass::Refresh(unsigned mask), thiscall, ret 4: applies
    // m_ControlSettings to the sound driver, the IShow music and the
    // vehicle sound gain (0x60dfe0).
    inline constexpr uintptr_t kRefresh = 0x60dfe0;
    inline constexpr uint32_t kRefreshSoundVolume   = 0x01;
    inline constexpr uint32_t kRefreshMusicVolume   = 0x02;
    inline constexpr uint32_t kRefreshVehicleVolume = 0x20;
    inline constexpr int kBindingCount = 60;

    // reM shared/GameKey.h; the field order is the profile ABI C_Input reads.
    struct NativeGameKey {
        uint32_t direction;
        uint16_t device;
        uint16_t code;
        uint32_t deviceIndex;
    };
    static_assert(sizeof(NativeGameKey) == 0x0c);

    // reM G_ControlSettings. Speech and the mouse values are read live by
    // C_human and C_player; the volumes need Refresh.
    struct NativeControlSettings {
        float steeringLinearity;
        float particleEffects;
        float cameraInertiaPower;
        float cameraInertiaScale;
        float unused10;
        float mouseSensitivityX;
        float mouseSensitivityY;
        float mouseSpeedLimitX;
        float soundVolume;
        float musicVolume;
        float carVolume;
        float speechVolume;
        uint32_t pedestrianCameraMode;
        uint32_t carCameraMode;
        float gamma;
        float forceFeedback[6];
        uint32_t indicatorMode;
        bool useMph;
        bool doubleTapSideRoll;
        bool invertMouseY;
        bool subtitles;
    };
    static_assert(sizeof(NativeControlSettings) == 0x5c);
    static_assert(offsetof(NativeControlSettings, mouseSensitivityX) == 0x14);
    static_assert(offsetof(NativeControlSettings, soundVolume) == 0x20);
    static_assert(offsetof(NativeControlSettings, speechVolume) == 0x2c);
    static_assert(offsetof(NativeControlSettings, indicatorMode) == 0x54);
    static_assert(offsetof(NativeControlSettings, invertMouseY) == 0x5a);

    struct NativeProfileHeader {
        std::byte _data[0x54];
    };
    static_assert(sizeof(NativeProfileHeader) == 0x54);

    // G_LoadSaveClass begins with MSVC's retail vector storage. It is valid
    // during ProfileEnumFiles..ProfileEnumRelease in the startup picker only.
    struct NativeLoadSave {
        [[nodiscard]] bool HasProfile(uint32_t index) const {
            return _begin && index < static_cast<uint32_t>(_end - _begin);
        }

        void SelectProfile(uint32_t index) {
            using Call = void(__thiscall *)(NativeLoadSave *, uint32_t);
            reinterpret_cast<Call>(kSelect)(this, index);
        }

        // The loaded profile's bindings and settings; ProfileLoad fills them
        // during native Init and the mod's write guards keep them off disk.
        [[nodiscard]] NativeGameKey *Bindings() {
            return _bindings;
        }
        [[nodiscard]] NativeControlSettings &Controls() {
            return _controls;
        }

        void PushBindingsToInput() {
            reinterpret_cast<void(__thiscall *)(NativeLoadSave *)>(kPushBindingsToInput)(this);
        }

        // The music bit reaches g_pMission->m_pGame; callers pass it only
        // while the mission has its game.
        void Refresh(uint32_t mask) {
            reinterpret_cast<void(__thiscall *)(NativeLoadSave *, uint32_t)>(kRefresh)(this, mask);
        }

        void *_allocator;
        NativeProfileHeader *_begin;
        NativeProfileHeader *_end;
        NativeProfileHeader *_capacity;
        std::byte _unused10[0x1f4 - 0x10];
        NativeGameKey _bindings[kBindingCount];
        NativeControlSettings _controls;
    };
    static_assert(offsetof(NativeLoadSave, _begin) == 0x04);
    static_assert(offsetof(NativeLoadSave, _end) == 0x08);
    static_assert(offsetof(NativeLoadSave, _bindings) == 0x1f4);
    static_assert(offsetof(NativeLoadSave, _controls) == 0x4c4);

    inline NativeLoadSave &Get() {
        return *reinterpret_cast<NativeLoadSave *>(kLoadSave);
    }
} // namespace Mafia1Online::SDK::Profile
