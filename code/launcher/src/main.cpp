#include <Windows.h>

#include <launcher/project.h>
#include <logging/logger.h>
#include <mafia1/sdk/retail_image.h>
#include <utils/crypto.h>
#include <utils/minidump.h>

#include <cstdint>
#include <filesystem>
#include <iterator>
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

    bool ContainsGameExecutable(const std::filesystem::path &folder) {
        std::error_code error;
        return std::filesystem::is_regular_file(folder / L"Game.exe", error);
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

    Framework::Launcher::ProjectConfiguration config;
    config.destinationDllName          = L"Mafia1OnlineClient.dll";
    config.executableName              = L"Game.exe";
    config.name                        = "Mafia1Online";
    config.platform                    = Framework::Launcher::ProjectPlatform::STEAM;
    config.launchType                  = Framework::Launcher::ProjectLaunchType::PE_LOADING;
    config.steamAppId                  = Mafia1Online::SDK::RetailImage::kSteamAppId;
    config.useAlternativeWorkDir       = true;
    config.alternativeWorkDir          = L"Mafia";
    config.allowManualGamePathFallback = true;
    config.promptTitle                 = "Select a Mafia Game.exe";
    config.promptFilter                = "Game.exe";
    config.promptFilterName            = "Game.exe";
    config.disablePersistentConfig     = true;
    config.writeSteamAppIdFile         = false;
    config.promptSelectionFunctor = [&config](std::wstring gameRoot) {
        const std::filesystem::path root(gameRoot);
        if (ContainsGameExecutable(root / L"Mafia")) {
            config.useAlternativeWorkDir = true;
        }
        else if (ContainsGameExecutable(root)) {
            config.useAlternativeWorkDir = false;
        }
        return gameRoot;
    };

    // A direct path supports copies outside Steam, including flat installs.
    // The executable hash is still validated before it is loaded.
    const DWORD gameRootLength = GetEnvironmentVariableW(L"MAFIA1ONLINE_GAME_ROOT", nullptr, 0);
    if (gameRootLength > 1) {
        std::wstring gameRoot(gameRootLength, L'\0');
        gameRoot.resize(GetEnvironmentVariableW(L"MAFIA1ONLINE_GAME_ROOT", gameRoot.data(), gameRootLength));
        config.platform         = Framework::Launcher::ProjectPlatform::CLASSIC;
        config.classicGamePath  = std::move(gameRoot);
        config.promptForGameExe = false;
        if (!ContainsGameExecutable(std::filesystem::path(config.classicGamePath) / L"Mafia") && ContainsGameExecutable(config.classicGamePath)) {
            config.useAlternativeWorkDir = false;
        }
    }
    Framework::Launcher::Project project(config);
    project.SetGameExecutableValidator(ValidateGameExecutable);
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
