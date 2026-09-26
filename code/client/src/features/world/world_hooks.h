#pragma once

namespace Mafia1Online::Features::World {
    bool InstallMenuExecuteHook();
    bool InstallWorldHooks();
    void UninstallWorldHooks();
    void SetNativeMissionActive(bool active);
    void SetNativeMissionLoading(bool loading);
    bool NativeMissionActive();
    bool WindowExitRequested();
} // namespace Mafia1Online::Features::World
