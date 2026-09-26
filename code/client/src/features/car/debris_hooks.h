#pragma once

#include <mafia1/sdk/car/native_car.h>

namespace Mafia1Online::SDK::Scene { struct NativeFrame; }
namespace Mafia1Online::SDK::Player { struct NativeActor; }
namespace Mafia1Online::Features::Car {
    bool InstallDebrisHooks();
    void UninstallDebrisHooks();
    SDK::Player::NativeActor *ApplyAuthoritativeDropOut(SDK::Car::NativeCar *car,
                                                        SDK::Scene::NativeFrame *source,
                                                        void *parameters,
                                                        SDK::Car::NativeDropOutType type,
                                                        int partIndex);
} // namespace Mafia1Online::Features::Car
