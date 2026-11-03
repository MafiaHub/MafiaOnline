#pragma once

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace Mafia1Online::Shared::MapObject {
    inline constexpr size_t kMaxModelLength = 63;
    inline constexpr size_t kMaxObjects     = 4096;
    inline constexpr float kMaxSpeed        = 1000.0f;
    inline constexpr float kMinScale        = 0.01f;
    inline constexpr float kMaxScale        = 100.0f;
    // Distances below this make a move rotation-only; its speed is then
    // radians per second.
    inline constexpr float kMinDistance = 0.0001f;

    // A move from the start pose to the target at a constant speed. A
    // resting object is a segment that is not moving; its pose is the start.
    struct MotionSegment {
        glm::vec3 startPosition {0.0f};
        glm::quat startRotation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 targetPosition {0.0f};
        glm::quat targetRotation {1.0f, 0.0f, 0.0f, 0.0f};
        float speed  = 0.0f;
        bool moving  = false;
    };

    struct MotionPose {
        glm::vec3 position {0.0f};
        glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
        bool done = true;
    };

    // Seconds the segment takes: distance over speed, or for a move that
    // only turns, the turned angle over speed.
    inline float MotionDuration(const MotionSegment &segment) {
        if (!segment.moving || !(segment.speed > 0.0f)) {
            return 0.0f;
        }
        const float distance = glm::length(segment.targetPosition - segment.startPosition);
        if (distance >= kMinDistance) {
            return distance / segment.speed;
        }
        const float cosine = std::min(1.0f, std::abs(glm::dot(segment.startRotation, segment.targetRotation)));
        return 2.0f * std::acos(cosine) / segment.speed;
    }

    inline MotionPose SampleMotion(const MotionSegment &segment, float elapsedSeconds) {
        if (!segment.moving) {
            return {segment.startPosition, segment.startRotation, true};
        }
        const float duration = MotionDuration(segment);
        if (!(duration > 0.0f) || !(elapsedSeconds < duration)) {
            return {segment.targetPosition, segment.targetRotation, true};
        }
        const float t = std::max(0.0f, elapsedSeconds) / duration;
        return {glm::mix(segment.startPosition, segment.targetPosition, t), glm::normalize(glm::slerp(segment.startRotation, segment.targetRotation, t)), false};
    }

    // A bare stock model file name under models\: letters, digits, '_', '-',
    // ' ' and '.', ending in .4ds or .i3d.
    inline bool ValidModelName(std::string_view name) {
        if (name.size() < 5 || name.size() > kMaxModelLength || name.front() == ' ' || name.front() == '.') {
            return false;
        }
        for (char ch : name) {
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.' || ch == ' ')) {
                return false;
            }
        }
        std::string tail(name.substr(name.size() - 4));
        std::transform(tail.begin(), tail.end(), tail.begin(), [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
        return tail == ".4ds" || tail == ".i3d";
    }

    // The name the native model cache opens: it rejects .4ds and swaps any
    // other extension for it (LS3DF I3D_model::Open), so .4ds becomes .i3d.
    // Lower case, so it also keys the mission's model instances.
    inline std::string CacheModelName(std::string_view name) {
        std::string result(name);
        std::transform(result.begin(), result.end(), result.begin(), [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
        if (result.size() >= 4 && result.compare(result.size() - 4, 4, ".4ds") == 0) {
            result.replace(result.size() - 4, 4, ".i3d");
        }
        return result;
    }

    // tables\materials.def has 47 entries. Glass (24) and crash frames (41)
    // make a shot break the hit volume's frame; script objects keep theirs.
    inline constexpr unsigned kMaterialCount = 47;
    inline bool ValidMaterial(unsigned material) {
        return material < kMaterialCount && material != 24 && material != 41;
    }
    inline constexpr unsigned kDefaultMaterial = 4; // Beton (concrete)
} // namespace Mafia1Online::Shared::MapObject
