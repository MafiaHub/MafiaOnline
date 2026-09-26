#pragma once

#include <utils/safe_win32.h>

#include <cstdint>

namespace Mafia1Online::Features::WebUi {
    class WebUiService;

    // IGraph::Present (composites the web view into the finished frame), the
    // IGraph input queries the game polls directly (hidden from it while the
    // web UI owns that device) and a subclass of the game window (keyboard and
    // IME messages for the focused view).
    bool InstallWebUiHooks(WebUiService &service, HWND window);
    void UninstallWebUiHooks();

    // Unfiltered DirectInput state, for the web UI's own use.
    namespace Native {
        bool TestKey(uint8_t scanCode);
        int MouseDeltaX();
        int MouseDeltaY();
        int MouseWheel();
        uint32_t MouseButtons();
    } // namespace Native
} // namespace Mafia1Online::Features::WebUi
