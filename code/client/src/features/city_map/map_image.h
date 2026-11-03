#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Mafia1Online::Features::CityMap {
    // An uncompressed 24- or 32-bit Windows bitmap read in place: its pixel
    // rows stay in the file's bytes.
    struct BitmapView {
        const uint8_t *pixels  = nullptr;
        uint32_t width         = 0;
        uint32_t height        = 0;
        uint32_t bytesPerPixel = 0;
        size_t stride          = 0;
        bool bottomUp          = true;

        // Row y counted from the top of the image.
        const uint8_t *Row(uint32_t y) const {
            return pixels + stride * (bottomUp ? height - 1 - y : y);
        }
    };

    inline constexpr uint32_t kMaxSourceSide = 8192;
    // The texture side the client asks LS3D for: a power of two LS3D does not
    // resample again (ClampTextureSize rounds up to the next one), and at most
    // 2048 so the managed texture and its system copy stay at 16 MB each in a
    // 32-bit process.
    inline constexpr uint32_t kMaxTextureSide = 2048;

    // Nothing for a file that is not a BM bitmap with a 40-byte or larger
    // info header, BI_RGB, 24 or 32 bits, sides of 1..kMaxSourceSide and all
    // of its rows present.
    std::optional<BitmapView> ParseBitmap(std::span<const uint8_t> file);

    // The power of two at least `side`, capped at kMaxTextureSide.
    uint32_t TextureSide(uint32_t side);

    // The image area-averaged to width x height, as ITexture::Create reads a
    // memory image: a 40-byte BITMAPINFOHEADER (24 bits, BI_RGB, positive
    // height) then bottom-up BGR rows padded to four bytes.
    std::vector<uint8_t> BuildMemoryBitmap(const BitmapView &source, uint32_t width, uint32_t height);
} // namespace Mafia1Online::Features::CityMap
