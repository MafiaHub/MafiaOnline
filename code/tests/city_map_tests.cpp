// The countryside city map: the projection DrawAll is given and the bitmap
// the client hands LS3D.
#include "features/city_map/map_image.h"

#include <mafia1/sdk/ui/map_projection.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Mafia1Online::Features::CityMap;
namespace Map = Mafia1Online::SDK::UI::Map;

namespace {
    int failures = 0;

#define CHECK(condition)                                                                                                                                                                                                                                                               \
    do {                                                                                                                                                                                                                                                                               \
        if (!(condition)) {                                                                                                                                                                                                                                                            \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                                                                                                                                                \
            ++failures;                                                                                                                                                                                                                                                                \
        }                                                                                                                                                                                                                                                                              \
    } while (false)

    bool Near(float a, float b, float tolerance) {
        return std::fabs(a - b) <= tolerance;
    }

    uint32_t Bits(float value) {
        return std::bit_cast<uint32_t>(value);
    }

    void Put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    void Put16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    // A BM file; pixel(x, y) gives B, G, R for row y from the top.
    template <typename Pixel>
    std::vector<uint8_t> Bitmap(int32_t width, int32_t height, uint16_t bits, Pixel pixel) {
        const uint32_t rows = static_cast<uint32_t>(height < 0 ? -height : height);
        const size_t stride = (static_cast<size_t>(width) * (bits / 8) + 3) & ~size_t(3);
        std::vector<uint8_t> file(54 + stride * rows, 0);
        file[0] = 'B';
        file[1] = 'M';
        Put32(file, 2, static_cast<uint32_t>(file.size()));
        Put32(file, 10, 54);
        Put32(file, 14, 40);
        Put32(file, 18, static_cast<uint32_t>(width));
        Put32(file, 22, static_cast<uint32_t>(height));
        Put16(file, 26, 1);
        Put16(file, 28, bits);
        for (uint32_t y = 0; y < rows; ++y) {
            const uint32_t stored = height > 0 ? rows - 1 - y : y;
            for (uint32_t x = 0; x < static_cast<uint32_t>(width); ++x) {
                uint8_t *at          = file.data() + 54 + stride * stored + static_cast<size_t>(x) * (bits / 8);
                const auto [b, g, r] = pixel(x, y);
                at[0]                = b;
                at[1]                = g;
                at[2]                = r;
            }
        }
        return file;
    }

    struct Bgr {
        uint8_t b, g, r;
    };

    // Pixel (x, y from the top) of a memory bitmap BuildMemoryBitmap made.
    Bgr At(const std::vector<uint8_t> &bitmap, uint32_t x, uint32_t y) {
        int32_t width = 0, height = 0;
        std::memcpy(&width, bitmap.data() + 4, 4);
        std::memcpy(&height, bitmap.data() + 8, 4);
        const size_t stride  = (static_cast<size_t>(width) * 3 + 3) & ~size_t(3);
        const uint8_t *pixel = bitmap.data() + 40 + stride * (height - 1 - y) + x * 3;
        return {pixel[0], pixel[1], pixel[2]};
    }

    void Parsing() {
        const auto gradient = [](uint32_t x, uint32_t y) {
            return Bgr {uint8_t(x), uint8_t(y), 7};
        };
        for (const int32_t height : {3, -3}) {
            const auto file = Bitmap(5, height, 24, gradient);
            const auto view = ParseBitmap(file);
            CHECK(view.has_value());
            if (!view)
                continue;
            CHECK(view->width == 5 && view->height == 3 && view->bytesPerPixel == 3 && view->stride == 16);
            CHECK(view->bottomUp == (height > 0));
            // Row 0 is the top row whichever way the file stores it.
            CHECK(view->Row(0)[3] == 1 && view->Row(0)[4] == 0 && view->Row(2)[4] == 2);
        }
        CHECK(ParseBitmap(Bitmap(4, 4, 32, gradient)).has_value());

        auto compressed = Bitmap(4, 4, 24, gradient);
        Put32(compressed, 30, 1);
        CHECK(!ParseBitmap(compressed));
        auto paletted = Bitmap(4, 4, 24, gradient);
        Put16(paletted, 28, 8);
        CHECK(!ParseBitmap(paletted));
        auto truncated = Bitmap(4, 4, 24, gradient);
        truncated.pop_back();
        CHECK(!ParseBitmap(truncated));
        auto wrongMagic = Bitmap(4, 4, 24, gradient);
        wrongMagic[0]   = 'X';
        CHECK(!ParseBitmap(wrongMagic));
        CHECK(!ParseBitmap(std::vector<uint8_t>(20, 0)));
        auto huge = Bitmap(4, 4, 24, gradient);
        Put32(huge, 18, kMaxSourceSide + 1);
        CHECK(!ParseBitmap(huge));
    }

    void Sizes() {
        // fullmap.bmp is 2619 x 3063; LS3D would round both up to 4096.
        CHECK(TextureSide(2619) == 2048 && TextureSide(3063) == 2048);
        CHECK(TextureSide(1024) == 1024 && TextureSide(1025) == 2048 && TextureSide(600) == 1024 && TextureSide(1) == 1);
    }

    void Resampling() {
        const auto file = Bitmap(4, 2, 24, [](uint32_t x, uint32_t y) {
            // Left half 0/100, right half 200/40, per row.
            return x < 2 ? Bgr {uint8_t(y == 0 ? 0 : 100), 10, 20} : Bgr {uint8_t(y == 0 ? 200 : 40), 30, 40};
        });
        const auto view = ParseBitmap(file);
        CHECK(view.has_value());
        if (!view)
            return;

        const auto bitmap = BuildMemoryBitmap(*view, 2, 1);
        int32_t value     = 0;
        std::memcpy(&value, bitmap.data(), 4);
        CHECK(value == 40);
        std::memcpy(&value, bitmap.data() + 4, 4);
        CHECK(value == 2);
        std::memcpy(&value, bitmap.data() + 8, 4);
        CHECK(value == 1);
        uint16_t bits = 0;
        std::memcpy(&bits, bitmap.data() + 14, 2);
        CHECK(bits == 24);
        CHECK(bitmap.size() == 40 + 8);
        // Each output pixel averages a 2 x 2 block.
        CHECK(At(bitmap, 0, 0).b == 50 && At(bitmap, 0, 0).g == 10 && At(bitmap, 0, 0).r == 20);
        CHECK(At(bitmap, 1, 0).b == 120 && At(bitmap, 1, 0).r == 40);

        // Same size keeps every pixel and the top row on top.
        const auto copy = BuildMemoryBitmap(*view, 4, 2);
        CHECK(At(copy, 0, 0).b == 0 && At(copy, 0, 1).b == 100 && At(copy, 3, 0).b == 200 && At(copy, 3, 1).b == 40);

        // Uneven downscale of a flat image stays flat; upscale repeats pixels.
        const auto flat     = Bitmap(7, 5, 32, [](uint32_t, uint32_t) {
            return Bgr {9, 99, 199};
        });
        const auto flatView = ParseBitmap(flat);
        CHECK(flatView.has_value());
        if (flatView) {
            const auto down = BuildMemoryBitmap(*flatView, 4, 2);
            const auto up   = BuildMemoryBitmap(*flatView, 16, 8);
            CHECK(At(down, 3, 1).b == 9 && At(down, 3, 1).g == 99 && At(down, 3, 1).r == 199 && At(down, 0, 0).r == 199);
            CHECK(At(up, 15, 7).g == 99 && At(up, 0, 0).b == 9);
        }

        // fullmap.bmp's own size to the texture the client creates.
        const auto large     = Bitmap(2619, 3063, 24, [](uint32_t x, uint32_t y) {
            return Bgr {uint8_t(x < 1310 ? 30 : 230), 128, uint8_t(y < 1532 ? 60 : 160)};
        });
        const auto largeView = ParseBitmap(large);
        CHECK(largeView.has_value());
        if (largeView) {
            const auto texture = BuildMemoryBitmap(*largeView, TextureSide(largeView->width), TextureSide(largeView->height));
            CHECK(texture.size() == 40 + 2048 * 2048 * 3);
            CHECK(At(texture, 0, 0).b == 30 && At(texture, 0, 0).r == 60 && At(texture, 2047, 2047).b == 230 && At(texture, 2047, 2047).r == 160);
            CHECK(At(texture, 1023, 1023).g == 128 && At(texture, 0, 2047).b == 30 && At(texture, 2047, 0).r == 60);
        }
    }

    void Projections() {
        // The retail constants as derived equal what the image holds.
        const auto derived = Map::Derive(Map::kRetail);
        CHECK(Bits(Map::kRetailDraw.uScale) == 0x39710693u && Bits(Map::kRetailDraw.vScale) == 0xba04fafcu);
        CHECK(Bits(Map::kRetailDraw.uBase) == 0x3f3c1186u && Bits(Map::kRetailDraw.vBase) == 0x3ef0c415u);
        CHECK(Bits(Map::kRetailDraw.halfWindowV) == 0x3e8ccccdu && Bits(Map::kRetailDraw.windowV) == 0x3f0ccccdu);
        CHECK(Bits(Map::kRetailDraw.playerOffsetScale) == 0x3fac37dbu && Bits(Map::kRetailDraw.destinationScale) == 0x3fe8ba2eu);
        CHECK(Bits(Map::kRetailDraw.maxWindowV) == 0x3ee66666u);
        CHECK(Near(derived.halfWindowV, Map::kRetailDraw.halfWindowV, 1e-7f) && Near(derived.maxWindowV, Map::kRetailDraw.maxWindowV, 1e-7f));
        CHECK(Near(derived.playerOffsetScale, Map::kRetailDraw.playerOffsetScale, 1e-6f) && Near(derived.destinationScale, Map::kRetailDraw.destinationScale, 1e-6f));

        // The values MafiaCountrysideMap 2.0 wrote for 1.2 (0x627908..0x627928, 0x623b08).
        const auto countryside = Map::Derive(Map::kCountryside);
        CHECK(Bits(countryside.windowV) == 0x3e3bbbbcu && Bits(countryside.uScale) == 0x39712c28u && Bits(countryside.vScale) == 0xb931a0f7u);
        CHECK(Near(countryside.destinationScale, std::bit_cast<float>(0x40ae8ba2u), 1e-5f));
        CHECK(Near(countryside.halfWindowV, 0.0916667f, 1e-6f) && Near(countryside.maxWindowV, 0.8166667f, 1e-6f));
        CHECK(Near(countryside.playerOffsetScale, 0.74f / 0.18333334f, 1e-5f));
        // The ASI kept the 0.275 retail half window and put the base at 0.868
        // to compensate; the centred base shows every point where it did.
        for (const float z : {-801.0f, 0.0f, 1500.0f, 3462.0f}) {
            const float asiCentre = z * -0.0001694f + 0.868f - 0.275f + 0.18333334f * 0.5f;
            CHECK(Near(Map::ToTexture(Map::kCountryside, 0.0f, z).v, asiCentre, 1e-6f));
        }

        // Every road of the three Free Ride maps lies on the countryside
        // image; the stock countryside does not on the retail one.
        struct Bounds {
            float minX, maxX, minZ, maxZ;
        };
        for (const Bounds bounds : {Bounds {-3027, 995, -801, 730}, Bounds {-2531, 1149, 669, 3462}, Bounds {-3027, 1149, -801, 3462}}) {
            for (const float x : {bounds.minX, bounds.maxX}) {
                for (const float z : {bounds.minZ, bounds.maxZ}) {
                    const auto point = Map::ToTexture(Map::kCountryside, x, z);
                    CHECK(point.u >= 0.0f && point.u <= 1.0f && point.v >= 0.0f && point.v <= 1.0f);
                }
            }
        }
        CHECK(Map::ToTexture(Map::kRetail, 0.0f, 3462.0f).v < 0.0f);

        // The window stays on the texture and is centred where it can be.
        const auto middle = Map::WindowOrigin(Map::kCountryside, {0.5f, 0.5f});
        CHECK(Near(middle.u, 0.3f, 1e-6f) && Near(middle.v, 0.5f - 0.18333334f * 0.5f, 1e-6f));
        const auto corner = Map::WindowOrigin(Map::kCountryside, {1.2f, 0.99f});
        CHECK(Near(corner.u, 0.6f, 1e-6f) && Near(corner.v, countryside.maxWindowV, 1e-6f));
        const auto top = Map::WindowOrigin(Map::kRetail, {-0.5f, 0.1f});
        CHECK(top.u == 0.0f && top.v == 0.0f);
    }
} // namespace

int main() {
    Parsing();
    Sizes();
    Resampling();
    Projections();
    if (failures) {
        std::printf("%d city map check(s) failed\n", failures);
        return 1;
    }
    std::printf("City map tests passed\n");
    return 0;
}
