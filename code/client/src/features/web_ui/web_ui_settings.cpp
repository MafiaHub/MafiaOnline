#include "web_ui_settings.h"

#include "shared/features/chat/text_policy.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <system_error>

namespace Mafia1Online::Features::WebUi {
    namespace {
        constexpr size_t kMaxRecent    = 8;
        constexpr size_t kMaxFavorites = 24;
        constexpr size_t kMaxHostBytes = 255;
        constexpr size_t kMaxName      = 40;

        bool ValidHost(const std::string &host) {
            return !host.empty() && host.size() <= kMaxHostBytes && std::all_of(host.begin(), host.end(), [](unsigned char ch) {
                return ch > 32 && ch < 127;
            });
        }

        int64_t Now() {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        bool ReadServer(const nlohmann::json &value, WebUiSettings::Server &server) {
            if (!value.is_object()) {
                return false;
            }
            const auto host = value.find("host");
            const auto port = value.find("port");
            if (host == value.end() || !host->is_string() || port == value.end() || !port->is_number_unsigned()) {
                return false;
            }
            const auto portValue = port->get<uint64_t>();
            server.host          = host->get<std::string>();
            if (!ValidHost(server.host) || portValue == 0 || portValue > 65535) {
                return false;
            }
            server.port = static_cast<uint16_t>(portValue);
            if (const auto name = value.find("name"); name != value.end() && name->is_string()) {
                server.name = Shared::Chat::SanitizeLine(name->get<std::string>(), kMaxName);
            }
            if (const auto lastUsed = value.find("lastUsed"); lastUsed != value.end() && lastUsed->is_number_integer()) {
                server.lastUsed = lastUsed->get<int64_t>();
            }
            return true;
        }

        nlohmann::json WriteServer(const WebUiSettings::Server &server) {
            return {{"host", server.host}, {"port", server.port}, {"name", server.name}, {"lastUsed", server.lastUsed}};
        }

        std::vector<WebUiSettings::Server> ReadServers(const nlohmann::json &root, const char *key, size_t limit) {
            std::vector<WebUiSettings::Server> servers;
            const auto list = root.find(key);
            if (list == root.end() || !list->is_array()) {
                return servers;
            }
            for (const auto &value : *list) {
                WebUiSettings::Server server;
                if (servers.size() < limit && ReadServer(value, server)) {
                    servers.push_back(std::move(server));
                }
            }
            return servers;
        }

        // Only known keys with sane values survive; the page is not trusted
        // to write arbitrary data into the mod's config.
        nlohmann::json FilterPreferences(const nlohmann::json &input) {
            nlohmann::json out = nlohmann::json::object();
            if (!input.is_object()) {
                return out;
            }
            const auto number = [&](const char *key, double minimum, double maximum) {
                if (const auto it = input.find(key); it != input.end() && it->is_number()) {
                    out[key] = std::clamp(it->get<double>(), minimum, maximum);
                }
            };
            const auto flag = [&](const char *key) {
                if (const auto it = input.find(key); it != input.end() && it->is_boolean()) {
                    out[key] = it->get<bool>();
                }
            };
            number("chatFadeSeconds", 0.0, 120.0);
            number("chatScale", 0.8, 1.5);
            number("uiScale", 0.8, 1.4);
            flag("chatTimestamps");
            flag("filmGrain");
            flag("reduceMotion");
            return out;
        }
    } // namespace

    void WebUiSettings::Load(std::filesystem::path path) {
        _path = std::move(path);
        std::ifstream input(_path);
        if (!input) {
            return;
        }
        const auto root = nlohmann::json::parse(input, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return;
        }
        if (const auto nickname = root.find("nickname"); nickname != root.end() && nickname->is_string()) {
            _nickname = Shared::Chat::SanitizeLine(nickname->get<std::string>(), Shared::Chat::kMaxNicknameCodePoints);
        }
        _recent    = ReadServers(root, "recent", kMaxRecent);
        _favorites = ReadServers(root, "favorites", kMaxFavorites);
        if (const auto preferences = root.find("preferences"); preferences != root.end()) {
            _preferences = FilterPreferences(*preferences);
        }
    }

    void WebUiSettings::Save() const {
        if (_path.empty()) {
            return;
        }
        nlohmann::json root = ToJson();
        std::error_code error;
        std::filesystem::create_directories(_path.parent_path(), error);
        const auto temporary = std::filesystem::path(_path).concat(".tmp");
        {
            std::ofstream output(temporary, std::ios::trunc);
            if (!output) {
                return;
            }
            output << root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
            if (!output) {
                return;
            }
        }
        std::filesystem::rename(temporary, _path, error);
    }

    void WebUiSettings::RememberConnection(const std::string &nickname, const std::string &host, uint16_t port) {
        if (!ValidHost(host) || port == 0) {
            return;
        }
        _nickname = Shared::Chat::SanitizeLine(nickname, Shared::Chat::kMaxNicknameCodePoints);
        std::string name;
        std::erase_if(_recent, [&](const Server &server) {
            if (server.host == host && server.port == port) {
                name = server.name;
                return true;
            }
            return false;
        });
        _recent.insert(_recent.begin(), Server {host, port, name, Now()});
        if (_recent.size() > kMaxRecent) {
            _recent.resize(kMaxRecent);
        }
        for (auto &favorite : _favorites) {
            if (favorite.host == host && favorite.port == port) {
                favorite.lastUsed = Now();
            }
        }
        Save();
    }

    void WebUiSettings::SetFavorite(const std::string &host, uint16_t port, const std::string &name, bool favorite) {
        if (!ValidHost(host) || port == 0) {
            return;
        }
        const std::string clean = Shared::Chat::SanitizeLine(name, kMaxName);
        auto it                 = std::find_if(_favorites.begin(), _favorites.end(), [&](const Server &server) {
            return server.host == host && server.port == port;
        });
        if (!favorite) {
            if (it != _favorites.end()) {
                _favorites.erase(it);
            }
        }
        else if (it != _favorites.end()) {
            it->name = clean;
        }
        else if (_favorites.size() < kMaxFavorites) {
            _favorites.push_back(Server {host, port, clean, 0});
        }
        Save();
    }

    void WebUiSettings::Forget(const std::string &host, uint16_t port) {
        std::erase_if(_recent, [&](const Server &server) {
            return server.host == host && server.port == port;
        });
        Save();
    }

    void WebUiSettings::SetPreferences(const nlohmann::json &preferences) {
        _preferences = FilterPreferences(preferences);
        Save();
    }

    nlohmann::json WebUiSettings::ToJson() const {
        nlohmann::json recent    = nlohmann::json::array();
        nlohmann::json favorites = nlohmann::json::array();
        for (const auto &server : _recent) {
            recent.push_back(WriteServer(server));
        }
        for (const auto &server : _favorites) {
            favorites.push_back(WriteServer(server));
        }
        return {{"nickname", _nickname}, {"recent", recent}, {"favorites", favorites}, {"preferences", _preferences}};
    }
} // namespace Mafia1Online::Features::WebUi
