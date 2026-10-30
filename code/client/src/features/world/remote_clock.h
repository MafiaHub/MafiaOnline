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
    // It follows the window's floor smoothly (time constant kFollowMs) and
    // never in steps, as every change of it is one of the shown motion's
    // speed; an offset kResetMs off is another clock and is taken at once.
    // CarService runs the same rule on its own per-car state.
    class RemoteClock final {
      public:
        static constexpr size_t kWindow   = 60;
        static constexpr double kFollowMs = 2000.0;
        static constexpr double kResetMs  = 1000.0;
        // The longest gap one report's follow step covers.
        static constexpr double kMaxStepMs = 250.0;

        // A report stamped `stampMs` (remote) arrived at `nowMs` (local).
        // True when it reset the mapping.
        bool Observe(double stampMs, double nowMs) {
            const double offset = nowMs - stampMs;
            const bool reset    = !_valid || std::abs(offset - _offset) > kResetMs;
            if (reset) {
                _window.clear();
                _offset     = offset;
                _lastUpdate = nowMs;
                _valid      = true;
            }
            _window.push_back(offset);
            while (_window.size() > kWindow) {
                _window.pop_front();
            }
            const double floor = *std::min_element(_window.begin(), _window.end());
            const double since = std::clamp(nowMs - _lastUpdate, 0.0, kMaxStepMs);
            _offset += (floor - _offset) * (1.0 - std::exp(-since / kFollowMs));
            _lastUpdate = nowMs;
            return reset;
        }

        // Local minus remote, in ms.
        [[nodiscard]] double Offset() const {
            return _offset;
        }
        [[nodiscard]] bool Valid() const {
            return _valid;
        }
        // The remote clock at local `nowMs`.
        [[nodiscard]] double RemoteAt(double nowMs) const {
            return nowMs - _offset;
        }
        void Reset() {
            _window.clear();
            _offset     = 0.0;
            _lastUpdate = 0.0;
            _valid      = false;
        }

      private:
        std::deque<double> _window;
        double _offset     = 0.0;
        double _lastUpdate = 0.0;
        bool _valid        = false;
    };
} // namespace Mafia1Online::Features::World
