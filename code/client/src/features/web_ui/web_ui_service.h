#pragma once

#include "web_ui_settings.h"

#include <utils/safe_win32.h>

#include <integrations/client/instance.h>

#include <bitset>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace Mafia1Online::Features::Chat {
    class ChatService;
} // namespace Mafia1Online::Features::Chat

namespace Mafia1Online::Features::World {
    class WorldService;
} // namespace Mafia1Online::Features::World

namespace Mafia1Online::Features::WebUi {
    // The Chromium (CEF) front end: one full-screen off-screen view drawn into
    // the game's Direct3D 8 frame. It replaces the main menu and the chat once
    // its page reports ready. If CEF does not start or the page stops
    // answering, quick join and the native chat take over.
    class WebUiService final {
      public:
        enum class Screen {
            Hidden,
            Menu,
            Game,
        };

        // False leaves the native UI in charge; not an error for the caller.
        bool Install(Framework::Integrations::Client::Instance &instance, Chat::ChatService &chat, const std::string &projectPath);
        void Shutdown();
        void Reset();
        void Update(World::WorldService &world);

        // Called by the IGraph::Present hook, outside any scene.
        void OnPresent();
        // True swallows the message.
        bool OnWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        // ReadKey router; true when the key belongs to the web UI.
        bool OnNativeKey(uint32_t scanCode, unsigned char character);

        void OnChatMessage(const std::string &author, const std::string &text, uint32_t color);
        void OnConnectionPhaseChanged(Framework::Integrations::Client::ConnectionPhase phase);
        void OnConnectionClosed(const std::string &reason);

        std::optional<std::string> TakeOutgoing();

        bool IsActive() const {
            return _pageReady;
        }
        bool IsCapturingInput() const {
            return _captured;
        }
        // IGraph's keyboard queries (TestKey, ReadKeys) answer "nothing held"
        // while the page owns the keyboard, so engine hotkeys stay quiet.
        bool HidesKeyboard() const {
            return _captured;
        }
        // In a mission the page's cursor takes the mouse from the camera and
        // the weapon. The main menu keeps it: its cursor is the page's cursor.
        bool HidesMouse() const {
            return _captured && _screen == Screen::Game;
        }

      private:
        struct ChatLine {
            std::string author;
            std::string text;
            uint32_t color = 0;
            int64_t time   = 0;
        };

        void BindPage();
        void OnPageEvent(const std::string &name, const std::string &payload);
        void Activate();
        void Deactivate();
        void Send(const char *type, const nlohmann::json &payload);
        void PushState(bool force);
        void PushPlayers(World::WorldService &world, bool force);
        void UpdateCapture();
        void ForwardMouse(Screen live);
        void ApplyFocus();
        void ReleaseHeldInput();
        bool HandleIme(HWND window, UINT message, LPARAM lParam);
        bool OnKeyMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        bool ToggleInGame(uint32_t key);
        void OpenChat(const std::string &prefill);
        void OpenPause();
        void CloseInGameScreens();
        Screen LiveScreen() const;

        Framework::Integrations::Client::Instance *_instance = nullptr;
        Chat::ChatService *_chat                             = nullptr;
        Framework::GUI::View *_view                          = nullptr;
        int _viewId                                          = -1;
        HWND _window                                         = nullptr;
        WebUiSettings _settings;

        bool _pageReady       = false;
        bool _captured        = false;
        bool _chatOpen        = false;
        bool _pauseOpen       = false;
        bool _scoreboardShown = false;
        bool _missionReady    = false;
        Screen _screen        = Screen::Hidden;
        Screen _pageScreen    = Screen::Hidden;
        std::optional<uint32_t> _restoreKeyboardFlags;
        Framework::Integrations::Client::ConnectionPhase _phase = Framework::Integrations::Client::ConnectionPhase::Disconnected;

        std::string _lastState;
        std::string _lastPlayers;
        std::chrono::steady_clock::time_point _lastHeartbeat;
        uint32_t _silentTicks = 0;
        std::optional<std::chrono::steady_clock::time_point> _closeRequestedAt;
        // Window-message key state by scan code, bit 8 the extended flag.
        std::bitset<0x200> _keysDown;
        std::bitset<0x200> _latched;
        std::chrono::steady_clock::time_point _lastPlayerPush;

        bool _windowActive       = true;
        std::vector<WPARAM> _heldKeys;
        int _viewportWidth       = 800;
        int _viewportHeight      = 600;
        int _cursorX             = 400;
        int _cursorY             = 300;
        int _mouseX              = -1;
        int _mouseY              = -1;
        uint32_t _mouseButtons   = 0;

        std::deque<ChatLine> _history;
        std::deque<std::string> _outgoing;
    };
} // namespace Mafia1Online::Features::WebUi
