#pragma once

#include <integrations/server/instance.h>

#include "features/bridge/bridge_service.h"
#include "features/car/car_service.h"
#include "features/car/debris_service.h"
#include "features/combat/combat_service.h"
#include "features/pickup/pickup_service.h"
#include "features/npc/npc_service.h"
#include "features/npc/police_service.h"
#include "features/pedestrian/pedestrian_service.h"
#include "features/player/player_service.h"
#include "features/pumper/pumper_service.h"
#include "features/railway/railway_service.h"
#include "features/simulation/simulation_service.h"
#include "features/taxi/taxi_service.h"
#include "features/traffic/traffic_service.h"
#include "features/world/mission_readiness.h"
#include "features/world/props_service.h"
#include "features/world/world_script_service.h"
#include "features/mod/mod_service.h"
#include "features/fire/fire_service.h"
#include "features/map_object/map_object_service.h"

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
        void LoadMods() { _mods.Load("mods"); }
        void OnAssetStreamerReady() override;
        void PostInit() override;
        void PostUpdate() override;
        void UpdateCarControllers();
        // Hands an accepted bullet impact to the car's simulation controller
        // and the vehicleHit script event.
        void SendCarHit(const Shared::Car::HitReport &report, float damage);
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

        Features::Pedestrian::PedestrianService &Pedestrians() {
            return _pedestrians;
        }

        Features::Npc::NpcService &Npcs() {
            return _npcs;
        }
        Features::Npc::PoliceService &Police() {
            return _police;
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

        Features::Simulation::SimulationService &Simulation() {
            return _simulation;
        }

        Features::Traffic::TrafficService &Traffic() {
            return _traffic;
        }
        Features::Taxi::TaxiService &Taxi() {
            return _taxi;
        }
        Features::Railway::RailwayService &Railways() {
            return _railways;
        }
        Features::Bridge::BridgeService &Bridges() {
            return _bridges;
        }
        Features::Pumper::PumperService &Pumpers() {
            return _pumpers;
        }
        Features::MapObject::MapObjectService &MapObjects() {
            return _mapObjects;
        }
        Features::Fire::FireService &Fires() {
            return _fires;
        }
        Features::World::PropsService &Props() {
            return _props;
        }

      private:
        // A player, or the simulation host, that has loaded the mission.
        bool CarPeer(MafiaNet::PeerGuid sender) const;

        Features::Mod::ModService _mods;
        std::string _mission;
        Shared::Entities::MissionEntity *_missionState = nullptr;
        Features::Player::PlayerService _players;
        Features::Car::CarService _cars;
        Features::Car::DebrisService _debris;
        Features::Pickup::PickupService _pickups;
        Features::Combat::CombatService _combat;
        Features::World::MissionReadiness _missionReadiness;
        Features::World::WorldScriptService _worldScript;
        Features::Simulation::SimulationService _simulation;
        Features::Pedestrian::PedestrianService _pedestrians;
        Features::Npc::NpcService _npcs;
        Features::Npc::PoliceService _police;
        Features::Traffic::TrafficService _traffic;
        Features::Taxi::TaxiService _taxi;
        Features::Railway::RailwayService _railways;
        Features::Bridge::BridgeService _bridges;
        Features::Pumper::PumperService _pumpers;
        Features::World::PropsService _props;
        Features::MapObject::MapObjectService _mapObjects;
        Features::Fire::FireService _fires;
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
        uint64_t _carContactSequence = 0;
        bool _allReadyNotified = false;
    };
} // namespace Mafia1Online::Core
