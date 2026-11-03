#pragma once

#include "shared/features/railway/railway_report.h"

#include <v8.h>

namespace Mafia1Online::Scripting {
    // A RailwayInfo object for one railway as the simulation host last
    // reported it.
    v8::Local<v8::Object> RailwayInfo(v8::Isolate *isolate, v8::Local<v8::Context> context, const Shared::Railway::State &state);

    // The Railway module: the mission's trams and metro trains, which the
    // simulation host runs and every player sees.
    class Railway final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };
} // namespace Mafia1Online::Scripting
