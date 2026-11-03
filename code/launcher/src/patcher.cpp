#include "patcher.h"

#include <Windows.h>

#include <Shellapi.h>

#include <mafia1online_patcher_config.h>
#include <services/build_patcher_ui.h>
#include <utils/string_utils.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Mafia1Online::Launcher {
    namespace {
        const char *PatchErrorName(Framework::Services::Builds::BuildPatchError error) {
            switch (error) {
            case Framework::Services::Builds::BuildPatchError::None: return "none";
            case Framework::Services::Builds::BuildPatchError::InvalidConfiguration: return "invalid configuration";
            case Framework::Services::Builds::BuildPatchError::Network: return "network";
            case Framework::Services::Builds::BuildPatchError::Authentication: return "authentication";
            case Framework::Services::Builds::BuildPatchError::InvalidManifest: return "invalid manifest";
            case Framework::Services::Builds::BuildPatchError::HashMismatch: return "hash mismatch";
            case Framework::Services::Builds::BuildPatchError::Filesystem: return "filesystem";
            case Framework::Services::Builds::BuildPatchError::Updater: return "updater";
            default: return "unknown";
            }
        }

        // argv[0] is dropped: the updater supplies the executable itself.
        std::vector<std::string> RestartArguments() {
            std::vector<std::string> args;
            int argc  = 0;
            auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (!argv) {
                return args;
            }
            for (int i = 1; i < argc; ++i) {
                args.push_back(Framework::Utils::StringUtils::WideToNormal(argv[i]));
            }
            LocalFree(argv);
            return args;
        }

        std::optional<std::string> NormalizeBuildChannel(std::string channel) {
            const auto first = channel.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                return std::nullopt;
            }
            channel.erase(0, first);
            channel.erase(channel.find_last_not_of(" \t\r\n") + 1);
            channel = Framework::Utils::StringUtils::ToLower(std::move(channel));
            if (channel.size() > 32 || !std::all_of(channel.begin(), channel.end(), [](unsigned char value) { return std::islower(value) || std::isdigit(value) || value == '.' || value == '_' || value == '-'; })) {
                return std::nullopt;
            }
            return channel;
        }

        std::string ReadChannelFile(const std::filesystem::path &path) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return {};
            }
            return NormalizeBuildChannel(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>())).value_or(std::string {});
        }

        // A standing choice, and the only thing here that outranks the build
        // an installation is running. Absent unless somebody put it there, so
        // the default is simply the current release: write `testing` into
        // .mafiahub/channel-override to follow prereleases, delete it to stop.
        //
        // Separate from the stamp below on purpose. The stamp was written by
        // whoever packaged the archive and goes stale the moment the build is
        // promoted; this was written by the person running it, and does not.
        std::string ResolvePinnedChannel(const std::filesystem::path &installRoot) {
            return ReadChannelFile(installRoot / ".mafiahub" / "channel-override");
        }

        // Only ever a seed, for the first start of an installation that has
        // not patched yet and so has no manifest to resolve a channel from.
        // After that the patcher asks Services which channel the build it is
        // running is served on, and a promotion reaches us without this file
        // being touched.
        //
        // `stable` when there is nothing to read, which is the honest default
        // for a download that came from the portal. An installation whose
        // files were copied out of the archive rather than unpacked used to
        // dead-end on a modal here; it now simply starts on the current
        // release, and its first patch replaces the guess with an answer.
        std::string ResolveSeedChannel(const std::filesystem::path &installRoot) {
            const auto stamped = ReadChannelFile(installRoot / ".mafiahub" / "channel");
            return stamped.empty() ? std::string {"stable"} : stamped;
        }

        // A download whose updates another storefront owns (a mod site's
        // archive, updated through its own manager) ships without
        // MafiaHubUpdater.exe and with this stamp in place of the channel.
        // Patching it would pull the updater back down with the rest of the
        // manifest and fight that storefront over the same files.
        bool IsExternallyDistributed(const std::filesystem::path &installRoot) {
            std::error_code error;
            return std::filesystem::exists(installRoot / ".mafiahub" / "distribution", error);
        }

        void ReplaceAll(std::string &value, const std::string &from, const std::string &to) {
            if (from.empty()) {
                return;
            }
            size_t start = 0;
            while ((start = value.find(from, start)) != std::string::npos) {
                value.replace(start, from.size(), to);
                start += to.size();
            }
        }

        // Offline is not worth a modal box: patcher.log records it, and an
        // outdated client is refused by the server anyway.
        bool PatchFailureIsUnreachableService(Framework::Services::Builds::BuildPatchError error) {
            return error == Framework::Services::Builds::BuildPatchError::Network || error == Framework::Services::Builds::BuildPatchError::Authentication;
        }

        void WarnPatchFailure(const Framework::Services::Builds::BuildPatchResult &result) {
            std::ostringstream message;
            message << "Mafia1Online could not check for or apply the latest update.\n\n";
            message << "The launcher will continue with the existing local files, but online play may fail if this build is outdated.\n\n";
            message << "Reason: " << PatchErrorName(result.error);

            std::string detail = result.message;
            ReplaceAll(detail, Mafia1Online::LauncherPatcherConfig::DownloadToken, "[redacted]");
            if (!detail.empty()) {
                message << " (" << detail << ")";
            }

            MessageBoxA(nullptr, message.str().c_str(), "Mafia1Online", MB_ICONWARNING);
        }
    } // namespace

    bool RunBuildPatcher(const std::filesystem::path &launcherExecutable) {
        if constexpr (!Mafia1Online::LauncherPatcherConfig::Enabled) {
            (void)launcherExecutable;
            return true;
        }
        else {
            const auto installRoot = launcherExecutable.parent_path();
            if (IsExternallyDistributed(installRoot)) {
                return true;
            }

            Framework::Services::Builds::BuildPatcherConfig config;
            config.apiUrl              = Mafia1Online::LauncherPatcherConfig::ApiUrl;
            config.modId               = Mafia1Online::LauncherPatcherConfig::ModId;
            config.channel             = ResolveSeedChannel(installRoot);
            config.pinnedChannel       = ResolvePinnedChannel(installRoot);
            config.downloadToken       = Mafia1Online::LauncherPatcherConfig::DownloadToken;
            config.installRoot         = installRoot;
            config.launcherExecutable  = launcherExecutable;
            config.updaterExecutable   = installRoot / L"MafiaHubUpdater.exe";
            config.restartArguments    = RestartArguments();
            config.downloadConcurrency = Mafia1Online::LauncherPatcherConfig::DownloadConcurrency;

            Framework::Services::Builds::BuildPatchUiConfig uiConfig;
            uiConfig.title       = L"Mafia1Online Updater";
            uiConfig.productName = L"Mafia1Online";

            const auto result = Framework::Services::Builds::RunBuildPatcherWithProgressWindow(std::move(config), uiConfig);
            if (result.RequiresRestart()) {
                return false;
            }
            if (!result.Succeeded() && !PatchFailureIsUnreachableService(result.error)) {
                WarnPatchFailure(result);
            }
            return true;
        }
    }
} // namespace Mafia1Online::Launcher
