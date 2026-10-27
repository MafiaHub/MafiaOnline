#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace Mafia1Online::Features::QuickJoin {
    struct Config {
        bool enabled = false;
        std::string nickname = "Player";
        std::string host = "127.0.0.1";
        uint16_t port = 27015;
        std::string password;
        uint32_t profileIndex = 0;
    };

    // config\client.json, or MAFIA1ONLINE_CLIENT_CONFIG when that is set.
    std::filesystem::path ConfigPath(const std::string &projectPath);
    void Initialize(const std::string &projectPath);
    const Config &GetConfig();
    std::optional<uint32_t> HandleProfileMenu(void *menu);
} // namespace Mafia1Online::Features::QuickJoin
