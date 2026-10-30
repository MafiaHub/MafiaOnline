#include "application.h"

#include "shared/register_entities.h"
#include "shared/features/simulation/simulation_ticket.h"

#include "features/car/car_hooks.h"
#include "features/car/debris_hooks.h"
#include "features/combat/combat_hooks.h"
#include "features/death/death_service.h"
#include "features/debug/sync_trace.h"
#include "features/menu/menu_hooks.h"
#include "features/player/player_hooks.h"
#include "features/quick_join/quick_join.h"
#include "features/seat/seat_hooks.h"
#include "features/simulation/debug_overlay.h"
#include "features/simulation/simulation_config.h"
#include "features/startup_video/startup_video_hooks.h"
#include "features/script/door_scripting.h"
#include "features/script/map_scripting.h"
#include "features/script/pickup_scripting.h"
#include "features/script/player_scripting.h"
#include "features/script/script_runtime.h"
#include "features/script/settings_scripting.h"
#include "features/script/visual_scripting.h"
#include "features/world/world_hooks.h"

#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/ui/native_indicators.h>
#include <networking/replication/replication_manager.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <integrations/client/networking/engine.h>
#include <logging/logger.h>
#include <networking/network_client.h>
#include <stdexcept>

namespace Mafia1Online::Core {
    using Framework::Integrations::Client::ConnectionPhase;
    void Application::PostInit() {
        _input->SetWindow(static_cast<HWND>(SDK::Graphics::GetGraph()->MainWindow()));
        if (!_mods.Install()) throw std::runtime_error("Could not hook rw_data.dll file loading");
        Shared::Entities::RegisterEntities();
        _cars.RegisterRPC();
        _pedestrians.RegisterRPC();
        if (Features::Simulation::IsHost()) {
            _npcs.RegisterRPC(_world);
            _traffic.RegisterRPC(_world);
        }
        _combat.RegisterRPC();
        _death.RegisterRPC();
        _seats.RegisterRPC();
        _players.RegisterRPC();
        _cameraFollow.RegisterRPC();
        _railways.RegisterRPC();
        _bridges.RegisterRPC();
        _pumpers.RegisterRPC();
        _props.RegisterRPC();
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
        if (Features::Simulation::IsHost() && !Features::Simulation::InstallDebugOverlay()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install the simulation host overlay");
            ExitProcess(1);
        }
        if (!_pedestrians.Install()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install pedestrian traffic hooks");
            ExitProcess(1);
        }
        if (Features::Simulation::IsHost() && !_npcs.Install()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install NPC hooks");
            ExitProcess(1);
        }
        if (Features::Simulation::IsHost() && !_traffic.Install(_cars, _npcs)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install traffic car hooks");
            ExitProcess(1);
        }
        if (!_railways.Install()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install railway hooks");
            ExitProcess(1);
        }
        if (!_bridges.Install(_death)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install bridge hooks");
            ExitProcess(1);
        }
        if (!_pumpers.Install(_world)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install gas station attendant hooks");
            ExitProcess(1);
        }
        if (!_props.Install()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install world prop hooks");
            ExitProcess(1);
        }
        if (!_cameraFollow.InstallTickHook()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native camera tick hook");
            ExitProcess(1);
        }
        // The simulation host has no screen for players: no chat, HUD, web UI
        // or gameplay menus.
        const bool host = Features::Simulation::IsHost();
        if (!host && !_chat.Install()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native chat and HUD hooks");
            Features::Death::UninstallDeathHooks();
            Features::Combat::UninstallCombatHooks();
            Features::Player::UninstallPlayerHooks();
            Features::Seat::UninstallSeatHooks();
            Features::Car::UninstallCarHooks();
            Features::World::UninstallWorldHooks();
            ExitProcess(1);
        }
        if (!host) {
            // The profile is loaded by now: G_LoadSaveClass::Init ran inside
            // InitSystem, before the first mission tick created this instance.
            _settings.Load(_projectPath);
            _settings.AttachInput(_input.get());
            _settings.ApplyAll();
        }
        if (!host) {
            _chat.SetOverlay([this] {
                _nametags.Render(_world);
                Scripting::RenderDrawCommands();
                Scripting::ClientCityMap::Render();
            });
            // Optional: without CEF, quick join and the native chat stay in charge.
            if (!_webUi.Install(*this, _chat, _settings, _projectPath)) {
                Features::Menu::SetNativeControlsSuppressed(false);
            }
        }
        if (!_pickups.Install(_world, _combat)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native pickup hooks");
            ExitProcess(1);
        }
        if (!host) {
            _gameplayMenus.Install(_world, _webUi);
            _chat.SetInputFilter([this](void *input) { _gameplayMenus.FilterInput(input); });
            _pickups.SetChoiceFilter([this](SDK::World::NativeItemVector &items) { return _gameplayMenus.FilterNearObjects(items); });
            _seats.SetUseFilter([this](const void *human) { return _gameplayMenus.AllowsNativeUse(human); });
        }
        if (!_doors.Install()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install native door hooks");
            ExitProcess(1);
        }
        if (!_mapObjects.Install()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->critical("Could not install the map object collision hook");
            ExitProcess(1);
        }
        _world.SetMissionClosingCallback([this] {
            // Crew models leave C_game's driver list while it still exists.
            _trafficCrew.Reset();
            _railways.Reset();
            _bridges.Reset();
            _pumpers.Reset();
            _props.Reset();
            _roadBarriers.OnMissionClosing();
            _traffic.Reset();
            _pedestrianReplicas.Reset(_world);
            _pedestrians.Reset();
            _npcReplicas.Reset(_world);
            _npcs.Reset();
            _gameplayMenus.Reset();
            _script.OnMissionClosing();
            _pickups.OnMissionClosing();
            _doors.Reset();
            _mapObjects.OnMissionClosing();
            _sounds.OnMissionClosing();
            _fires.OnMissionClosing();
            _hud.OnMissionClosing();
            _environment.OnMissionClosing();
            _mouths.Reset();
        });
        if (!Features::Car::InstallDebrisHooks()) {
            ExitProcess(1);
        }
    }

    void Application::PostUpdate() {
        _input->Update();
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
        // Railways first: riders are placed on this frame's wagons.
        _railways.Update(_world);
        _bridges.Update(_world);
        _pumpers.Update(_world);
        _props.Update(_world);
        _players.Update(_world);
        _pedestrians.Update(_world);
        // NPC replicas first: one that replaces a walker takes that walker's
        // replica over before the walker's removal is applied.
        _npcReplicas.Update(_world);
        _pedestrianReplicas.Update(_world);
        if (Features::Simulation::IsHost()) {
            _npcs.Update(_world);
            // Binds its cars before CarService first sees them.
            _traffic.Update(_world);
        }
        _cars.Update(_world);
        _trafficCrew.Update(_world);
        _roadBarriers.Update(_world);
        const bool host = Features::Simulation::IsHost();
        if (!host && _world.IsReady() && !SDK::UI::NativeIndicators::Get().MapEnabled()) {
            // Held TAB shows the city map; retail enables it only from the
            // ENABLEMAP mission script, and C_game::Done disables it again.
            SDK::UI::NativeIndicators::Get().MapEnable(true);
        }
        _debris.Update(_world);
        _pickups.Update(_world);
        _doors.Update(_world);
        _mapObjects.Update(_world);
        _seats.Update(_world);
        if (!host) _cameraFollow.Update(_world);
        _death.Update(_world, _combat);
        _combat.Update(_world);
        _environment.Update(_world);
        if (!host) {
            _sounds.Update(_world);
            _fires.Update(_world);
            _hud.Update(_world);
            _script.Update(_world);
        }
        UpdatePlayerInterface();
        ConnectFromConfiguration();
        if (Features::Debug::SyncTrace::Enabled()) {
            _cars.TraceFrameEnd();
            Features::Debug::SyncTrace::EndFrame();
            Features::Debug::SyncTrace::BeginFrame(SyncTraceFocus());
        }
    }

    std::vector<glm::vec3> Application::SyncTraceFocus() const {
        // A player traces around its camera; the host, which nobody watches,
        // around every player.
        std::vector<glm::vec3> focus;
        if (!Features::Simulation::IsHost()) {
            auto *mission = SDK::Core::Mission::Get();
            auto *scene   = mission ? mission->GetScene() : nullptr;
            if (auto *camera = scene ? scene->ActiveCamera() : nullptr) {
                const auto eye = camera->WorldPosition();
                focus.emplace_back(eye.x, eye.y, eye.z);
            }
            return focus;
        }
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
                if (player->spawned) {
                    focus.push_back(player->position);
                }
            });
        }
        return focus;
    }

    void Application::UpdatePlayerInterface() {
        if (Features::Simulation::IsHost()) {
            return;
        }
        using Features::Settings::ModKey;
        _settings.Update();
        if (_world.IsReady() && _world.LoadedMissionGeneration() != _settingsMissionGeneration) {
            _settingsMissionGeneration = _world.LoadedMissionGeneration();
            _settings.OnMissionReady();
        }
        _chat.SetOpenKeys(_settings.ModKeyDik(ModKey::Chat), _settings.ModKeyDik(ModKey::Command));
        _nametags.SetVisible(_settings.Hud().nametags, _settings.Hud().nametagHealth);
        const auto *localState = _combat.GetLocalState(_world);
        _chat.Update(_world.IsReady(), localState && localState->spawned ? std::optional<float>(localState->health + _combat.HeldDamage(localState->networkId)) : std::nullopt);
        while (auto line = _chat.TakeOutgoing()) {
            SubmitChatLine(*line);
        }
        while (auto key = _chat.TakeGameKey()) {
            // K by default: retail has no player siren control.
            if (*key == _settings.ModKeyDik(ModKey::Siren)) {
                (void)_cars.ToggleSiren(_world);
            }
            // F10 by default: compare the two observer car sync modes.
            else if (*key == _settings.ModKeyDik(ModKey::CarSync)) {
                const char *mode = _cars.ToggleSyncMode() ? "Car sync: predicted physics" : "Car sync: interpolated";
                _chat.OnMessage("", mode, 0xE0C080FF);
                _webUi.OnChatMessage("", mode, 0xE0C080FF);
            }
        }
        _webUi.Update(_world);
        _voiceInput.Update(_chat.IsInputActive() || _webUi.IsCapturingInput());
        _mouths.Update(_world);
        _gameplayMenus.Update();
        while (auto line = _webUi.TakeOutgoing()) {
            SubmitChatLine(*line);
        }
    }

    void Application::ConnectFromConfiguration() {
        const auto &quickJoin = Features::QuickJoin::GetConfig();
        if (quickJoin.enabled && !_quickJoinConnectIssued && GetConnectionPhase() == ConnectionPhase::Disconnected) {
            _quickJoinConnectIssued = true;
            Framework::Integrations::Client::CurrentState state {quickJoin.host, quickJoin.port, quickJoin.nickname, quickJoin.password};
            if (Features::Simulation::IsHost()) {
                state.ticket = Shared::Simulation::MakeTicket(Features::Simulation::GetConfig().token);
            }
            SetCurrentState(std::move(state));
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
        // Local, never sent: "/synctrace" toggles the sync trace,
        // "/synctrace mark <text>" writes a marker event into it.
        if (first != std::string::npos && line.compare(first, 10, "/synctrace") == 0 && (line.size() == first + 10 || line[first + 10] == ' ')) {
            const auto rest = line.find_first_not_of(' ', first + 10);
            const char *status = nullptr;
            if (rest != std::string::npos && line.compare(rest, 4, "mark") == 0) {
                const auto text = line.find_first_not_of(' ', rest + 4);
                Features::Debug::SyncTrace::Event("mark", 0, text == std::string::npos ? "" : line.substr(text, last - text + 1));
                status = Features::Debug::SyncTrace::Enabled() ? "Sync trace: marked" : "Sync trace is off";
            }
            else {
                status = Features::Debug::SyncTrace::Toggle() ? "Sync trace on: logs/synctrace" : "Sync trace off";
            }
            _chat.OnMessage("", status, 0xE0C080FF);
            _webUi.OnChatMessage("", status, 0xE0C080FF);
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
        _pedestrianReplicas.Reset(_world);
        _pedestrians.Uninstall();
        _npcReplicas.Reset(_world);
        _trafficCrew.Reset();
        _roadBarriers.Reset(_world);
        _railways.Uninstall();
        _bridges.Uninstall();
        _pumpers.Uninstall();
        _props.Uninstall();
        if (Features::Simulation::IsHost()) {
            _traffic.Uninstall();
            _npcs.Uninstall();
        }
        if (Features::Simulation::IsHost()) {
            Features::Simulation::UninstallDebugOverlay();
        }
        _script.Reset();
        _sounds.Reset();
        _fires.Reset();
        _hud.Reset();
        _environment.Reset();
        _pickups.Shutdown();
        _doors.Shutdown();
        _mapObjects.Uninstall();
        Features::Car::UninstallDebrisHooks();
        _webUi.Shutdown();
        _settings.Shutdown();
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
        _trafficCrew.Reset();
        _roadBarriers.Reset(_world);
        _traffic.Reset();
        _pedestrianReplicas.Reset(_world);
        _pedestrians.Reset();
        _npcReplicas.Reset(_world);
        _npcs.Reset();
        _gameplayMenus.Reset();
        _mods.Reset();
        _autoEnterPending = false;
        _cameraFollow.Reset();
        _script.Reset();
        _sounds.Reset();
        _fires.Reset();
        _hud.Reset();
        _environment.Reset();
        _chat.Reset();
        _nametags.Reset();
        _mouths.Reset();
        _pickups.Reset();
        _doors.Reset();
        _mapObjects.Reset();
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
        Scripting::ClientCityMap::RegisterResourceCleanup(*GetScriptingModule()->GetResourceManager());
        Scripting::SettingsApi::RegisterResourceCleanup(*GetScriptingModule()->GetResourceManager());
        // Every resource runs in an isolate of its own; the bindings' classes go with it.
        GetScriptingModule()->GetEngine()->AddRuntimeDisposingListener([](v8::Isolate *isolate) {
            Scripting::Player::UnregisterIsolate(isolate);
            Scripting::Vehicle::UnregisterIsolate(isolate);
            Scripting::Door::UnregisterIsolate(isolate);
            Scripting::Pickup::UnregisterIsolate(isolate);
        });
        _visualResourceCleanupRegistered = true;
    }

    void Application::OnChatMessageReceived(const Framework::Networking::RPC::ChatMessage &message) {
        _chat.OnMessage(message.author, message.text, message.color);
        _webUi.OnChatMessage(message.author, message.text, message.color);
    }
} // namespace Mafia1Online::Core
