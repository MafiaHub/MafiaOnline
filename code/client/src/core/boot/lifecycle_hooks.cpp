#include <utils/safe_win32.h>

#include "lifecycle_hooks.h"

#include "core/application_module.h"
#include "features/world/world_hooks.h"
#include "features/profile/profile_write_hooks.h"
#include "features/display/system_init_hooks.h"
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/core/system.h>
#include <mafia1/sdk/graphics/native_graph.h>

#include <MinHook.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace Mafia1Online::Core::Boot {
    namespace {
        using MissionTick = void(__thiscall *)(void *, unsigned int);
        using CloseSystem = void(__cdecl *)();

        MissionTick gMissionTickOriginal = nullptr;
        CloseSystem gCloseSystemOriginal = nullptr;
        std::unique_ptr<ApplicationModule> gApplicationModule;

        void __fastcall MissionTickHook(void *mission, void *, unsigned int frameTimeMs) {
            gMissionTickOriginal(mission, frameTimeMs);
            gApplicationModule->OnTick();
        }

        void __cdecl CloseSystemHook() {
            // Quitting runs CEF's and the engine's teardown for a few seconds
            // with nothing left to draw. Hide the window first so the quit is
            // immediate for the player; IGraph owns it until the original
            // releases the graphics driver. The graph is absent when
            // initialization failed before creating it.
            if (auto *graph = SDK::Graphics::GetGraph()) {
                ShowWindow(static_cast<HWND>(graph->MainWindow()), SW_HIDE);
            }
            gApplicationModule->OnShutdown();
            MH_DisableHook(reinterpret_cast<void *>(SDK::Core::Mission::kTick));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Core::System::kClose));
            Features::Display::UninstallSystemInitHook();
            gCloseSystemOriginal();
            Features::Profile::UninstallWriteHooks();
        }
    } // namespace

    void InstallLifecycleHooks(std::string projectPath) {
        gApplicationModule = std::make_unique<ApplicationModule>(std::move(projectPath));
        if (!Features::World::InstallMenuExecuteHook()) {
            throw std::runtime_error("Could not hook GM_Menu::ExecuteMenu before the first menu loop");
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Core::Mission::kTick), reinterpret_cast<void *>(&MissionTickHook), reinterpret_cast<void **>(&gMissionTickOriginal)) != MH_OK) {
            throw std::runtime_error("Could not hook C_mission::Tick");
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Core::System::kClose), reinterpret_cast<void *>(&CloseSystemHook), reinterpret_cast<void **>(&gCloseSystemOriginal)) != MH_OK) {
            throw std::runtime_error("Could not hook CloseSystem");
        }
    }
} // namespace Mafia1Online::Core::Boot
