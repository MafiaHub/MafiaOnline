#include <utils/safe_win32.h>

#include "core/boot/lifecycle_hooks.h"
#include "features/compat/wine_fpu.h"
#include "features/menu/menu_hooks.h"
#include "features/display/system_init_hooks.h"
#include "features/profile/profile_write_hooks.h"
#include "features/quick_join/quick_join.h"
#include "features/startup_video/startup_video_hooks.h"
#include "shared/version.h"

#include <MinHook.h>
#include <logging/logger.h>
#include <mafia1/sdk/retail_image.h>
#include <utils/string_utils.h>

#include <cstdint>
#include <exception>
#include <stdexcept>

extern "C" void __declspec(dllexport) InitClient(const wchar_t *projectPath) {
    // The retail image contains absolute addresses and has no usable relocation
    // table. The launcher verifies its own base before the Framework PE loader runs.
    if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != Mafia1Online::SDK::RetailImage::kBase) {
        Framework::Logging::GetLogger("Mafia1Online")->critical("Mafia 1 image was not loaded at 0x400000");
        return;
    }

    auto *logging = Framework::Logging::GetInstance();
    logging->SetLogName("Mafia1Online");
    logging->SetLogFolder(Framework::Utils::StringUtils::WideToNormal(projectPath) + "\\logs");
    Framework::Logging::GetLogger("Mafia1Online")->info("Client {} loaded against the supported Mafia 1 image", Mafia1Online::Version::rel);

    if (MH_Initialize() != MH_OK) {
        Framework::Logging::GetLogger("Mafia1Online")->critical("MinHook initialization failed");
        ExitProcess(1);
    }
    try {
        const auto path = Framework::Utils::StringUtils::WideToNormal(projectPath);
        if (!Mafia1Online::Features::Compat::InstallWineFpuHook()) {
            throw std::runtime_error("Could not install Wine x87 compatibility hook");
        }
        Mafia1Online::Features::QuickJoin::Initialize(path);
        if (!Mafia1Online::Features::Profile::InstallWriteHooks()) {
            throw std::runtime_error("Could not install profile write guards");
        }
        if (!Mafia1Online::Features::Menu::InstallMenuHooks()) {
            throw std::runtime_error("Could not install native menu hooks");
        }
        if (!Mafia1Online::Features::StartupVideo::InstallHooks()) {
            throw std::runtime_error("Could not install intro video hook");
        }
        if (!Mafia1Online::Features::Display::InstallSystemInitHook()) {
            throw std::runtime_error("Could not hook InitSystem");
        }
        Mafia1Online::Core::Boot::InstallLifecycleHooks(path);
    }
    catch (const std::exception &error) {
        Framework::Logging::GetLogger("Mafia1Online")->critical("Lifecycle hook installation failed: {}", error.what());
        ExitProcess(1);
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Framework::Logging::GetLogger("Mafia1Online")->critical("Enabling lifecycle hooks failed");
        ExitProcess(1);
    }
}
