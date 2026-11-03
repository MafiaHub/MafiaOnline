#pragma once

namespace Mafia1Online::Features::Npc {
    class NpcHost;

    // Simulation host: hands every armed gangster retail CreateMafianos makes
    // of a hit gangster walker to the host.
    bool InstallGangsterSpawn(NpcHost &host);
    void UninstallGangsterSpawn();
} // namespace Mafia1Online::Features::Npc
