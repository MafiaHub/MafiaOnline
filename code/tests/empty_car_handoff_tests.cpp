// Who simulates a car nobody sits in (Server::UpdateControllers).
#include "shared/features/car/empty_car_handoff.h"

#include <cstdio>

using namespace Mafia1Online::Shared::Car;

namespace {
    int failures = 0;

#define CHECK(condition)                                                                                                                                 \
    do {                                                                                                                                                 \
        if (!(condition)) {                                                                                                                              \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                  \
            ++failures;                                                                                                                                  \
        }                                                                                                                                                \
    } while (false)

    EmptyCarHandoffView Closer() {
        EmptyCarHandoffView view;
        view.currentUsable = true;
        view.currentDistance = 30.0f;
        view.hasNearest = true;
        view.nearestDistance = 2.0f;
        return view;
    }

    void Handoff() {
        CHECK(HandEmptyCarToNearest(Closer()));

        // Not clearly closer: it stays.
        auto near = Closer();
        near.currentDistance = 11.0f;
        CHECK(!HandEmptyCarToNearest(near));

        // Not twice within the hold.
        auto recent = Closer();
        recent.recentlyHanded = true;
        CHECK(!HandEmptyCarToNearest(recent));

        // Not while the thrown driver falls by its door: the new controller
        // would rebuild the car's physics around it.
        auto thrown = Closer();
        thrown.seatAnimation = true;
        CHECK(!HandEmptyCarToNearest(thrown));

        // A gone controller is replaced whatever runs at the car.
        auto gone = thrown;
        gone.currentUsable = false;
        CHECK(HandEmptyCarToNearest(gone));

        auto same = Closer();
        same.nearestIsCurrent = true;
        CHECK(!HandEmptyCarToNearest(same));
        auto nobody = gone;
        nobody.hasNearest = false;
        CHECK(!HandEmptyCarToNearest(nobody));
        CHECK(kThrowHandoffHold > kExitHandoffHold);
    }
} // namespace

int main() {
    Handoff();
    if (failures != 0) {
        std::printf("%d empty car hand-over check(s) failed\n", failures);
        return 1;
    }
    std::printf("Empty car hand-over tests passed\n");
    return 0;
}
