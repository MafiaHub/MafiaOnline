#pragma once

#include <atomic>
#include <filesystem>
#include <future>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::Features::Mod {
    struct PreparedMods {
        uint64_t generation = 0;
        std::unordered_map<std::string, std::string> files;
        std::vector<std::string> missions;
        std::string error;
    };

    // Owns session routing and asynchronous verification/extraction. Engine handles remain
    // native rw_data handles, so already open files survive removal of the routing table.
    class ModService final {
      public:
        bool Install();
        void Shutdown();
        void Begin(uint64_t generation, const nlohmann::json &config, const std::filesystem::path &downloads, const std::filesystem::path &cache);
        void Update();
        void Reset();
        std::optional<PreparedMods> TakeCompleted();
        std::string Resolve(std::string_view path) const;
        bool HasMission(std::string_view mission) const;
        bool IsPreparing() const {
            return _pending.valid();
        }

      private:
        void *_openAddress = nullptr;
        mutable std::shared_mutex _mutex;
        std::unordered_map<std::string, std::string> _files;
        std::vector<std::string> _missions;
        std::shared_ptr<std::atomic<bool>> _cancel;
        std::future<PreparedMods> _pending;
        std::vector<std::future<PreparedMods>> _retired;
        std::optional<PreparedMods> _completed;
    };
} // namespace Mafia1Online::Features::Mod
