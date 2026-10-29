#pragma once

#include "taxi_service.h"

#include <v8.h>

#include <cstdint>

namespace Mafia1Online::Scripting {
    // The Taxi module: retail taxi passengers, hailed by the simulation
    // host's walkers, boarded into players' cars and dropped off by scripts.
    class Taxi final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        // The TaxiService events as script events (taxiHail, taxiHailEnd,
        // taxiPickup, taxiPassengerEnter, taxiArrive, taxiPassengerExit).
        static Features::Taxi::TaxiService::Events Events();
    };
} // namespace Mafia1Online::Scripting
