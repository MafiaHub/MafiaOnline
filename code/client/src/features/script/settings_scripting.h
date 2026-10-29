#pragma once

#include <v8.h>

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Mafia1Online::Scripting {
    // Settings: lets a resource list its keys and switches on the player's
    // settings screen, so the player can move or flip them, and read the
    // web-audio volume the player chose.
    class SettingsApi final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static void RegisterResourceCleanup(Framework::Scripting::ResourceManager &manager);
    };
} // namespace Mafia1Online::Scripting
