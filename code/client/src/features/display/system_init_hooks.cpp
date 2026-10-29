#include <utils/safe_win32.h>

#include "system_init_hooks.h"

#include "features/compat/ls3df_index_leak.h"
#include "features/quick_join/quick_join.h"
#include "features/simulation/simulation_config.h"

#include <MinHook.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/game_setup.h>
#include <mafia1/sdk/core/system.h>
#include <mafia1/sdk/graphics/native_graph.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <fstream>
#include <stdexcept>

namespace Mafia1Online::Features::Display {
    namespace {
        constexpr int32_t kWindowWidth  = 1024;
        constexpr int32_t kWindowHeight = 768;
        int32_t gWindowWidth  = kWindowWidth;
        int32_t gWindowHeight = kWindowHeight;

        using InitSystem = bool(__fastcall *)(HINSTANCE, bool *);
        using VideoMemOverflow = void(__cdecl *)(int, int, const char *);

        struct DisplaySettings {
            int32_t width;
            int32_t height;
            uint8_t fullscreen;
            uint8_t suspendInactive;
            uint8_t soundDisabled;
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
            setup.soundDisabled = gOriginalSettings.soundDisabled;
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
            gOriginalSettings = {setup.width, setup.height, setup.fullscreen, setup.suspendInactive, setup.soundDisabled};

            setup.width = gWindowWidth;
            setup.height = gWindowHeight;
            setup.fullscreen = 0;
            setup.suspendInactive = 0;
            // InitSystem creates no ISND driver while this is set (0x5bfa32).
            if (Simulation::IsHost()) {
                setup.soundDisabled = 1;
            }
            gTemporarySettingsActive.store(true, std::memory_order_release);

            const bool initialized = gInitSystemOriginal(instance, profileSelectionClosed);
            // InitSystem loads LS3DF.dll; no mission has drawn a shadow yet.
            if (initialized && !Compat::InstallLs3dfIndexLeakFix()) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not install the LS3DF index buffer leak fix; exit may crash");
            }
            if (initialized && Simulation::GetConfig().render == Simulation::RenderMode::Headless) {
                ShowWindow(static_cast<HWND>(SDK::Graphics::GetGraph()->MainWindow()), SW_HIDE);
            }

            RestoreOriginalSettings();
            return initialized;
        }
    } // namespace

    void Initialize(const std::string &projectPath) {
        std::ifstream input(QuickJoin::ConfigPath(projectPath));
        if (!input) {
            return;
        }
        try {
            const auto root = nlohmann::json::parse(input);
            const auto window = root.find("window");
            if (window == root.end()) {
                return;
            }
            const auto width = window->at("width").get<int32_t>();
            const auto height = window->at("height").get<int32_t>();
            if (width < 640 || width > 7680 || height < 480 || height > 4320) {
                throw std::invalid_argument("window must be 640x480 to 7680x4320");
            }
            gWindowWidth = width;
            gWindowHeight = height;
            Framework::Logging::GetLogger("Mafia1Online")->info("Window size {}x{} from client config", width, height);
        }
        catch (const std::exception &error) {
            Framework::Logging::GetLogger("Mafia1Online")->error("Invalid window configuration ({}); using {}x{}", error.what(), kWindowWidth, kWindowHeight);
        }
    }

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
