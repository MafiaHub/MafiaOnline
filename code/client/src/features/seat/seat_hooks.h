#pragma once

namespace Mafia1Online::Features::Seat {
    class SeatService;
    bool InstallSeatHooks(SeatService &service);
    void UninstallSeatHooks();
} // namespace Mafia1Online::Features::Seat
