#include <utils/safe_win32.h>

#include "quick_join.h"

#include "features/menu/menu_hooks.h"

#include <logging/logger.h>
#include <mafia1/sdk/menu/native_menu.h>
#include <mafia1/sdk/profile/native_profile.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace Mafia1Online::Features::QuickJoin {
    namespace {
        Config gConfig;

        bool ValidHost(const std::string &host) {
            return !host.empty() && host.size() <= 255 && std::all_of(host.begin(), host.end(), [](unsigned char ch) { return ch >= 32 && ch < 127; });
        }
    } // namespace

    void Initialize(const std::string &projectPath) {
        const char *configOverride = std::getenv("MAFIA1ONLINE_CLIENT_CONFIG");
        const auto path = configOverride && *configOverride ? std::filesystem::path(configOverride) : std::filesystem::path(projectPath) / "config" / "client.json";
        std::ifstream input(path);
        if (!input) {
            return;
        }
        try {
            const auto root = nlohmann::json::parse(input);
            const auto &settings = root.at("quickJoin");
            if (!settings.is_object()) {
                throw std::invalid_argument("quickJoin must be an object");
            }
            Config configured;
            if (const auto profile = settings.find("profileIndex"); profile != settings.end()) {
                configured.profileIndex = profile->get<uint32_t>();
            }
            if (configured.profileIndex > 255) {
                throw std::invalid_argument("invalid quickJoin.profileIndex");
            }
            configured.enabled = settings.at("enabled").get<bool>();
            if (!configured.enabled) {
                gConfig = std::move(configured);
                return;
            }
            configured.nickname = settings.at("nickname").get<std::string>();
            configured.host = settings.at("host").get<std::string>();
            const auto port = settings.at("port").get<uint32_t>();
            configured.password = settings.at("password").get<std::string>();
            if (configured.nickname.empty() || configured.nickname.size() > 64 || !ValidHost(configured.host) || port == 0 || port > 65535 || configured.password.size() > 128) {
                throw std::invalid_argument("invalid quickJoin value");
            }
            configured.port = static_cast<uint16_t>(port);
            gConfig = std::move(configured);
            Menu::SetConnectionDefaults(gConfig.nickname, gConfig.host, gConfig.port, gConfig.password);
            Framework::Logging::GetLogger("Mafia1Online")->info("Quick join enabled from repository config; profile index {}", gConfig.profileIndex);
        }
        catch (const std::exception &) {
            Framework::Logging::GetLogger("Mafia1Online")->error("Invalid client configuration; quick join is disabled");
            gConfig = {};
        }
    }

    const Config &GetConfig() {
        return gConfig;
    }

    // The profile only carries bindings and settings here, so the retail
    // picker is skipped: the configured profile, else the first one.
    std::optional<uint32_t> HandleProfileMenu(void *menu) {
        if (!menu || !static_cast<SDK::Menu::NativeMenu *>(menu)->IsProfileSelect()) {
            return std::nullopt;
        }
        auto &loadSave = SDK::Profile::Get();
        if (!loadSave.HasProfile(0)) {
            Framework::Logging::GetLogger("Mafia1Online")->error("Mafia1Online requires an existing Mafia profile; create one in the original game first");
            MessageBoxW(nullptr, L"Mafia1Online needs an existing Mafia profile. Create one in the original game, then launch Mafia1Online again.", L"Mafia1Online", MB_ICONERROR);
            static_cast<SDK::Menu::NativeMenu *>(menu)->Destroy();
            return SDK::Menu::kProfileBackResult;
        }
        uint32_t index = gConfig.profileIndex;
        if (!loadSave.HasProfile(index)) {
            Framework::Logging::GetLogger("Mafia1Online")->warn("Configured profile index {} is absent; using the first profile", index);
            index = 0;
        }
        loadSave.SelectProfile(index);
        static_cast<SDK::Menu::NativeMenu *>(menu)->Destroy();
        Framework::Logging::GetLogger("Mafia1Online")->info("Selected existing Mafia profile index {}", index);
        return SDK::Menu::kProfileSelectedResult;
    }
} // namespace Mafia1Online::Features::QuickJoin
