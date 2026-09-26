#pragma once

#include "shared/features/car/car_mesh_checkpoint.h"

#include <vector>

namespace Mafia1Online::SDK::Car { struct NativeCar; }

namespace Mafia1Online::Features::Car {
    bool CaptureNativeMesh(SDK::Car::NativeCar &car, std::vector<Shared::Car::MeshVertex> &out);
} // namespace Mafia1Online::Features::Car
