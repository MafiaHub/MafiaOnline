#pragma once

#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Mafia1Online::SDK::Graphics {
    // Installed LS3DF.dll MsgProc, rebased from the module that Game.exe loads.
    inline constexpr uintptr_t kWindowProcedureRva = 0x6da60;

#if defined(_WIN32)
    inline void *WindowProcedure() {
        return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(GetModuleHandleW(L"LS3DF.dll")) + kWindowProcedureRva);
    }
#endif
} // namespace Mafia1Online::SDK::Graphics
