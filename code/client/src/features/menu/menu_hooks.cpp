#include "menu_hooks.h"

#include "core/application.h"

#include <utils/safe_win32.h>

#include <MinHook.h>
#include <core_modules.h>
#include <mafia1/sdk/menu/native_menu.h>
#include <mafia1/sdk/ui/native_hud.h>

#include <utility>
#include <vector>

namespace Mafia1Online::Features::Menu {
    namespace {
        using MainMenuCreate = int(__thiscall *)(void *);
        using MainMenuClick  = int(__thiscall *)(void *, uint32_t);
        using DrawCursor     = void(__thiscall *)(void *, float, float);

        MainMenuCreate gOriginalCreate  = nullptr;
        MainMenuClick gOriginalClick    = nullptr;
        DrawCursor gOriginalDrawCursor  = nullptr;
        std::optional<Request> gPendingRequest;
        std::string gNickname = "Player";
        std::string gHost     = "127.0.0.1";
        uint16_t gPort        = 27015;
        std::string gPassword;
        std::string gStatus      = "Disconnected";
        // Hide retail controls from the first menu frame while CEF loads.
        bool gSuppressed         = true;
        bool gConnectionActive   = false;
        bool gPlayAvailable      = false;
        // Components this module hid in the live main menu, so lifting the
        // suppression restores exactly the retail visibility.
        SDK::Menu::NativeMenu *gHiddenMenu = nullptr;
        std::vector<SDK::Menu::NativeComponent *> gHidden;

        // Without the web UI the connection comes from quick join, so the
        // retail main menu keeps only Exit.
        bool KeepsFallbackItem(const SDK::Menu::NativeComponent *component) {
            return component->Id() == SDK::Menu::kMainMenuExitItem;
        }

        bool IsMainMenuItem(const SDK::Menu::NativeComponent *component) {
            return component->Id() / 100 == SDK::Menu::kMainMenuExitItem / 100;
        }

        void ApplyVisibility(SDK::Menu::NativeMenu *menu) {
            if (gHiddenMenu != menu) {
                gHidden.clear();
                gHiddenMenu = menu;
            }
            if (!gSuppressed) {
                for (auto *component : gHidden) {
                    component->SetVisible(true);
                }
                gHidden.clear();
            }
            for (auto it = menu->ComponentsBegin(); it != menu->ComponentsEnd(); ++it) {
                auto *component = *it;
                const bool hide = gSuppressed || (IsMainMenuItem(component) && !KeepsFallbackItem(component));
                if (hide && component->Visible()) {
                    component->SetVisible(false);
                    if (gSuppressed) {
                        gHidden.push_back(component);
                    }
                }
            }
        }

        void __fastcall DrawCursorHook(void *indicators, void *, float x, float y) {
            if (!gSuppressed) {
                gOriginalDrawCursor(indicators, x, y);
            }
        }

        int __fastcall MainMenuCreateHook(void *menu, void *) {
            const int result = gOriginalCreate(menu);
            gHidden.clear();
            gHiddenMenu = nullptr;
            ApplyVisibility(static_cast<SDK::Menu::NativeMenu *>(menu));
            return result;
        }

        int __fastcall MainMenuClickHook(void *menu, void *, uint32_t componentId) {
            if (!gSuppressed && componentId == SDK::Menu::kMainMenuExitItem) {
                return gOriginalClick(menu, componentId);
            }
            return 0;
        }

        constexpr uintptr_t kHookAddresses[] = {SDK::Menu::kMainMenuOnCreate, SDK::Menu::kMainMenuOnClick, SDK::UI::kDrawCursor};
    } // namespace

    bool InstallMenuHooks() {
        return MH_CreateHook(reinterpret_cast<void *>(SDK::Menu::kMainMenuOnCreate), reinterpret_cast<void *>(&MainMenuCreateHook), reinterpret_cast<void **>(&gOriginalCreate)) == MH_OK
            && MH_CreateHook(reinterpret_cast<void *>(SDK::Menu::kMainMenuOnClick), reinterpret_cast<void *>(&MainMenuClickHook), reinterpret_cast<void **>(&gOriginalClick)) == MH_OK
            && MH_CreateHook(reinterpret_cast<void *>(SDK::UI::kDrawCursor), reinterpret_cast<void *>(&DrawCursorHook), reinterpret_cast<void **>(&gOriginalDrawCursor)) == MH_OK;
    }

    void UninstallMenuHooks() {
        for (const uintptr_t address : kHookAddresses) {
            MH_DisableHook(reinterpret_cast<void *>(address));
            MH_RemoveHook(reinterpret_cast<void *>(address));
        }
        gPendingRequest.reset();
        gHidden.clear();
        gHiddenMenu = nullptr;
    }

    std::optional<Request> TakeRequest() {
        return std::exchange(gPendingRequest, std::nullopt);
    }

    void SetStatus(const std::string &status) {
        gStatus = status.substr(0, 240);
    }

    void SetConnectionActive(bool active) {
        gConnectionActive = active;
    }

    void SetPlayAvailable(bool available) {
        gPlayAvailable = available;
    }

    void SetConnectionDefaults(const std::string &nickname, const std::string &host, uint16_t port, const std::string &password) {
        gNickname = nickname;
        gHost     = host;
        gPort     = port;
        gPassword = password;
    }

    State GetState() {
        return State {
            .status           = gStatus,
            .connectionActive = gConnectionActive,
            .playAvailable    = gPlayAvailable,
            .nickname         = gNickname,
            .host             = gHost,
            .password         = gPassword,
            .port             = gPort,
        };
    }

    void Submit(Request request) {
        if (request.action == Action::Connect) {
            SetConnectionDefaults(request.nickname, request.host, request.port, request.password);
        }
        gPendingRequest = std::move(request);
    }

    bool RequestPlay() {
        if (!gConnectionActive || !gPlayAvailable) {
            return false;
        }
        auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
        if (!application->World().RequestEnterGame()) {
            SetStatus("Mission is not ready to enter");
            return false;
        }
        return true;
    }

    void SetNativeControlsSuppressed(bool suppressed) {
        if (gSuppressed == suppressed) {
            return;
        }
        gSuppressed = suppressed;
        if (auto *menu = SDK::Menu::GetActiveMainMenu()) {
            ApplyVisibility(menu);
        }
    }
} // namespace Mafia1Online::Features::Menu
