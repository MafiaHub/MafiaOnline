#pragma once

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>

namespace Mafia1Online::Features::Car {
    // One replicated car pose, dated on the local clock (ms) from the
    // controller's physics clock.
    struct CarPoseSample {
        double time = 0.0;
        glm::vec3 position {0.0f};
        glm::vec3 velocity {0.0f};
        glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 angularVelocity {0.0f};
    };

    // An observed car's poses, rendered a delay behind by Hermite
    // interpolation (Framework::Utils::SnapshotBuffer's rules, with the
    // velocities as tangents).
    //  - The delay follows the learnt send interval, three times its jitter
    //    and the decaying peak of how late poses arrive, and moves slowly, so
    //    a rate band change only slows or speeds the render clock.
    //  - A long gap behind a car at rest (a parked car's unchanged pose is
    //    not sent again) is a restart: it stays put until one interval before
    //    the new pose, rather than creeping across the pause. Behind a moving
    //    car the gap is a stall: it glides on and the gap raises the delay.
    //  - A pose that arrives where the rendered car was not (it extrapolated
    //    past a pose that braked, say) does not move it there at once: the
    //    jump becomes a correction that decays over ~200 ms, in position and
    //    rotation. One beyond kSnapDistance or kSnapCos is applied at once.
    class CarPoseBuffer final {
      public:
        static constexpr float kRestartGapMs     = 250.0f;
        static constexpr float kRestSpeed        = 0.1f;
        static constexpr float kRestCoverage     = 0.25f;
        static constexpr float kMaxEstimateGapMs = 1000.0f;
        // Extrapolation past the newest pose, at most.
        static constexpr double kMaxExtrapolationMs = 150.0;
        // Above this span Hermite tangents overshoot: interpolate linearly.
        static constexpr float kHermiteSpanSeconds = 0.3f;
        static constexpr float kMinDelayMs         = 40.0f;
        static constexpr float kMaxDelayMs         = 300.0f;
        // A correction's time constant: 7% of it is left after 200 ms.
        static constexpr float kCorrectionSeconds = 0.075f;
        static constexpr float kSnapDistance      = 5.0f;
        // |cos(half angle)| below this (about 46 degrees) is a snap.
        static constexpr float kSnapCos = 0.92f;

        void Clear() {
            _poses.clear();
            ClearCorrection();
        }

        bool Empty() const {
            return _poses.empty();
        }

        const CarPoseSample &Newest() const {
            return _poses.back();
        }

        float DelayMs() const {
            return _delayMs;
        }

        float IntervalMs() const {
            return _intervalMs;
        }

        // `arrival` is the local clock the pose was received at; `reset`
        // starts over (a new controller's clock).
        void Push(const CarPoseSample &pose, double arrival, bool reset) {
            if (reset) {
                Clear();
            }
            if (!_poses.empty() && pose.time <= _poses.back().time) {
                return;
            }
            CarPoseSample before;
            const bool correct = _presented && Sample(_lastRender, before);
            if (!_poses.empty()) {
                const CarPoseSample last = _poses.back();
                const float gap          = static_cast<float>(pose.time - last.time);
                const float speed        = glm::length(last.velocity);
                const float moved        = glm::distance(pose.position, last.position);
                const bool resting       = speed < kRestSpeed || moved < kRestCoverage * speed * gap * 0.001f;
                const bool longGap       = gap >= std::max(kRestartGapMs, 3.0f * _intervalMs);
                if (longGap && resting) {
                    CarPoseSample rest  = last;
                    rest.velocity        = glm::vec3(0.0f);
                    rest.angularVelocity = glm::vec3(0.0f);
                    rest.time            = pose.time - _intervalMs;
                    if (rest.time > last.time) {
                        Append(rest);
                    }
                }
                else {
                    const float interval = longGap ? std::min(gap, 2.0f * _intervalMs) : gap;
                    _jitterMs            = _jitterMs + (std::min(std::fabs(gap - _intervalMs), kMaxEstimateGapMs) - _jitterMs) * 0.1f;
                    _intervalMs          = _intervalMs + (std::min(interval, kMaxEstimateGapMs) - _intervalMs) * 0.1f;
                }
            }
            // A decaying peak rather than a mean, since one late pose is an
            // underrun.
            const float lateness = static_cast<float>(std::clamp(arrival - pose.time, 0.0, 500.0));
            _latenessPeak        = std::max(lateness, _latenessPeak * 0.97f);
            Append(pose);
            CarPoseSample after;
            if (correct && Sample(_lastRender, after)) {
                // What the car showed against what the new pose says it
                // showed: carry the difference, so the rendered car stays put
                // and closes it gradually.
                _offset += before.position - after.position;
                const glm::quat to = glm::dot(before.rotation, after.rotation) < 0.0f ? -after.rotation : after.rotation;
                // Shown: _turn * sampled. Keep it: turn' * after = turn * before.
                _turn = glm::normalize(_turn * before.rotation * glm::inverse(to));
                if (glm::length(_offset) > kSnapDistance || std::abs(_turn.w) < kSnapCos) {
                    ClearCorrection();
                }
            }
        }

        // The pose at local clock `time`: Hermite between the bracketing
        // poses, the oldest before them, and up to kMaxExtrapolationMs along
        // the newest's velocities after.
        bool Sample(double time, CarPoseSample &out) const {
            if (_poses.empty()) {
                return false;
            }
            if (time <= _poses.front().time) {
                out = _poses.front();
                return true;
            }
            const auto &newest = _poses.back();
            if (time >= newest.time) {
                const float dt = static_cast<float>(std::min(time - newest.time, kMaxExtrapolationMs)) * 0.001f;
                out            = newest;
                out.position += newest.velocity * dt;
                if (const float spin = glm::length(newest.angularVelocity); spin > 0.0001f) {
                    out.rotation = glm::normalize(glm::angleAxis(spin * dt, newest.angularVelocity / spin) * newest.rotation);
                }
                out.time = time;
                return true;
            }
            const auto next  = std::upper_bound(_poses.begin(), _poses.end(), time, [](double t, const CarPoseSample &p) { return t < p.time; });
            const auto &b    = *next;
            const auto &a    = *std::prev(next);
            const float span = static_cast<float>(b.time - a.time) * 0.001f;
            const float u    = static_cast<float>((time - a.time) / (b.time - a.time));
            out.time         = time;
            if (span > kHermiteSpanSeconds) {
                out.position = glm::mix(a.position, b.position, u);
                out.velocity = glm::mix(a.velocity, b.velocity, u);
            }
            else {
                // Cubic Hermite through both poses with their velocities, so
                // the path and its derivative stay continuous across samples.
                const float u2 = u * u;
                const float u3 = u2 * u;
                out.position   = (2.0f * u3 - 3.0f * u2 + 1.0f) * a.position + (u3 - 2.0f * u2 + u) * span * a.velocity + (-2.0f * u3 + 3.0f * u2) * b.position +
                               (u3 - u2) * span * b.velocity;
                out.velocity = ((6.0f * u2 - 6.0f * u) * a.position + (-6.0f * u2 + 6.0f * u) * b.position) / span + (3.0f * u2 - 4.0f * u + 1.0f) * a.velocity +
                               (3.0f * u2 - 2.0f * u) * b.velocity;
            }
            out.rotation        = glm::slerp(a.rotation, glm::dot(a.rotation, b.rotation) < 0.0f ? -b.rotation : b.rotation, u);
            out.angularVelocity = glm::mix(a.angularVelocity, b.angularVelocity, u);
            return true;
        }

        // This frame's render time, `now` less the delay, which then moves
        // toward its target: slowly, since every change shifts the sample
        // time and at speed reads as a jerk; faster on an underrun.
        double AdvanceDelay(double now, float frameMs) {
            const float target      = std::clamp(_intervalMs + 3.0f * _jitterMs + _latenessPeak + 8.0f, kMinDelayMs, kMaxDelayMs);
            const double renderTime = now - _delayMs;
            const bool underrun     = !_poses.empty() && renderTime > _poses.back().time;
            const float step        = frameMs * (underrun ? 0.1f : 0.01f);
            // Growing by a fifth of every frame slowed a car to 80% on screen
            // through the gap; extrapolation carries it, and the delay grows
            // by 5% a frame.
            _delayMs += std::clamp(target - _delayMs, -step, underrun ? step * 0.5f : step);
            return renderTime;
        }

        // The pose to show at `renderTime`: Sample plus what is left of the
        // correction, which decays by `dtSeconds`.
        bool Present(double renderTime, float dtSeconds, CarPoseSample &out) {
            if (!Sample(renderTime, out)) {
                return false;
            }
            const float keep = std::exp(-std::max(dtSeconds, 0.0f) / kCorrectionSeconds);
            _offset *= keep;
            _turn = glm::normalize(glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), _turn.w < 0.0f ? -_turn : _turn, keep));
            out.position += _offset;
            out.rotation = glm::normalize(_turn * out.rotation);
            _lastRender  = renderTime;
            _presented   = true;
            return true;
        }

        // The car is shown some other way this frame (corrected native
        // physics, a snap): no correction carries over.
        void ClearCorrection() {
            _offset    = glm::vec3(0.0f);
            _turn      = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            _presented = false;
        }

      private:
        void Append(const CarPoseSample &pose) {
            _poses.push_back(pose);
            while (_poses.size() > 64) {
                _poses.pop_front();
            }
        }

        std::deque<CarPoseSample> _poses;
        float _delayMs      = 100.0f;
        float _latenessPeak = 0.0f;
        float _intervalMs   = 33.0f;
        float _jitterMs     = 0.0f;
        glm::vec3 _offset {0.0f};
        glm::quat _turn {1.0f, 0.0f, 0.0f, 0.0f};
        double _lastRender = 0.0;
        bool _presented    = false;
    };
} // namespace Mafia1Online::Features::Car
