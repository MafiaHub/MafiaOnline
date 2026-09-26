#pragma once

#include "shared/features/pickup/pickup_request.h"
#include "shared/features/pickup/weapon_pickup_entity.h"

#include <mafianet/peerinterface.h>

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::Features::Player {
    class PlayerService;
}
namespace Mafia1Online::Features::Combat {
    class CombatService;
}
namespace Mafia1Online::Features::Car {
    class CarService;
}

namespace Mafia1Online::Features::Pickup {
    // Server-owned weapons in the world. The use key on a client becomes a
    // request; the server checks reach, life and inventory, then moves the
    // weapon and ammunition into the player's server inventory.
    class PickupService final {
      public:
        void RegisterRPC(Player::PlayerService &players, Combat::CombatService &combat, Car::CarService &cars);
        // Zero lifetime keeps the pickup until it is taken or destroyed.
        uint64_t Create(uint8_t weaponId, const glm::vec3 &position, float yaw, uint16_t loaded, uint16_t reserve,
                        uint64_t missionGeneration, std::chrono::milliseconds lifetime);
        bool Destroy(uint64_t id);
        Shared::Entities::WeaponPickupEntity *Find(uint64_t id) const;
        std::vector<uint64_t> List() const;
        void Update();
        void ResetForMission();
        // Taken(pickupId, playerId, weaponId) fires after an accepted pickup.
        void SetTakenCallback(std::function<void(uint64_t, uint64_t, uint8_t)> callback) {
            _taken = std::move(callback);
        }

      private:
        void OnRequest(const Shared::Pickup::PickupRequest &request, MafiaNet::PeerGuid sender);

        struct Record {
            std::chrono::steady_clock::time_point expiresAt;
            bool expires = false;
        };
        static constexpr size_t kMaximumPickups = 256;
        std::unordered_map<uint64_t, Record> _pickups;
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _lastRequest;
        Player::PlayerService *_players = nullptr;
        Combat::CombatService *_combat = nullptr;
        Car::CarService *_cars = nullptr;
        std::function<void(uint64_t, uint64_t, uint8_t)> _taken;
    };
} // namespace Mafia1Online::Features::Pickup
