#pragma once

#include <chrono>

namespace Mafia1Online::Shared::Car {
    // Who simulates a car nobody sits in. The server moves it to a clearly
    // closer player, at most every kEmptyHandoffHold, and never while a human
    // still plays a door, throw-out or pull-out animation at it: the new
    // controller rebuilds the car's physics from its frame (C_car::Reset),
    // which closes the doors and pushes the human by the door aside, so a
    // throw-out ended there as a side step (Uskok, state 328) after two
    // seconds instead of its 6.4 s fall and getting up.
    inline constexpr auto kEmptyHandoffHold = std::chrono::seconds(2);
    inline constexpr float kEmptyHysteresis = 10.0f;
    // The thrown occupant's fall and getting up (states 308-311) ran 6.4 s;
    // a door exit's clip runs at most 3.12 s (anims\AutoSmVys, AutoBigNas).
    inline constexpr auto kThrowHandoffHold = std::chrono::seconds(8);
    inline constexpr auto kExitHandoffHold  = std::chrono::seconds(4);

    struct EmptyCarHandoffView {
        // The present controller is connected, mission-ready and placed.
        bool currentUsable = false;
        float currentDistance = 0.0f;
        // The closest usable player, if any, and whether it is the present one.
        bool hasNearest = false;
        bool nearestIsCurrent = false;
        float nearestDistance = 0.0f;
        // A hand-over within kEmptyHandoffHold.
        bool recentlyHanded = false;
        // A door or throw animation of a human at this car still runs.
        bool seatAnimation = false;
    };

    // True: hand the car to the nearest player.
    constexpr bool HandEmptyCarToNearest(const EmptyCarHandoffView &view) {
        if (!view.hasNearest || view.nearestIsCurrent) {
            return false;
        }
        if (!view.currentUsable) {
            return true;
        }
        return !view.seatAnimation && !view.recentlyHanded && view.currentDistance - view.nearestDistance > kEmptyHysteresis;
    }
} // namespace Mafia1Online::Shared::Car
