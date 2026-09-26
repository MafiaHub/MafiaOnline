#include <utils/safe_win32.h>

#include "wine_fpu.h"
#include "x87_nearbyint.h"

#include <MinHook.h>
#include <logging/logger.h>

namespace Mafia1Online::Features::Compat {
    bool InstallWineFpuHook() {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll || !GetProcAddress(ntdll, "wine_get_version")) {
            return true;
        }

        // V8's optimizing compiler calls nearbyint to classify numeric constants.
        // Wine's implementation asserts in _setfp when it clears inexact while
        // another x87 exception flag is set. _clearfp is not on this call path.
        HMODULE ucrtbase = GetModuleHandleW(L"ucrtbase.dll");
        if (!ucrtbase) return true;
        auto *target = GetProcAddress(ucrtbase, "nearbyint");
        if (!target) return false;

        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(target), &owner) || owner != ucrtbase) {
            Framework::Logging::GetLogger("Mafia1Online")->warn("Skipping Wine nearbyint hook: target {:p} is outside ucrtbase.dll", reinterpret_cast<void *>(target));
            return true;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(target), reinterpret_cast<void *>(&X87NearbyInt), nullptr) != MH_OK) {
            return false;
        }
        Framework::Logging::GetLogger("Mafia1Online")->info("Installed Wine ucrtbase nearbyint compatibility hook at {:p}", reinterpret_cast<void *>(target));
        return true;
    }
}
