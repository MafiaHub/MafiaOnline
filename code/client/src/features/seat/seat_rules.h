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
} // namespace Mafia1Online::Features::Seat
