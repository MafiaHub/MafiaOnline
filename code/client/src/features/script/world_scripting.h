#pragma once

#include <v8.h>

namespace Mafia1Online::Scripting {
    class ClientWorld final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };
} // namespace Mafia1Online::Scripting
