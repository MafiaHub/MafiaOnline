#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace Mafia1Online::Logging {
    // Every client process (the simulation host and each player) writes its
    // own log: three processes rotating one Mafia1Online.log overwrote each
    // other's lines and cut lines in half.
    inline constexpr std::string_view kProcessLogPrefix = "Mafia1Online-";
    // Old per-process logs past this many are removed at start.
    inline constexpr size_t kKeptProcessLogs = 30;

    // "host" for the simulation host, the quick-join nickname for a player,
    // "client" otherwise; only letters, digits, '-' and '_' survive.
    inline std::string ProcessLogRole(const nlohmann::json &config) {
        const auto simulation = config.find("simulation");
        if (simulation != config.end() && simulation->is_object()) {
            const auto enabled = simulation->find("enabled");
            if (enabled != simulation->end() && enabled->is_boolean() && enabled->get<bool>()) {
                return "host";
            }
        }
        std::string role;
        const auto quickJoin = config.find("quickJoin");
        if (quickJoin != config.end() && quickJoin->is_object()) {
            const auto enabled = quickJoin->find("enabled");
            const auto nickname = quickJoin->find("nickname");
            if (enabled != quickJoin->end() && enabled->is_boolean() && enabled->get<bool>() && nickname != quickJoin->end() && nickname->is_string()) {
                for (const char ch : nickname->get<std::string>()) {
                    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
                        role.push_back(ch);
                    }
                }
                role.resize(std::min<size_t>(role.size(), 32));
            }
        }
        return role.empty() ? std::string("client") : role;
    }

    inline std::string ProcessLogName(const std::string &role, uint32_t processId) {
        return std::string(kProcessLogPrefix) + role + "-" + std::to_string(processId);
    }

    // The launcher and the client DLL each own a logger; both name it from
    // the client config (QuickJoin::ConfigPath) so they share one file.
    inline std::string ProcessLogNameFor(const std::filesystem::path &configPath, uint32_t processId) {
        std::string role = "client";
        if (std::ifstream input(configPath); input) {
            const auto config = nlohmann::json::parse(input, nullptr, false);
            if (config.is_object()) {
                role = ProcessLogRole(config);
            }
        }
        return ProcessLogName(role, processId);
    }

    struct LogFile {
        std::string name;
        int64_t modified = 0;
    };

    // The per-process logs (and their rotations) beyond the newest `kept`.
    inline std::vector<std::string> StaleProcessLogs(std::vector<LogFile> files, size_t kept = kKeptProcessLogs) {
        std::erase_if(files, [](const LogFile &file) {
            return file.name.size() <= kProcessLogPrefix.size() + 4 || file.name.compare(0, kProcessLogPrefix.size(), kProcessLogPrefix) != 0 ||
                   file.name.compare(file.name.size() - 4, 4, ".log") != 0;
        });
        std::sort(files.begin(), files.end(), [](const LogFile &a, const LogFile &b) { return a.modified > b.modified; });
        std::vector<std::string> stale;
        for (size_t i = kept; i < files.size(); ++i) {
            stale.push_back(files[i].name);
        }
        return stale;
    }
} // namespace Mafia1Online::Logging
