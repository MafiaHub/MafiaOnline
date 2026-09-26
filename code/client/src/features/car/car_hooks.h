#pragma once

namespace Mafia1Online::SDK::Car {
    struct NativeCar;
}
namespace Mafia1Online::SDK::Player {
    struct Vector3;
}
namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Mafia1Online::Features::Car {
    bool InstallCarHooks();
    void UninstallCarHooks();
    void ApplyAuthoritativeExplosion(SDK::Car::NativeCar *car);
    bool ApplyAuthoritativeCarHit(SDK::Car::NativeCar *car, const SDK::Player::Vector3 &direction,
                                  const SDK::Player::Vector3 &position, const SDK::Player::Vector3 &normal, float damage,
                                  SDK::Scene::NativeFrame *frame);
} // namespace Mafia1Online::Features::Car
