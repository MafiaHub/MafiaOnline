#pragma once

#include <mafianet/bit_stream.h>

#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

// Bit-packed wire forms of the per-tick values (poses, aim, rides). Each
// writer is symmetric: one body serializes both ways. Ranges are chosen so
// the step is a power of two; every Fixed value outside its range, or not
// finite, takes a one-bit escape and travels as a raw float, so a script
// that moves a body off the map still arrives exactly.
namespace Mafia1Online::Shared::Net {
    // A step of (max - min) / (2^bits - 1) over [min, max].
    inline bool SerializeFixed(MafiaNet::BitStream *stream, bool write, float &value, float min, float max, int bits) {
        bool escaped = !(value >= min && value <= max);
        if (!stream->Serialize(write, escaped)) {
            return false;
        }
        if (escaped) {
            return stream->Serialize(write, value);
        }
        const uint32_t top = bits >= 32 ? 0xffffffffu : (1u << bits) - 1u;
        uint32_t quantized = 0;
        if (write) {
            const double unit = (static_cast<double>(value) - min) / (static_cast<double>(max) - min);
            quantized         = static_cast<uint32_t>(std::clamp(std::llround(unit * top), 0ll, static_cast<long long>(top)));
        }
        if (!stream->SerializeBitsFromIntegerRange(write, quantized, 0u, top)) {
            return false;
        }
        if (!write) {
            value = static_cast<float>(min + (static_cast<double>(max) - min) * quantized / top);
        }
        return true;
    }

    // The value Fixed delivers for `value`, so a sender can keep what the
    // receivers will hold (and memcmp-dedupe on it).
    inline float QuantizeFixed(float value, float min, float max, int bits) {
        if (!(value >= min && value <= max)) {
            return value;
        }
        const uint32_t top = (1u << bits) - 1u;
        const double unit  = (static_cast<double>(value) - min) / (static_cast<double>(max) - min);
        return static_cast<float>(min + (static_cast<double>(max) - min) * std::llround(unit * top) / top);
    }

    // An angle in radians, wrapped to [-pi, pi), in 2^bits steps. A
    // non-finite angle reads back as 0.
    inline bool SerializeAngle(MafiaNet::BitStream *stream, bool write, float &radians, int bits) {
        const uint32_t steps = 1u << bits;
        uint32_t quantized   = 0;
        if (write && std::isfinite(radians)) {
            const double turn = (static_cast<double>(radians) + glm::pi<double>()) / glm::two_pi<double>();
            quantized         = static_cast<uint32_t>(std::llround((turn - std::floor(turn)) * steps)) % steps;
        }
        if (!stream->SerializeBitsFromIntegerRange(write, quantized, 0u, steps - 1u)) {
            return false;
        }
        if (!write) {
            radians = static_cast<float>(quantized * glm::two_pi<double>() / steps - glm::pi<double>());
        }
        return true;
    }

    // A unit direction as yaw (atan2(x, z)) and pitch (asin(y)).
    inline bool SerializeDirection(MafiaNet::BitStream *stream, bool write, glm::vec3 &direction, int yawBits, int pitchBits) {
        float yaw   = 0.0f;
        float pitch = 0.0f;
        if (write) {
            const float length = glm::length(direction);
            const glm::vec3 unit = length > 1e-6f && std::isfinite(length) ? direction / length : glm::vec3(0.0f, 0.0f, 1.0f);
            yaw                = std::atan2(unit.x, unit.z);
            pitch              = std::asin(std::clamp(unit.y, -1.0f, 1.0f));
        }
        if (!SerializeAngle(stream, write, yaw, yawBits)) {
            return false;
        }
        float escapedPitch = pitch;
        if (!SerializeFixed(stream, write, escapedPitch, -glm::half_pi<float>(), glm::half_pi<float>(), pitchBits)) {
            return false;
        }
        if (!write) {
            pitch     = escapedPitch;
            direction = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
        }
        return true;
    }

    // A rotation as its three smallest components (the largest is rebuilt,
    // sign fixed positive) over [-1/sqrt2, 1/sqrt2] in `bits` bits each.
    inline bool SerializeQuat(MafiaNet::BitStream *stream, bool write, glm::quat &rotation, int bits) {
        constexpr float kLimit = 0.70710678f;
        uint32_t largest       = 0;
        float parts[3]         = {};
        if (write) {
            glm::quat q = glm::length(rotation) > 1e-6f ? glm::normalize(rotation) : glm::identity<glm::quat>();
            const float values[4] = {q.w, q.x, q.y, q.z};
            for (uint32_t i = 1; i < 4; ++i) {
                if (std::fabs(values[i]) > std::fabs(values[largest])) {
                    largest = i;
                }
            }
            const float sign = values[largest] < 0.0f ? -1.0f : 1.0f;
            for (uint32_t i = 0, out = 0; i < 4; ++i) {
                if (i != largest) {
                    parts[out++] = std::clamp(values[i] * sign, -kLimit, kLimit);
                }
            }
        }
        if (!stream->SerializeBitsFromIntegerRange(write, largest, 0u, 3u)) {
            return false;
        }
        const uint32_t top = (1u << bits) - 1u;
        for (float &part : parts) {
            uint32_t quantized = write ? static_cast<uint32_t>(std::llround((part + kLimit) / (2.0f * kLimit) * top)) : 0;
            if (!stream->SerializeBitsFromIntegerRange(write, quantized, 0u, top)) {
                return false;
            }
            if (!write) {
                part = -kLimit + 2.0f * kLimit * static_cast<float>(quantized) / static_cast<float>(top);
            }
        }
        if (!write) {
            const float rest = std::sqrt(std::max(0.0f, 1.0f - parts[0] * parts[0] - parts[1] * parts[1] - parts[2] * parts[2]));
            float values[4];
            for (uint32_t i = 0, in = 0; i < 4; ++i) {
                values[i] = i == largest ? rest : parts[in++];
            }
            rotation = glm::normalize(glm::quat(values[0], values[1], values[2], values[3]));
        }
        return true;
    }

    // Lost Heaven, from the road graphs (resources/freeride/server/data
    // roads_freeride, roads_freekrajina, roads_freeride_extended): x within
    // -3,028..1,149, z within -801..3,463, y within -26..122. Poses carry x
    // and z over +-8,192 m and y over -512..+1,536 m at 1/64 m, with the
    // Fixed escape for anything a script puts beyond.
    inline constexpr float kWorldHalfExtent = 8192.0f;
    inline constexpr float kWorldMinHeight  = -512.0f;
    inline constexpr float kWorldMaxHeight  = 1536.0f;
    inline constexpr int kWorldPlaneBits    = 20;
    inline constexpr int kWorldHeightBits   = 17;

    inline bool SerializeWorldPosition(MafiaNet::BitStream *stream, bool write, glm::vec3 &position, int planeBits = kWorldPlaneBits,
                                       int heightBits = kWorldHeightBits) {
        return SerializeFixed(stream, write, position.x, -kWorldHalfExtent, kWorldHalfExtent, planeBits) &&
               SerializeFixed(stream, write, position.y, kWorldMinHeight, kWorldMaxHeight, heightBits) &&
               SerializeFixed(stream, write, position.z, -kWorldHalfExtent, kWorldHalfExtent, planeBits);
    }
    // Cars at 1/256 m: a 1/64 m step is up to 8 mm of noise per pose, which
    // at 30 Hz shimmers on a car seen from a few metres.
    inline constexpr int kCarPlaneBits  = 22;
    inline constexpr int kCarHeightBits = 19;
    // A heading about +y, as the yaw-only rotations humans carry.
    inline float YawOf(const glm::quat &rotation) {
        const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        return std::atan2(forward.x, forward.z);
    }

    inline bool SerializeHeading(MafiaNet::BitStream *stream, bool write, glm::quat &rotation, int bits) {
        float yaw = write ? YawOf(rotation) : 0.0f;
        if (!SerializeAngle(stream, write, yaw, bits)) {
            return false;
        }
        if (!write) {
            rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        return true;
    }

    // A walking body's ground motion: its planar velocity over +-16 m/s at
    // 1/32 m/s per axis. Humans back off and strafe, which a speed along
    // the heading would extrapolate forward.
    inline bool SerializePlanarVelocity(MafiaNet::BitStream *stream, bool write, glm::vec3 &velocity) {
        float x = velocity.x;
        float z = velocity.z;
        if (!SerializeFixed(stream, write, x, -16.0f, 16.0f, 10) || !SerializeFixed(stream, write, z, -16.0f, 16.0f, 10)) {
            return false;
        }
        if (!write) {
            velocity = {x, 0.0f, z};
        }
        return true;
    }

    // A linear velocity over +-64 m/s at 1/32 m/s per axis.
    inline bool SerializeVelocity(MafiaNet::BitStream *stream, bool write, glm::vec3 &velocity) {
        return SerializeFixed(stream, write, velocity.x, -64.0f, 64.0f, 12) && SerializeFixed(stream, write, velocity.y, -64.0f, 64.0f, 12) &&
               SerializeFixed(stream, write, velocity.z, -64.0f, 64.0f, 12);
    }
} // namespace Mafia1Online::Shared::Net
