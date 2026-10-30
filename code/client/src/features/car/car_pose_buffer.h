#pragma once

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iterator>

namespace Mafia1Online::Features::Car {
    // One replicated car pose, dated on the local clock (ms) from the
    // controller's physics clock: the frame origin and rotation, and the
    // native C_Vehicle velocities, which are the mass centre's.
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
    //  - The render time can run a lead ahead of now - delay, which eases
    //    toward its target rather than stepping (AdvanceLead); past now, to
    //    predict where the car is on its controller, extrapolation's bound
    //    counts from now.
    //  - Motion is native physics': the body turns about its mass centre
    //    (reM C_Vehicle_kernel.cpp:2677-2709), so the frame origin moves at
    //    the mass centre's velocity plus the turn, and past the newest pose
    //    the car runs on along an arc.
    class CarPoseBuffer final {
      public:
        static constexpr float kRestartGapMs     = 250.0f;
        static constexpr float kRestSpeed        = 0.1f;
        static constexpr float kRestCoverage     = 0.25f;
        static constexpr float kMaxEstimateGapMs = 1000.0f;
        // Extrapolation past the newest pose, at most: at its velocities, then
        // easing to a stop over the last kExtrapolationEaseMs of it rather
        // than halting at once.
        static constexpr double kMaxExtrapolationMs  = 150.0;
        static constexpr double kExtrapolationEaseMs = 50.0;
        // The most a lead past now (TransitMs) predicts; the bound above
        // then counts from there, so the prediction itself never eases.
        static constexpr float kMaxTransitMs = 250.0f;
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

        float LeadMs() const {
            return _leadMs;
        }

        // The native car's mass centre in its frame (C_Vehicle 0x3f8).
        void SetMassCenter(const glm::vec3 &local) {
            _massCenter = local;
        }

        // The world rotation native physics turns a car by over `seconds` at
        // `angular`: C_Vehicle::Move multiplies its basis by the matrix as a
        // row vector, so a point moves by r x w (reM C_Vehicle_kernel.cpp:
        // 2677-2690), the opposite sense to glm::angleAxis about w.
        static glm::quat Spin(const glm::vec3 &angular, float seconds) {
            const float spin = glm::length(angular);
            if (spin < 0.0001f) {
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
            return glm::angleAxis(-spin * seconds, angular / spin);
        }

        // `arrival` is the local clock the pose was received at; `reset`
        // starts over (a new controller's clock), from where the car was
        // shown: a new controller's poses rarely continue the last ones.
        void Push(const CarPoseSample &pose, double arrival, bool reset) {
            CarPoseSample before;
            const bool correct = _presented && Sample(_lastRender, before);
            if (reset) {
                _poses.clear();
            }
            if (!_poses.empty() && pose.time <= _poses.back().time) {
                return;
            }
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
        // poses, and along the newest's or the oldest's velocities after or
        // before them (Extrapolate). A stream's first pose arrives about now,
        // up to a delay ahead of the render time: the car runs into it rather
        // than standing there for the delay.
        bool Sample(double time, CarPoseSample &out) const {
            if (_poses.empty()) {
                return false;
            }
            if (time <= _poses.front().time) {
                out = Extrapolate(_poses.front(), time, kMaxDelayMs + kExtrapolationEaseMs);
                return true;
            }
            const auto &newest = _poses.back();
            if (time >= newest.time) {
                out = Extrapolate(newest, time, kMaxExtrapolationMs + std::max(0.0, static_cast<double>(_leadMs - _delayMs)));
                return true;
            }
            const auto next    = std::upper_bound(_poses.begin(), _poses.end(), time, [](double t, const CarPoseSample &p) { return t < p.time; });
            const auto &b      = *next;
            const auto &a      = *std::prev(next);
            const float span   = static_cast<float>(b.time - a.time) * 0.001f;
            const float u      = static_cast<float>((time - a.time) / (b.time - a.time));
            const glm::quat to = glm::dot(a.rotation, b.rotation) < 0.0f ? -b.rotation : b.rotation;
            out.time            = time;
            out.angularVelocity = glm::mix(a.angularVelocity, b.angularVelocity, u);
            if (span > kHermiteSpanSeconds) {
                out.position = glm::mix(a.position, b.position, u);
                out.velocity = glm::mix(a.velocity, b.velocity, u);
                out.rotation = glm::slerp(a.rotation, to, u);
                return true;
            }
            // Each pose turned on at its own angular velocity to this time,
            // blended with zero slope at both ends, so the rotation leaves and
            // reaches the poses at their angular velocities. A pair those do
            // not explain (a collision between them) is slerped.
            if (std::abs(glm::dot(Spin(a.angularVelocity, span) * a.rotation, to)) < kSnapCos ||
                std::abs(glm::dot(Spin(b.angularVelocity, -span) * to, a.rotation)) < kSnapCos) {
                out.rotation = glm::slerp(a.rotation, to, u);
            }
            else {
                const glm::quat fromA = glm::normalize(Spin(a.angularVelocity, span * u) * a.rotation);
                const glm::quat fromB = glm::normalize(Spin(b.angularVelocity, span * (u - 1.0f)) * to);
                out.rotation          = glm::normalize(glm::slerp(fromA, glm::dot(fromA, fromB) < 0.0f ? -fromB : fromB, u * u * (3.0f - 2.0f * u)));
            }
            // Cubic Hermite through both frame origins with their velocities,
            // so the path and its derivative stay continuous across samples.
            const glm::vec3 va = OriginVelocity(a);
            const glm::vec3 vb = OriginVelocity(b);
            const float u2     = u * u;
            const float u3     = u2 * u;
            out.position = (2.0f * u3 - 3.0f * u2 + 1.0f) * a.position + (u3 - 2.0f * u2 + u) * span * va + (-2.0f * u3 + 3.0f * u2) * b.position + (u3 - u2) * span * vb;
            const glm::vec3 origin =
                ((6.0f * u2 - 6.0f * u) * a.position + (-6.0f * u2 + 6.0f * u) * b.position) / span + (3.0f * u2 - 4.0f * u + 1.0f) * va + (3.0f * u2 - 2.0f * u) * vb;
            out.velocity = origin - glm::cross(out.angularVelocity, out.rotation * _massCenter);
            return true;
        }

        // `from` carried to local clock `time`: its mass centre along an
        // arc (the tyres turn the velocity with the heading, so by the yaw
        // about the car's up axis alone), the body turning about it, at most
        // `limitMs` worth, the last kExtrapolationEaseMs of it easing.
        CarPoseSample Extrapolate(const CarPoseSample &from, double time, double limitMs) const {
            const double full  = limitMs - kExtrapolationEaseMs;
            const double reach = std::abs(time - from.time);
            double run         = reach;
            float rate         = 1.0f;
            if (reach > full) {
                const double fade = std::exp(-(reach - full) / kExtrapolationEaseMs);
                run               = full + kExtrapolationEaseMs * (1.0 - fade);
                rate              = static_cast<float>(fade);
            }
            const float dt      = static_cast<float>(time < from.time ? -run : run) * 0.001f;
            const glm::vec3 up  = from.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 yaw = up * glm::dot(from.angularVelocity, up);
            // The turning velocity's integral: straight along the turn axis,
            // round a circle of radius v / w across it.
            glm::vec3 travel = from.velocity * dt;
            if (const float spin = glm::length(yaw); spin > 0.0001f) {
                const glm::vec3 axis   = -yaw / spin;
                const glm::vec3 along  = axis * glm::dot(from.velocity, axis);
                const glm::vec3 across = from.velocity - along;
                travel = along * dt + across * (std::sin(spin * dt) / spin) + glm::cross(axis, across) * ((1.0f - std::cos(spin * dt)) / spin);
            }
            CarPoseSample out;
            out.time            = time;
            out.rotation        = glm::normalize(Spin(from.angularVelocity, dt) * from.rotation);
            out.position        = from.position + from.rotation * _massCenter + travel - out.rotation * _massCenter;
            out.velocity        = Spin(yaw, dt) * from.velocity * rate;
            out.angularVelocity = from.angularVelocity * rate;
            return out;
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

        // How far the controller's car is past its newest pose here: half the
        // server's round trip to the controller (its PlayerEntity::ping, or
        // WorldStateEntity::simulationPing for the simulation host) and half
        // this client's.
        static float TransitMs(uint16_t controllerPing, int ownPing) {
            const float transit = 0.5f * (static_cast<float>(controllerPing) + static_cast<float>(std::max(ownPing, 0)));
            return std::clamp(transit, 0.0f, kMaxTransitMs);
        }

        // The smoothing AdvanceLead needs to take the lead `changeMs` without
        // the car running faster or slower than `rate` (a fraction of its
        // speed) or taking less than `minimumSeconds`: a critically damped
        // step of A changes fastest, by 2A / (e s) per second, at the start.
        static float LeadSeconds(float changeMs, float rate, float minimumSeconds) {
            return std::max(minimumSeconds, 2.0f * std::abs(changeMs) / (2.7182818f * rate * 1000.0f));
        }

        // How far past now - delay the car is shown, eased toward `targetMs`
        // critically damped over about `smoothSeconds`: a change of render
        // clock is one of the car's speed on screen, so it must neither step
        // nor overshoot.
        float AdvanceLead(float targetMs, float dtSeconds, float smoothSeconds) {
            const float omega  = 2.0f / smoothSeconds;
            const float x      = omega * std::max(dtSeconds, 0.0f);
            const float decay  = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
            const float change = _leadMs - targetMs;
            const float pull   = (_leadRate + omega * change) * std::max(dtSeconds, 0.0f);
            _leadRate          = (_leadRate - omega * pull) * decay;
            _leadMs            = targetMs + (change + pull) * decay;
            return _leadMs;
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

        // The car was shown at `position` and `rotation` some other way
        // (native physics) at local clock `time`: the difference from the
        // replicated path becomes the correction, so Present carries on from
        // there. Beyond a snap the car starts on the path.
        void Rebase(const glm::vec3 &position, const glm::quat &rotation, double time) {
            ClearCorrection();
            CarPoseSample path;
            if (!Sample(time, path)) {
                return;
            }
            _offset = position - path.position;
            // Shown: _turn * sampled.
            _turn = glm::normalize(rotation * glm::inverse(path.rotation));
            if (glm::length(_offset) > kSnapDistance || std::abs(_turn.w) < kSnapCos) {
                ClearCorrection();
                return;
            }
            _lastRender = time;
            _presented  = true;
        }

        // A snap: no correction carries over.
        void ClearCorrection() {
            _offset    = glm::vec3(0.0f);
            _turn      = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            _presented = false;
        }

      private:
        // The frame origin's velocity: a point r from the mass centre moves
        // at v - w x r (reM C_Vehicle_kernel.cpp:657, 1653).
        glm::vec3 OriginVelocity(const CarPoseSample &pose) const {
            return pose.velocity + glm::cross(pose.angularVelocity, pose.rotation * _massCenter);
        }

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
        glm::vec3 _massCenter {0.0f};
        glm::vec3 _offset {0.0f};
        glm::quat _turn {1.0f, 0.0f, 0.0f, 0.0f};
        float _leadMs      = 0.0f;
        float _leadRate    = 0.0f;
        double _lastRender = 0.0;
        bool _presented    = false;
    };
} // namespace Mafia1Online::Features::Car
