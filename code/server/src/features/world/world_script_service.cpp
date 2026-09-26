#include "world_script_service.h"

#include "features/car/car_service.h"
#include "features/player/player_service.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/sound/sound_entity.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/door/door_intent.h"
#include "shared/features/world/world_state_entity.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <vector>

namespace Mafia1Online::Features::World {
    namespace {
        constexpr auto kDoorUseCooldown = std::chrono::milliseconds(120);
        constexpr float kDoorUseReach = 4.5f;
        constexpr float kDoorPositionTolerance = 1.0f;
    } // namespace

    void WorldScriptService::Init(Player::PlayerService &players, Car::CarService &cars) {
        _players = &players;
        _cars    = &cars;
        _state   = Framework::CoreModules::GetReplication()->CreateEntity<Shared::Entities::WorldStateEntity>();
        _state->streaming.alwaysVisible = true;
        _state->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
    }

    void WorldScriptService::Shutdown() {
        DestroyAllSounds();
        _doors.clear();
        _doorUseAt.clear();
        _state   = nullptr;
        _players = nullptr;
        _cars    = nullptr;
    }

    void WorldScriptService::ResetForMission(uint64_t missionGeneration) {
        DestroyAllSounds();
        for (const auto &[name, id] : _doors) {
            if (auto *door = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::DoorEntity>(id)) {
                Framework::CoreModules::GetReplication()->DestroyEntity(door);
            }
        }
        _doors.clear();
        _doorUseAt.clear();
        if (_state) {
            _state->frames.clear();
            _state->frameOpacities.clear();
            _state->frameGeneration = missionGeneration;
        }
    }

    void WorldScriptService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        for (uint64_t id : _sounds) {
            auto *sound = replication->GetEntity<Shared::Entities::SoundEntity>(id);
            if (!sound || sound->attachedId == 0) {
                continue;
            }
            std::optional<glm::vec3> target;
            if (auto *player = _players->FindByNetworkId(sound->attachedId)) {
                if (player->spawned) {
                    target = player->position;
                }
            }
            else if (auto *car = _cars->Find(sound->attachedId)) {
                target = car->position;
            }
            else {
                sound->attachedId = 0;
            }
            if (target && glm::distance(*target, sound->position) > 0.05f) {
                sound->position = *target;
            }
        }
    }

    Shared::Entities::SoundEntity *WorldScriptService::CreateSound(const std::string &wave, const glm::vec3 &position, float radius, float volume, uint64_t missionGeneration) {
        if (_sounds.size() >= kMaxSounds) {
            return nullptr;
        }
        auto *sound                    = Framework::CoreModules::GetReplication()->CreateEntity<Shared::Entities::SoundEntity>();
        sound->missionGeneration       = missionGeneration;
        sound->wave                    = wave;
        sound->position                = position;
        sound->radius                  = radius;
        sound->volume                  = volume;
        sound->streaming.alwaysVisible = true;
        sound->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        _sounds.insert(sound->GetNetworkID());
        return sound;
    }

    Shared::Entities::SoundEntity *WorldScriptService::FindSound(uint64_t id) const {
        return _sounds.contains(id) ? Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::SoundEntity>(id) : nullptr;
    }

    bool WorldScriptService::DestroySound(uint64_t id) {
        auto *sound = FindSound(id);
        if (!sound) {
            return false;
        }
        _sounds.erase(id);
        Framework::CoreModules::GetReplication()->DestroyEntity(sound);
        return true;
    }

    size_t WorldScriptService::DestroyAllSounds() {
        const std::vector<uint64_t> ids(_sounds.begin(), _sounds.end());
        size_t destroyed = 0;
        for (uint64_t id : ids) {
            destroyed += DestroySound(id) ? 1 : 0;
        }
        _sounds.clear();
        return destroyed;
    }

    bool WorldScriptService::ValidDoorName(std::string_view name) {
        if (name.empty() || name.size() > Shared::Entities::DoorEntity::kMaxNameLength || name.starts_with("mp_")) {
            return false;
        }
        for (const char c : name) {
            if (c < 0x21 || c > 0x7e || c == '/' || c == '\\') {
                return false;
            }
        }
        return true;
    }

    Shared::Entities::DoorEntity *WorldScriptService::CreateDoor(std::string_view name, const glm::vec3 &position, uint64_t generation) {
        if (!ValidDoorName(name) || generation == 0 ||
            !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
            return nullptr;
        }
        if (auto *existing = FindDoor(name)) {
            return existing;
        }
        if (_doors.size() >= Shared::Entities::DoorEntity::kMaxMissionDoors) {
            return nullptr;
        }
        auto *door = Framework::CoreModules::GetReplication()->CreateEntity<Shared::Entities::DoorEntity>();
        door->frameName = std::string(name);
        door->missionGeneration = generation;
        door->position = position;
        door->streaming.alwaysVisible = true;
        door->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        _doors.emplace(door->frameName, door->GetNetworkID());
        return door;
    }

    Shared::Entities::DoorEntity *WorldScriptService::FindDoor(std::string_view name) const {
        const auto it = _doors.find(std::string(name));
        return it == _doors.end() ? nullptr : FindDoor(it->second);
    }

    Shared::Entities::DoorEntity *WorldScriptService::FindDoor(uint64_t id) const {
        return Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::DoorEntity>(id);
    }

    bool WorldScriptService::RemoveDoor(uint64_t id) {
        auto *door = FindDoor(id);
        if (!door || !_doors.contains(door->frameName)) {
            return false;
        }
        _doors.erase(door->frameName);
        Framework::CoreModules::GetReplication()->DestroyEntity(door);
        return true;
    }

    bool WorldScriptService::ApplyDoorIntent(const Shared::Door::DoorIntent &intent, uint64_t senderGuid, uint64_t generation) {
        auto *player = _players->FindByGuid(static_cast<MafiaNet::PeerGuid>(senderGuid));
        if (!player || player->GetNetworkID() != intent.playerId || !player->spawned || !player->alive ||
            player->missionGeneration != generation || intent.missionGeneration != generation ||
            player->spawnGeneration != intent.spawnGeneration || !ValidDoorName(intent.frameName)) {
            return false;
        }
        const glm::vec3 reported(intent.x, intent.y, intent.z);
        if (!std::isfinite(reported.x) || !std::isfinite(reported.y) || !std::isfinite(reported.z)) {
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto last = _doorUseAt[static_cast<uint64_t>(senderGuid)];
        if (last != std::chrono::steady_clock::time_point {} && now - last < kDoorUseCooldown) {
            return false;
        }
        _doorUseAt[static_cast<uint64_t>(senderGuid)] = now;
        auto *door = FindDoor(intent.frameName);
        const glm::vec3 doorPosition = door ? door->position : reported;
        if (glm::distance(player->position, doorPosition) > kDoorUseReach ||
            (door && glm::distance(door->position, reported) > kDoorPositionTolerance)) {
            return false;
        }
        if (!door) {
            door = CreateDoor(intent.frameName, reported, generation);
        }
        if (!door || !door->enabled || (door->locked && intent.open)) {
            return false;
        }
        door->open = intent.open;
        door->reverse = intent.reverse;
        door->pairedReverse = intent.pairedReverse;
        door->openFraction = 1.0f;
        ++door->revision;
        return true;
    }
} // namespace Mafia1Online::Features::World
