#include "chat_service.h"

#include "features/world/world_hooks.h"

#include <utils/safe_win32.h>

#include <MinHook.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/input/native_input.h>
#include <mafia1/sdk/ui/native_hud.h>

#include <algorithm>
#include <iterator>
#include <cmath>
#include <string_view>

namespace Mafia1Online::Features::Chat {
    namespace {
        using SceneCallback = uint32_t(__stdcall *)(uint32_t, uint32_t, uint32_t);
        using InputUpdate   = void(__thiscall *)(void *, bool);
        using ReadKey       = uint32_t(__stdcall *)(void *);

        SceneCallback gOriginalSceneCallback = nullptr;
        InputUpdate gOriginalInputUpdate     = nullptr;
        ReadKey gOriginalReadKey             = nullptr;
        void *gReadKeyAddress                = nullptr;
        uintptr_t gTranslatedKeyAddress      = 0;
        ChatService *gService                = nullptr;

        constexpr uint32_t kDikEscape    = 0x01;
        constexpr uint32_t kDikBackspace = 0x0e;
        constexpr uint32_t kDikT         = 0x14;
        constexpr uint32_t kDikEnter     = 0x1c;
        constexpr uint32_t kDikSlash     = 0x35;
        constexpr uint32_t kDikNumEnter  = 0x9c;
        constexpr uint32_t kDikHome      = 0xc7;
        constexpr uint32_t kDikUp        = 0xc8;
        constexpr uint32_t kDikPageUp    = 0xc9;
        constexpr uint32_t kDikLeft      = 0xcb;
        constexpr uint32_t kDikRight     = 0xcd;
        constexpr uint32_t kDikEnd       = 0xcf;
        constexpr uint32_t kDikDown      = 0xd0;
        constexpr uint32_t kDikPageDown  = 0xd1;
        constexpr uint32_t kDikDelete    = 0xd3;

        // The server enforces 128 code points; the native code page is single
        // byte, so this bounds the input the same way.
        constexpr size_t kMaxInputBytes   = 128;
        constexpr size_t kMaxMessages     = 100;
        constexpr size_t kMaxSent         = 30;
        constexpr int kVisibleRows        = 10;
        constexpr auto kShowFor           = std::chrono::seconds(12);
        constexpr auto kFadeFor           = std::chrono::milliseconds(2000);
        constexpr auto kRepeatDelay       = std::chrono::milliseconds(420);
        constexpr auto kRepeatInterval    = std::chrono::milliseconds(35);

        // Virtual 800x600 layout, scaled by the HUD's own factors. The top
        // left corner stays clear for the in-car radar.
        constexpr float kLeft        = 14.0f;
        constexpr float kTop         = 190.0f;
        constexpr float kWidth       = 400.0f;
        constexpr float kRowHeight   = 15.0f;
        constexpr float kPadding     = 5.0f;
        constexpr uint32_t kPanelColor      = 0x78000000;
        constexpr uint32_t kInputColor      = 0xb0000000;
        constexpr uint32_t kTextColor       = 0xffffffff;
        constexpr uint32_t kAuthorColor     = 0xffe8c070;
        constexpr uint32_t kNoticeColor     = 0xfff4dfb0;
        constexpr uint32_t kInputTextColor  = 0xfff4dfb0;

        uint32_t WireToNative(uint32_t rgba, uint32_t fallback) {
            return rgba == 0 ? fallback : 0xff000000u | (rgba >> 8);
        }

        uint32_t WithAlpha(uint32_t argb, float alpha) {
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            return (argb & 0x00ffffffu) | (static_cast<uint32_t>(static_cast<float>(argb >> 24) * alpha) << 24);
        }

        std::string Utf8ToNative(std::string_view utf8) {
            if (utf8.empty()) {
                return {};
            }
            const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            if (wideCount <= 0) {
                return {};
            }
            std::wstring wide(static_cast<size_t>(wideCount), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), wide.data(), wideCount);
            const int nativeCount = WideCharToMultiByte(CP_ACP, 0, wide.data(), wideCount, nullptr, 0, nullptr, nullptr);
            if (nativeCount <= 0) {
                return {};
            }
            std::string native(static_cast<size_t>(nativeCount), '\0');
            WideCharToMultiByte(CP_ACP, 0, wide.data(), wideCount, native.data(), nativeCount, nullptr, nullptr);
            return native;
        }

        std::string NativeToUtf8(std::string_view native) {
            if (native.empty()) {
                return {};
            }
            const int wideCount = MultiByteToWideChar(CP_ACP, 0, native.data(), static_cast<int>(native.size()), nullptr, 0);
            if (wideCount <= 0) {
                return {};
            }
            std::wstring wide(static_cast<size_t>(wideCount), L'\0');
            MultiByteToWideChar(CP_ACP, 0, native.data(), static_cast<int>(native.size()), wide.data(), wideCount);
            const int utf8Count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wideCount, nullptr, 0, nullptr, nullptr);
            if (utf8Count <= 0) {
                return {};
            }
            std::string utf8(static_cast<size_t>(utf8Count), '\0');
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wideCount, utf8.data(), utf8Count, nullptr, nullptr);
            return utf8;
        }

        uint32_t __stdcall SceneCallbackHook(uint32_t message, uint32_t arg1, uint32_t arg2) {
            if (message == 2 && gService && gService->IsMissionReady()) {
                gService->PrepareNativeHud();
            }
            const uint32_t result = gOriginalSceneCallback(message, arg1, arg2);
            if (message == 2 && gService && gService->IsMissionReady()) {
                gService->RenderOverlay();
                gService->Render();
            }
            return result;
        }

        void __fastcall InputUpdateHook(void *input, void *, bool acquire) {
            gOriginalInputUpdate(input, acquire);
            if (!gService || !gService->IsInputSuppressed()) {
                return;
            }
            for (bool pressedOnly : {false, true}) {
                float *state    = nullptr;
                const int count = SDK::Input::GetState(input, &state, pressedOnly);
                if (count > 0) {
                    std::fill_n(state, count, 0.0f);
                }
            }
        }

        uint32_t __stdcall ReadKeyHook(void *graph) {
            const uint32_t scanCode = gOriginalReadKey(graph);
            const bool suppressPause = scanCode == kDikEscape && Features::World::NativeMissionActive();
            const unsigned char character = *reinterpret_cast<unsigned char *>(gTranslatedKeyAddress);
            if (gService && gService->RouteKey(scanCode, character)) {
                return 0;
            }
            if (!gService || !gService->IsMissionReady()) {
                return suppressPause ? 0 : scanCode;
            }
            const bool consumed = gService->ShouldConsumeNativeKey(scanCode);
            gService->OnNativeKey(scanCode, character);
            return consumed || suppressPause ? 0 : scanCode;
        }
    } // namespace

    bool ChatService::Install() {
        gService = this;
        const uintptr_t ls3dfBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"LS3DF.dll"));
        gReadKeyAddress           = reinterpret_cast<void *>(ls3dfBase + SDK::Input::kIGraphReadKeyOffset);
        gTranslatedKeyAddress     = ls3dfBase + SDK::Input::kTranslatedKeyOffset;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::UI::kSceneCallback), reinterpret_cast<void *>(&SceneCallbackHook), reinterpret_cast<void **>(&gOriginalSceneCallback)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Input::kUpdate), reinterpret_cast<void *>(&InputUpdateHook), reinterpret_cast<void **>(&gOriginalInputUpdate)) != MH_OK
            || MH_CreateHook(gReadKeyAddress, reinterpret_cast<void *>(&ReadKeyHook), reinterpret_cast<void **>(&gOriginalReadKey)) != MH_OK) {
            Shutdown();
            return false;
        }
        for (void *address : {reinterpret_cast<void *>(SDK::UI::kSceneCallback), reinterpret_cast<void *>(SDK::Input::kUpdate), gReadKeyAddress}) {
            if (MH_EnableHook(address) != MH_OK) {
                Shutdown();
                return false;
            }
        }
        return true;
    }

    void ChatService::Shutdown() {
        for (void *address : {reinterpret_cast<void *>(SDK::UI::kSceneCallback), reinterpret_cast<void *>(SDK::Input::kUpdate), gReadKeyAddress}) {
            if (address) {
                MH_DisableHook(address);
                MH_RemoveHook(address);
            }
        }
        gService              = nullptr;
        gReadKeyAddress       = nullptr;
        gTranslatedKeyAddress = 0;
        _overlay              = {};
        _keyRouter            = {};
        _inputCapture         = {};
        _nativeChatEnabled    = true;
        Reset();
    }

    void ChatService::Reset() {
        _missionReady = false;
        _localHealth.reset();
        CloseInput();
        _messages.clear();
        _outgoing.clear();
        _gameKeys.clear();
        _lastScanCode      = 0;
        _swallowedScanCode = 0;
    }

    void ChatService::SetNativeChatEnabled(bool enabled) {
        _nativeChatEnabled = enabled;
        if (!enabled) {
            CloseInput();
        }
    }

    void ChatService::Update(bool missionReady, std::optional<float> localHealth) {
        _missionReady = missionReady;
        _localHealth  = missionReady ? localHealth : std::nullopt;
        if (!missionReady) {
            CloseInput();
            _lastScanCode      = 0;
            _swallowedScanCode = 0;
        }
    }

    void ChatService::OnMessage(const std::string &author, const std::string &text, uint32_t color) {
        Message message;
        message.author   = Utf8ToNative(author);
        message.text     = Utf8ToNative(text);
        message.received = Clock::now();
        if (message.text.empty()) {
            return;
        }
        if (message.author.empty()) {
            message.textColor = WireToNative(color, kNoticeColor);
        }
        else {
            message.authorColor = WireToNative(color, kAuthorColor);
            message.textColor   = kTextColor;
        }
        _messages.push_back(std::move(message));
        while (_messages.size() > kMaxMessages) {
            _messages.pop_front();
        }
    }

    std::optional<std::string> ChatService::TakeOutgoing() {
        if (_outgoing.empty()) {
            return std::nullopt;
        }
        std::string line = std::move(_outgoing.front());
        _outgoing.pop_front();
        return line;
    }

    std::optional<uint32_t> ChatService::TakeGameKey() {
        if (_gameKeys.empty()) {
            return std::nullopt;
        }
        const uint32_t key = _gameKeys.front();
        _gameKeys.pop_front();
        return key;
    }

    bool ChatService::ShouldConsumeNativeKey(uint32_t scanCode) const {
        return _inputActive || scanCode == kDikT || scanCode == kDikSlash || (scanCode != 0 && scanCode == _swallowedScanCode);
    }

    void ChatService::OpenInput(const std::string &prefill) {
        _inputActive = true;
        _input       = prefill;
        _cursor      = _input.size();
        _sentIndex   = -1;
        _draft.clear();
        _scroll      = 0;
        _inputOpened = Clock::now();
    }

    void ChatService::CloseInput() {
        _inputActive = false;
        _input.clear();
        _cursor    = 0;
        _sentIndex = -1;
        _draft.clear();
        _scroll = 0;
    }

    void ChatService::OnNativeKey(uint32_t scanCode, unsigned char character) {
        const auto now = Clock::now();
        if (scanCode == 0) {
            _lastScanCode      = 0;
            _swallowedScanCode = 0;
            return;
        }
        if (_inputActive || scanCode == kDikT || scanCode == kDikSlash) {
            _swallowedScanCode = scanCode;
        }
        if (scanCode == _lastScanCode) {
            if (!_inputActive || now - _keyDownSince < kRepeatDelay || now - _lastRepeat < kRepeatInterval ||
                scanCode == kDikEnter || scanCode == kDikNumEnter || scanCode == kDikEscape) {
                return;
            }
            _lastRepeat = now;
            ApplyKey(scanCode, character);
            return;
        }
        _lastScanCode = scanCode;
        _keyDownSince = now;
        _lastRepeat   = now;
        if (!_inputActive) {
            if (scanCode == kDikT) {
                OpenInput({});
            }
            else if (scanCode == kDikSlash) {
                OpenInput("/");
            }
            else if (_missionReady && _gameKeys.size() < 16) {
                _gameKeys.push_back(scanCode);
            }
            return;
        }
        ApplyKey(scanCode, character);
    }

    void ChatService::ApplyKey(uint32_t scanCode, unsigned char character) {
        switch (scanCode) {
        case kDikEscape: CloseInput(); return;
        case kDikEnter:
        case kDikNumEnter: {
            std::string line = _input;
            while (!line.empty() && line.back() == ' ') {
                line.pop_back();
            }
            if (!line.empty()) {
                _outgoing.push_back(NativeToUtf8(line));
                if (_sent.empty() || _sent.back() != line) {
                    _sent.push_back(line);
                    while (_sent.size() > kMaxSent) {
                        _sent.pop_front();
                    }
                }
            }
            CloseInput();
            return;
        }
        case kDikBackspace:
            if (_cursor > 0) {
                _input.erase(--_cursor, 1);
            }
            return;
        case kDikDelete:
            if (_cursor < _input.size()) {
                _input.erase(_cursor, 1);
            }
            return;
        case kDikLeft: _cursor = _cursor > 0 ? _cursor - 1 : 0; return;
        case kDikRight: _cursor = std::min(_cursor + 1, _input.size()); return;
        case kDikHome: _cursor = 0; return;
        case kDikEnd: _cursor = _input.size(); return;
        case kDikUp:
        case kDikDown: {
            if (_sent.empty()) {
                return;
            }
            const int last = static_cast<int>(_sent.size()) - 1;
            if (scanCode == kDikUp) {
                if (_sentIndex < 0) {
                    _draft     = _input;
                    _sentIndex = last;
                }
                else if (_sentIndex > 0) {
                    --_sentIndex;
                }
            }
            else if (_sentIndex >= 0) {
                _sentIndex = _sentIndex < last ? _sentIndex + 1 : -1;
            }
            _input  = _sentIndex < 0 ? _draft : _sent[static_cast<size_t>(_sentIndex)];
            _cursor = _input.size();
            return;
        }
        case kDikPageUp: _scroll += kVisibleRows - 1; return;
        case kDikPageDown: _scroll = std::max(0, _scroll - (kVisibleRows - 1)); return;
        default: break;
        }
        if (character >= 32 && character != 127 && _input.size() < kMaxInputBytes) {
            _input.insert(_cursor++, 1, static_cast<char>(character));
        }
    }

    void ChatService::PrepareNativeHud() {
        if (_localHealth && std::isfinite(*_localHealth)) {
            const uint32_t health = static_cast<uint32_t>(std::clamp(std::lround(*_localHealth), 0l, 100l));
            SDK::UI::SetLives(health);
            SDK::UI::Indicators().SetLivesVisible();
        }
        else {
            SDK::UI::Indicators().ClearLivesVisible();
        }
    }

    void ChatService::RenderOverlay() {
        if (_overlay) {
            _overlay();
        }
    }

    std::vector<ChatService::Row> ChatService::WrapMessage(const Message &message, float width, float height) const {
        std::vector<Row> rows;
        const std::string prefix = message.author.empty() ? std::string() : message.author + ": ";
        const float prefixWidth  = prefix.empty() ? 0.0f : SDK::UI::MeasureText(prefix.c_str(), height);
        const std::string &text  = message.text;
        size_t start             = 0;
        bool first               = true;
        while (start < text.size()) {
            const float available = width - (first ? prefixWidth : 0.0f);
            // Longest prefix that fits, preferring to break after a space.
            size_t end = start;
            size_t lastSpace = std::string::npos;
            while (end < text.size()) {
                const std::string candidate = text.substr(start, end + 1 - start);
                if (SDK::UI::MeasureText(candidate.c_str(), height) > available) {
                    break;
                }
                if (text[end] == ' ') {
                    lastSpace = end;
                }
                ++end;
            }
            if (end < text.size() && lastSpace != std::string::npos && lastSpace > start) {
                end = lastSpace + 1;
            }
            if (end == start) {
                end = start + 1;
            }
            Row row;
            row.message = &message;
            row.author  = first ? prefix : std::string();
            row.text    = text.substr(start, end - start);
            rows.push_back(std::move(row));
            first = false;
            start = end;
            while (start < text.size() && text[start] == ' ') {
                ++start;
            }
        }
        if (rows.empty()) {
            rows.push_back({&message, prefix, {}});
        }
        return rows;
    }

    void ChatService::Render() {
        if (!_nativeChatEnabled) {
            return;
        }
        const auto &indicators = SDK::UI::Indicators();
        const float sx         = indicators.ScaleX();
        const float sy         = indicators.ScaleY();
        const float left       = kLeft * sx + indicators.MenuOffsetX();
        const float width      = kWidth * sx;
        const float rowHeight  = kRowHeight * sy;
        const float top        = kTop * sy;
        const float padding    = kPadding * sy;
        const auto now         = Clock::now();

        // Newest rows last; walk messages backwards until the view is full.
        std::vector<Row> rows;
        const size_t wanted = static_cast<size_t>(kVisibleRows + std::max(0, _scroll));
        for (auto it = _messages.rbegin(); it != _messages.rend() && rows.size() < wanted; ++it) {
            if (!_inputActive && now - it->received > kShowFor + kFadeFor) {
                break;
            }
            auto wrapped = WrapMessage(*it, width, rowHeight);
            rows.insert(rows.begin(), std::make_move_iterator(wrapped.begin()), std::make_move_iterator(wrapped.end()));
        }
        const int maxScroll = std::max(0, static_cast<int>(rows.size()) - kVisibleRows);
        if (_scroll > maxScroll) {
            _scroll = maxScroll;
        }
        const size_t lastRow  = rows.size() - static_cast<size_t>(std::min<int>(_scroll, static_cast<int>(rows.size())));
        const size_t firstRow = lastRow > static_cast<size_t>(kVisibleRows) ? lastRow - kVisibleRows : 0;

        if (_inputActive) {
            SDK::Graphics::FillRect(left - padding, top - padding, width + padding * 2.0f,
                                    rowHeight * kVisibleRows + padding * 2.0f, kPanelColor);
        }
        float y = top + rowHeight * static_cast<float>(kVisibleRows - static_cast<int>(lastRow - firstRow));
        for (size_t index = firstRow; index < lastRow; ++index, y += rowHeight) {
            const Row &row = rows[index];
            float alpha    = 1.0f;
            if (!_inputActive) {
                const auto age = now - row.message->received;
                if (age > kShowFor) {
                    alpha = 1.0f - std::chrono::duration<float>(age - kShowFor).count() / std::chrono::duration<float>(kFadeFor).count();
                }
            }
            if (alpha <= 0.0f) {
                continue;
            }
            float x = left;
            if (!row.author.empty()) {
                SDK::UI::DrawShadowedText(row.author.c_str(), x, y, width, rowHeight, WithAlpha(row.message->authorColor, alpha));
                x += SDK::UI::MeasureText(row.author.c_str(), rowHeight);
            }
            if (!row.text.empty()) {
                SDK::UI::DrawShadowedText(row.text.c_str(), x, y, left + width - x, rowHeight, WithAlpha(row.message->textColor, alpha));
            }
        }
        if (_scroll > 0 && _inputActive) {
            SDK::UI::DrawShadowedText("...", left + width - 20.0f * sx, top - padding, 20.0f * sx, rowHeight, 0xffb0b0b0);
        }

        if (!_inputActive) {
            return;
        }
        const float inputTop = top + rowHeight * kVisibleRows + padding * 2.0f + 2.0f * sy;
        SDK::Graphics::FillRect(left - padding, inputTop - padding * 0.5f, width + padding * 2.0f, rowHeight + padding, kInputColor);
        // Keep the cursor visible when the line is wider than the box.
        const std::string prompt = "> ";
        const float promptWidth  = SDK::UI::MeasureText(prompt.c_str(), rowHeight);
        const float available    = width - promptWidth - 4.0f * sx;
        size_t visibleStart      = 0;
        while (visibleStart < _cursor &&
               SDK::UI::MeasureText(_input.substr(visibleStart, _cursor - visibleStart).c_str(), rowHeight) > available) {
            ++visibleStart;
        }
        const std::string visible = _input.substr(visibleStart);
        SDK::UI::DrawShadowedText(prompt.c_str(), left, inputTop, promptWidth, rowHeight, kInputTextColor);
        SDK::UI::DrawShadowedText(visible.c_str(), left + promptWidth, inputTop, available, rowHeight, kTextColor);
        const bool blinkOn = (std::chrono::duration_cast<std::chrono::milliseconds>(now - _inputOpened).count() / 500) % 2 == 0;
        if (blinkOn) {
            const float cursorX = left + promptWidth + SDK::UI::MeasureText(_input.substr(visibleStart, _cursor - visibleStart).c_str(), rowHeight);
            SDK::Graphics::FillRect(cursorX, inputTop + rowHeight * 0.1f, std::max(1.0f, 1.5f * sx), rowHeight * 0.8f, kTextColor);
        }
    }
} // namespace Mafia1Online::Features::Chat
