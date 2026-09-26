#include "world_service.h"

#include "features/menu/menu_hooks.h"
#include "features/world/world_hooks.h"

#include "shared/features/world/mission_actors.h"
#include "shared/features/world/mission_catalog.h"
#include "shared/features/world/mission_entity.h"
#include "shared/features/world/mission_load_result.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/data_file.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/core/system.h>
#include <mafia1/sdk/menu/native_menu.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace Mafia1Online::Features::World {
    namespace {
        bool HideMissionActorModels(SDK::Scene::NativeScene *scene, const std::string &mission) {
            std::vector<uint8_t> bytes;
            std::vector<Shared::World::MissionActorRecord> actors;
            const auto path = "missions\\" + mission + "\\scene2.bin";
            if (!SDK::Core::DataFile::ReadAll(path.c_str(), bytes) || !Shared::World::ReadMissionActors(bytes, actors)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Could not read mission actor definitions for '{}'", mission);
                return false;
            }
            size_t hidden = 0;
            for (const auto &actor : actors) {
                if (Shared::World::KeepMissionActorGeometry(actor.type))
                    continue;
                if (auto *frame = scene->FindFrame(actor.frame.c_str())) {
                    frame->SetOn(false);
                    ++hidden;
                }
            }
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Hidden {} mission actor models in '{}'; static map geometry retained", hidden, mission);
            return true;
        }
    } // namespace

    void WorldService::Update() {
        // Framework exposes replication only after the connection-ready gate.
        // Native menu ticks reach us before that gate and again after disconnect.
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        replication->ForEach<Shared::Entities::MissionEntity>([this](Shared::Entities::MissionEntity *state) {
            if (state->generation == _selectedMissionGeneration && state->mission == _selectedMission) {
                return;
            }
            if (!Shared::World::IsStockMission(state->mission) && std::find(_modMissions.begin(), _modMissions.end(), state->mission) == _modMissions.end()) {
                if (!_hasRejectedMission || state->generation != _rejectedMissionGeneration || state->mission != _rejectedMission) {
                    _rejectedMission           = state->mission;
                    _rejectedMissionGeneration = state->generation;
                    _hasRejectedMission        = true;
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Server selected unsupported Mafia 1 mission '{}'", state->mission);
                    Features::Menu::SetStatus("Unsupported server mission");
                    Features::Menu::SetPlayAvailable(false);
                    _selectedMission.clear();
                    _selectedMissionGeneration = 0;
                    _enterRequested = false;
                    ExitGameLoop();
                }
                return;
            }
            _nativeObjects.Clear();
            ++_generation;
            _selectedMission           = state->mission;
            _selectedMissionGeneration = state->generation;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Server selected mission '{}' (generation {})", _selectedMission, _selectedMissionGeneration);
            if (_inGameLoop) {
                Features::Menu::SetStatus("Changing mission: " + _selectedMission);
                ExitGameLoop();
            }
            else {
                Features::Menu::SetStatus("Mission selected: " + _selectedMission);
                Features::Menu::SetPlayAvailable(true);
            }
        });

        if (_enterRequested && !_closingMenu && !_inGameLoop) {
            if (void *menu = SDK::Menu::GetActiveMainMenu()) {
                _closingMenu = true;
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Closing Mafia main menu for mission '{}'", _selectedMission);
                SDK::Menu::CloseMainMenuForGame(menu);
            }
        }
    }

    bool WorldService::RequestEnterGame() {
        if (_selectedMission.empty() || _selectedMissionGeneration == 0 || _enterRequested || !Framework::CoreModules::GetReplication()) {
            return false;
        }
        _enterRequested = true;
        Features::Menu::SetPlayAvailable(false);
        Features::Menu::SetStatus("Loading mission: " + _selectedMission);
        return true;
    }

    void WorldService::RunRequestedMission() {
        _closingMenu = false;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Entered Mafia mission handoff (requested {}, selected '{}', generation {})", _enterRequested, _selectedMission, _selectedMissionGeneration);
        if (!_enterRequested || _selectedMission.empty() || !Framework::CoreModules::GetReplication()) {
            return;
        }

        while (_enterRequested && !WindowExitRequested() && !_selectedMission.empty() && Framework::CoreModules::GetReplication()) {
            const std::string name = _selectedMission;
            const uint64_t generation = _selectedMissionGeneration;
            void *mission = SDK::Core::Mission::Get();
            SetNativeMissionActive(true);
            SetNativeMissionLoading(true);
            const int openResult = SDK::Core::Mission::Open(mission, name.c_str());
            SetNativeMissionLoading(false);
            if (openResult != 0) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Native mission '{}' failed to open (code {})", name, openResult);
                ReportLoadState(generation, static_cast<uint8_t>(Shared::World::MissionLoadState::SceneFailed));
                Features::Menu::SetStatus("Mission could not load: " + name);
                SDK::Core::Mission::Close(mission);
                break;
            }
            if (WindowExitRequested()) {
                SDK::Core::Mission::Close(mission);
                break;
            }

            if (!HideMissionActorModels(static_cast<SDK::Core::Mission::NativeMission *>(mission)->GetScene(), name)) {
                ReportLoadState(generation, static_cast<uint8_t>(Shared::World::MissionLoadState::SceneFailed));
                Features::Menu::SetStatus("Mission actor definitions could not load");
                SDK::Core::Mission::Close(mission);
                break;
            }

            const std::string collisionPath = "missions\\" + name + "\\tree.klz";
            if (!SDK::Core::System::LoadCollision(collisionPath.c_str())) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Native collision for mission '{}' failed to load", name);
                ReportLoadState(generation, static_cast<uint8_t>(Shared::World::MissionLoadState::CollisionFailed));
                Features::Menu::SetStatus("Mission collision load failed");
                SDK::Core::Mission::Close(mission);
                break;
            }
            if (WindowExitRequested()) {
                SDK::Core::Mission::Close(mission);
                break;
            }

            SDK::Core::Mission::ResetEvents(mission);
            auto *game = SDK::Core::Mission::GetGame(mission);
            if (!SDK::Core::System::InitGame(game)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Native game initialization for mission '{}' failed", name);
                ReportLoadState(generation, static_cast<uint8_t>(Shared::World::MissionLoadState::GameInitFailed));
                Features::Menu::SetStatus("Mission initialization failed");
                SDK::Core::Mission::Close(mission);
                break;
            }
            if (WindowExitRequested()) {
                SDK::Core::Mission::Close(mission);
                break;
            }

            game->SetTrafficVisible(false);
            const auto *nativePlayer = game->Player();
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info(
                "Native game initialized: player {:p}, player dead {}, death triggered {}, state {}, death timer {}",
                static_cast<const void *>(nativePlayer), nativePlayer && nativePlayer->IsDead(),
                game->PlayerDeathTriggered(), static_cast<uint32_t>(game->GetState()),
                game->PlayerDeathMenuTimer());

            _loadedMissionGeneration = generation;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Native mission '{}' loaded (generation {})", name, generation);
            ReportLoadState(generation, static_cast<uint8_t>(Shared::World::MissionLoadState::Ready));
            _inGameLoop = true;
            SDK::Core::System::RunGameLoop();
            _inGameLoop = false;
            SDK::Core::Mission::Close(mission);
            SetNativeMissionActive(false);

            // A new server generation received during the old game loop is
            // loaded without reopening the retail main menu in between.
            if (_selectedMissionGeneration == generation) {
                break;
            }
        }

        SetNativeMissionActive(false);
        _enterRequested = false;
        Features::Menu::SetPlayAvailable(!_selectedMission.empty() && Framework::CoreModules::GetReplication());
    }

    void WorldService::ExitGameLoop() {
        if (_inGameLoop) {
            SDK::Core::System::ExitGameLoop(SDK::Core::Mission::GetGame(SDK::Core::Mission::Get()));
        }
    }

    void WorldService::ReportLoadState(uint64_t generation, uint8_t state) {
        if (auto *network = Framework::CoreModules::GetNetworkPeer(); network && Framework::CoreModules::GetReplication()) {
            Shared::World::MissionLoadResult result;
            result.generation = generation;
            result.state = static_cast<Shared::World::MissionLoadState>(state);
            network->BroadcastRPC(result);
        }
    }

    void WorldService::Reset() {
        _modMissions.clear();
        ExitGameLoop();
        _enterRequested = false;
        _closingMenu = false;
        _nativeObjects.Clear();
        ++_generation;
        _selectedMission.clear();
        _selectedMissionGeneration = 0;
        _rejectedMission.clear();
        _rejectedMissionGeneration = 0;
        _hasRejectedMission        = false;
        _loadedMissionGeneration = 0;
        Features::Menu::SetPlayAvailable(false);
    }

    void WorldService::NotifyMissionClosing() {
        if (_missionClosing) {
            _missionClosing();
        }
        if (_loadedMissionGeneration != 0) {
            ReportLoadState(_loadedMissionGeneration, static_cast<uint8_t>(Shared::World::MissionLoadState::Unloaded));
            _loadedMissionGeneration = 0;
        }
        _nativeObjects.Clear();
        ++_generation;
    }
} // namespace Mafia1Online::Features::World
