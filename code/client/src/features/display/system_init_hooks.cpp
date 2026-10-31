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

#include <algorithm>
#include <atomic>
#include <fstream>
#include <optional>
#include <stdexcept>

namespace Mafia1Online::Features::Display {
    namespace {
        using Settings::DisplayMode;

        constexpr int32_t kWindowWidth  = 1024;
        constexpr int32_t kWindowHeight = 768;
        // InitSystem passes the refresh rate to IGRAPH_INIT_DESC as a byte (0x5bf8f6).
        constexpr uint32_t kMaxRefreshByte = 255;

        using InitSystem       = bool(__fastcall *)(HINSTANCE, bool *);
        using VideoMemOverflow = void(__cdecl *)(int, int, const char *);

        // The S_game_setup fields this hook overrides for InitSystem only.
        // WinMain may save the setup to the registry when the game exits, so
        // the player's own values go back once InitSystem has used these.
        struct SavedSetup {
            int32_t width;
            int32_t height;
            int32_t bitDepth;
            int32_t refreshRate;
            uint8_t fullscreen;
            uint8_t suspendInactive;
            uint8_t soundDisabled;
        };

        // What Initialize read; the window is decided in the hook, where the
        // adapter's modes can be checked.
        std::optional<Settings::DisplaySettings> gChosen;
        std::optional<std::pair<int32_t, int32_t>> gConfigWindow;

        ActiveDisplay gActive;
        std::vector<std::pair<uint32_t, uint32_t>> gFullscreenModes;

        InitSystem gInitSystemOriginal             = nullptr;
        VideoMemOverflow gVideoMemOverflowOriginal = nullptr;
        SavedSetup gOriginalSettings {};
        std::atomic<bool> gTemporarySettingsActive {false};

        auto Log() {
            return Framework::Logging::GetLogger("Mafia1Online");
        }

        void RestoreOriginalSettings() {
            auto &setup           = SDK::Core::InitSettings();
            setup.width           = gOriginalSettings.width;
            setup.height          = gOriginalSettings.height;
            setup.bitDepth        = gOriginalSettings.bitDepth;
            setup.refreshRate     = gOriginalSettings.refreshRate;
            setup.fullscreen      = gOriginalSettings.fullscreen;
            setup.suspendInactive = gOriginalSettings.suspendInactive;
            setup.soundDisabled   = gOriginalSettings.soundDisabled;
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

        // IDirect3D8, as LS3DF's IGraph::Init calls it (0x1006eaae onwards):
        // Direct3DCreate8(220), then GetAdapterModeCount at +0x18 and
        // EnumAdapterModes at +0x1c; Release is the IUnknown slot.
        struct D3DDisplayMode {
            UINT width;
            UINT height;
            UINT refreshRate;
            uint32_t format;
        };
        static_assert(sizeof(D3DDisplayMode) == 16);
        // D3DFMT_R8G8B8, A8R8G8B8, X8R8G8B8: IGraph::Init's 32-bit class.
        constexpr uint32_t kFormats32[] = {20, 21, 22};

        struct AdapterMode {
            uint32_t width;
            uint32_t height;
            uint32_t refreshRate;
            uint32_t format;
        };

        // The adapter's 32-bit modes and the desktop's format, read before
        // InitSystem creates the device (an exclusive device changes both).
        std::vector<AdapterMode> gModes;
        uint32_t gDesktopFormat = 0;
        // D3DFMT_X8R8G8B8: the desktop format of every 32-bit Windows desktop.
        constexpr uint32_t kFormatX8R8G8B8 = 22;

        // The adapter's 32-bit modes, as IGraph::Init will list them, and
        // IDirect3D8::GetAdapterDisplayMode (+0x20) into gDesktopFormat.
        std::vector<AdapterMode> EnumerateModes(uint8_t adapter) {
            std::vector<AdapterMode> modes;
            const HMODULE library = LoadLibraryW(L"d3d8.dll");
            if (!library) {
                return modes;
            }
            using Create = void *(WINAPI *)(UINT);
            const auto create = reinterpret_cast<Create>(GetProcAddress(library, "Direct3DCreate8"));
            void *d3d         = create ? create(220) : nullptr;
            if (d3d) {
                void **table          = *static_cast<void ***>(d3d);
                using ModeCount       = UINT(__stdcall *)(void *, UINT);
                using EnumModes       = HRESULT(__stdcall *)(void *, UINT, UINT, D3DDisplayMode *);
                using DisplayMode     = HRESULT(__stdcall *)(void *, UINT, D3DDisplayMode *);
                using Release         = ULONG(__stdcall *)(void *);
                D3DDisplayMode desktop {};
                if (SUCCEEDED(reinterpret_cast<DisplayMode>(table[8])(d3d, adapter, &desktop))) {
                    gDesktopFormat = desktop.format;
                }
                const UINT count      = reinterpret_cast<ModeCount>(table[6])(d3d, adapter);
                for (UINT index = 0; index < count; ++index) {
                    D3DDisplayMode mode {};
                    if (FAILED(reinterpret_cast<EnumModes>(table[7])(d3d, adapter, index, &mode))) {
                        continue;
                    }
                    if (std::find(std::begin(kFormats32), std::end(kFormats32), mode.format) != std::end(kFormats32)) {
                        modes.push_back({mode.width, mode.height, mode.refreshRate, mode.format});
                    }
                }
                reinterpret_cast<Release>(table[2])(d3d);
            }
            FreeLibrary(library);
            return modes;
        }

        void RecordFullscreenModes(const std::vector<AdapterMode> &modes) {
            gFullscreenModes.clear();
            for (const auto &mode : modes) {
                if (mode.width < Settings::kMinDisplayWidth || mode.height < Settings::kMinDisplayHeight) {
                    continue;
                }
                const std::pair size {mode.width, mode.height};
                if (std::find(gFullscreenModes.begin(), gFullscreenModes.end(), size) == gFullscreenModes.end()) {
                    gFullscreenModes.push_back(size);
                }
            }
            std::sort(gFullscreenModes.begin(), gFullscreenModes.end(), [](const auto &a, const auto &b) {
                return a.first * a.second != b.first * b.second ? a.first * a.second > b.first * b.second : a.first > b.first;
            });
        }

        // The highest refresh the adapter offers at that size; nullopt when
        // IGraph::Init would find no back buffer format for it and fail.
        std::optional<FullscreenMode> FindMode(const std::vector<AdapterMode> &modes, int32_t width, int32_t height) {
            std::optional<FullscreenMode> best;
            for (const auto &mode : modes) {
                if (static_cast<int32_t>(mode.width) != width || static_cast<int32_t>(mode.height) != height) {
                    continue;
                }
                const uint32_t refresh = mode.refreshRate <= kMaxRefreshByte ? mode.refreshRate : 0;
                if (!best || refresh > best->refreshRate) {
                    best = FullscreenMode {refresh, mode.format};
                }
            }
            return best;
        }

        ActiveDisplay Resolve(uint8_t adapter) {
            const auto [monitorWidth, monitorHeight] = MonitorSize();
            gModes                                    = EnumerateModes(adapter);
            const auto &modes                         = gModes;
            RecordFullscreenModes(modes);

            if (Simulation::IsHost()) {
                return {DisplayMode::Windowed, kWindowWidth, kWindowHeight, 0};
            }

            Settings::DisplaySettings chosen;
            if (gChosen) {
                chosen = *gChosen;
            }
            else if (gConfigWindow) {
                chosen = {DisplayMode::Windowed, static_cast<uint32_t>(gConfigWindow->first), static_cast<uint32_t>(gConfigWindow->second)};
            }

            const bool sized    = chosen.width != 0 && chosen.height != 0;
            const int32_t width  = sized ? static_cast<int32_t>(chosen.width) : 0;
            const int32_t height = sized ? static_cast<int32_t>(chosen.height) : 0;

            switch (chosen.mode) {
            case DisplayMode::Windowed: return {DisplayMode::Windowed, sized ? width : kWindowWidth, sized ? height : kWindowHeight, 0};
            case DisplayMode::Fullscreen: {
                const int32_t fullWidth  = sized ? width : monitorWidth;
                const int32_t fullHeight = sized ? height : monitorHeight;
                if (const auto mode = FindMode(modes, fullWidth, fullHeight)) {
                    return {DisplayMode::Fullscreen, fullWidth, fullHeight, mode->refreshRate};
                }
                Log()->warn("Display: the adapter has no 32-bit {}x{} mode for fullscreen; using borderless", fullWidth, fullHeight);
                break;
            }
            case DisplayMode::Borderless: break;
            }
            return {DisplayMode::Borderless, monitorWidth, monitorHeight, 0};
        }

        // The windowed device made by IGraph::Init: its top-level window has a
        // caption (0x90CA0000) and renders through a child window covering the
        // client area. Without the caption at the monitor's origin, the client
        // area and the back buffer are the whole monitor. LS3D's MsgProc
        // (0x1006da60) never moves or resizes the window afterwards.
        void MakeBorderless() {
            const auto window = static_cast<HWND>(SDK::Graphics::GetGraph()->MainWindow());
            if (!window) {
                return;
            }
            HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO info {sizeof(info)};
            if (!GetMonitorInfoW(monitor, &info)) {
                return;
            }
            SetWindowLongPtrW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(window, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }

        bool __fastcall InitSystemHook(HINSTANCE instance, bool *profileSelectionClosed) {
            auto &setup        = SDK::Core::InitSettings();
            gOriginalSettings  = {setup.width, setup.height, setup.bitDepth, setup.refreshRate, setup.fullscreen, setup.suspendInactive, setup.soundDisabled};
            gActive            = Resolve(setup.adapter);
            const bool fullscreen = gActive.mode == DisplayMode::Fullscreen;

            setup.width      = gActive.width;
            setup.height     = gActive.height;
            setup.fullscreen = fullscreen ? 1 : 0;
            if (fullscreen) {
                // IGraph::Init matches the mode list by 16- or 32-bit class only.
                setup.bitDepth    = 32;
                setup.refreshRate = static_cast<int32_t>(gActive.refreshRate);
            }
            setup.suspendInactive = 0;
            // InitSystem creates no ISND driver while this is set (0x5bfa32).
            if (Simulation::IsHost()) {
                setup.soundDisabled = 1;
            }
            gTemporarySettingsActive.store(true, std::memory_order_release);
            Log()->info("Display: {} {}x{} (refresh {} Hz, 0 is the driver's default)", Settings::DisplayModeId(gActive.mode), gActive.width, gActive.height, gActive.refreshRate);

            const bool initialized = gInitSystemOriginal(instance, profileSelectionClosed);
            // InitSystem loads LS3DF.dll; no mission has drawn a shadow yet.
            if (initialized && !Compat::InstallLs3dfIndexLeakFix()) {
                Log()->error("Could not install the LS3DF index buffer leak fix; exit may crash");
            }
            if (initialized && Simulation::GetConfig().render == Simulation::RenderMode::Headless) {
                ShowWindow(static_cast<HWND>(SDK::Graphics::GetGraph()->MainWindow()), SW_HIDE);
            }
            else if (initialized && gActive.mode == DisplayMode::Borderless) {
                MakeBorderless();
            }

            RestoreOriginalSettings();
            return initialized;
        }

        void ReadConfigWindow(const std::string &projectPath) {
            std::ifstream input(QuickJoin::ConfigPath(projectPath));
            if (!input) {
                return;
            }
            try {
                const auto root   = nlohmann::json::parse(input);
                const auto window = root.find("window");
                if (window == root.end()) {
                    return;
                }
                const auto width  = window->at("width").get<int32_t>();
                const auto height = window->at("height").get<int32_t>();
                if (width < 640 || width > 7680 || height < 480 || height > 4320) {
                    throw std::invalid_argument("window must be 640x480 to 7680x4320");
                }
                gConfigWindow = std::pair {width, height};
            }
            catch (const std::exception &error) {
                Log()->error("Invalid window configuration ({}); ignoring it", error.what());
            }
        }

        void ReadDisplaySettings(const std::string &projectPath) {
            std::ifstream input(Settings::SettingsFilePath(projectPath));
            if (!input) {
                return;
            }
            const auto root = nlohmann::json::parse(input, nullptr, false);
            if (root.is_discarded() || !root.is_object()) {
                // SettingsService::Load reports and sets the file aside.
                return;
            }
            gChosen = Settings::ParseSettings(root).display;
        }
    } // namespace

    void Initialize(const std::string &projectPath) {
        ReadConfigWindow(projectPath);
        ReadDisplaySettings(projectPath);
    }

    const ActiveDisplay &Active() {
        return gActive;
    }

    void SetActive(const ActiveDisplay &active) {
        gActive = active;
    }

    const std::vector<std::pair<uint32_t, uint32_t>> &FullscreenModes() {
        return gFullscreenModes;
    }

    std::optional<FullscreenMode> FindFullscreenMode(int32_t width, int32_t height) {
        return FindMode(gModes, width, height);
    }

    uint32_t DesktopFormat() {
        return gDesktopFormat ? gDesktopFormat : kFormatX8R8G8B8;
    }

    std::pair<int32_t, int32_t> MonitorSize() {
        // The launcher makes the process per-monitor DPI aware before the game
        // maps (Launcher::Project), so these are the monitor's own pixels.
        HMONITOR monitor = MonitorFromPoint(POINT {0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info {sizeof(info)};
        if (!GetMonitorInfoW(monitor, &info)) {
            return {kWindowWidth, kWindowHeight};
        }
        return {info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top};
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
