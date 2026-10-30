#pragma once

// Pure seat decisions SeatService takes from the native seat state, kept
// free of the SDK so the tests can drive them.
namespace Mafia1Online::Features::Seat {
    // One seat of a network car as this client's natives have it.
    struct SeatDoorView {
        // C_car::m_uSeatProtectionMask (+0x21c4) holds this seat's bit: only
        // then does C_car::Update register its use prompt (reM C_car.cpp:
        // 1952-2004, 0x41ffb0).
        bool enterable = true;
        // C_car::GetOwner(seat) is set.
        bool owned = false;
        // That owner is linked to the seat (+0x98 the car, +0xac the seat),
        // or is no human.
        bool ownerSeated = false;
        // That owner plays this seat's door here (+0x9c the car, +0xac the
        // seat): an entry not linked yet.
        bool ownerAtDoor = false;
        // A human plays this seat's door here, or a replay holds the seat.
        bool doorBusy = false;
    };

    // C_human::Use_Actor clears the seat's bit for the door animation
    // (C_human.cpp:15767, 15877) and only Intern_UseCar(bool), the end of
    // that animation, sets it again (16180, 16232). A door that never ended
    // here (a snapped or removed human) leaves the seat without a prompt for
    // good. Give it back once nobody is at that door and the seat is empty
    // or properly taken.
    constexpr bool RestoresEnterable(const SeatDoorView &seat) {
        return !seat.enterable && !seat.doorBusy && (!seat.owned || seat.ownerSeated);
    }

    // An owner this client must free itself: a human neither linked to the
    // seat nor at its door. Retail frees a seat only from the human that
    // holds it (exit, climb, throw), so one whose entry ended in another
    // seat here holds this one for good: the seat shows taken and promptless.
    constexpr bool IsStaleOwner(const SeatDoorView &seat) {
        return seat.owned && !seat.ownerSeated && !seat.ownerAtDoor;
    }

    // How reconciliation links a human into a seat.
    enum class PlaceStep {
        // Intern_UseCar(car, seat): no door entry under way.
        Link,
        // Its door entry leads to this seat: end it (Intern_UseCar(true)).
        FinishEntry,
        // Its door entry leads to the paired seat: end it there and climb
        // across as retail's slide does (Do_ClimbInCarLR).
        FinishAndClimb,
        // Its door entry leads elsewhere: end it, free that seat and link.
        FinishAndMove,
    };

    // `entrySeat`: the seat its pending door entry leads to, or -1.
    constexpr PlaceStep PlaceStepFor(int entrySeat, int seat, bool seatFree) {
        if (entrySeat < 0) {
            return PlaceStep::Link;
        }
        if (entrySeat == seat) {
            return PlaceStep::FinishEntry;
        }
        return (entrySeat ^ 1) == seat && seatFree ? PlaceStep::FinishAndClimb : PlaceStep::FinishAndMove;
    }

    // What the local player's door entry into a seat has come to.
    enum class LocalEntryStep {
        // The door still plays.
        Wait,
        // In the seat, but C_human::AI climbs to the paired one next (the
        // passenger door's slide across): report the entry, not the end.
        AwaitClimb,
        // In the seat for good: report the entry.
        Report,
        // Already climbed across: report the entry and the move.
        ReportSlide,
    };

    constexpr LocalEntryStep NextLocalEntryStep(bool seated, bool climbPending, bool inPairedSeat) {
        if (inPairedSeat) {
            return LocalEntryStep::ReportSlide;
        }
        if (!seated) {
            return LocalEntryStep::Wait;
        }
        return climbPending ? LocalEntryStep::AwaitClimb : LocalEntryStep::Report;
    }
} // namespace Mafia1Online::Features::Seat
