#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Framework::Networking {
    class AssetStreamer;
}

namespace Mafia1Online::Features::Mod {
    // Startup snapshot, shared by every connection until the server restarts.
    class ModService final {
      public:
        void Load(const std::filesystem::path &directory);
        void AddUploads(Framework::Networking::AssetStreamer &streamer) const;
        void Update() {}
        void Reset();
        bool HasMission(std::string_view name) const;
        const nlohmann::json &Manifest() const {
            return _manifest;
        }

      private:
        std::vector<std::filesystem::path> _packages;
        std::vector<std::string> _missions;
        nlohmann::json _manifest = nlohmann::json::array();
    };
} // namespace Mafia1Online::Features::Mod
