#pragma once

namespace Mafia1Online::Features::Simulation {
    // Replaces retail GameLoopSingle1 (0x5be750) on the simulation host. Each
    // iteration pumps window messages, waits for the configured tick, runs
    // C_mission::Tick and, unless headless, renders the free camera's view.
    // There is no player input, pause menu, indicator HUD or replay. Returns
    // when C_game leaves the running state, like the retail loop.
    void RunLoop();
} // namespace Mafia1Online::Features::Simulation
