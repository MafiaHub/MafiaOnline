#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::UI {
    inline constexpr uintptr_t kIndicators     = 0x6bf980;
    inline constexpr uintptr_t kSceneCallback  = 0x5bd8d0;
    inline constexpr uintptr_t kPlayerSetLives = 0x5f88e0;
    inline constexpr uintptr_t kConsoleAddText = 0x5f9d50;
    inline constexpr uintptr_t kOutText        = 0x603880;
    inline constexpr uintptr_t kTextSize       = 0x6036d0;
    // G_IndicatorsClass::DrawCursor, void __thiscall(indicators, float x,
    // float y), ret 8. Its only caller is GM_Menu::Draw at 0x5ea8a8.
    inline constexpr uintptr_t kDrawCursor     = 0x604770;

    // G_IndicatorsClass::OutText options.
    inline constexpr uint32_t kTextShadow      = 0x1;
    inline constexpr uint32_t kTextCentered    = 0x2;
    inline constexpr uint32_t kTextRightAlign  = 0x4;
    inline constexpr uint32_t kTextScaleToFit  = 0x10;
    // The HUD font the retail console and chat input use.
    inline constexpr uint32_t kHudFont         = 3;

    inline constexpr uint32_t kLivesFlag          = 0x4;

    struct NativeIndicators {
        void SetLivesVisible() { _flags |= kLivesFlag; }
        void ClearLivesVisible() { _flags &= ~kLivesFlag; }
        [[nodiscard]] float ScaleX() const { return _scaleX; }
        [[nodiscard]] float ScaleY() const { return _scaleY; }
        [[nodiscard]] float MenuOffsetX() const { return _menuOffsetX; }

        std::byte _unused00[0x40a4];
        uint32_t _flags;
        std::byte _unused40a8[0x18];
        float _scaleX;
        float _scaleY;
        std::byte _unused40c8[0xab8];
        float _menuOffsetX;
    };
    static_assert(sizeof(NativeIndicators) == 0x4b84);
    static_assert(offsetof(NativeIndicators, _flags) == 0x40a4);
    static_assert(offsetof(NativeIndicators, _scaleX) == 0x40c0);
    static_assert(offsetof(NativeIndicators, _scaleY) == 0x40c4);
    static_assert(offsetof(NativeIndicators, _menuOffsetX) == 0x4b80);

    inline NativeIndicators &Indicators() {
        return *reinterpret_cast<NativeIndicators *>(kIndicators);
    }

    inline void SetLives(uint32_t health) {
        using Call = void(__thiscall *)(void *, uint32_t);
        reinterpret_cast<Call>(kPlayerSetLives)(&Indicators(), health);
    }

    inline void AddConsoleLine(const char *line, uint32_t color) {
        using Call = void(__thiscall *)(void *, unsigned char *, uint32_t);
        reinterpret_cast<Call>(kConsoleAddText)(&Indicators(), reinterpret_cast<unsigned char *>(const_cast<char *>(line)), color);
    }

    // Screen pixels. Color is 0xAARRGGBB; height is the line height.
    inline void DrawText(const char *line, float x, float y, float width, float height, uint32_t color,
                         uint32_t options = kTextScaleToFit, uint32_t font = kHudFont) {
        using Call = float(__thiscall *)(void *, unsigned char *, float, float, float, float, uint32_t, uint32_t, uint32_t, unsigned char *);
        reinterpret_cast<Call>(kOutText)(&Indicators(), reinterpret_cast<unsigned char *>(const_cast<char *>(line)), x, y, width, height, color, options, font, nullptr);
    }

    // Text with a one pixel drop shadow drawn as a second, darker pass. The
    // font's own shadow option leaves a stray underline once translucent
    // quads have selected the HUD alpha state.
    inline void DrawShadowedText(const char *line, float x, float y, float width, float height, uint32_t color,
                                 uint32_t options = 0, uint32_t font = kHudFont) {
        const uint32_t shadow = (color & 0xff000000u) ? ((((color >> 24) * 3 / 4) << 24) & 0xff000000u) : 0u;
        DrawText(line, x + 1.0f, y + 1.0f, width, height, shadow, options, font);
        DrawText(line, x, y, width, height, color, options, font);
    }

    // Width in pixels of the text OutText draws at this line height. OutText
    // scales the line height by the font's character height scale (font
    // definitions start the indicators object, 0x101c bytes each, scale at
    // +0x08) before TextSize (0x6036d0, ret 0x10) measures it.
    inline float MeasureText(const char *line, float height, uint32_t font = kHudFont, const char *end = nullptr) {
        const auto *definition = reinterpret_cast<const std::byte *>(&Indicators()) + static_cast<size_t>(font) * 0x101c;
        const float characterHeight = height * *reinterpret_cast<const float *>(definition + 0x08);
        using Call = float(__thiscall *)(void *, unsigned char *, float, uint32_t, unsigned char *);
        return reinterpret_cast<Call>(kTextSize)(&Indicators(), reinterpret_cast<unsigned char *>(const_cast<char *>(line)), characterHeight, font,
                                                 reinterpret_cast<unsigned char *>(const_cast<char *>(end)));
    }
} // namespace Mafia1Online::SDK::UI
