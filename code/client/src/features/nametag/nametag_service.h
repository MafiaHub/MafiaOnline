#pragma once

#include <integrations/client/ui/nametag_list.h>

#include <string>
#include <unordered_map>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Nametag {
    // Names and health bars above remote players, drawn with the retail HUD
    // font. The Framework list ranks and fades them; this draws the survivors
    // that are on screen and not behind static world geometry.
    class NametagService final {
      public:
        NametagService();
        void Render(World::WorldService &world);
        void Reset();

      private:
        Framework::Integrations::Client::UI::Nametags::Config _config;
        Framework::Integrations::Client::UI::Nametags::List _list;
        struct Neck {
            uint64_t generation = 0;
            SDK::Scene::NativeFrame *frame = nullptr;
        };
        std::unordered_map<uint64_t, std::string> _labels; // native code page, per player
        std::unordered_map<uint64_t, Neck> _necks;
    };
} // namespace Mafia1Online::Features::Nametag
