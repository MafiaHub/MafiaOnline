#pragma once

#include <v8.h>

#include <cstdint>

namespace Mafia1Online::Scripting {
    // The Pumper module: the mission's gas station attendants, which the
    // simulation host runs, every player sees and the server lets fuel the
    // players' cars.
    class Pumper final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };

    // vehicleRefuel(vehicle, player, info): the attendant's fuel event filled
    // the car with litres for cost.
    void EmitVehicleRefuel(uint64_t carId, uint64_t playerId, uint8_t index, float litres, uint32_t cost);
    // pumperRefused(player, vehicle, info): a "use the pump" the policy, the
    // attendant or the price turned down.
    void EmitPumperRefused(uint64_t playerId, uint64_t carId, uint8_t index, const char *reason, uint32_t cost);
} // namespace Mafia1Online::Scripting
