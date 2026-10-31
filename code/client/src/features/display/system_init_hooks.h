#pragma once

#include "features/settings/settings_model.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Mafia1Online::Features::Display {
    // The window InitSystem makes, chosen once per start: the player's
    // display settings (settings.json "display"), else the client config's
    // "window": {"width", "height"} as a window, else borderless at the
    // monitor's resolution. The simulation host keeps its 1024 x 768 window.
    void Initialize(const std::string &projectPath);
    bool InstallSystemInitHook();
    void UninstallSystemInitHook();

    struct ActiveDisplay {
        Settings::DisplayMode mode = Settings::DisplayMode::Windowed;
        int32_t width              = 0;
        int32_t height             = 0;
        // Exclusive fullscreen only; 0 is the driver's default.
        uint32_t refreshRate = 0;
    };

    // What the game is running with, after any fallback; valid once
    // InitSystem has run.
    const ActiveDisplay &Active();

    // The 32-bit sizes the D3D8 adapter offers for exclusive fullscreen,
    // largest first: the list IGraph::Init matches against (0x1006e790).
    const std::vector<std::pair<uint32_t, uint32_t>> &FullscreenModes();

    // The primary monitor's resolution.
    std::pair<int32_t, int32_t> MonitorSize();
} // namespace Mafia1Online::Features::Display
