#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Mafia1Online::Features::Chat {
    // In-game chat drawn with the retail HUD font: a scrollback panel with
    // colored author names, and an editable input line (T or / to open).
    class ChatService final {
      public:
        bool Install();
        void Shutdown();
        void Reset();
        void Update(bool missionReady, std::optional<float> localHealth);
        // Color is the wire format 0xRRGGBBAA, 0 for the default.
        void OnMessage(const std::string &author, const std::string &text, uint32_t color);
        std::optional<std::string> TakeOutgoing();
        // Drawn every scene frame before the chat, e.g. nametags.
        void SetOverlay(std::function<void()> overlay) {
            _overlay = std::move(overlay);
        }
        // A front end that sees every native key before the chat and the game:
        // `router` returns true for a key it took, `capture` while it owns the
        // keyboard so gameplay actions stay cleared. Either may be empty.
        void SetKeyRouter(std::function<bool(uint32_t, unsigned char)> router, std::function<bool()> capture) {
            _keyRouter    = std::move(router);
            _inputCapture = std::move(capture);
        }
        // False while another front end draws the chat; messages still arrive.
        void SetNativeChatEnabled(bool enabled);
        // Runs after chat/web capture has cleared gameplay input.
        void SetInputFilter(std::function<void(void *)> filter) { _inputFilter = std::move(filter); }
        void FilterInput(void *input) const { if (_inputFilter) _inputFilter(input); }
        bool RouteKey(uint32_t scanCode, unsigned char character) const {
            return _keyRouter && _keyRouter(scanCode, character);
        }
        bool IsInputActive() const {
            return _inputActive;
        }
        bool IsInputSuppressed() const {
            return _inputActive || _swallowedScanCode != 0 || (_inputCapture && _inputCapture());
        }
        bool IsMissionReady() const {
            return _missionReady;
        }
        bool ShouldConsumeNativeKey(uint32_t scanCode) const;

        void OnNativeKey(uint32_t scanCode, unsigned char character);
        void PrepareNativeHud();
        void RenderOverlay();
        void Render();

      private:
        using Clock = std::chrono::steady_clock;

        struct Message {
            std::string author; // native code page
            std::string text;   // native code page
            uint32_t authorColor = 0;
            uint32_t textColor   = 0;
            Clock::time_point received;
        };
        struct Row {
            const Message *message = nullptr;
            std::string author;
            std::string text;
        };

        void OpenInput(const std::string &prefill);
        void CloseInput();
        void ApplyKey(uint32_t scanCode, unsigned char character);
        std::vector<Row> WrapMessage(const Message &message, float width, float height) const;

        bool _missionReady = false;
        bool _inputActive  = false;
        std::optional<float> _localHealth;
        std::string _input;
        size_t _cursor = 0;
        std::deque<Message> _messages;
        std::deque<std::string> _outgoing;
        std::deque<std::string> _sent;
        int _sentIndex = -1;
        std::string _draft;
        int _scroll = 0;
        uint32_t _lastScanCode      = 0;
        uint32_t _swallowedScanCode = 0;
        Clock::time_point _keyDownSince;
        Clock::time_point _lastRepeat;
        Clock::time_point _inputOpened;
        std::function<void()> _overlay;
        std::function<bool(uint32_t, unsigned char)> _keyRouter;
        std::function<bool()> _inputCapture;
        bool _nativeChatEnabled = true;
        std::function<void(void *)> _inputFilter;
    };
} // namespace Mafia1Online::Features::Chat
