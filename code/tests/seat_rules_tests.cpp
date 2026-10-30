// The pure seat decisions SeatService takes from the native seat state.
#include "features/seat/seat_rules.h"

#include <cstdio>

using namespace Mafia1Online::Features::Seat;

namespace {
    int failures = 0;

#define CHECK(condition)                                                                                                                                 \
    do {                                                                                                                                                 \
        if (!(condition)) {                                                                                                                              \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                  \
            ++failures;                                                                                                                                  \
        }                                                                                                                                                \
    } while (false)

    SeatDoorView Cleared() {
        SeatDoorView seat;
        seat.enterable = false;
        return seat;
    }

    void Prompts() {
        // A seat that still has its bit needs nothing.
        CHECK(!RestoresEnterable(SeatDoorView {}));

        // An empty seat whose door was left unfinished gets its prompt back.
        CHECK(RestoresEnterable(Cleared()));

        // Nor while a human still plays that door here (an exit, or a replay
        // holding the seat): its end sets the bit as retail does.
        auto busy     = Cleared();
        busy.doorBusy = true;
        CHECK(!RestoresEnterable(busy));

        // An owner still at the door is an entry under way.
        auto entering        = Cleared();
        entering.owned       = true;
        entering.ownerAtDoor = true;
        CHECK(!RestoresEnterable(entering));

        // A properly seated owner: the bit belongs set (its entry ended
        // elsewhere, e.g. snapped into the paired seat).
        auto seated        = Cleared();
        seated.owned       = true;
        seated.ownerSeated = true;
        CHECK(RestoresEnterable(seated));

        // A stale owner, neither seated nor at the door, is reconciliation's
        // to clear first.
        auto stale  = Cleared();
        stale.owned = true;
        CHECK(!RestoresEnterable(stale));
    }

    void Owners() {
        CHECK(!IsStaleOwner(SeatDoorView {}));

        SeatDoorView seated;
        seated.owned       = true;
        seated.ownerSeated = true;
        CHECK(!IsStaleOwner(seated));

        // Use_Actor reserves the seat before Intern_UseCar links it.
        SeatDoorView entering;
        entering.owned       = true;
        entering.ownerAtDoor = true;
        CHECK(!IsStaleOwner(entering));

        // Its entry ended in the paired seat: this one stays taken here.
        SeatDoorView stale;
        stale.owned = true;
        CHECK(IsStaleOwner(stale));
        // Once freed, nothing keeps its bit clear.
        stale.owned     = false;
        stale.enterable = false;
        CHECK(RestoresEnterable(stale));
    }

    void Placing() {
        CHECK(PlaceStepFor(-1, 0, true) == PlaceStep::Link);
        CHECK(PlaceStepFor(1, 1, false) == PlaceStep::FinishEntry);
        // A driver who came in by the passenger door slides across.
        CHECK(PlaceStepFor(1, 0, true) == PlaceStep::FinishAndClimb);
        CHECK(PlaceStepFor(0, 1, true) == PlaceStep::FinishAndClimb);
        CHECK(PlaceStepFor(3, 2, true) == PlaceStep::FinishAndClimb);
        // Do_ClimbInCarLR refuses a taken seat.
        CHECK(PlaceStepFor(1, 0, false) == PlaceStep::FinishAndMove);
        // Front to rear is no climb.
        CHECK(PlaceStepFor(1, 2, true) == PlaceStep::FinishAndMove);
        CHECK(PlaceStepFor(0, 3, true) == PlaceStep::FinishAndMove);
    }
} // namespace

int main() {
    Prompts();
    Owners();
    Placing();
    if (failures != 0) {
        std::printf("%d seat rule check(s) failed\n", failures);
        return 1;
    }
    std::printf("Seat rule tests passed\n");
    return 0;
}
