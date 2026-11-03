#pragma once

#include <filesystem>

namespace Mafia1Online::Launcher {
    // Brings the installation up to date with the MafiaHub build channel it
    // is stamped with, as KCDC's launcher does; a no-op returning true when
    // M1O_PATCHER_ENABLED is off (developer builds) or .mafiahub/distribution
    // hands updates to another storefront. Returns false when
    // MafiaHubUpdater.exe is taking over and will relaunch us, so the caller
    // must exit without starting the game. A failed patch only warns.
    bool RunBuildPatcher(const std::filesystem::path &launcherExecutable);
} // namespace Mafia1Online::Launcher
