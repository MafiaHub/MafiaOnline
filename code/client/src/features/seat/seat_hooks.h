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

    // While one lives, C_human::Can_DropOutFromCar answers that the door is
    // clear. A remote human's exit replays the door its controller opened;
    // the test would read this client's copy of the car's surroundings, and
    // a near car or walker here made Use_Actor pop it out (ForceExitCar)
    // or climb it across without the door.
    class RemoteExitScope final {
      public:
        RemoteExitScope();
        ~RemoteExitScope();
        RemoteExitScope(const RemoteExitScope &)            = delete;
        RemoteExitScope &operator=(const RemoteExitScope &) = delete;
    };
} // namespace Mafia1Online::Features::Seat
