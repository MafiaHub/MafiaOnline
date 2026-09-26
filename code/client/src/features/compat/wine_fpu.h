#pragma once

namespace Mafia1Online::Features::Compat {
    // Work around Wine's 32-bit nearbyint assertion without touching syscall DLLs.
    bool InstallWineFpuHook();
} // namespace Mafia1Online::Features::Compat
