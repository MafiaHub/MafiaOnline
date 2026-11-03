#pragma once

namespace Mafia1Online::Features::Npc {
    class NpcHost;

    // Simulation host: the retail police on foot. The police manager
    // recruits officers from police walkers (each handed to the host as an
    // NPC), and their arrests and fines are reported instead of ending the
    // game. Police cars and roadblocks stay off: their cars would exist on
    // the host only.
    bool InstallPolice(NpcHost &host);
    void UninstallPolice();
} // namespace Mafia1Online::Features::Npc
