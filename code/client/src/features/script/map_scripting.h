#pragma once

#include <v8.h>

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Mafia1Online::Scripting {
    // CityMap: script blips on the retail held-TAB city map. Blips belong to
    // the resource that set them and to the loaded mission; they never
    // replicate.
    class ClientCityMap final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static void RegisterResourceCleanup(Framework::Scripting::ResourceManager &manager);
        // Draws over the map DrawAll has just drawn; a no-op while it is hidden.
        static void Render();
        static void Reset();
    };
} // namespace Mafia1Online::Scripting
