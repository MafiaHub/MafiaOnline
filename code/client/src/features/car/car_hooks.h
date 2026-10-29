#pragma once

namespace Mafia1Online::SDK::Car {
    struct NativeCar;
}
namespace Mafia1Online::SDK::Player {
    struct Vector3;
    struct NativeActor;
}
namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Mafia1Online::Features::Car {
    bool InstallCarHooks();
    void UninstallCarHooks();
    void ApplyAuthoritativeExplosion(SDK::Car::NativeCar *car);
    // attacker: the shooter's native replica, or null. hitType: C_car::Hit's
    // (Shared::Car::NativeHitType).
    bool ApplyAuthoritativeCarHit(SDK::Car::NativeCar *car, const SDK::Player::Vector3 &direction,
                                  const SDK::Player::Vector3 &position, const SDK::Player::Vector3 &normal, float damage,
                                  SDK::Scene::NativeFrame *frame, SDK::Player::NativeActor *attacker = nullptr, int hitType = 0);
} // namespace Mafia1Online::Features::Car
