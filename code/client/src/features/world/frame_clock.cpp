#include "frame_clock.h"

#include <mafianet/get_time.h>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::World::FrameClock {
    namespace {
        constexpr double kFollowMs = 1000.0;
        constexpr double kSnapMs   = 250.0;
        double gNowMs              = 0.0;
        bool gStarted              = false;

        double WallMs() {
            return static_cast<double>(MafiaNet::GetTimeUS()) * 0.001;
        }
    } // namespace

    void Advance(unsigned int frameMs) {
        const double wall = WallMs();
        if (!gStarted || std::fabs(wall - gNowMs) > kSnapMs) {
            gNowMs   = wall;
            gStarted = true;
            return;
        }
        const double frame = static_cast<double>(frameMs);
        gNowMs += frame + (wall - gNowMs - frame) * std::min(1.0, frame / kFollowMs);
    }

    double NowMs() {
        return gStarted ? gNowMs : WallMs();
    }
} // namespace Mafia1Online::Features::World::FrameClock
