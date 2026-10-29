#pragma once

#include <string>

namespace Mafia1Online::Features::Display {
    // The optional "window": {"width", "height"} of the client config; the
    // game window is 1024 by 768 without it.
    void Initialize(const std::string &projectPath);
    bool InstallSystemInitHook();
    void UninstallSystemInitHook();
} // namespace Mafia1Online::Features::Display
