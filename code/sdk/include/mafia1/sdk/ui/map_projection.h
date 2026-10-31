#pragma once

#include <algorithm>
#include <cstdint>

namespace Mafia1Online::SDK::UI::Map {
    // How G_IndicatorsClass::DrawAll (0x5fb060) maps the world onto the city
    // map texture: u = x * uScale + uBase, v = z * vScale + vBase, and the
    // window of windowU x windowV texture units it shows around the followed
    // car or the camera.
    struct Projection {
        float uScale  = 0.0f;
        float uBase   = 0.0f;
        float vScale  = 0.0f;
        float vBase   = 0.0f;
        float windowU = 0.0f;
        float windowV = 0.0f;
    };

    // Retail 0mapar.bmp, the city only. The scales and bases are the .rdata
    // floats DrawAll reads (0x627928, 0x627920, 0x627924, 0x62791c); the
    // window is 0x6236c0 (0.4, shared) and 0x623b08 (0.55, shared).
    inline constexpr Projection kRetail {0.00022986f, 0.73464239f, -0.00050728f, 0.47024599f, 0.4f, 0.55f};

    // fullmap.bmp of MafiaCountrysideMap 2.0 (2016): the city at the bottom
    // and the countryside north of it, three times the retail height in world
    // metres. The ASI wrote u scale 0.00023, v scale -0.0001694 and a 0.55 / 3
    // window, and moved v base to 0.868 to make up for leaving the retail
    // half window (0.275) in place; centred properly that base is
    // 0.868 - 0.55 / 3, which also lines the image up with the stock road
    // graphs of FREERIDE and FREEKRAJINA.
    inline constexpr Projection kCountryside {0.00023f, 0.73464239f, -0.0001694f, 0.68466663f, 0.4f, 0.18333334f};

    // The map's on-screen size as fractions of the menu area (DrawAll's
    // 0.1..0.9 and 0.13..0.87 ratios).
    inline constexpr float kScreenWidthRatio  = 0.8f;
    inline constexpr float kScreenHeightRatio = 0.74f;

    struct Point {
        float u = 0.0f;
        float v = 0.0f;
    };

    inline Point ToTexture(const Projection &projection, float worldX, float worldZ) {
        return {worldX * projection.uScale + projection.uBase, worldZ * projection.vScale + projection.vBase};
    }

    // The top-left texture coordinate DrawAll shows: the point (clamped to
    // the texture) less half a window, kept inside the texture.
    inline Point WindowOrigin(const Projection &projection, Point texture) {
        const float u = std::clamp(texture.u, 0.0f, 1.0f);
        const float v = std::clamp(texture.v, 0.0f, 1.0f);
        return {std::clamp(u - projection.windowU * 0.5f, 0.0f, 1.0f - projection.windowU), std::clamp(v - projection.windowV * 0.5f, 0.0f, 1.0f - projection.windowV)};
    }

    // The floats the vertical half of DrawAll's map reads. The horizontal
    // window is the same 0.4 in both projections, so its shared constants stay.
    struct DrawConstants {
        float uScale            = 0.0f; // 0x627928
        float vScale            = 0.0f; // 0x627924
        float uBase             = 0.0f; // 0x627920
        float vBase             = 0.0f; // 0x62791c
        float halfWindowV       = 0.0f; // 0x627918
        float windowV           = 0.0f; // 0x623b08, pooled with unrelated AI code
        float playerOffsetScale = 0.0f; // 0x627910: 0.74 / windowV
        float destinationScale  = 0.0f; // 0x627908: 1 / windowV
        float maxWindowV        = 0.0f; // immediate at 0x5ff84c: 1 - windowV
    };

    inline constexpr DrawConstants Derive(const Projection &projection) {
        return {projection.uScale, projection.vScale, projection.uBase, projection.vBase, projection.windowV * 0.5f, projection.windowV, kScreenHeightRatio / projection.windowV, 1.0f / projection.windowV, 1.0f - projection.windowV};
    }

    // The retail values bit for bit, as the image holds them.
    inline constexpr DrawConstants kRetailDraw {0.00022986f, -0.00050728f, 0.73464239f, 0.47024599f, 0.275f, 0.55f, 1.3454546f, 1.8181818f, 0.45f};
} // namespace Mafia1Online::SDK::UI::Map
