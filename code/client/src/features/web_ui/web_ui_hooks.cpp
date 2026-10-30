#include "web_ui_hooks.h"

#include "web_ui_service.h"
#include "features/script/visual_scripting.h"

#include <MinHook.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/menu/native_load_progress.h>

namespace Mafia1Online::Features::WebUi {
    namespace {
        using Graph        = SDK::Graphics::NativeGraph;
        using Present      = void(__stdcall *)(Graph *);
        using TestKey      = bool(__stdcall *)(Graph *, uint8_t);
        using ReadKeys     = int64_t(__stdcall *)(Graph *);
        using MouseQuery   = int(__stdcall *)(Graph *);
        using MouseButtons = uint32_t(__stdcall *)(Graph *);
        using LoadProgress = SDK::Menu::NativeLoadProgress;
        using LoadPresent  = void(__thiscall *)(LoadProgress *);
        using LoadSet      = void(__thiscall *)(LoadProgress *, float);

        Present gPresentOriginal           = nullptr;
        TestKey gTestKeyOriginal           = nullptr;
        ReadKeys gReadKeysOriginal         = nullptr;
        MouseQuery gMouseDeltaXOriginal    = nullptr;
        MouseQuery gMouseDeltaYOriginal    = nullptr;
        MouseQuery gMouseWheelOriginal     = nullptr;
        MouseButtons gMouseButtonsOriginal = nullptr;
        LoadPresent gLoadPresentOriginal   = nullptr;
        LoadSet gLoadMissionOpenOriginal   = nullptr;
        LoadSet gLoadGameInitOriginal      = nullptr;
        void *gTargets[10]                 = {};
        WNDPROC gWindowProcedure           = nullptr;
        HWND gWindow                       = nullptr;
        WebUiService *gService             = nullptr;

        void __stdcall PresentHook(Graph *graph) {
            Scripting::RenderPreviewCommands();
            gService->OnPresent();
            gPresentOriginal(graph);
        }

        bool __stdcall TestKeyHook(Graph *graph, uint8_t scanCode) {
            return !gService->HidesKeyboard() && gTestKeyOriginal(graph, scanCode);
        }

        int64_t __stdcall ReadKeysHook(Graph *graph) {
            return gService->HidesKeyboard() ? 0 : gReadKeysOriginal(graph);
        }

        int __stdcall MouseDeltaXHook(Graph *graph) {
            return gService->HidesMouse() ? 0 : gMouseDeltaXOriginal(graph);
        }

        int __stdcall MouseDeltaYHook(Graph *graph) {
            return gService->HidesMouse() ? 0 : gMouseDeltaYOriginal(graph);
        }

        int __stdcall MouseWheelHook(Graph *graph) {
            return gService->HidesMouse() ? 0 : gMouseWheelOriginal(graph);
        }

        uint32_t __stdcall MouseButtonsHook(Graph *graph) {
            return gService->HidesMouse() ? 0 : gMouseButtonsOriginal(graph);
        }

        // The progress the retail bar is about to show, as a share of the
        // whole load (see NativeLoadProgress); the callers present right after.
        void __fastcall LoadMissionOpenHook(LoadProgress *progress, void *, float value) {
            gService->OnLoadingProgress(value * progress->MissionOpenWeight(), false);
            gLoadMissionOpenOriginal(progress, value);
        }

        void __fastcall LoadGameInitHook(LoadProgress *progress, void *, float value) {
            const float weight = progress->MissionOpenWeight();
            gService->OnLoadingProgress(weight + (1.0f - weight) * value, true);
            gLoadGameInitOriginal(progress, value);
        }

        // Nothing else draws or ticks while a mission loads: this frame is
        // the only chance to pump CEF and show the page instead.
        void __fastcall LoadPresentHook(LoadProgress *progress, void *) {
            if (!gService->OnLoadingPresent()) {
                gLoadPresentOriginal(progress);
            }
        }

        LRESULT CALLBACK WindowProcedureHook(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
            // Null once the service is gone but another subclass kept this one in the chain.
            if (gService && gService->OnWindowMessage(window, message, wParam, lParam)) {
                return 0;
            }
            return CallWindowProcA(gWindowProcedure, window, message, wParam, lParam);
        }

        bool Hook(size_t slot, void *target, void *detour, void **original) {
            if (MH_CreateHook(target, detour, original) != MH_OK) {
                return false;
            }
            gTargets[slot] = target;
            return MH_EnableHook(target) == MH_OK;
        }
    } // namespace

    bool InstallWebUiHooks(WebUiService &service, HWND window) {
        gService = &service;
        // The live IGraph vtable, so the hooks follow wherever LS3DF.dll loaded.
        const auto *vtable = SDK::Graphics::GetGraph()->vtable;
        const bool hooked  = Hook(0, reinterpret_cast<void *>(vtable->present), reinterpret_cast<void *>(&PresentHook), reinterpret_cast<void **>(&gPresentOriginal))
                          && Hook(1, reinterpret_cast<void *>(vtable->testKey), reinterpret_cast<void *>(&TestKeyHook), reinterpret_cast<void **>(&gTestKeyOriginal))
                          && Hook(2, reinterpret_cast<void *>(vtable->readKeys), reinterpret_cast<void *>(&ReadKeysHook), reinterpret_cast<void **>(&gReadKeysOriginal))
                          && Hook(3, reinterpret_cast<void *>(vtable->mouseDeltaX), reinterpret_cast<void *>(&MouseDeltaXHook), reinterpret_cast<void **>(&gMouseDeltaXOriginal))
                          && Hook(4, reinterpret_cast<void *>(vtable->mouseDeltaY), reinterpret_cast<void *>(&MouseDeltaYHook), reinterpret_cast<void **>(&gMouseDeltaYOriginal))
                          && Hook(5, reinterpret_cast<void *>(vtable->mouseWheel), reinterpret_cast<void *>(&MouseWheelHook), reinterpret_cast<void **>(&gMouseWheelOriginal))
                          && Hook(6, reinterpret_cast<void *>(vtable->mouseButtons), reinterpret_cast<void *>(&MouseButtonsHook), reinterpret_cast<void **>(&gMouseButtonsOriginal))
                          && Hook(7, reinterpret_cast<void *>(SDK::Menu::kLoadProgressPresent), reinterpret_cast<void *>(&LoadPresentHook), reinterpret_cast<void **>(&gLoadPresentOriginal))
                          && Hook(8, reinterpret_cast<void *>(SDK::Menu::kLoadProgressSetMissionOpen), reinterpret_cast<void *>(&LoadMissionOpenHook), reinterpret_cast<void **>(&gLoadMissionOpenOriginal))
                          && Hook(9, reinterpret_cast<void *>(SDK::Menu::kLoadProgressSetGameInit), reinterpret_cast<void *>(&LoadGameInitHook), reinterpret_cast<void **>(&gLoadGameInitOriginal));
        if (!hooked) {
            UninstallWebUiHooks();
            return false;
        }
        gWindow          = window;
        gWindowProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProcedureHook)));
        if (!gWindowProcedure) {
            gWindow = nullptr;
            UninstallWebUiHooks();
            return false;
        }
        return true;
    }

    void UninstallWebUiHooks() {
        // Only unwind the subclass if nobody chained on top of it since.
        if (gWindow && gWindowProcedure && reinterpret_cast<WNDPROC>(GetWindowLongPtrA(gWindow, GWLP_WNDPROC)) == &WindowProcedureHook) {
            SetWindowLongPtrA(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gWindowProcedure));
            gWindowProcedure = nullptr;
            gWindow          = nullptr;
        }
        for (void *&target : gTargets) {
            if (target) {
                MH_DisableHook(target);
                MH_RemoveHook(target);
                target = nullptr;
            }
        }
        gService = nullptr;
    }

    namespace Native {
        bool TestKey(uint8_t scanCode) {
            auto *graph = SDK::Graphics::GetGraph();
            return gTestKeyOriginal ? gTestKeyOriginal(graph, scanCode) : graph->vtable->testKey(graph, scanCode);
        }

        int MouseDeltaX() {
            auto *graph = SDK::Graphics::GetGraph();
            return gMouseDeltaXOriginal ? gMouseDeltaXOriginal(graph) : graph->vtable->mouseDeltaX(graph);
        }

        int MouseDeltaY() {
            auto *graph = SDK::Graphics::GetGraph();
            return gMouseDeltaYOriginal ? gMouseDeltaYOriginal(graph) : graph->vtable->mouseDeltaY(graph);
        }

        int MouseWheel() {
            auto *graph = SDK::Graphics::GetGraph();
            return gMouseWheelOriginal ? gMouseWheelOriginal(graph) : graph->vtable->mouseWheel(graph);
        }

        uint32_t MouseButtons() {
            auto *graph = SDK::Graphics::GetGraph();
            return gMouseButtonsOriginal ? gMouseButtonsOriginal(graph) : graph->vtable->mouseButtons(graph);
        }
    } // namespace Native
} // namespace Mafia1Online::Features::WebUi
