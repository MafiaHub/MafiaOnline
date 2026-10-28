#pragma once

namespace Mafia1Online::SDK::Seat {
    struct NativeHuman;
}
namespace Mafia1Online::SDK::Player {
    struct NativeActor;
}

namespace Mafia1Online::Features::Seat {
    class SeatService;
    bool InstallSeatHooks(SeatService &service);
    void UninstallSeatHooks();

    // C_human::Use_Actor on a railway (ACTOR_RAILWAY branch: step aboard or
    // off); false keeps the native call from running now.
    using RailwayUseHandler = bool (*)(SDK::Seat::NativeHuman *human, SDK::Player::NativeActor *railway, int action, int seat, int animationSpeedState);
    void SetRailwayUseHandler(RailwayUseHandler handler);
    // The original Use_Actor, past every filter.
    void UseActorNative(SDK::Seat::NativeHuman *human, SDK::Player::NativeActor *target, int action, int seat, int animationSpeedState);
} // namespace Mafia1Online::Features::Seat
