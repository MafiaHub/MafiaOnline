#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Core {
    // Retail Game.exe's packed S_game_setup. WinMain loads this from the
    // registry before InitSystem and may save it again when the game exits.
#pragma pack(push, 1)
    struct GameSetup {
        uint8_t version;
        uint8_t adapter;
        int32_t width;
        int32_t height;
        int32_t bitDepth;
        int32_t refreshRate;
        uint8_t antialias;
        uint8_t multipass;
        uint8_t clipAlways;
        uint8_t fullscreen;
        uint8_t hardwareTnl;
        uint8_t vsync;
        uint8_t textureLowDetail;
        uint8_t tripleBuffer;
        uint8_t lightmapTruecolor;
        uint8_t textureCompressed;
        uint8_t textureTruecolor;
        uint8_t soundDisabled;
        uint8_t disableEax;
        uint8_t suspendInactive;
        uint8_t softwareMixing;
        uint8_t soundDevice;
        int32_t language;
        uint8_t wBuffer;
        float textureLodBias;
        float lightmapLodBias;
        int32_t textureQuality;
        int32_t textureSize;
        int32_t lightmapQuality;
        int32_t lightmapSize;
    };
#pragma pack(pop)

    static_assert(sizeof(GameSetup) == 0x3f);
    static_assert(offsetof(GameSetup, width) == 0x02);
    static_assert(offsetof(GameSetup, height) == 0x06);
    static_assert(offsetof(GameSetup, fullscreen) == 0x15);
    static_assert(offsetof(GameSetup, suspendInactive) == 0x1f);

    // Valid after the fixed-base retail executable has been verified.
    inline constexpr uintptr_t kInitSettings = 0x647ee4;

    inline GameSetup &InitSettings() {
        return *reinterpret_cast<GameSetup *>(kInitSettings);
    }
} // namespace Mafia1Online::SDK::Core
