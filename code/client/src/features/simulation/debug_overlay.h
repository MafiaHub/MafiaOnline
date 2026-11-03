#pragma once

namespace Mafia1Online::Features::Simulation {
    // The simulation host's markers over the debug view: walkers (green),
    // players (magenta), generators running for a player (yellow) and idle
    // generators (grey). It replaces the scene callback's HUD stage, so the
    // host draws no indicators, inventory or console of its own.
    bool InstallDebugOverlay();
    void UninstallDebugOverlay();
} // namespace Mafia1Online::Features::Simulation
