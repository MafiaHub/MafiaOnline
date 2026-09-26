#include <utils/safe_win32.h>

#include "world_hooks.h"

#include "core/application.h"
#include "features/quick_join/quick_join.h"

#include <MinHook.h>
#include <core_modules.h>
#include <mafia1/sdk/core/game.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/graphics/native_window.h>
#include <mafia1/sdk/menu/native_menu.h>
#include <mafia1/sdk/script/native_program.h>
#include <mafia1/sdk/rail/native_rail_generator.h>
#include <mafia1/sdk/rail/native_railway.h>
#include <mafia1/sdk/world/native_bridge.h>
#include <logging/logger.h>

#include <array>
#include <intrin.h>

namespace Mafia1Online::Features::World {
    namespace {
        using MissionClose                 = void(__thiscall *)(void *);
        MissionClose gMissionCloseOriginal = nullptr;
        using MenuExecute = uint32_t(__fastcall *)(void *, bool, bool);
        MenuExecute gMenuExecuteOriginal = nullptr;
        using MissionProgram = void(__thiscall *)(void *);
        using MissionProgramRun = void(__thiscall *)(void *, char *);
        using GameTick = void(__thiscall *)(SDK::Core::Game::NativeGame *, unsigned int);
        using ProgramCallProcess = bool(__thiscall *)(SDK::Script::NativeProgram *, unsigned int);
        using RailGeneratorAI = void(__thiscall *)(SDK::Rail::NativeRailGenerator *, unsigned int);
        using RailwayAI = void(__thiscall *)(SDK::Rail::NativeRailway *, unsigned int);
        using BridgeGameInit = void(__thiscall *)(SDK::World::NativeBridge *);
        MissionProgram gProgramInitOriginal = nullptr;
        MissionProgram gProgramDoneOriginal = nullptr;
        MissionProgramRun gProgramRunOriginal = nullptr;
        GameTick gGameTickOriginal = nullptr;
        ProgramCallProcess gProgramCallProcessOriginal = nullptr;
        RailGeneratorAI gRailGeneratorAIOriginal = nullptr;
        RailwayAI gRailwayAIOriginal = nullptr;
        BridgeGameInit gBridgeGameInitOriginal = nullptr;
        using WindowProcedure = LRESULT(__stdcall *)(HWND, UINT, WPARAM, LPARAM);
        WindowProcedure gWindowProcedureOriginal = nullptr;
        bool gNativeMissionActive = false;
        bool gWindowExitRequested = false;
        bool gReportedNativePlayerFailure = false;
        bool gReportedSuppressedFailureMenu = false;

        Core::Application &Application() {
            return *static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
        }

        void __fastcall MissionCloseHook(void *mission, void *) {
            Application().World().NotifyMissionClosing();
            gMissionCloseOriginal(mission);
        }

        LRESULT __stdcall WindowProcedureHook(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
            if (message != WM_CLOSE) {
                return gWindowProcedureOriginal(window, message, wParam, lParam);
            }

            gWindowExitRequested = true;
            if (gNativeMissionActive) {
                if (auto *menu = SDK::Menu::GetActiveMenu()) {
                    SDK::Menu::CloseAll(menu, 0);
                }
                Application().World().ExitGameLoop();
            }
            else if (auto *menu = SDK::Menu::GetActiveMenu()) {
                SDK::Menu::CloseAll(menu, menu->IsProfileSelect() ? SDK::Menu::kProfileBackResult : SDK::Menu::kMainMenuExitResult);
            }
            return 0;
        }

        uint32_t __fastcall MenuExecuteHook(void *menu, bool tickMission, bool unused) {
            if (gWindowExitRequested && menu) {
                auto *nativeMenu = static_cast<SDK::Menu::NativeMenu *>(menu);
                if (nativeMenu->IsProfileSelect() || nativeMenu->IsMainMenu()) {
                    const uint32_t result = nativeMenu->IsProfileSelect() ? SDK::Menu::kProfileBackResult : SDK::Menu::kMainMenuExitResult;
                    nativeMenu->Destroy();
                    return result;
                }
            }
            if (auto result = Features::QuickJoin::HandleProfileMenu(menu)) {
                return *result;
            }
            if (gNativeMissionActive && menu && static_cast<SDK::Menu::NativeMenu *>(menu)->IsGameFailed()) {
                if (!gReportedSuppressedFailureMenu) {
                    gReportedSuppressedFailureMenu = true;
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn(
                        "Suppressed retail Game Over during server mission (caller {:p})", _ReturnAddress());
                }
                static_cast<SDK::Menu::NativeMenu *>(menu)->Destroy();
                return 0;
            }
            const bool mainMenu = SDK::Menu::IsMainMenu(menu);
            if (gNativeMissionActive) {
                if (auto result = Application().GameplayMenus().HandleMenu(static_cast<SDK::Menu::NativeMenu *>(menu))) return *result;
            }
            const uint32_t result = gMenuExecuteOriginal(menu, tickMission, unused);
            if (mainMenu) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Mafia main menu closed with result {}", result);
            }
            if (mainMenu && result == 20u && !gWindowExitRequested) {
                Application().World().RunRequestedMission();
            }
            return mainMenu && gWindowExitRequested ? SDK::Menu::kMainMenuExitResult : result;
        }

        void __fastcall ProgramInitHook(void *mission, void *) {
            if (!gNativeMissionActive) {
                gProgramInitOriginal(mission);
            }
        }

        void __fastcall ProgramDoneHook(void *mission, void *) {
            if (!gNativeMissionActive) {
                gProgramDoneOriginal(mission);
            }
        }

        void __fastcall ProgramRunHook(void *mission, void *, char *name) {
            if (!gNativeMissionActive) {
                gProgramRunOriginal(mission, name);
            }
        }

        bool __fastcall ProgramCallProcessHook(SDK::Script::NativeProgram *program, void *, unsigned int frameTimeMs) {
            if (gNativeMissionActive) {
                return false;
            }
            return gProgramCallProcessOriginal(program, frameTimeMs);
        }

        void __fastcall RailGeneratorAIHook(SDK::Rail::NativeRailGenerator *generator, void *, unsigned int frameTimeMs) {
            if (!gNativeMissionActive) {
                gRailGeneratorAIOriginal(generator, frameTimeMs);
            }
        }

        void __fastcall RailwayAIHook(SDK::Rail::NativeRailway *railway, void *, unsigned int frameTimeMs) {
            const auto &mission = Application().World().SelectedMission();
            if (!gNativeMissionActive || (mission != "freeride" && mission != "freeridenoc")) {
                gRailwayAIOriginal(railway, frameTimeMs);
            }
        }

        void __fastcall BridgeGameInitHook(SDK::World::NativeBridge *bridge, void *) {
            gBridgeGameInitOriginal(bridge);
            if (gNativeMissionActive) {
                bridge->ShutDown(true);
            }
        }

        void __fastcall GameTickHook(SDK::Core::Game::NativeGame *game, void *, unsigned int frameTimeMs) {
            if (gNativeMissionActive) {
                const auto *player = game->Player();
                if (!gReportedNativePlayerFailure && (game->PlayerDeathTriggered() || (player && player->IsDead()))) {
                    gReportedNativePlayerFailure = true;
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn(
                        "Native player failure state: player {:p}, dead {}, death triggered {}, death timer {}",
                        static_cast<const void *>(player), player && player->IsDead(),
                        game->PlayerDeathTriggered(), game->PlayerDeathMenuTimer());
                }
                game->ClearPlayerDeathTriggered();
                if (player && player->IsDead()) {
                    game->KeepPlayerDeathMenuPending();
                }
            }
            gGameTickOriginal(game, frameTimeMs);
        }

        constexpr std::array<uintptr_t, 9> kHookAddresses {
            SDK::Core::Mission::kClose,
            SDK::Core::Mission::kGlobalProgramGameInit,
            SDK::Core::Mission::kGlobalProgramGameDone,
            SDK::Core::Mission::kGlobalProgramRun,
            SDK::Core::Game::kTick,
            SDK::Script::NativeProgram::kCallProcess,
            SDK::Rail::NativeRailGenerator::kAI,
            SDK::Rail::NativeRailway::kAI,
            SDK::World::kBridgeGameInit,
        };
    } // namespace

    bool InstallMenuExecuteHook() {
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Menu::kExecuteMenu), reinterpret_cast<void *>(&MenuExecuteHook), reinterpret_cast<void **>(&gMenuExecuteOriginal)) != MH_OK) {
            return false;
        }
        if (MH_CreateHook(SDK::Graphics::WindowProcedure(), reinterpret_cast<void *>(&WindowProcedureHook), reinterpret_cast<void **>(&gWindowProcedureOriginal)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Menu::kExecuteMenu));
            return false;
        }
        return true;
    }

    bool InstallWorldHooks() {
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Core::Mission::kClose), reinterpret_cast<void *>(&MissionCloseHook), reinterpret_cast<void **>(&gMissionCloseOriginal)) != MH_OK) {
            return false;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Core::Mission::kGlobalProgramGameInit), reinterpret_cast<void *>(&ProgramInitHook), reinterpret_cast<void **>(&gProgramInitOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Core::Mission::kGlobalProgramGameDone), reinterpret_cast<void *>(&ProgramDoneHook), reinterpret_cast<void **>(&gProgramDoneOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Core::Mission::kGlobalProgramRun), reinterpret_cast<void *>(&ProgramRunHook), reinterpret_cast<void **>(&gProgramRunOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Core::Game::kTick), reinterpret_cast<void *>(&GameTickHook), reinterpret_cast<void **>(&gGameTickOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Script::NativeProgram::kCallProcess), reinterpret_cast<void *>(&ProgramCallProcessHook), reinterpret_cast<void **>(&gProgramCallProcessOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Rail::NativeRailGenerator::kAI), reinterpret_cast<void *>(&RailGeneratorAIHook), reinterpret_cast<void **>(&gRailGeneratorAIOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Rail::NativeRailway::kAI), reinterpret_cast<void *>(&RailwayAIHook), reinterpret_cast<void **>(&gRailwayAIOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::World::kBridgeGameInit), reinterpret_cast<void *>(&BridgeGameInitHook), reinterpret_cast<void **>(&gBridgeGameInitOriginal)) != MH_OK) {
            UninstallWorldHooks();
            return false;
        }
        for (const uintptr_t address : kHookAddresses) {
            if (MH_EnableHook(reinterpret_cast<void *>(address)) != MH_OK) {
                UninstallWorldHooks();
                return false;
            }
        }
        return true;
    }

    void UninstallWorldHooks() {
        for (const uintptr_t address : kHookAddresses) {
            MH_DisableHook(reinterpret_cast<void *>(address));
            MH_RemoveHook(reinterpret_cast<void *>(address));
        }
        MH_DisableHook(reinterpret_cast<void *>(SDK::Menu::kExecuteMenu));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Menu::kExecuteMenu));
        MH_DisableHook(SDK::Graphics::WindowProcedure());
        MH_RemoveHook(SDK::Graphics::WindowProcedure());
        gNativeMissionActive = false;
        gReportedNativePlayerFailure = false;
        gReportedSuppressedFailureMenu = false;
    }

    void SetNativeMissionActive(bool active) {
        gNativeMissionActive = active;
        if (!active) {
            gReportedNativePlayerFailure = false;
            gReportedSuppressedFailureMenu = false;
        }
    }

    bool NativeMissionActive() {
        return gNativeMissionActive;
    }

    bool WindowExitRequested() {
        return gWindowExitRequested;
    }
} // namespace Mafia1Online::Features::World
