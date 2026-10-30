#pragma once

#include <v8.h>

namespace Mafia1Online::Scripting {
    // The Traffic module: the jam watch over the simulation host's traffic,
    // area resets and restarting or handing over the host.
    class Traffic final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };
} // namespace Mafia1Online::Scripting
