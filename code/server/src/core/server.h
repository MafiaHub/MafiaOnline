#pragma once

#include <integrations/server/instance.h>

#include "features/car/car_service.h"
#include "features/car/debris_service.h"
#include "features/combat/combat_service.h"
#include "features/pickup/pickup_service.h"
#include "features/player/player_service.h"
#include "features/world/mission_readiness.h"
#include "features/world/world_script_service.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Mafia1Online::Shared::Entities {
    class MissionEntity;
}

namespace Mafia1Online::Core {
    class Server final: public Framework::Integrations::Server::Instance {
      public:
        void PostInit() override;
        void PostUpdate() override;
        void UpdateCarControllers();
        bool AllowChat(uint64_t senderNetworkId);
        void PreShutdown() override;
        void OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &info) override;
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid) override;
        void OnChatMessage(uint64_t senderNetworkId, const std::string &text) override;
        void OnChatCommand(uint64_t senderNetworkId, const std::string &text, const std::string &command, const std::vector<std::string> &args) override;
        void ModuleRegister(Framework::Scripting::Engine *engine) override;
        // Events.onClient handlers receive the project's Player class.
        v8::Local<v8::Value> WrapScriptPlayer(v8::Isolate *isolate, uint64_t networkId) override;

        bool ChangeMission(std::string_view name);
        // Colors are packed 0xRRGGBBAA; 0 uses the client's notice color.
        bool SendNotice(uint64_t playerNetworkId, std::string_view message, uint32_t color = 0);
        void BroadcastNotice(std::string_view message, uint32_t color = 0);

        const std::string &Mission() const {
            return _mission;
        }

        uint64_t MissionGeneration() const;

        bool AllPlayersReady() const {
            return _missionReadiness.AllReady();
        }

        bool IsPlayerReady(MafiaNet::PeerGuid guid) const {
            return _missionReadiness.IsReady(guid);
        }

        Features::Player::PlayerService &Players() {
            return _players;
        }

        Features::Car::CarService &Cars() {
            return _cars;
        }

        Features::Pickup::PickupService &Pickups() {
            return _pickups;
        }
        Features::Car::DebrisService &Debris() {
            return _debris;
        }

        Features::Combat::CombatService &Combat() {
            return _combat;
        }

        Features::World::WorldScriptService &WorldScript() {
            return _worldScript;
        }

      private:
        std::string _mission;
        Shared::Entities::MissionEntity *_missionState = nullptr;
        Features::Player::PlayerService _players;
        Features::Car::CarService _cars;
        Features::Car::DebrisService _debris;
        Features::Pickup::PickupService _pickups;
        Features::Combat::CombatService _combat;
        Features::World::MissionReadiness _missionReadiness;
        Features::World::WorldScriptService _worldScript;
        std::unordered_set<uint64_t> _reportedCarLoadFailures;
        // Clients whose native car creation failed, per car. None of them can
        // simulate that car.
        std::unordered_map<uint64_t, std::unordered_set<uint64_t>> _carLoadFailures;
        struct ChatBucket {
            float tokens = 0.0f;
            std::chrono::steady_clock::time_point last;
            std::chrono::steady_clock::time_point lastWarning;
        };
        std::unordered_map<uint64_t, ChatBucket> _chatBuckets;
        std::chrono::steady_clock::time_point _lastControllerUpdate;
        // Last hand-off of an empty car, which follows the nearest player.
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _emptyCarHandoff;
        uint64_t _carHitSequence = 0;
        bool _allReadyNotified = false;
    };
} // namespace Mafia1Online::Core
