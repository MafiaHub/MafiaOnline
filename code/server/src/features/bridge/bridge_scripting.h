#pragma once

#include <v8.h>

namespace Mafia1Online::Scripting {
    // The Bridge module: the mission's drawbridges, which the simulation host
    // runs and every player sees.
    class Bridge final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };
} // namespace Mafia1Online::Scripting
