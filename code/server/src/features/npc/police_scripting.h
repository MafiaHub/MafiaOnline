#pragma once

#include <v8.h>

#include <cstdint>

namespace Mafia1Online::Scripting {
    // Script name of one retail police crime bit ("firing", "carTheft", ...),
    // or null for a bit retail does not use.
    const char *PoliceCrimeName(uint16_t crime);

    // The Police module: each player's retail crime record, which the
    // simulation host's police manager keeps.
    class Police final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };
} // namespace Mafia1Online::Scripting
