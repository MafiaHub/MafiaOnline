#include "map_image.h"

#include <algorithm>
#include <cstring>

namespace Mafia1Online::Features::CityMap {
    namespace {
        constexpr size_t kFileHeaderSize   = 14;
        constexpr uint32_t kInfoHeaderSize = 40;

        // BITMAPINFOHEADER, as LoadImage (LS3DF 0x10079150) copies it.
        struct InfoHeader {
            uint32_t size;
            int32_t width;
            int32_t height;
            uint16_t planes;
            uint16_t bitCount;
            uint32_t compression;
            uint32_t sizeImage;
            int32_t xPelsPerMeter;
            int32_t yPelsPerMeter;
            uint32_t clrUsed;
            uint32_t clrImportant;
        };
        static_assert(sizeof(InfoHeader) == kInfoHeaderSize);

        template <typename T>
        T Read(std::span<const uint8_t> bytes, size_t offset) {
            T value;
            std::memcpy(&value, bytes.data() + offset, sizeof(T));
            return value;
        }

        size_t PaddedStride(uint32_t width, uint32_t bytesPerPixel) {
            return (static_cast<size_t>(width) * bytesPerPixel + 3) & ~static_cast<size_t>(3);
        }

        struct Tap {
            uint32_t index;
            float weight;
        };

        // Destination pixel i covers source [i * scale, (i + 1) * scale); each
        // source pixel weighs its overlap, so the weights of a pixel sum to one.
        struct Taps {
            std::vector<uint32_t> first; // per destination pixel, into taps; one past the end last
            std::vector<Tap> taps;
        };

        Taps BuildTaps(uint32_t source, uint32_t destination) {
            Taps result;
            result.first.reserve(destination + 1);
            const double scale = static_cast<double>(source) / destination;
            for (uint32_t index = 0; index < destination; ++index) {
                result.first.push_back(static_cast<uint32_t>(result.taps.size()));
                const double begin = index * scale;
                const double end   = std::min(static_cast<double>(source), (index + 1) * scale);
                for (auto pixel = static_cast<uint32_t>(begin); pixel < source && pixel < end; ++pixel) {
                    const double overlap = std::min(end, pixel + 1.0) - std::max(begin, static_cast<double>(pixel));
                    if (overlap > 0.0) {
                        result.taps.push_back({pixel, static_cast<float>(overlap / scale)});
                    }
                }
            }
            result.first.push_back(static_cast<uint32_t>(result.taps.size()));
            return result;
        }
    } // namespace

    std::optional<BitmapView> ParseBitmap(std::span<const uint8_t> file) {
        if (file.size() < kFileHeaderSize + kInfoHeaderSize || file[0] != 'B' || file[1] != 'M') {
            return std::nullopt;
        }
        const auto pixelOffset = Read<uint32_t>(file, 10);
        const auto info        = Read<InfoHeader>(file, kFileHeaderSize);
        if (info.size < kInfoHeaderSize || info.planes != 1 || info.compression != 0 || (info.bitCount != 24 && info.bitCount != 32)) {
            return std::nullopt;
        }
        const int64_t height = info.height < 0 ? -static_cast<int64_t>(info.height) : info.height;
        if (info.width < 1 || info.width > static_cast<int32_t>(kMaxSourceSide) || height < 1 || height > kMaxSourceSide) {
            return std::nullopt;
        }
        BitmapView view;
        view.width         = static_cast<uint32_t>(info.width);
        view.height        = static_cast<uint32_t>(height);
        view.bytesPerPixel = info.bitCount / 8u;
        view.stride        = PaddedStride(view.width, view.bytesPerPixel);
        view.bottomUp      = info.height > 0;
        if (pixelOffset < kFileHeaderSize + info.size || pixelOffset > file.size() || file.size() - pixelOffset < view.stride * view.height) {
            return std::nullopt;
        }
        view.pixels = file.data() + pixelOffset;
        return view;
    }

    uint32_t TextureSide(uint32_t side) {
        uint32_t result = 1;
        while (result < side && result < kMaxTextureSide) {
            result *= 2;
        }
        return result;
    }

    std::vector<uint8_t> BuildMemoryBitmap(const BitmapView &source, uint32_t width, uint32_t height) {
        const size_t stride = PaddedStride(width, 3);
        std::vector<uint8_t> result(kInfoHeaderSize + stride * height, 0);
        InfoHeader header {};
        header.size      = kInfoHeaderSize;
        header.width     = static_cast<int32_t>(width);
        header.height    = static_cast<int32_t>(height);
        header.planes    = 1;
        header.bitCount  = 24;
        header.sizeImage = static_cast<uint32_t>(stride * height);
        std::memcpy(result.data(), &header, sizeof(header));

        const Taps columns = BuildTaps(source.width, width);
        const Taps rows    = BuildTaps(source.height, height);
        // Raw pointers: this runs once per mission load on the game thread,
        // and checked iterators would make a debug build stall for seconds.
        const size_t samples = static_cast<size_t>(width) * 3;
        std::vector<float> horizontalBuffer(samples);
        std::vector<float> accumulatedBuffer(samples);
        float *const horizontal     = horizontalBuffer.data();
        float *const accumulated    = accumulatedBuffer.data();
        const uint32_t *columnFirst = columns.first.data();
        const Tap *columnTaps       = columns.taps.data();
        const uint32_t *rowFirst    = rows.first.data();
        const Tap *rowTaps          = rows.taps.data();
        const size_t bytesPerPixel  = source.bytesPerPixel;
        uint8_t *const pixels       = result.data() + kInfoHeaderSize;
        for (uint32_t y = 0; y < height; ++y) {
            std::fill(accumulated, accumulated + samples, 0.0f);
            for (uint32_t rowTap = rowFirst[y]; rowTap < rowFirst[y + 1]; ++rowTap) {
                const uint8_t *const row = source.Row(rowTaps[rowTap].index);
                float *out               = horizontal;
                for (uint32_t x = 0; x < width; ++x, out += 3) {
                    float blue = 0.0f, green = 0.0f, red = 0.0f;
                    for (uint32_t columnTap = columnFirst[x]; columnTap < columnFirst[x + 1]; ++columnTap) {
                        const Tap tap        = columnTaps[columnTap];
                        const uint8_t *pixel = row + tap.index * bytesPerPixel;
                        blue += pixel[0] * tap.weight;
                        green += pixel[1] * tap.weight;
                        red += pixel[2] * tap.weight;
                    }
                    out[0] = blue;
                    out[1] = green;
                    out[2] = red;
                }
                const float weight = rowTaps[rowTap].weight;
                for (size_t index = 0; index < samples; ++index) {
                    accumulated[index] += horizontal[index] * weight;
                }
            }
            // Bottom-up, as LoadImage and BuildImageARGB expect.
            uint8_t *const destination = pixels + stride * (height - 1 - y);
            for (size_t index = 0; index < samples; ++index) {
                const float value  = accumulated[index] + 0.5f;
                destination[index] = static_cast<uint8_t>(value <= 0.0f ? 0.0f : value >= 255.0f ? 255.0f : value);
            }
        }
        return result;
    }
} // namespace Mafia1Online::Features::CityMap
