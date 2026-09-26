#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Mafia1Online::Features::WebUi {
    // The connection screen's own memory: last nickname, recent and favorite
    // servers and UI preferences. Stored beside the mod's client config; the
    // password is never written.
    class WebUiSettings final {
      public:
        struct Server {
            std::string host;
            uint16_t port = 27015;
            std::string name;
            int64_t lastUsed = 0;
        };

        void Load(std::filesystem::path path);
        void RememberConnection(const std::string &nickname, const std::string &host, uint16_t port);
        void SetFavorite(const std::string &host, uint16_t port, const std::string &name, bool favorite);
        void Forget(const std::string &host, uint16_t port);
        void SetPreferences(const nlohmann::json &preferences);

        const std::string &Nickname() const {
            return _nickname;
        }

        nlohmann::json ToJson() const;

      private:
        void Save() const;

        std::filesystem::path _path;
        std::string _nickname;
        std::vector<Server> _recent;
        std::vector<Server> _favorites;
        nlohmann::json _preferences = nlohmann::json::object();
    };
} // namespace Mafia1Online::Features::WebUi
