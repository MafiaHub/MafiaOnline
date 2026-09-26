#include <utils/safe_win32.h>

#include "system_init_hooks.h"

#include <MinHook.h>
#include <mafia1/sdk/core/game_setup.h>
#include <mafia1/sdk/core/system.h>

#include <atomic>

namespace Mafia1Online::Features::Display {
    namespace {
        constexpr int32_t kWindowWidth  = 1024;
        constexpr int32_t kWindowHeight = 768;

        using InitSystem = bool(__fastcall *)(HINSTANCE, bool *);
        using VideoMemOverflow = void(__cdecl *)(int, int, const char *);

        struct DisplaySettings {
            int32_t width;
            int32_t height;
            uint8_t fullscreen;
            uint8_t suspendInactive;
        };

        InitSystem gInitSystemOriginal = nullptr;
        VideoMemOverflow gVideoMemOverflowOriginal = nullptr;
        DisplaySettings gOriginalSettings {};
        std::atomic<bool> gTemporarySettingsActive {false};

        void RestoreOriginalSettings() {
            auto &setup = SDK::Core::InitSettings();
            setup.width = gOriginalSettings.width;
            setup.height = gOriginalSettings.height;
            setup.fullscreen = gOriginalSettings.fullscreen;
            setup.suspendInactive = gOriginalSettings.suspendInactive;
            gTemporarySettingsActive.store(false, std::memory_order_release);
        }

        void __cdecl VideoMemOverflowHook(int error, int context, const char *message) {
            // The retail OOM callback serializes the complete setup and exits.
            // It can run before InitSystem returns and restores these fields.
            if (error == SDK::Core::System::kOutOfVideoMemoryError && gTemporarySettingsActive.load(std::memory_order_acquire)) {
                RestoreOriginalSettings();
            }
            gVideoMemOverflowOriginal(error, context, message);
        }

        bool __fastcall InitSystemHook(HINSTANCE instance, bool *profileSelectionClosed) {
            auto &setup = SDK::Core::InitSettings();
            gOriginalSettings = {setup.width, setup.height, setup.fullscreen, setup.suspendInactive};

            setup.width = kWindowWidth;
            setup.height = kWindowHeight;
            setup.fullscreen = 0;
            setup.suspendInactive = 0;
            gTemporarySettingsActive.store(true, std::memory_order_release);

            const bool initialized = gInitSystemOriginal(instance, profileSelectionClosed);

            RestoreOriginalSettings();
            return initialized;
        }
    } // namespace

    bool InstallSystemInitHook() {
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Core::System::kInitSystem), reinterpret_cast<void *>(&InitSystemHook), reinterpret_cast<void **>(&gInitSystemOriginal)) != MH_OK) {
            return false;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Core::System::kVideoMemOverflow), reinterpret_cast<void *>(&VideoMemOverflowHook), reinterpret_cast<void **>(&gVideoMemOverflowOriginal)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Core::System::kInitSystem));
            return false;
        }
        return true;
    }

    void UninstallSystemInitHook() {
        MH_DisableHook(reinterpret_cast<void *>(SDK::Core::System::kInitSystem));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Core::System::kInitSystem));
        MH_DisableHook(reinterpret_cast<void *>(SDK::Core::System::kVideoMemOverflow));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Core::System::kVideoMemOverflow));
    }
} // namespace Mafia1Online::Features::Display
