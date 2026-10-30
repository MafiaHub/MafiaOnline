#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Menu {
    // GM_LoadProgress, the retail loading screen (menu definition
    // "loadprogress"). C_mission::Open (0x5409d0) creates one into
    // G_LoadSaveClass::m_pLoadProgress (0x6d4bfc) when its last argument is
    // true, with a mission-open weight of 0.7. The scene callback cbLoadScene
    // (0x5bcc10) reports the scene and mission data through
    // SetProgressMissionOpen and presents; C_game::Init (0x5a0810) reports its
    // own steps through _show_progress_ (0x5a3960), SetProgressGameInit and
    // Present, then deletes the menu. Each Present draws one complete frame:
    // ResetRenderProps, Clear, BeginScene, GM_Menu::Draw, EndScene and
    // IGraph::Present (vtable +0x3c), so it is the only frame the loading
    // screen shows; nothing else ticks while a mission loads.
    inline constexpr uintptr_t kLoadProgressPresent        = 0x5e3d40;
    inline constexpr uintptr_t kLoadProgressSetMissionOpen = 0x5e3d20;
    inline constexpr uintptr_t kLoadProgressSetGameInit    = 0x5e3cf0;

    struct NativeLoadProgress {
        // Share of the bar the mission open fills (the constructor's second
        // argument, stored at +0x30): SetProgressMissionOpen(p) draws p * w,
        // SetProgressGameInit(p) draws w + (1 - w) * p.
        [[nodiscard]] float MissionOpenWeight() const { return _missionOpenWeight; }

        const void *_vtable;
        std::byte _unused04[0x2c];
        float _missionOpenWeight;
    };
    static_assert(offsetof(NativeLoadProgress, _missionOpenWeight) == 0x30);
} // namespace Mafia1Online::SDK::Menu
