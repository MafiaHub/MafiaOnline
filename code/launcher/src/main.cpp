#include <Windows.h>

#include "patcher.h"

#include <launcher/project.h>
#include <logging/logger.h>
#include <mafia1/sdk/retail_image.h>
#include <utils/crypto.h>
#include <utils/minidump.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace {
    LONG WINAPI StopAfterCrash(EXCEPTION_POINTERS *exception) {
        const LONG result = Framework::Utils::MiniDump::ExceptionFilter(exception);
        if (result != EXCEPTION_CONTINUE_EXECUTION) {
            // Returning to Wine's unhandled-exception path opens WineDbg and
            // leaves the game and CEF audio helpers alive behind a frozen window.
            TerminateProcess(GetCurrentProcess(), exception->ExceptionRecord->ExceptionCode);
        }
        return result;
    }

    bool ValidateGameExecutable(const std::wstring &path) {
        const auto actual = Framework::Utils::Crypto::Sha256FileHex(std::filesystem::path(path));
        if (Framework::Utils::Crypto::ConstantTimeEquals(actual, Mafia1Online::SDK::RetailImage::kSha256)) {
            return true;
        }

        const std::wstring message = actual.empty() ? L"Could not read the selected Game.exe." : L"This Game.exe build is unsupported. Mafia1Online currently requires SHA-256 303eb95e...3f298.";
        MessageBoxW(nullptr, message.c_str(), L"Mafia1Online", MB_ICONERROR);
        return false;
    }

    // MafiaEntryPoint creates this mutex at 0x5bec16 and returns without
    // starting when GetLastError reports ERROR_ALREADY_EXISTS, so a second
    // local client would never open. The handle is discarded; nothing else
    // opens the name. See docs/native_contract.md.
    constexpr char kSingleInstanceMutex[] = "Mafia Launcher Super Mutex Shit";

    HANDLE WINAPI CreateMutexA_Stub(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCSTR name) {
        if (name == nullptr || std::strcmp(name, kSingleInstanceMutex) != 0) {
            return CreateMutexA(attributes, initialOwner, name);
        }
        HANDLE mutex = CreateMutexA(attributes, initialOwner, nullptr);
        SetLastError(ERROR_SUCCESS);
        return mutex;
    }
} // namespace

int main() {
    if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != Mafia1Online::SDK::RetailImage::kBase) {
        MessageBoxW(nullptr, L"Mafia1OnlineLauncher must load at 0x400000 to host Game.exe.", L"Mafia1Online", MB_ICONERROR);
        return 1;
    }

    // The framework resolves the client DLL and its resources from the working
    // directory. Anchor it to this launcher so shortcuts can start elsewhere.
    wchar_t launcherPath[32768];
    const DWORD launcherPathLength = GetModuleFileNameW(nullptr, launcherPath, static_cast<DWORD>(std::size(launcherPath)));
    if (launcherPathLength == 0 || launcherPathLength >= std::size(launcherPath) || !SetCurrentDirectoryW(std::filesystem::path(launcherPath).parent_path().c_str())) {
        MessageBoxW(nullptr, L"Could not use the launcher directory.", L"Mafia1Online", MB_ICONERROR);
        return 1;
    }

    // Patch first: the updater replaces files this process would hold open.
    if (!Mafia1Online::Launcher::RunBuildPatcher(std::filesystem::path(std::wstring(launcherPath, launcherPathLength)))) {
        return 0; // MafiaHubUpdater.exe relaunches us
    }

    Framework::Launcher::ProjectConfiguration config;
    config.destinationDllName      = L"Mafia1OnlineClient.dll";
    config.executableName          = L"Game.exe";
    config.name                    = "Mafia1Online";
    config.launchType              = Framework::Launcher::ProjectLaunchType::PE_LOADING;
    // Steam nests the game in a Mafia folder; a flat copy keeps Game.exe at the root, which
    // the launcher falls back to when the nested layout is missing.
    config.useAlternativeWorkDir   = true;
    config.alternativeWorkDir      = L"Mafia";

    // A direct path supports copies outside Steam, including flat installs.
    // The executable hash is still validated before it is loaded.
    const DWORD gameRootLength = GetEnvironmentVariableW(L"MAFIA1ONLINE_GAME_ROOT", nullptr, 0);
    if (gameRootLength > 1) {
        std::wstring gameRoot(gameRootLength, L'\0');
        gameRoot.resize(GetEnvironmentVariableW(L"MAFIA1ONLINE_GAME_ROOT", gameRoot.data(), gameRootLength));
        config.classicGamePath         = std::move(gameRoot);
        // A stored pick would override the requested copy.
        config.disablePersistentConfig = true;
        config.platforms               = {std::make_shared<Framework::Launcher::Platforms::Classic>(Framework::Launcher::Platforms::Classic::Options {.prompt = false})};
    }
    else {
        // Without Steam, or without Mafia in its library, the player selects Game.exe once; the
        // pick is kept in Mafia1Online_launcher.json beside the launcher and wins on later runs.
        Framework::Launcher::Platforms::Classic::Options manual;
        manual.title      = "Select a Mafia Game.exe";
        manual.filter     = "Game.exe";
        manual.filterName = "Game.exe";
        config.platforms  = {
            std::make_shared<Framework::Launcher::Platforms::Steam>(Framework::Launcher::Platforms::Steam::Options {.appId = Mafia1Online::SDK::RetailImage::kSteamAppId, .writeAppIdFile = false}),
            std::make_shared<Framework::Launcher::Platforms::Classic>(std::move(manual)),
        };
    }
    Framework::Launcher::Project project(config);
    project.SetGameExecutableValidator(ValidateGameExecutable);
    project.SetFunctionResolver([](HMODULE, const char *name) -> LPVOID {
        return std::strcmp(name, "CreateMutexA") == 0 ? reinterpret_cast<LPVOID>(&CreateMutexA_Stub) : nullptr;
    });
    SetUnhandledExceptionFilter(StopAfterCrash);

    // CEF helpers outlive a faulted browser process unless the OS owns their
    // lifetime. Keep them in a job that closes with this launcher.
    HANDLE processJob = CreateJobObjectW(nullptr, nullptr);
    if (processJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(processJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
            || !AssignProcessToJobObject(processJob, GetCurrentProcess())) {
            const DWORD error = GetLastError();
            Framework::Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER, false)->warn("Could not bind CEF helpers to launcher lifetime (Win32 error {})", error);
            CloseHandle(processJob);
            processJob = nullptr;
        }
    }
    else {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER, false)->warn("Could not create CEF helper job (Win32 error {})", GetLastError());
    }

    const bool launched = project.Launch();
    if (processJob) {
        CloseHandle(processJob);
    }
    return launched ? 0 : 1;
}
