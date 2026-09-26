#pragma once

#include <integrations/client/instance.h>

#include "features/car/car_service.h"
#include "features/camera/camera_follow_service.h"
#include "features/car/debris_service.h"
#include "features/chat/chat_service.h"
#include "features/nametag/nametag_service.h"
#include "features/pickup/pickup_service.h"
#include "features/combat/combat_service.h"
#include "features/death/death_service.h"
#include "features/door/door_service.h"
#include "features/effects/effects_service.h"
#include "features/environment/environment_service.h"
#include "features/hud/hud_service.h"
#include "features/script/script_service.h"
#include "features/sound/sound_service.h"
#include "features/player/player_service.h"
#include "features/seat/seat_service.h"
#include "features/web_ui/web_ui_service.h"
#include "features/world/world_service.h"
#include "features/mod/mod_service.h"
#include "features/gameplay_menu/gameplay_menu_service.h"

namespace Mafia1Online::Core {
    class Application final: public Framework::Integrations::Client::Instance {
      public:
        void PostInit() override;
        void PostUpdate() override;
        void PreShutdown() override;
        void OnConnectionClosed() override;
        void OnConnectionPhaseChanged(Framework::Integrations::Client::ConnectionPhase phase) override;
        Framework::Integrations::Client::InitialAssetProcessingDecision OnInitialAssetDownloadReady(uint64_t generation, const Framework::Integrations::Client::AssetDownloadStatus &status) override;
        void OnChatMessageReceived(const Framework::Networking::RPC::ChatMessage &message) override;
        void ModuleRegister(Framework::Scripting::Engine *engine) override;
        void PostScriptInit() override;

        // The directory the launcher loaded the client from; set before Init.
        void SetProjectPath(std::string projectPath) {
            _projectPath = std::move(projectPath);
        }

        Features::World::WorldService &World() {
            return _world;
        }

        Features::Mod::ModService &Mods() { return _mods; }
        Features::GameplayMenu::GameplayMenuService &GameplayMenus() { return _gameplayMenus; }

        Features::Car::CarService &Cars() {
            return _cars;
        }

        Features::Car::DebrisService &Debris() {
            return _debris;
        }

        Features::Combat::CombatService &Combat() {
            return _combat;
        }

        Features::Player::PlayerService &Players() {
            return _players;
        }

        Features::Hud::HudService &Hud() {
            return _hud;
        }

        Features::Sound::SoundService &Sounds() {
            return _sounds;
        }

      private:
        void SubmitChatLine(const std::string &line);
        Features::Mod::ModService _mods;
        std::string _assetError;
        bool _autoEnterPending = false;
        Features::World::WorldService _world;
        Features::Player::PlayerService _players;
        Features::Car::CarService _cars;
        Features::Camera::CameraFollowService _cameraFollow;
        Features::Car::DebrisService _debris;
        Features::Seat::SeatService _seats;
        Features::Combat::CombatService _combat;
        Features::Death::DeathService _death;
        Features::Chat::ChatService _chat;
        Features::Nametag::NametagService _nametags;
        Features::Pickup::PickupService _pickups;
        Features::Door::DoorService _doors;
        Features::WebUi::WebUiService _webUi;
        Features::GameplayMenu::GameplayMenuService _gameplayMenus;
        Features::Environment::EnvironmentService _environment;
        Features::Sound::SoundService _sounds;
        Features::Effects::EffectsService _effects;
        Features::Hud::HudService _hud;
        Features::Script::ScriptService _script;
        std::string _projectPath;
        bool _quickJoinConnectIssued = false;
        bool _quickJoinPlayIssued    = false;
        bool _visualResourceCleanupRegistered = false;
    };
} // namespace Mafia1Online::Core
