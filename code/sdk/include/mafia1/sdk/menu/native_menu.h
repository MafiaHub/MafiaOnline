#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Menu {
    inline constexpr uintptr_t kExecuteMenu              = 0x5eba40;
    inline constexpr uintptr_t kMenuCloseAll             = 0x5eae40;
    inline constexpr uintptr_t kMainMenuOnCreate         = 0x5e0690;
    inline constexpr uintptr_t kMainMenuOnClick          = 0x5e06e0;
    inline constexpr uintptr_t kMainMenuVtable           = 0x625eb8;
    inline constexpr uintptr_t kGameFailedVtable         = 0x624898;
    inline constexpr uintptr_t kProfileSelectVtable      = 0x6279dc;
    inline constexpr uint32_t kProfileBackResult         = 15u;
    inline constexpr uint32_t kProfileSelectedResult     = 16u;
    inline constexpr uint32_t kMainMenuExitResult        = 24u;
    inline constexpr uint32_t kMainMenuExitItem          = 990113u;
    inline constexpr uintptr_t kDestroy                  = 0x5ea690;
    inline constexpr uintptr_t kMenuSetHidden            = 0x5eaf20;
    inline constexpr uintptr_t kActiveMenu               = 0x6bd890;
    inline constexpr uintptr_t kMenuLoopResult           = 0x6bd8a8;
    // GM_Menu::Tick (0x5ea8c0) integrates the DirectInput mouse deltas into
    // these screen-pixel globals and clamps them to IGraph's screen size.
    inline constexpr uintptr_t kMenuMouseX               = 0x6bd8a0;
    inline constexpr uintptr_t kMenuMouseY               = 0x6bd8a4;

    // GM_Component: FindComponentByID (0x5eb2f0) compares the id at +0x04;
    // SetHidden (0x5eaf20) toggles draw bit 0x10 of the flags at +0x08, and
    // drawing and hit testing both require it.
    struct NativeComponent {
        static constexpr uint32_t kDrawEnabled = 0x10;
        [[nodiscard]] uint32_t Id() const { return _id; }
        [[nodiscard]] bool Visible() const { return (_flags & kDrawEnabled) != 0; }
        void SetVisible(bool visible) { _flags = visible ? _flags | kDrawEnabled : _flags & ~kDrawEnabled; }

        const void *_vtable;
        uint32_t _id;
        uint32_t _flags;
    };
    static_assert(offsetof(NativeComponent, _id) == 0x04);
    static_assert(offsetof(NativeComponent, _flags) == 0x08);

    struct NativeMenu {
        [[nodiscard]] bool IsMainMenu() const { return _vtable == reinterpret_cast<const void *>(kMainMenuVtable); }
        [[nodiscard]] bool IsGameFailed() const { return _vtable == reinterpret_cast<const void *>(kGameFailedVtable); }
        [[nodiscard]] bool IsProfileSelect() const { return _vtable == reinterpret_cast<const void *>(kProfileSelectVtable); }
        void Destroy() {
            using Call = void(__thiscall *)(NativeMenu *);
            reinterpret_cast<Call>(kDestroy)(this);
        }
        // Every component the menu definition created, in definition order.
        [[nodiscard]] NativeComponent *const *ComponentsBegin() const { return _componentsBegin; }
        [[nodiscard]] NativeComponent *const *ComponentsEnd() const { return _componentsEnd; }

        const void *_vtable;
        std::byte _unused04[0x10];
        NativeComponent **_componentsBegin;
        NativeComponent **_componentsEnd;
    };
    static_assert(offsetof(NativeMenu, _componentsBegin) == 0x14);
    static_assert(offsetof(NativeMenu, _componentsEnd) == 0x18);

    inline bool IsMainMenu(const void *menu) {
        return menu && static_cast<const NativeMenu *>(menu)->IsMainMenu();
    }

    inline int MenuMouseX() {
        return *reinterpret_cast<const int *>(kMenuMouseX);
    }

    inline int MenuMouseY() {
        return *reinterpret_cast<const int *>(kMenuMouseY);
    }

    inline uint32_t GetLoopResult() {
        return *reinterpret_cast<const uint32_t *>(kMenuLoopResult);
    }

    inline void SetVisible(void *menu, uint32_t id, bool visible) {
        // Retail SetHidden has inverted naming: true sets the draw-enabled bit.
        using Call = void(__thiscall *)(void *, uint32_t, bool);
        reinterpret_cast<Call>(kMenuSetHidden)(menu, id, visible);
    }

    inline NativeMenu *GetActiveMenu() {
        return *reinterpret_cast<NativeMenu **>(kActiveMenu);
    }

    inline NativeMenu *GetActiveMainMenu() {
        auto *menu = GetActiveMenu();
        return IsMainMenu(menu) ? menu : nullptr;
    }

    inline void CloseAll(void *menu, uint32_t loopResult) {
        using Call = void(__thiscall *)(void *, uint32_t, uint32_t);
        reinterpret_cast<Call>(kMenuCloseAll)(menu, 0xffffffffu, loopResult);
    }

    inline void CloseMainMenuForGame(void *menu) {
        CloseAll(menu, 20u);
    }

    inline void CloseMainMenuForExit(void *menu) {
        CloseAll(menu, kMainMenuExitResult);
    }
} // namespace Mafia1Online::SDK::Menu
