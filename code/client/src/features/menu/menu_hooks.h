#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Mafia1Online::Features::Menu {
    enum class Action {
        Connect,
        Disconnect
    };

    struct Request {
        Action action;
        std::string nickname;
        std::string host;
        std::string password;
        uint16_t port = 27015;
    };

    // Connection screen state, UTF-8.
    struct State {
        std::string status;
        bool connectionActive = false;
        bool playAvailable    = false;
        std::string nickname;
        std::string host;
        std::string password;
        uint16_t port = 27015;
    };

    bool InstallMenuHooks();
    void UninstallMenuHooks();
    std::optional<Request> TakeRequest();
    void SetStatus(const std::string &status);
    void SetConnectionActive(bool active);
    void SetPlayAvailable(bool available);
    void SetConnectionDefaults(const std::string &nickname, const std::string &host, uint16_t port, const std::string &password);
    State GetState();
    void Submit(Request request);
    // False and a status line when no mission is ready to enter.
    bool RequestPlay();
    // While the web UI draws the connection screen, every retail main menu
    // component and the menu cursor are hidden; the menu scene keeps running.
    void SetNativeControlsSuppressed(bool suppressed);
} // namespace Mafia1Online::Features::Menu
