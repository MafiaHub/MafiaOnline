#pragma once

#include "features/settings/settings_model.h"

#include <cstdint>

namespace Mafia1Online::Features::Display {
    // Moves a running game between a window, borderless and exclusive
    // fullscreen, at any size, without a restart. The windows and the engine's
    // screen setup and present parameters are rewritten the way IGraph::Init
    // (0x1006e790) writes them for that mode (a windowed device renders
    // through a child window, flag 0x80; an exclusive one through the topmost
    // main window, flag 0x2), then the device is Reset as IGraph::Present
    // recovers a lost one, and the HUD, viewport and camera aspect follow.

    // False on the simulation host, which keeps its start window.
    bool AppliesLive(const Settings::DisplaySettings &wanted);

    // Queues wanted for the next ApplyPending; the latest request wins.
    void RequestLive(const Settings::DisplaySettings &wanted);

    // Game thread, right after IGraph::Present returned: the device is outside
    // BeginScene/EndScene and nothing holds a frame's resources.
    void ApplyPending();

    // Bumped each time Active() changes after start.
    uint64_t ActiveRevision();
} // namespace Mafia1Online::Features::Display
