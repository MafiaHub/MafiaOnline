#include "web_ui_service.h"

#include "features/compat/environment_flag.h"

#include "web_ui_hooks.h"

#include "features/chat/chat_service.h"
#include "features/menu/menu_hooks.h"
#include "features/quick_join/quick_join.h"
#include "features/world/world_hooks.h"
#include "features/world/world_service.h"
#include "shared/features/chat/text_policy.h"
#include "shared/features/player/player_entity.h"
#include "shared/version.h"

#include <core_modules.h>
#include <graphics/backend/d3d8.h>
#include <gui/manager.h>
#include <logging/logger.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/menu/native_menu.h>
#include <networking/replication/replication_manager.h>

#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <utility>
#include <vector>

namespace Mafia1Online::Features::WebUi {
    namespace {
        using Framework::Integrations::Client::ConnectionPhase;
        using Clock = std::chrono::steady_clock;

        constexpr char kResourceHost[] = "mafia1online";
        // DirectInput codes; window messages carry the same scan code with
        // the extended flag, which DirectInput folds into bit 7 instead.
        constexpr uint32_t kDikEscape  = 0x01;
        // Tab is the retail city map; the player list is held F1 instead.
        constexpr uint32_t kDikF1      = 0x3b;
        constexpr uint32_t kDikT       = 0x14;
        constexpr uint32_t kDikSlash   = 0x35;
        constexpr uint32_t kKeyEnter    = 0x01c;
        constexpr uint32_t kKeyNumEnter = 0x11c;
        // How long the page has to close its chat after Enter.
        constexpr auto kCloseGrace = std::chrono::milliseconds(700);
        constexpr size_t kMaxHistory   = 100;
        constexpr size_t kMaxOutgoing  = 16;
        // The page beats every second. Both limits must pass, so a long
        // blocking mission load (no pump, no ticks) cannot trip it.
        constexpr auto kHeartbeatTimeout = std::chrono::seconds(8);
        constexpr uint32_t kHeartbeatTicks = 240;
        constexpr auto kPlayerInterval   = std::chrono::milliseconds(1000);

        const char *ScreenName(WebUiService::Screen screen) {
            switch (screen) {
            case WebUiService::Screen::Menu: return "menu";
            case WebUiService::Screen::Game: return "game";
            default: return "hidden";
            }
        }

        const char *PhaseName(ConnectionPhase phase) {
            switch (phase) {
            case ConnectionPhase::Connecting: return "connecting";
            case ConnectionPhase::Authenticating: return "authenticating";
            case ConnectionPhase::Downloading: return "downloading";
            case ConnectionPhase::Starting: return "starting";
            case ConnectionPhase::InGame: return "connected";
            default: return "disconnected";
            }
        }

        int64_t NowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        // Next to the client config the launcher was pointed at, so a second
        // local instance with its own config keeps its own server list.
        std::filesystem::path SettingsPath(const std::string &projectPath) {
            const char *configOverride = std::getenv("MAFIA1ONLINE_CLIENT_CONFIG");
            if (configOverride && *configOverride) {
                const std::filesystem::path config(configOverride);
                return config.parent_path() / (config.stem().string() + ".ui.json");
            }
            return std::filesystem::path(projectPath) / "config" / "ui.json";
        }

        bool ValidHost(const std::string &host) {
            return !host.empty() && host.size() <= 255 && std::all_of(host.begin(), host.end(), [](unsigned char ch) {
                return ch > 32 && ch < 127;
            });
        }

        std::string StringField(const nlohmann::json &object, const char *key) {
            const auto it = object.find(key);
            return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
        }

        uint32_t PortField(const nlohmann::json &object, const char *key) {
            const auto it = object.find(key);
            if (it == object.end()) {
                return 0;
            }
            if (it->is_number_unsigned()) {
                return static_cast<uint32_t>(std::min<uint64_t>(it->get<uint64_t>(), 0x10000));
            }
            if (it->is_string()) {
                const std::string text = it->get<std::string>();
                char *end              = nullptr;
                const unsigned long value = std::strtoul(text.c_str(), &end, 10);
                return !text.empty() && *end == '\0' ? static_cast<uint32_t>(std::min<unsigned long>(value, 0x10000)) : 0;
            }
            return 0;
        }

        uint32_t KeyOf(LPARAM lParam) {
            return static_cast<uint32_t>((lParam >> 16) & 0xff) | (lParam & (1 << 24) ? 0x100u : 0u);
        }

        uint32_t CefButtonFlags(uint32_t buttons) {
            uint32_t flags = 0;
            if (buttons & SDK::Graphics::kMouseLeft) {
                flags |= EVENTFLAG_LEFT_MOUSE_BUTTON;
            }
            if (buttons & SDK::Graphics::kMouseRight) {
                flags |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
            }
            if (buttons & SDK::Graphics::kMouseMiddle) {
                flags |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
            }
            return flags;
        }
    } // namespace

    bool WebUiService::Install(Framework::Integrations::Client::Instance &instance, Chat::ChatService &chat, const std::string &projectPath) {
        if (Compat::EnvironmentFlagEnabled(L"MAFIA1ONLINE_NATIVE_UI")) {
            Framework::Logging::GetLogger("Web")->info("CEF skipped by MAFIA1ONLINE_NATIVE_UI");
            return false;
        }
        const auto logger = Framework::Logging::GetLogger("Web");
        auto *renderer    = instance.GetRenderer();
        auto *manager     = instance.GetWebManager();
        if (!renderer || !renderer->IsInitialized() || !renderer->GetD3D8Backend() || !manager) {
            return false;
        }

        const std::filesystem::path pageRoot = std::filesystem::path(projectPath) / "ui";
        std::error_code error;
        if (!std::filesystem::is_regular_file(pageRoot / "index.html", error)) {
            logger->error("Web UI page is missing at {}; build it with tools/build_ui.sh", (pageRoot / "index.html").string());
            return false;
        }

        int width  = 800;
        int height = 600;
        (void)renderer->GetBackBufferSize(width, height);
        if (const auto result = manager->Init(projectPath, {width, height}, renderer, false); !result) {
            logger->error("Web UI disabled, CEF did not start: {}", result.GetError().message);
            return false;
        }

        manager->RegisterResourceDirectory(kResourceHost, pageRoot);
        const std::string url = Framework::GUI::Manager::ResourceURL(kResourceHost, "index.html");
        _viewId               = manager->CreateView(url, 0, 0);
        _view                 = _viewId >= 0 ? manager->GetView(_viewId) : nullptr;
        if (!_view) {
            logger->error("Web UI disabled, the view could not be created");
            _viewId = -1;
            return false;
        }
        _view->LockToOrigin(url);
        _view->Display(false);

        _instance = &instance;
        _chat     = &chat;
        _window   = static_cast<HWND>(SDK::Graphics::GetGraph()->MainWindow());
        _settings.Load(SettingsPath(projectPath));
        BindPage();

        if (!InstallWebUiHooks(*this, _window)) {
            logger->error("Web UI disabled, its hooks could not be installed");
            manager->DestroyView(_viewId);
            _view     = nullptr;
            _viewId   = -1;
            _instance = nullptr;
            _chat     = nullptr;
            return false;
        }
        chat.SetKeyRouter(
            [this](uint32_t scanCode, unsigned char character) {
                return OnNativeKey(scanCode, character);
            },
            [this] {
                return HidesKeyboard();
            });
        return true;
    }

    void WebUiService::Shutdown() {
        if (!_instance) {
            return;
        }
        Deactivate();
        UninstallWebUiHooks();
        if (_chat) {
            _chat->SetKeyRouter({}, {});
        }
        if (auto *manager = _instance->GetWebManager(); manager && _viewId >= 0) {
            manager->DestroyView(_viewId);
        }
        _view     = nullptr;
        _viewId   = -1;
        _instance = nullptr;
        _chat     = nullptr;
        _history.clear();
        _outgoing.clear();
    }

    void WebUiService::Reset() {
        _history.clear();
        _outgoing.clear();
        _chatOpen        = false;
        _pauseOpen       = false;
        _scoreboardShown = false;
        _lastPlayers.clear();
        if (_pageReady) {
            Send("session:reset", nlohmann::json::object());
        }
    }

    void WebUiService::BindPage() {
        for (const char *name : {"ui:ready", "ui:alive", "ui:screen", "menu:connect", "menu:disconnect", "menu:play", "app:quit", "chat:send", "chat:close",
                                 "pause:close", "servers:favorite", "servers:forget", "settings:save"}) {
            const std::string eventName = name;
            _view->AddEventListener(eventName, [this, eventName](const std::string &payload) {
                OnPageEvent(eventName, payload);
            });
        }
    }

    void WebUiService::OnPageEvent(const std::string &name, const std::string &payload) {
        const nlohmann::json data = payload.empty() ? nlohmann::json::object() : nlohmann::json::parse(payload, nullptr, false);
        if (data.is_discarded()) {
            return;
        }
        _lastHeartbeat = Clock::now();
        _silentTicks   = 0;

        if (name == "ui:ready") {
            Activate();
            return;
        }
        if (!_pageReady) {
            return;
        }
        if (name == "ui:alive") {
            return;
        }
        if (name == "ui:screen") {
            const std::string screen = data.is_string() ? data.get<std::string>() : std::string();
            _pageScreen              = screen == "menu" ? Screen::Menu : screen == "game" ? Screen::Game : Screen::Hidden;
            return;
        }
        if (name == "menu:connect") {
            if (!data.is_object()) {
                return;
            }
            Menu::Request request;
            request.action         = Menu::Action::Connect;
            request.nickname       = Shared::Chat::SanitizeLine(StringField(data, "nickname"), Shared::Chat::kMaxNicknameCodePoints);
            request.host           = StringField(data, "host");
            request.password       = StringField(data, "password");
            const uint32_t port    = PortField(data, "port");
            const char *validation = nullptr;
            if (_phase != ConnectionPhase::Disconnected || Menu::GetState().connectionActive) {
                validation = "Disconnect before connecting again";
            }
            else if (request.nickname.empty()) {
                validation = "Choose a nickname first";
            }
            else if (!ValidHost(request.host) || port == 0 || port > 65535) {
                validation = "Enter a server address and a valid port";
            }
            else if (request.password.size() > 128) {
                validation = "The password is too long";
            }
            if (validation) {
                Menu::SetStatus(validation);
                Send("toast", {{"kind", "error"}, {"text", validation}});
                return;
            }
            request.port = static_cast<uint16_t>(port);
            _settings.RememberConnection(request.nickname, request.host, request.port);
            Menu::Submit(std::move(request));
            Menu::SetStatus("Connecting...");
            return;
        }
        if (name == "menu:disconnect") {
            _pauseOpen = false;
            Menu::Submit(Menu::Request {.action = Menu::Action::Disconnect});
            return;
        }
        if (name == "menu:play") {
            (void)Menu::RequestPlay();
            return;
        }
        if (name == "app:quit") {
            // The window-close path already knows how to leave a menu or a
            // running mission in order; the page must not know either.
            PostMessageA(_window, WM_CLOSE, 0, 0);
            return;
        }
        if (name == "chat:send") {
            const std::string line = Shared::Chat::SanitizeLine(data.is_object() ? StringField(data, "text") : std::string(), Shared::Chat::kMaxMessageCodePoints);
            if (!line.empty() && _outgoing.size() < kMaxOutgoing && _screen == Screen::Game) {
                _outgoing.push_back(line);
            }
            return;
        }
        if (name == "chat:close") {
            _chatOpen = false;
            _closeRequestedAt.reset();
            UpdateCapture();
            return;
        }
        if (name == "pause:close") {
            _pauseOpen = false;
            UpdateCapture();
            return;
        }
        if ((name == "servers:favorite" || name == "servers:forget") && data.is_object()) {
            const uint32_t port = PortField(data, "port");
            if (port == 0 || port > 65535) {
                return;
            }
            if (name == "servers:forget") {
                _settings.Forget(StringField(data, "host"), static_cast<uint16_t>(port));
            }
            else {
                const auto favorite = data.find("favorite");
                _settings.SetFavorite(StringField(data, "host"), static_cast<uint16_t>(port), StringField(data, "name"),
                                      favorite != data.end() && favorite->is_boolean() && favorite->get<bool>());
            }
            PushState(false);
            return;
        }
        if (name == "settings:save") {
            _settings.SetPreferences(data);
            PushState(false);
        }
    }

    void WebUiService::Activate() {
        if (!_view) {
            return;
        }
        _pageReady           = true;
        _pageScreen          = Screen::Hidden;
        _lastHeartbeat       = Clock::now();
        _silentTicks         = 0;
        _lastState.clear();
        _lastPlayers.clear();
        _mouseX       = -1;
        _mouseY       = -1;
        _mouseButtons = 0;
        _view->Display(true);
        Menu::SetNativeControlsSuppressed(true);
        _chat->SetNativeChatEnabled(false);
        PushState(true);
        nlohmann::json history = nlohmann::json::array();
        for (const auto &line : _history) {
            history.push_back({{"author", line.author}, {"text", line.text}, {"color", line.color}, {"time", line.time}});
        }
        Send("chat:history", history);
    }

    void WebUiService::Deactivate() {
        const bool wasReady = _pageReady;
        _pageReady          = false;
        _chatOpen           = false;
        _pauseOpen          = false;
        _scoreboardShown    = false;
        _pageScreen         = Screen::Hidden;
        UpdateCapture();
        if (_view) {
            _view->Display(false);
        }
        if (_restoreKeyboardFlags) {
            SDK::Graphics::GetGraph()->KeyboardInit(*_restoreKeyboardFlags);
            _restoreKeyboardFlags.reset();
        }
        if (wasReady) {
            Menu::SetNativeControlsSuppressed(false);
            _chat->SetNativeChatEnabled(true);
        }
    }

    void WebUiService::Send(const char *type, const nlohmann::json &payload) {
        if (!_view) {
            return;
        }
        const nlohmann::json message = {{"type", type}, {"payload", payload}};
        _view->EvaluateScript("window.__m1o&&window.__m1o(" + message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + ")");
    }

    WebUiService::Screen WebUiService::LiveScreen() const {
        // The retail main-menu pointer may remain live after its close result
        // has handed control to a loaded mission. The mission is authoritative
        // here; otherwise CEF keeps the connection page over gameplay and
        // captures the selector's Enter key.
        if (_missionReady && World::NativeMissionActive()) {
            return Screen::Game;
        }
        if (SDK::Menu::GetActiveMainMenu()) {
            return Screen::Menu;
        }
        return Screen::Hidden;
    }

    void WebUiService::PushState(bool force) {
        if (!_pageReady) {
            return;
        }
        const auto menu      = Menu::GetState();
        const auto &quick    = QuickJoin::GetConfig();
        const auto &download = _instance->GetAssetDownloadStatus();
        const auto current   = _instance->GetCurrentState();
        std::string nickname = menu.nickname;
        if (!quick.enabled && !_settings.Nickname().empty()) {
            nickname = _settings.Nickname();
        }

        const nlohmann::json state = {
            {"screen", ScreenName(_screen)},
            {"version", Version::rel},
            {"limits", {{"nickname", Shared::Chat::kMaxNicknameCodePoints}, {"message", Shared::Chat::kMaxMessageCodePoints}}},
            {"connection",
             {{"phase", PhaseName(_phase)},
              {"status", menu.status},
              {"active", menu.connectionActive},
              {"playAvailable", menu.playAvailable},
              {"host", current.host},
              {"port", current.port},
              {"downloading", download.downloading},
              {"progress", download.progress}}},
            {"defaults", {{"nickname", nickname}, {"host", menu.host}, {"port", menu.port}}},
            {"settings", _settings.ToJson()},
        };
        std::string encoded = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (!force && encoded == _lastState) {
            return;
        }
        _lastState = std::move(encoded);
        Send("state", state);
    }

    void WebUiService::PushPlayers(World::WorldService &world, bool force) {
        const auto now = Clock::now();
        if (!force && now - _lastPlayerPush < kPlayerInterval) {
            return;
        }
        _lastPlayerPush   = now;
        auto *replication = Framework::CoreModules::GetReplication();
        struct Row {
            uint16_t index;
            nlohmann::json value;
        };
        std::vector<Row> rows;
        if (replication) {
            const uint64_t myGuid = static_cast<uint64_t>(replication->GetMyGUID());
            replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
                rows.push_back({player->playerIndex,
                                {{"id", player->GetNetworkID()},
                                 {"name", player->nickname},
                                 {"health", player->health},
                                 {"alive", player->alive},
                                 {"spawned", player->spawned},
                                 {"local", player->controllerGuid == myGuid}}});
            });
        }
        std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
            return a.index < b.index;
        });
        nlohmann::json list = nlohmann::json::array();
        for (auto &row : rows) {
            list.push_back(std::move(row.value));
        }
        const nlohmann::json payload = {{"players", list}, {"mission", world.SelectedMission()}};
        std::string encoded          = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (!force && encoded == _lastPlayers) {
            return;
        }
        _lastPlayers = std::move(encoded);
        Send("players", payload);
    }

    void WebUiService::Update(World::WorldService &world) {
        if (!_view) {
            return;
        }
        _missionReady = world.IsReady();
        _screen       = LiveScreen();
        if (!_pageReady) {
            return;
        }
        // CEF can throttle the built-in page while a resource modal covers it.
        if (HasFocusedResourceView()) {
            _silentTicks = 0;
            _lastHeartbeat = Clock::now();
        }
        else if (++_silentTicks > kHeartbeatTicks && Clock::now() - _lastHeartbeat > kHeartbeatTimeout) {
            Framework::Logging::GetLogger("Web")->error("Web UI stopped responding; the native chat is back");
            Deactivate();
            return;
        }

        if (_screen != Screen::Game) {
            _chatOpen  = false;
            _pauseOpen = false;
        }
        const bool scoreboard = _screen == Screen::Game && !_captured && Native::TestKey(static_cast<uint8_t>(kDikF1));
        if (scoreboard != _scoreboardShown) {
            _scoreboardShown = scoreboard;
            if (scoreboard) {
                PushPlayers(world, true);
            }
            Send("scoreboard", {{"visible", scoreboard}});
        }
        PushState(false);
        if (_screen == Screen::Game) {
            PushPlayers(world, false);
        }
        if (_closeRequestedAt && Clock::now() - *_closeRequestedAt > kCloseGrace) {
            CloseInGameScreens();
        }
        UpdateCapture();

        // Every page key and toggle comes from window messages. Under Wine an
        // exclusive DirectInput keyboard swallows them, so it is reacquired
        // shared; recreating it here, between frames, keeps ReadKey's edges.
        const uint32_t flags = SDK::Graphics::KeyboardFlags();
        if (flags & SDK::Graphics::kKeyboardExclusive) {
            if (!_restoreKeyboardFlags) {
                _restoreKeyboardFlags = flags;
            }
            SDK::Graphics::GetGraph()->KeyboardInit(flags & ~SDK::Graphics::kKeyboardExclusive);
        }
    }

    void WebUiService::UpdateCapture() {
        const bool want = _pageReady && (_screen == Screen::Menu || (_screen == Screen::Game && _pageScreen == Screen::Game && (_chatOpen || _pauseOpen)));
        if (want == _captured) {
            return;
        }
        _captured = want;
        if (want && _screen == Screen::Game) {
            _cursorX = _viewportWidth / 2;
            _cursorY = _viewportHeight / 2;
        }
        if (!want) {
            ReleaseHeldInput();
            _closeRequestedAt.reset();
        }
        ApplyFocus();
    }

    void WebUiService::CloseInGameScreens() {
        _closeRequestedAt.reset();
        if (!_chatOpen && !_pauseOpen) {
            return;
        }
        _chatOpen  = false;
        _pauseOpen = false;
        Send("chat:closed", nlohmann::json::object());
        UpdateCapture();
    }

    // Keys already held for gameplay (walking) would otherwise auto-repeat
    // into the page; they stay latched until released.
    void WebUiService::OpenChat(const std::string &prefill) {
        _latched |= _keysDown;
        _chatOpen = true;
        UpdateCapture();
        Send("chat:open", {{"prefill", prefill}});
    }

    void WebUiService::OpenPause() {
        _latched |= _keysDown;
        _pauseOpen = true;
        UpdateCapture();
        _lastPlayers.clear();
        _lastPlayerPush = {};
        Send("pause:open", nlohmann::json::object());
    }

    bool WebUiService::ToggleInGame(uint32_t key) {
        if (!_pageReady || LiveScreen() != Screen::Game || _pageScreen != Screen::Game) {
            return false;
        }
        if (key == kDikEscape) {
            if (_chatOpen || _pauseOpen) {
                CloseInGameScreens();
            }
            else {
                OpenPause();
            }
            return true;
        }
        if (!_captured && (key == kDikT || key == kDikSlash)) {
            OpenChat(key == kDikSlash ? "/" : "");
            return true;
        }
        return false;
    }

    // ReadKey sees the same presses through DirectInput. The window messages
    // decide; this only keeps the keys away from the game and native chat.
    bool WebUiService::OnNativeKey(uint32_t scanCode, unsigned char character) {
        (void)character;
        // Zero is "no key"; the chat still needs it to clear its held-key state.
        if (scanCode == 0) {
            return false;
        }
        if (HidesKeyboard()) {
            return true;
        }
        return LiveScreen() == Screen::Game && (scanCode == kDikT || scanCode == kDikSlash || scanCode == kDikEscape);
    }

    void WebUiService::ApplyFocus() {
        if (_view) {
            _view->Focus(_captured && _windowActive && !HasFocusedResourceView());
        }
    }

    Framework::GUI::View *WebUiService::FocusedResourceView() const {
        if (!_instance) {
            return nullptr;
        }
        auto *manager = _instance->GetWebManager();
        if (!manager) {
            return nullptr;
        }
        Framework::GUI::View *top = nullptr;
        for (auto *view : manager->GetGCViews()) {
            if (view->HasFocus() && view->ShouldDisplay() && (!top || view->GetZIndex() >= top->GetZIndex())) {
                top = view;
            }
        }
        return top;
    }

    bool WebUiService::HasFocusedResourceView() const {
        if (!_instance) return false;
        auto *manager = _instance->GetWebManager();
        return manager && manager->IsAnyGCViewFocused();
    }

    // Everything the page believes is held gets its release before the view
    // loses focus, or a key or button would stay down in the page.
    void WebUiService::ReleaseHeldInput() {
        if (!_view) {
            return;
        }
        if (_view->HasFocus()) {
            for (const WPARAM key : _heldKeys) {
                const UINT scanCode = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
                _view->ProcessKeyboardEvent(_window, WM_KEYUP, key, static_cast<LPARAM>(0xc0000001u | (scanCode << 16)));
            }
        }
        _heldKeys.clear();
        auto browser = _view->GetBrowser();
        if (browser && _mouseButtons != 0) {
            CefMouseEvent event;
            event.x = _mouseX;
            event.y = _mouseY;
            for (const auto &[bit, type] : {std::pair {SDK::Graphics::kMouseLeft, MBT_LEFT}, std::pair {SDK::Graphics::kMouseRight, MBT_RIGHT},
                                            std::pair {SDK::Graphics::kMouseMiddle, MBT_MIDDLE}}) {
                if (_mouseButtons & bit) {
                    browser->GetHost()->SendMouseClickEvent(event, type, true, 1);
                }
            }
        }
        _mouseButtons = 0;
    }

    bool WebUiService::OnWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (!_view) {
            return false;
        }
        switch (message) {
        case WM_ACTIVATE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            const bool active = message == WM_SETFOCUS || (message == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE);
            if (active != _windowActive) {
                _windowActive = active;
                // Key-ups sent while unfocused never arrive.
                if (!active) {
                    ReleaseHeldInput();
                    _keysDown.reset();
                    _latched.reset();
                    CloseInGameScreens();
                }
                ApplyFocus();
            }
            return false;
        }
        default: break;
        }
        if (!_windowActive) {
            return false;
        }
        // Resource views are separate CEF browsers. The built-in page owns its
        // own input path; send window messages to the top focused resource view
        // before the game or the built-in page can consume them.
        if (auto *resource = FocusedResourceView()) {
            switch (message) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYUP:
            case WM_CHAR:
                resource->ProcessKeyboardEvent(window, message, wParam, lParam);
                return true;
            case WM_IME_STARTCOMPOSITION:
            case WM_IME_COMPOSITION:
            case WM_IME_ENDCOMPOSITION:
            case WM_IME_CHAR:
                return HandleIme(resource, window, message, lParam);
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MOUSEWHEEL: {
                resource->ProcessMouseEvent(window, message, wParam, lParam);
                _lastResourceMouseMessage = Clock::now();
                POINT point {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (message == WM_MOUSEWHEEL) {
                    ScreenToClient(window, &point);
                }
                _cursorX = std::clamp(static_cast<int>(point.x), 0, std::max(0, _viewportWidth - 1));
                _cursorY = std::clamp(static_cast<int>(point.y), 0, std::max(0, _viewportHeight - 1));
                _mouseX = _cursorX;
                _mouseY = _cursorY;
                if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) _mouseButtons |= SDK::Graphics::kMouseLeft;
                if (message == WM_LBUTTONUP) _mouseButtons &= ~SDK::Graphics::kMouseLeft;
                if (message == WM_RBUTTONDOWN) _mouseButtons |= SDK::Graphics::kMouseRight;
                if (message == WM_RBUTTONUP) _mouseButtons &= ~SDK::Graphics::kMouseRight;
                if (message == WM_MBUTTONDOWN) _mouseButtons |= SDK::Graphics::kMouseMiddle;
                if (message == WM_MBUTTONUP) _mouseButtons &= ~SDK::Graphics::kMouseMiddle;
                SetResourceCursor(resource, _cursorX, _cursorY, (_mouseButtons & SDK::Graphics::kMouseLeft) != 0);
                return true;
            }
            default: break;
            }
        }
        switch (message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
        case WM_CHAR: return OnKeyMessage(window, message, wParam, lParam);
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_COMPOSITION:
        case WM_IME_ENDCOMPOSITION:
        case WM_IME_CHAR: return _captured && HandleIme(_view, window, message, lParam);
        default: return false;
        }
    }

    // A toggle fires only on a fresh press: not an auto-repeat (bit 30), and
    // not a key this stream already saw go down. The key that toggled stays
    // latched until its key-up, so its repeats and WM_CHAR never reach the page.
    bool WebUiService::OnKeyMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        const uint32_t key = KeyOf(lParam);
        const bool down    = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool up      = message == WM_KEYUP || message == WM_SYSKEYUP;
        if (up) {
            _keysDown.reset(key);
            if (_latched.test(key)) {
                _latched.reset(key);
                return true;
            }
        }
        else if (down) {
            const bool fresh = !(lParam & (1 << 30)) && !_keysDown.test(key);
            _keysDown.set(key);
            if (_latched.test(key)) {
                return true;
            }
            if (fresh && ToggleInGame(key)) {
                _latched.set(key);
                return true;
            }
            if (fresh && _chatOpen && (key == kKeyEnter || key == kKeyNumEnter) && !_closeRequestedAt) {
                _closeRequestedAt = Clock::now();
            }
        }
        else if (_latched.test(key)) {
            return true;
        }
        if (!_captured) {
            return false;
        }
        if (down && std::find(_heldKeys.begin(), _heldKeys.end(), wParam) == _heldKeys.end()) {
            _heldKeys.push_back(wParam);
        }
        if (up) {
            std::erase(_heldKeys, wParam);
        }
        // Dead keys compose inside TranslateMessage and arrive as one WM_CHAR.
        _view->ProcessKeyboardEvent(window, message, wParam, lParam);
        return false;
    }

    // Off-screen CEF has no window for the IME to draw into, so compositions
    // go through the browser host's IME API instead of WM_IME_CHAR.
    bool WebUiService::HandleIme(Framework::GUI::View *target, HWND window, UINT message, LPARAM lParam) {
        auto browser = target->GetBrowser();
        if (!browser) {
            return false;
        }
        auto host = browser->GetHost();
        switch (message) {
        case WM_IME_STARTCOMPOSITION: return true;
        case WM_IME_ENDCOMPOSITION: host->ImeFinishComposingText(false); return false;
        case WM_IME_CHAR: return true;
        default: break;
        }
        HIMC context = ImmGetContext(window);
        if (!context) {
            return false;
        }
        const auto read = [context](DWORD kind) {
            const LONG bytes = ImmGetCompositionStringW(context, kind, nullptr, 0);
            std::wstring text(bytes > 0 ? static_cast<size_t>(bytes) / sizeof(wchar_t) : 0, L'\0');
            if (bytes > 0) {
                ImmGetCompositionStringW(context, kind, text.data(), static_cast<DWORD>(bytes));
            }
            return text;
        };
        if (lParam & GCS_RESULTSTR) {
            host->ImeCommitText(read(GCS_RESULTSTR), CefRange::InvalidRange(), 0);
        }
        if (lParam & GCS_COMPSTR) {
            const std::wstring composition = read(GCS_COMPSTR);
            const int cursor               = lParam & GCS_CURSORPOS ? static_cast<int>(ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0) & 0xffff) : static_cast<int>(composition.size());
            host->ImeSetComposition(composition, {}, CefRange::InvalidRange(), CefRange(cursor, cursor));
        }
        ImmReleaseContext(window, context);
        return true;
    }

    void WebUiService::SetResourceCursor(Framework::GUI::View *target, int x, int y, bool pressed) {
        // Blink captures native scrollbars before dispatching page mousemove.
        // Drive the software cursor from the coordinates delivered to CEF.
        target->EvaluateScript("window.__m1oSetCursor&&window.__m1oSetCursor(" + std::to_string(x) + "," + std::to_string(y) + "," +
            (pressed ? "true" : "false") + "," + std::to_string(_viewportWidth) + "," + std::to_string(_viewportHeight) + ")");
    }

    void WebUiService::ForwardMouse(Screen live, Framework::GUI::View *target) {
        auto browser = target->GetBrowser();
        if (!browser) {
            return;
        }
        auto host = browser->GetHost();
        int x     = 0;
        int y     = 0;
        if (live == Screen::Menu) {
            x = SDK::Menu::MenuMouseX();
            y = SDK::Menu::MenuMouseY();
        }
        else {
            // No native cursor in a mission: integrate the DirectInput deltas
            // the game no longer sees while the page owns the mouse.
            _cursorX = std::clamp(_cursorX + Native::MouseDeltaX(), 0, std::max(0, _viewportWidth - 1));
            _cursorY = std::clamp(_cursorY + Native::MouseDeltaY(), 0, std::max(0, _viewportHeight - 1));
            x        = _cursorX;
            y        = _cursorY;
        }
        const auto now  = Native::MouseButtons();
        const int wheel = Native::MouseWheel();
        const bool moved = x != _mouseX || y != _mouseY;
        const bool buttonsChanged = now != _mouseButtons;

        CefMouseEvent event;
        event.x         = x;
        event.y         = y;
        event.modifiers = CefButtonFlags(now);
        if (x != _mouseX || y != _mouseY) {
            _mouseX = x;
            _mouseY = y;
            host->SendMouseMoveEvent(event, false);
        }
        const struct {
            uint32_t bit;
            cef_mouse_button_type_t type;
        } buttons[] = {{SDK::Graphics::kMouseLeft, MBT_LEFT}, {SDK::Graphics::kMouseRight, MBT_RIGHT}, {SDK::Graphics::kMouseMiddle, MBT_MIDDLE}};
        for (const auto &button : buttons) {
            const bool down = (now & button.bit) != 0;
            if (down != ((_mouseButtons & button.bit) != 0)) {
                host->SendMouseClickEvent(event, button.type, !down, 1);
            }
        }
        _mouseButtons = now;
        if (wheel != 0) {
            host->SendMouseWheelEvent(event, 0, wheel);
        }
        if (target != _view && (moved || buttonsChanged)) {
            SetResourceCursor(target, x, y, (now & SDK::Graphics::kMouseLeft) != 0);
        }
    }

    void WebUiService::OnPresent() {
        if (!_pageReady) {
            return;
        }
        auto *renderer = _instance->GetRenderer();
        auto *backend  = renderer->GetD3D8Backend();
        if (!backend->IsDeviceReady()) {
            return;
        }
        auto *manager = _instance->GetWebManager();
        int width     = 0;
        int height    = 0;
        if (renderer->GetBackBufferSize(width, height)) {
            manager->Resize(width, height);
            _viewportWidth  = width;
            _viewportHeight = height;
        }

        const Screen live = LiveScreen();
        auto *resource = FocusedResourceView();
        // A modal resource view may deliberately leave transparent pixels for
        // native model rendering. Keep the built-in chat/HUD page out of that
        // viewport while the modal owns focus; its browser stays alive.
        if (_view && _view->ShouldDisplay() == (resource != nullptr)) {
            _view->Display(resource == nullptr);
        }
        const bool resourceMouseRecent = resource && Clock::now() - _lastResourceMouseMessage < std::chrono::milliseconds(250);
        if (_windowActive && resource) {
            if (_mouseTargetId != resource->GetId()) {
                _mouseTargetId = resource->GetId();
                _mouseX = -1;
                _mouseY = -1;
                _mouseButtons = 0;
                _cursorX = _viewportWidth / 2;
                _cursorY = _viewportHeight / 2;
            }
            if (!resourceMouseRecent) {
                ForwardMouse(live, resource);
            }
        }
        else if (_windowActive && (live == Screen::Menu || (live == Screen::Game && _captured))) {
            _mouseTargetId = -1;
            ForwardMouse(live, _view);
        }
        else {
            _mouseTargetId = -1;
        }
        if (_view && _view->HasFocus() != (_captured && _windowActive && !resource)) {
            _view->Focus(_captured && _windowActive && !resource);
        }
        if (live == Screen::Hidden || live != _pageScreen) {
            return;
        }
        auto *device = backend->GetDevice();
        if (FAILED(device->BeginScene())) {
            return;
        }
        manager->Render();
        device->EndScene();
    }

    void WebUiService::OnChatMessage(const std::string &author, const std::string &text, uint32_t color) {
        if (text.empty()) {
            return;
        }
        ChatLine line {author, text, color, NowMs()};
        if (_pageReady) {
            Send("chat:message", {{"author", line.author}, {"text", line.text}, {"color", line.color}, {"time", line.time}});
        }
        _history.push_back(std::move(line));
        while (_history.size() > kMaxHistory) {
            _history.pop_front();
        }
    }

    void WebUiService::OnConnectionPhaseChanged(ConnectionPhase phase) {
        const bool connected = phase == ConnectionPhase::InGame && _phase != ConnectionPhase::InGame;
        _phase               = phase;
        if (!_pageReady) {
            return;
        }
        PushState(false);
        if (connected) {
            const auto current = _instance->GetCurrentState();
            Send("toast", {{"kind", "success"}, {"text", "Connected to " + current.host + ":" + std::to_string(current.port)}});
        }
    }

    void WebUiService::OnConnectionClosed(const std::string &reason) {
        _phase = ConnectionPhase::Disconnected;
        Reset();
        if (!_pageReady) {
            return;
        }
        Send("toast", {{"kind", reason.empty() ? "info" : "error"}, {"text", reason.empty() ? std::string("Disconnected") : "Disconnected: " + reason}});
        PushState(false);
    }

    std::optional<std::string> WebUiService::TakeOutgoing() {
        if (_outgoing.empty()) {
            return std::nullopt;
        }
        std::string line = std::move(_outgoing.front());
        _outgoing.pop_front();
        return line;
    }
} // namespace Mafia1Online::Features::WebUi
