#pragma once

#include "game/entities/native_object_registry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Mafia1Online::SDK::Menu {
    struct NativeMenu;
}
namespace Mafia1Online::SDK::World {
    struct NativeItemVector;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}
namespace Mafia1Online::Features::WebUi {
    class WebUiService;
}

namespace Mafia1Online::Features::GameplayMenu {
    // Owns snapshots only. Native menus and their caller-owned vectors are
    // consumed synchronously and destroyed before the player tick returns.
    class GameplayMenuService final {
      public:
        void Install(World::WorldService &world, WebUi::WebUiService &ui);
        void Reset();
        void Update();
        std::optional<uint32_t> HandleMenu(SDK::Menu::NativeMenu *menu);
        void FilterInput(void *input);
        bool FilterNearObjects(SDK::World::NativeItemVector &items);
        bool AllowsNativeUse(const void *human) const;

      private:
        enum class Kind : uint8_t {
            Inventory,
            Interaction
        };
        struct Choice {
            uint16_t itemId  = 0;
            int32_t action   = 0;
            uint32_t slot    = 0;
            uint32_t index   = 0;
            uintptr_t object = 0;
            uintptr_t source = 0;
            Game::Entities::NativeObjectHandle target {};
            std::string label;
            std::string detail;
            bool canSelect = true;
            bool canDrop   = false;
            bool drop      = false;
        };
        struct Step {
            Kind kind;
            Choice choice;
        };
        struct Row {
            Choice choice;
            void *item;
        };

        bool CaptureContext();
        bool ContextValid() const;
        bool Matches(const Choice &a, const Choice &b) const;
        std::vector<Row> ReadRows(SDK::Menu::NativeMenu *menu, Kind kind) const;
        uint32_t Execute(SDK::Menu::NativeMenu *menu, Kind kind, const std::vector<Row> &rows, size_t index, bool drop);
        void Open(Kind kind, const std::vector<Row> &rows);
        void ClearInput(void *input) const;

        World::WorldService *_world = nullptr;
        WebUi::WebUiService *_ui    = nullptr;
        Game::Entities::NativeObjectHandle _player {};
        uintptr_t _vehicle  = 0;
        int32_t _seat       = -1;
        bool _vehicleEngine = false;
        uint64_t _mission   = 0;
        uint32_t _spawn     = 0;
        uint64_t _nextId    = 0;
        uint64_t _openId    = 0;
        Kind _kind          = Kind::Inventory;
        std::vector<Choice> _choices;
        std::vector<Step> _path;
        std::vector<Step> _automaticPath;
        std::vector<Step> _replay;
        size_t _replayIndex  = 0;
        bool _injected       = false;
        bool _failed         = false;
        bool _waitForRelease = false;
    };
} // namespace Mafia1Online::Features::GameplayMenu
