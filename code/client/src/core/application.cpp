#include "application.h"

#include <mafia1/sdk/input/native_input.h>
#include <algorithm>

#include "shared/register_entities.h"

#include "features/car/car_hooks.h"
#include "features/car/debris_hooks.h"
#include "features/combat/combat_hooks.h"
#include "features/death/death_service.h"
#include "features/menu/menu_hooks.h"
#include "features/player/player_hooks.h"
#include "features/quick_join/quick_join.h"
#include "features/seat/seat_hooks.h"
#include "features/startup_video/startup_video_hooks.h"
#include "features/script/script_runtime.h"
#include "features/script/visual_scripting.h"
#include "features/world/world_hooks.h"

#include <mafia1/sdk/ui/native_indicators.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <integrations/client/networking/engine.h>
#include <logging/logger.h>
#include <networking/network_client.h>
#include <stdexcept>

namespace Mafia1Online::Core {
    using Framework::Integrations::Client::ConnectionPhase;
    void Application::PostInit() {
        if (!_mods.Install()) throw std::runtime_error("Could not hook rw_data.dll file loading");
        Shared::Entities::RegisterEntities();
        _cars.RegisterRPC();
        _combat.RegisterRPC();
        _death.RegisterRPC();
        _seats.RegisterRPC();
        _players.RegisterRPC();
        _cameraFollow.RegisterRPC();
        _effects.RegisterRPC(_world);
        _sounds.RegisterRPC(_world);
        if (!Features::World::InstallWorldHooks()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install C_mission::Close hook");
            ExitProcess(1);
        }
        if (!Features::Car::InstallCarHooks()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install car lifecycle hooks");
            Features::World::UninstallWorldHooks();
            ExitProcess(1);
        }
        if (!Features::Player::InstallPlayerHooks()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install player lifecycle hooks");
            Features::Car::UninstallCarHooks();
            Features::World::UninstallWorldHooks();
            ExitProcess(1);
        }
        if (!Features::Seat::InstallSeatHooks(_seats)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native seat hooks");
            Features::Player::UninstallPlayerHooks();
            Features::Car::UninstallCarHooks();
            Features::World::UninstallWorldHooks();
            ExitProcess(1);
        }
        if (!Features::Combat::InstallCombatHooks(_combat)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native combat hooks");
            Features::Player::UninstallPlayerHooks();
            Features::Seat::UninstallSeatHooks();
            Features::Car::UninstallCarHooks();
            Features::World::UninstallWorldHooks();
            ExitProcess(1);
        }
        if (!Features::Death::InstallDeathHooks(_death)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native human damage hooks");
            Features::Combat::UninstallCombatHooks();
            Features::Player::UninstallPlayerHooks();
            Features::Seat::UninstallSeatHooks();
            Features::Car::UninstallCarHooks();
            Features::World::UninstallWorldHooks();
            ExitProcess(1);
        }
        if (!_cameraFollow.InstallTickHook()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native camera tick hook");
            ExitProcess(1);
        }
        if (!_chat.Install()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native chat and HUD hooks");
            Features::Death::UninstallDeathHooks();
            Features::Combat::UninstallCombatHooks();
            Features::Player::UninstallPlayerHooks();
            Features::Seat::UninstallSeatHooks();
            Features::Car::UninstallCarHooks();
            Features::World::UninstallWorldHooks();
            ExitProcess(1);
        }
        _chat.SetOverlay([this] { _nametags.Render(_world); Scripting::RenderDrawCommands(); });
        // Optional: without CEF, quick join and the native chat stay in charge.
        if (!_webUi.Install(*this, _chat, _projectPath)) {
            Features::Menu::SetNativeControlsSuppressed(false);
        }
        if (!_pickups.Install(_world, _combat)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native pickup hooks");
            ExitProcess(1);
        }
        _gameplayMenus.Install(_world, _webUi);
        _chat.SetInputFilter([this](void *input) {
            if (_input->IsInputLocked()) {
                for (bool pressedOnly : {false, true}) {
                    float *state = nullptr;
                    const int count = SDK::Input::GetState(input, &state, pressedOnly);
                    if (count > 0) std::fill_n(state, count, 0.0f);
                }
            }
            _gameplayMenus.FilterInput(input);
        });
        _pickups.SetChoiceFilter([this](SDK::World::NativeItemVector &items) { return _gameplayMenus.FilterNearObjects(items); });
        _seats.SetUseFilter([this](const void *human) { return _gameplayMenus.AllowsNativeUse(human); });
        if (!_doors.Install()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native door hooks");
            ExitProcess(1);
        }
        _world.SetMissionClosingCallback([this] {
            _gameplayMenus.Reset();
            _script.OnMissionClosing();
            _pickups.OnMissionClosing();
            _doors.Reset();
            _sounds.OnMissionClosing();
            _hud.OnMissionClosing();
            _environment.OnMissionClosing();
        });
        if (!Features::Car::InstallDebrisHooks()) {
            ExitProcess(1);
        }
    }

    void Application::PostUpdate() {
        _mods.Update();
        if (auto prepared = _mods.TakeCompleted()) {
            _assetError = prepared->error;
            if (_assetError.empty()) _world.SetModMissions(std::move(prepared->missions));
            else Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Server mods: {}", _assetError);
            CompleteDeferredInitialAssetProcessing(prepared->generation, _assetError.empty());
        }
        const uint64_t previousMissionGeneration = _world.SelectedMissionGeneration();
        _world.Update();
        if (_autoEnterPending && GetConnectionPhase() == ConnectionPhase::InGame && _world.RequestEnterGame()) _autoEnterPending = false;
        if (previousMissionGeneration != 0 && previousMissionGeneration != _world.SelectedMissionGeneration()) {
            _combat.Reset();
            _death.Reset();
            _seats.Reset();
            _debris.Reset(_world);
        }
        _players.Update(_world);
        _cars.Update(_world);
        if (_world.IsReady() && !SDK::UI::NativeIndicators::Get().MapEnabled()) {
            // Held TAB shows the city map; retail enables it only from the
            // ENABLEMAP mission script, and C_game::Done disables it again.
            SDK::UI::NativeIndicators::Get().MapEnable(true);
        }
        _debris.Update(_world);
        _pickups.Update(_world);
        _doors.Update(_world);
        _seats.Update(_world);
        _cameraFollow.Update(_world);
        _death.Update(_world, _combat);
        _combat.Update(_world);
        _environment.Update(_world);
        _sounds.Update(_world);
        _hud.Update(_world);
        _script.Update(_world);
        const auto *localState = _combat.GetLocalState(_world);
        _chat.Update(_world.IsReady(), localState && localState->spawned ? std::optional<float>(localState->health) : std::nullopt);
        while (auto line = _chat.TakeOutgoing()) {
            SubmitChatLine(*line);
        }
        _input->SetReady(_world.IsReady());
        _input->Update();
        if (IsLocalInputAvailable()) {
            if (_input->IsKeyPressed('K')) {
                (void)_cars.ToggleSiren(_world);
            }
            if (_input->IsKeyPressed(FW_KEY_F10)) {
                const char *mode = _cars.ToggleSyncMode() ? "Car sync: predicted physics" : "Car sync: interpolated";
                _chat.OnMessage("", mode, 0xE0C080FF);
                _webUi.OnChatMessage("", mode, 0xE0C080FF);
            }
        }
        _webUi.Update(_world);
        _gameplayMenus.Update();
        while (auto line = _webUi.TakeOutgoing()) {
            SubmitChatLine(*line);
        }
        const auto &quickJoin = Features::QuickJoin::GetConfig();
        if (quickJoin.enabled && !_quickJoinConnectIssued && GetConnectionPhase() == ConnectionPhase::Disconnected) {
            _quickJoinConnectIssued = true;
            SetCurrentState({quickJoin.host, quickJoin.port, quickJoin.nickname, quickJoin.password});
            if (const auto result = ConnectToServer(quickJoin.host, quickJoin.port, quickJoin.password); !result) {
                Features::Menu::SetStatus(result.GetError().message);
            }
        }
        if (quickJoin.enabled && _quickJoinConnectIssued && !_quickJoinPlayIssued && GetConnectionPhase() == ConnectionPhase::InGame) {
            _quickJoinPlayIssued = _world.RequestEnterGame();
        }
        if (auto request = Features::Menu::TakeRequest()) {
            if (request->action == Features::Menu::Action::Disconnect) {
                (void)GetNetworkingEngine()->GetNetworkClient()->Disconnect();
            }
            else if (GetConnectionPhase() != ConnectionPhase::Disconnected) {
                Features::Menu::SetStatus("Disconnect before connecting again");
            }
            else {
                SetCurrentState({request->host, request->port, request->nickname, request->password});
                if (const auto result = ConnectToServer(request->host, request->port, request->password); !result) {
                    Features::Menu::SetStatus(result.GetError().message);
                }
            }
        }
    }

    void Application::SubmitChatLine(const std::string &line) {
        const auto first = line.find_first_not_of(" \t\r\n");
        const auto last = line.find_last_not_of(" \t\r\n");
        if (first != std::string::npos && last == first + 1 && line[first] == '/' && (line[last] == 'q' || line[last] == 'Q')) {
            // Keep this local and use the same ordered shutdown as the Quit
            // button. Server scripts never receive the built-in command.
            PostMessageA(static_cast<HWND>(SDK::Graphics::GetGraph()->MainWindow()), WM_CLOSE, 0, 0);
            return;
        }
        SendChatMessage(line);
    }

    void Application::PreShutdown() {
        _gameplayMenus.Reset();
        _chat.SetInputFilter({});
        _pickups.SetChoiceFilter({});
        _seats.SetUseFilter({});
        _mods.Shutdown();
        _cameraFollow.Reset();
        _cameraFollow.UninstallTickHook();
        _script.Reset();
        _sounds.Reset();
        _hud.Reset();
        _environment.Reset();
        _pickups.Shutdown();
        _doors.Shutdown();
        Features::Car::UninstallDebrisHooks();
        _webUi.Shutdown();
        _chat.Shutdown();
        Features::Death::UninstallDeathHooks();
        Features::Combat::UninstallCombatHooks();
        Features::Seat::UninstallSeatHooks();
        Features::Menu::UninstallMenuHooks();
        Features::StartupVideo::UninstallHooks();
        _players.Reset(_world);
        _debris.Reset(_world);
        _cars.Reset();
        _seats.Reset();
        _death.Reset();
        _combat.Reset();
        Features::Player::UninstallPlayerHooks();
        Features::Car::UninstallCarHooks();
        Features::World::UninstallWorldHooks();
        _world.Reset();
    }

    void Application::OnConnectionClosed() {
        _input->SetReady(false);
        _input->SetInputLocked(false);
        _input->Update();
        _gameplayMenus.Reset();
        _mods.Reset();
        _autoEnterPending = false;
        _cameraFollow.Reset();
        _script.Reset();
        _sounds.Reset();
        _hud.Reset();
        _environment.Reset();
        _chat.Reset();
        _nametags.Reset();
        _pickups.Reset();
        _doors.Reset();
        _players.Reset(_world);
        _debris.Reset(_world);
        _cars.Reset();
        _seats.Reset();
        _death.Reset();
        _combat.Reset();
        _world.Reset();
        Features::Menu::SetConnectionActive(false);
        const auto &reason = GetLastDisconnectionReason();
        Features::Menu::SetStatus(reason.empty() ? "Disconnected" : "Disconnected: " + reason);
        _webUi.OnConnectionClosed(reason);
        if (!_assetError.empty()) Features::Menu::SetStatus("Server mods: " + _assetError);
    }

    Framework::Integrations::Client::InitialAssetProcessingDecision Application::OnInitialAssetDownloadReady(uint64_t generation, const Framework::Integrations::Client::AssetDownloadStatus &) {
        Features::Menu::SetStatus("Verifying and preparing server assets...");
        _mods.Begin(generation, GetServerConfig(), GetAssetCachePath(), std::filesystem::path(_projectPath) / "cache" / "mods");
        return Framework::Integrations::Client::InitialAssetProcessingDecision::Defer;
    }

    void Application::OnConnectionPhaseChanged(ConnectionPhase phase) {
        if (phase == ConnectionPhase::Connecting) {
            _assetError.clear();
            _autoEnterPending = true;
        }
        Features::Menu::SetConnectionActive(phase != ConnectionPhase::Disconnected);
        switch (phase) {
        case ConnectionPhase::Disconnected: Features::Menu::SetStatus("Disconnected"); break;
        case ConnectionPhase::Connecting: Features::Menu::SetStatus("Connecting..."); break;
        case ConnectionPhase::Authenticating: Features::Menu::SetStatus("Authenticating..."); break;
        case ConnectionPhase::Downloading: Features::Menu::SetStatus("Downloading server assets..."); break;
        case ConnectionPhase::Starting: Features::Menu::SetStatus("Starting session..."); break;
        case ConnectionPhase::InGame: Features::Menu::SetStatus("Connected"); break;
        }
        _webUi.OnConnectionPhaseChanged(phase);
    }

    void Application::ModuleRegister(Framework::Scripting::Engine *engine) {
        Scripting::RegisterClientScripting(engine);
    }

    void Application::PostScriptInit() {
        if (_visualResourceCleanupRegistered) return;
        Scripting::RegisterVisualResourceCleanup(*GetScriptingModule()->GetResourceManager());
        _visualResourceCleanupRegistered = true;
    }

    void Application::OnChatMessageReceived(const Framework::Networking::RPC::ChatMessage &message) {
        _chat.OnMessage(message.author, message.text, message.color);
        _webUi.OnChatMessage(message.author, message.text, message.color);
    }
} // namespace Mafia1Online::Core
