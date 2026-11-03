#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>

namespace Mafia1Online::Features::World {
    // Maps a remote frame clock (a host's World::FrameClock, dated by the
    // step that produced each report) onto this one: local frame clock minus
    // remote stamp, at the least delayed arrival of the last kWindow reports.
    // Both are frame clocks, so the offset holds steady but for the network.
    // It follows the window's floor continuously in time (time constant
    // kFollowMs), not report by report: every change of it is one of the
    // shown motion's speed, and a step at each report would move everything
    // drawn by it on or back at once. An offset kResetMs off is another
    // clock and is taken at once. CarService runs the same rule, stepped at
    // each pose, on its own per-car state.
    class RemoteClock final {
      public:
        static constexpr size_t kWindow   = 60;
        static constexpr double kFollowMs = 2000.0;
        static constexpr double kResetMs  = 1000.0;

        // A report stamped `stampMs` (remote) arrived at `nowMs` (local).
        // True when it reset the mapping.
        bool Observe(double stampMs, double nowMs) {
            const double offset = nowMs - stampMs;
            const bool reset    = !_valid || std::abs(offset - OffsetAt(nowMs)) > kResetMs;
            if (reset) {
                _window.clear();
                _anchor = offset;
                _valid  = true;
            }
            else {
                _anchor = OffsetAt(nowMs);
            }
            _anchorTime = nowMs;
            _window.push_back(offset);
            while (_window.size() > kWindow) {
                _window.pop_front();
            }
            _floor = *std::min_element(_window.begin(), _window.end());
            return reset;
        }

        // Local minus remote at local `nowMs`, in ms.
        [[nodiscard]] double OffsetAt(double nowMs) const {
            const double since = std::max(nowMs - _anchorTime, 0.0);
            return _floor + (_anchor - _floor) * std::exp(-since / kFollowMs);
        }
        [[nodiscard]] bool Valid() const {
            return _valid;
        }
        // The remote clock at local `nowMs`.
        [[nodiscard]] double RemoteAt(double nowMs) const {
            return nowMs - OffsetAt(nowMs);
        }
        void Reset() {
            _window.clear();
            _floor      = 0.0;
            _anchor     = 0.0;
            _anchorTime = 0.0;
            _valid      = false;
        }

      private:
        std::deque<double> _window;
        // The offset eases from _anchor, at _anchorTime, toward _floor.
        double _floor      = 0.0;
        double _anchor     = 0.0;
        double _anchorTime = 0.0;
        bool _valid        = false;
    };
} // namespace Mafia1Online::Features::World
