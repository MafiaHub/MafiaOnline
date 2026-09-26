#include "pickup_service.h"

#include "features/car/car_service.h"
#include "features/combat/combat_service.h"
#include "features/player/player_service.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>

#include <cmath>

namespace Mafia1Online::Features::Pickup {
    void PickupService::RegisterRPC(Player::PlayerService &players, Combat::CombatService &combat, Car::CarService &cars) {
        _players = &players;
        _combat  = &combat;
        _cars    = &cars;
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Pickup::PickupRequest>([this](const Shared::Pickup::PickupRequest &request, MafiaNet::Packet *packet) {
            OnRequest(request, MafiaNet::ToPeerGuid(packet->guid));
        });
    }

    uint64_t PickupService::Create(uint8_t weaponId, const glm::vec3 &position, float yaw, uint16_t loaded, uint16_t reserve,
                                   uint64_t missionGeneration, std::chrono::milliseconds lifetime) {
        if (weaponId < 2 || weaponId > 15 || _pickups.size() >= kMaximumPickups || !std::isfinite(position.x) ||
            !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(yaw)) {
            return 0;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        auto *pickup      = replication->CreateEntity<Shared::Entities::WeaponPickupEntity>();
        pickup->missionGeneration = missionGeneration;
        pickup->weaponId          = weaponId;
        pickup->position          = position;
        pickup->yaw               = yaw;
        pickup->loaded            = loaded;
        pickup->reserve           = reserve;
        // Until player positions drive interest, every pickup is visible.
        pickup->streaming.alwaysVisible = true;
        Record record;
        record.expires   = lifetime.count() > 0;
        record.expiresAt = std::chrono::steady_clock::now() + lifetime;
        _pickups[pickup->GetNetworkID()] = record;
        return pickup->GetNetworkID();
    }

    Shared::Entities::WeaponPickupEntity *PickupService::Find(uint64_t id) const {
        if (!_pickups.contains(id)) {
            return nullptr;
        }
        return Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::WeaponPickupEntity>(id);
    }

    bool PickupService::Destroy(uint64_t id) {
        auto *pickup = Find(id);
        _pickups.erase(id);
        if (!pickup) {
            return false;
        }
        Framework::CoreModules::GetReplication()->DestroyEntity(pickup);
        return true;
    }

    std::vector<uint64_t> PickupService::List() const {
        std::vector<uint64_t> ids;
        ids.reserve(_pickups.size());
        for (const auto &[id, record] : _pickups) {
            ids.push_back(id);
        }
        return ids;
    }

    void PickupService::Update() {
        const auto now = std::chrono::steady_clock::now();
        std::vector<uint64_t> expired;
        for (const auto &[id, record] : _pickups) {
            if (record.expires && now >= record.expiresAt) {
                expired.push_back(id);
            }
        }
        for (uint64_t id : expired) {
            Destroy(id);
        }
    }

    void PickupService::ResetForMission() {
        for (uint64_t id : List()) {
            Destroy(id);
        }
        _lastRequest.clear();
    }

    void PickupService::OnRequest(const Shared::Pickup::PickupRequest &request, MafiaNet::PeerGuid sender) {
        auto *player = _players->FindByGuid(sender);
        auto *pickup = Find(request.pickupId);
        if (!player || !pickup || player->GetNetworkID() != request.networkId || !player->spawned || !player->alive ||
            player->missionGeneration != request.missionGeneration || player->spawnGeneration != request.spawnGeneration ||
            pickup->missionGeneration != player->missionGeneration || _cars->SeatForPlayer(player->GetNetworkID(), player->spawnGeneration).has_value()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        auto &last     = _lastRequest[player->GetNetworkID()];
        if (last != std::chrono::steady_clock::time_point {} && now - last < std::chrono::milliseconds(250)) {
            return;
        }
        last = now;
        // Retail use reach is 0.7 horizontally from a probe 0.8 ahead of the
        // player, plus latency.
        const glm::vec3 offset = pickup->position - player->position;
        if (glm::length(glm::vec3(offset.x, 0.0f, offset.z)) > 2.2f || std::abs(offset.y) > 2.0f) {
            return;
        }
        uint16_t leftLoaded  = 0;
        uint16_t leftReserve = 0;
        if (!_combat->TakeWeapon(player->GetNetworkID(), pickup->weaponId, pickup->loaded, pickup->reserve, leftLoaded, leftReserve)) {
            return;
        }
        const uint64_t id      = pickup->GetNetworkID();
        const uint8_t weaponId = pickup->weaponId;
        if (leftLoaded == 0 && leftReserve == 0) {
            Destroy(id);
        }
        else {
            pickup->loaded  = leftLoaded;
            pickup->reserve = leftReserve;
        }
        if (_taken) {
            _taken(id, player->GetNetworkID(), weaponId);
        }
    }
} // namespace Mafia1Online::Features::Pickup
