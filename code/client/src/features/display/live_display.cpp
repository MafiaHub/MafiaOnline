#include <utils/safe_win32.h>

#include "live_display.h"

#include "system_init_hooks.h"

#include "features/simulation/simulation_config.h"

#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/ui/native_indicators.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

namespace Mafia1Online::Features::Display {
    namespace {
        using Settings::DisplayMode;
        using Settings::DisplaySettings;
        namespace Graphics = SDK::Graphics;

        // The default window when the settings name no size, as InitSystem gets it.
        constexpr int32_t kDefaultWidth  = 1024;
        constexpr int32_t kDefaultHeight = 768;
        // IGraph::Init's windowed top-level style (0x90CA0000 at 0x1006e9e7)
        // and what MakeBorderless swaps it for.
        constexpr LONG kWindowedStyle   = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
        constexpr LONG kBorderlessStyle = WS_POPUP | WS_VISIBLE;
        // Init's exclusive window: WS_EX_TOPMOST, WS_VISIBLE (0x1006e8d5).
        constexpr LONG kExclusiveStyle = WS_VISIBLE;

        std::mutex gPendingMutex;
        std::optional<DisplaySettings> gPending;
        std::atomic<uint64_t> gActiveRevision {0};
        // ParheliaApplyViewport may Present, which comes back to ApplyPending.
        bool gApplying = false;

        auto Log() {
            return Framework::Logging::GetLogger("Mafia1Online");
        }

        // The settings as a concrete mode and size; nullopt for a fullscreen
        // size the adapter lacks, which Resolve would also start borderless.
        ActiveDisplay Target(const DisplaySettings &wanted) {
            const bool sized = wanted.width != 0 && wanted.height != 0;
            switch (wanted.mode) {
            case DisplayMode::Windowed:
                return {DisplayMode::Windowed, sized ? static_cast<int32_t>(wanted.width) : kDefaultWidth, sized ? static_cast<int32_t>(wanted.height) : kDefaultHeight, 0};
            case DisplayMode::Fullscreen: {
                const auto [monitorWidth, monitorHeight] = MonitorSize();
                const int32_t width                      = sized ? static_cast<int32_t>(wanted.width) : monitorWidth;
                const int32_t height                     = sized ? static_cast<int32_t>(wanted.height) : monitorHeight;
                if (const auto mode = FindFullscreenMode(width, height)) {
                    return {DisplayMode::Fullscreen, width, height, mode->refreshRate};
                }
                Log()->warn("Display: the adapter has no 32-bit {}x{} mode for fullscreen; using borderless", width, height);
                break;
            }
            case DisplayMode::Borderless: break;
            }
            return {DisplayMode::Borderless, 0, 0, 0};
        }

        RECT MonitorRect(HWND window) {
            MONITORINFO info {sizeof(info)};
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
            return info.rcMonitor;
        }

        // The windowed device's top-level window and child, both client-sized
        // width x height. A window keeps its client corner when it already
        // was one, else it is centred in the work area; its caption stays on
        // screen either way.
        void PlaceWindowed(HWND main, HWND child, const ActiveDisplay &from, const ActiveDisplay &target) {
            const bool borderless = target.mode == DisplayMode::Borderless;
            const LONG style      = borderless ? kBorderlessStyle : kWindowedStyle;
            SetWindowLongW(main, GWL_STYLE, style);
            SetWindowLongW(main, GWL_EXSTYLE, 0);

            RECT window = MonitorRect(main);
            if (!borderless) {
                MONITORINFO info {sizeof(info)};
                GetMonitorInfoW(MonitorFromWindow(main, MONITOR_DEFAULTTONEAREST), &info);
                const RECT &work = info.rcWork;
                POINT origin {};
                if (from.mode == DisplayMode::Windowed) {
                    ClientToScreen(main, &origin);
                }
                else {
                    origin = {work.left + (work.right - work.left - target.width) / 2, work.top + (work.bottom - work.top - target.height) / 2};
                }
                window = {origin.x, origin.y, origin.x + target.width, origin.y + target.height};
                AdjustWindowRectExForDpi(&window, style, FALSE, 0, GetDpiForWindow(main));
                OffsetRect(&window, std::max(work.left - window.left, std::min<LONG>(0, work.right - window.right)),
                           std::max(work.top - window.top, std::min<LONG>(0, work.bottom - window.bottom)));
            }
            const int width  = window.right - window.left;
            const int height = window.bottom - window.top;
            SetWindowPos(main, HWND_NOTOPMOST, window.left, window.top, width, height, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            // AdjustWindowRectEx, like IGraph::Init's SM_CXFIXEDFRAME sum, leaves
            // out the wider frame DWM draws around a caption; grow by what is missing.
            RECT client {};
            GetClientRect(main, &client);
            const int dx = target.width - client.right;
            const int dy = target.height - client.bottom;
            if (dx != 0 || dy != 0) {
                SetWindowPos(main, nullptr, 0, 0, width + dx, height + dy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            SetWindowPos(child, nullptr, 0, 0, target.width, target.height, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        // The child a windowed device renders through, as IGraph::Init makes
        // it when flag 0x80 is set (0x1006ea4b): the main window's own class.
        HWND EnsureChild(HWND main, int32_t width, int32_t height) {
            if (auto *child = Graphics::ChildWindowSlot()) {
                return static_cast<HWND>(child);
            }
            char className[64] = {};
            GetClassNameA(main, className, sizeof(className));
            const auto instance = reinterpret_cast<HINSTANCE>(GetClassLongPtrW(main, GCLP_HMODULE));
            HWND child          = CreateWindowExA(0, className, "LS3D engine", WS_CHILD | WS_VISIBLE, 0, 0, width, height, main, nullptr, instance, nullptr);
            Graphics::ChildWindowSlot() = child;
            return child;
        }

        // The engine's screen setup and present parameters as IGraph::Init
        // would write them for target, then the device Reset with them.
        bool Rebuild(const ActiveDisplay &from, const ActiveDisplay &target) {
            auto *graph       = Graphics::GetGraph();
            const auto main   = static_cast<HWND>(graph->MainWindow());
            auto &desc        = Graphics::InitDesc();
            auto &params      = Graphics::PresentParams();
            const bool exclusive = target.mode == DisplayMode::Fullscreen;

            int32_t width  = target.width;
            int32_t height = target.height;
            HWND child     = nullptr;
            if (exclusive) {
                if (auto *old = Graphics::ChildWindowSlot()) {
                    Graphics::ChildWindowSlot() = nullptr;
                    DestroyWindow(static_cast<HWND>(old));
                }
                SetWindowLongW(main, GWL_STYLE, kExclusiveStyle);
                SetWindowLongW(main, GWL_EXSTYLE, WS_EX_TOPMOST);
                const RECT monitor = MonitorRect(main);
                SetWindowPos(main, HWND_TOPMOST, monitor.left, monitor.top, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);

                desc.flags      = (desc.flags & ~Graphics::kInitDescChildWindow) | Graphics::kInitDescExclusive;
                desc.fullLeft   = 0;
                desc.fullTop    = 0;
                desc.fullWidth  = 0;
                desc.fullHeight = 0;
                desc.refreshRate = static_cast<uint8_t>(target.refreshRate);

                params.windowed             = 0;
                params.backBufferFormat     = FindFullscreenMode(width, height)->format;
                params.backBufferCount      = (desc.flags & Graphics::kInitDescTripleBuffer) ? 2 : 1;
                params.deviceWindow         = nullptr;
                params.refreshRate          = target.refreshRate;
                params.presentationInterval = (desc.flags & Graphics::kInitDescVSync) ? Graphics::kPresentIntervalOne : Graphics::kPresentIntervalImmediate;
            }
            else {
                if (target.mode == DisplayMode::Borderless) {
                    const RECT monitor = MonitorRect(main);
                    width              = monitor.right - monitor.left;
                    height             = monitor.bottom - monitor.top;
                }
                const ActiveDisplay sized {target.mode, width, height, 0};
                child = EnsureChild(main, width, height);
                PlaceWindowed(main, child, from, sized);

                desc.flags       = (desc.flags & ~Graphics::kInitDescExclusive) | Graphics::kInitDescChildWindow;
                desc.fullLeft    = 0;
                desc.fullTop     = 0;
                desc.fullWidth   = width;
                desc.fullHeight  = height;
                desc.refreshRate = 0;

                params.windowed             = 1;
                params.backBufferFormat     = DesktopFormat();
                params.backBufferCount      = 1;
                params.deviceWindow         = child;
                params.refreshRate          = 0;
                params.presentationInterval = Graphics::kPresentIntervalDefault;
            }
            desc.width              = width;
            desc.height             = height;
            params.backBufferWidth  = static_cast<uint32_t>(width);
            params.backBufferHeight = static_cast<uint32_t>(height);

            if (!Graphics::ResetDevice()) {
                return false;
            }
            // Leaving an exclusive mode restores the desktop and may move the
            // focus window; put the windows where they belong again.
            if (!exclusive && from.mode == DisplayMode::Fullscreen) {
                PlaceWindowed(main, child, from, {target.mode, width, height, 0});
            }
            SetActive({target.mode, width, height, target.refreshRate});
            return true;
        }

        // What reads the screen size once instead of every frame.
        void RefreshGame() {
            auto *graph = Graphics::GetGraph();
            // Reset left D3D's viewport at the whole back buffer; IGraph's copy,
            // which I3D_driver::Render compares the scene's against, did not move.
            graph->SetViewport({0, 0, static_cast<uint32_t>(graph->ScreenWidth()), static_cast<uint32_t>(graph->ScreenHeight()), 0.0f, 1.0f});

            auto &indicators = SDK::UI::NativeIndicators::Get();
            indicators.ResolutionChanged();
            // The mission scene and its camera live from C_mission::Init on.
            const auto *mission = SDK::Core::Mission::Get();
            if (mission && mission->GetScene()) {
                indicators.ParheliaApplyViewport(indicators.ParheliaSingleScreen());
            }
        }

        // Moves the game to target; on a refused Reset, back to what ran before.
        bool Apply(const ActiveDisplay &target) {
            const ActiveDisplay before = Active();
            if (Rebuild(before, target)) {
                RefreshGame();
                return true;
            }
            Log()->error("Display: the device could not be Reset for {} {}x{}; going back to {} {}x{}", Settings::DisplayModeId(target.mode), target.width, target.height,
                         Settings::DisplayModeId(before.mode), before.width, before.height);
            // Failing again leaves the device lost; IGraph::Present then retries
            // this same Reset until it succeeds, as after Alt-Tab.
            if (!Rebuild(target, before)) {
                Log()->error("Display: the device could not be Reset back either");
            }
            RefreshGame();
            return false;
        }
    } // namespace

    bool AppliesLive(const DisplaySettings &) {
        return !Simulation::IsHost();
    }

    void RequestLive(const DisplaySettings &wanted) {
        std::lock_guard lock(gPendingMutex);
        gPending = wanted;
    }

    void ApplyPending() {
        if (gApplying) {
            return;
        }
        std::optional<DisplaySettings> wanted;
        {
            std::lock_guard lock(gPendingMutex);
            wanted = std::exchange(gPending, std::nullopt);
        }
        if (!wanted || !AppliesLive(*wanted)) {
            return;
        }
        const auto main = static_cast<HWND>(Graphics::GetGraph()->MainWindow());
        // A minimized window has no client area to size a back buffer from.
        if (IsIconic(main)) {
            RequestLive(*wanted);
            return;
        }

        const ActiveDisplay target  = Target(*wanted);
        const ActiveDisplay &active = Active();
        if (target.mode == active.mode && (target.mode == DisplayMode::Borderless || (target.width == active.width && target.height == active.height))) {
            return;
        }

        gApplying          = true;
        const bool applied = Apply(target);
        gApplying          = false;
        if (applied) {
            Log()->info("Display: now {} {}x{} (refresh {} Hz)", Settings::DisplayModeId(Active().mode), Active().width, Active().height, Active().refreshRate);
        }
        gActiveRevision.fetch_add(1, std::memory_order_release);
    }

    uint64_t ActiveRevision() {
        return gActiveRevision.load(std::memory_order_acquire);
    }
} // namespace Mafia1Online::Features::Display
