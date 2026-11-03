#pragma once

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Mafia1Online::Features::Railway {
    // A railway's front on its track (reM C_railway::Place): the point
    // m_vFrontPosition and the web path node m_BackPathNodes[0] it moves
    // straight toward. On reaching it (within kNodeReached, or past it, the
    // angle to the rail direction at or over 90 degrees) it heads for the
    // node's last forward link, so the track ahead of any front is fixed by
    // the web path every client loads alike.
    struct TrackPoint {
        glm::vec3 position {0.0f};
        uint32_t node = 0;
    };

    // Walks the track of a web path. `Graph` provides
    //   glm::vec3 Position(uint32_t node) const;
    //   std::optional<uint32_t> Next(uint32_t node) const;      // forward link
    //   std::optional<uint32_t> Previous(uint32_t node) const;  // back link
    template <typename Graph>
    class TrackPath final {
      public:
        // reM RAILWAY_PATH_NODE_REACHED_DISTANCE.
        static constexpr float kNodeReached = 0.1f;
        // The most nodes one walk passes: a report interval covers a few.
        static constexpr int kMaxNodes = 64;

        explicit TrackPath(const Graph &graph): _graph(graph) {}

        // The front as Place would take it next: a node reached or passed
        // (Place moves the front by whole steps, so it overshoots by up to
        // one; past is behind the node along the leg into it) gives way to
        // the next one.
        [[nodiscard]] TrackPoint Settle(TrackPoint point) const {
            for (int i = 0; i < kMaxNodes; ++i) {
                const glm::vec3 node   = _graph.Position(point.node);
                const glm::vec3 toNode = node - point.position;
                const auto next        = _graph.Next(point.node);
                if (!next) {
                    break;
                }
                const auto previous = _graph.Previous(point.node);
                const bool passed   = previous && glm::dot(toNode, node - _graph.Position(*previous)) <= 0.0f;
                if (glm::length(toNode) >= kNodeReached && !passed) {
                    break;
                }
                point.node = *next;
            }
            return point;
        }

        // Metres along the track from `from` to `to`, both settled; none
        // when `to` is not ahead within kMaxNodes.
        [[nodiscard]] std::optional<float> Distance(const TrackPoint &from, const TrackPoint &to) const {
            glm::vec3 at   = from.position;
            uint32_t node  = from.node;
            float distance = 0.0f;
            for (int i = 0; i < kMaxNodes; ++i) {
                const glm::vec3 end = _graph.Position(node);
                if (node == to.node) {
                    // `to` lies on the leg from `at` toward this node.
                    const glm::vec3 leg = end - at;
                    const float length  = glm::length(leg);
                    const float along   = length > 0.0001f ? glm::dot(to.position - at, leg / length) : 0.0f;
                    if (along < -kNodeReached) {
                        return std::nullopt;
                    }
                    return distance + std::max(along, 0.0f);
                }
                const auto next = _graph.Next(node);
                if (!next) {
                    return std::nullopt;
                }
                distance += glm::length(end - at);
                at   = end;
                node = *next;
            }
            return std::nullopt;
        }

        // The front `distance` metres on from `from` (settled), stopping at
        // the track's end. Never off the track.
        [[nodiscard]] TrackPoint Advance(const TrackPoint &from, float distance) const {
            glm::vec3 at  = from.position;
            uint32_t node = from.node;
            distance      = std::max(distance, 0.0f);
            for (int i = 0; i < kMaxNodes; ++i) {
                const glm::vec3 end = _graph.Position(node);
                const float length  = glm::length(end - at);
                if (distance <= length) {
                    return {length > 0.0001f ? at + (end - at) * (distance / length) : end, node};
                }
                const auto next = _graph.Next(node);
                if (!next) {
                    return {end, node};
                }
                distance -= length;
                at   = end;
                node = *next;
            }
            return {at, node};
        }

      private:
        const Graph &_graph;
    };

    // Distance along a report pair's track at u in [0, 1]: a cubic Hermite
    // from 0 to `length` over `spanMs`, its tangents the native speeds at
    // either end (m/s), which Place moves the front by. Tangents are held
    // to 3 x length (Fritsch-Carlson), so the railway never runs backward.
    struct TrackMotion {
        float distance = 0.0f;
        float speed    = 0.0f;
    };
    inline TrackMotion HermiteAlong(float length, float speedA, float speedB, double spanMs, float u) {
        const float span = static_cast<float>(spanMs) * 0.001f;
        if (!(span > 0.0f) || !(length > 0.0f)) {
            return {std::max(length, 0.0f) * std::clamp(u, 0.0f, 1.0f), 0.0f};
        }
        u              = std::clamp(u, 0.0f, 1.0f);
        const float m0 = std::clamp(speedA * span, 0.0f, 3.0f * length);
        const float m1 = std::clamp(speedB * span, 0.0f, 3.0f * length);
        const float u2 = u * u;
        const float u3 = u2 * u;
        const float distance = (u3 - 2.0f * u2 + u) * m0 + (-2.0f * u3 + 3.0f * u2) * length + (u3 - u2) * m1;
        const float slope    = (3.0f * u2 - 4.0f * u + 1.0f) * m0 + (-6.0f * u2 + 6.0f * u) * length + (3.0f * u2 - 2.0f * u) * m1;
        return {distance, slope / span};
    }

    // Past the newest report: on along the track at its speed, changing it
    // by the last pair's acceleration, for at most `limitMs`; a braking
    // railway comes to rest rather than reversing, and none passes
    // `maximumSpeed` (C_railway m_fMaxSpeed).
    inline TrackMotion ExtrapolateAlong(float speed, float acceleration, double aheadMs, double limitMs, float maximumSpeed) {
        float t = static_cast<float>(std::clamp(aheadMs, 0.0, limitMs)) * 0.001f;
        speed   = std::clamp(speed, 0.0f, maximumSpeed);
        if (acceleration < 0.0f && speed > 0.0f) {
            t = std::min(t, -speed / acceleration);
        }
        else if (acceleration > 0.0f && speed < maximumSpeed) {
            const float toMaximum = (maximumSpeed - speed) / acceleration;
            if (t > toMaximum) {
                const float distance = speed * toMaximum + 0.5f * acceleration * toMaximum * toMaximum;
                return {distance + maximumSpeed * (t - toMaximum), maximumSpeed};
            }
        }
        else {
            acceleration = 0.0f;
        }
        if (speed <= 0.0f && acceleration <= 0.0f) {
            return {0.0f, 0.0f};
        }
        return {speed * t + 0.5f * acceleration * t * t, std::clamp(speed + acceleration * t, 0.0f, maximumSpeed)};
    }
} // namespace Mafia1Online::Features::Railway
