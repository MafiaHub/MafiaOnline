#pragma once

#include <cstdint>
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

    void Initialize(const std::string &projectPath);
    const Config &GetConfig();
    std::optional<uint32_t> HandleProfileMenu(void *menu);
} // namespace Mafia1Online::Features::QuickJoin
