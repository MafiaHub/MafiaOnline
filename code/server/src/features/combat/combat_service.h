#pragma once

#include "shared/features/combat/combat_protocol.h"
#include "shared/features/combat/damage_event.h"
#include "shared/features/combat/detonation.h"
#include "shared/features/combat/fatal_fall_report.h"
#include "shared/features/combat/hit_report.h"
#include "shared/features/combat/vehicle_impact.h"
#include "shared/features/car/car_hit_report.h"

#include <mafianet/peerinterface.h>
#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Mafia1Online::Features::Player {
    class PlayerService;
}
namespace Mafia1Online::Features::Car {
    class CarService;
}

namespace Mafia1Online::Shared::Entities {
    class PlayerEntity;
}

namespace Mafia1Online::Features::Combat {
    enum class DamageCause : uint8_t { Script, Firearm, Melee, Explosion, Fire, Fall, Drowning, Vehicle };

    class CombatService final {
      public:
        void RegisterRPC(Player::PlayerService &players, Car::CarService &cars);
        void OnPlayerConnect(uint64_t networkId);
        void OnPlayerDisconnect(uint64_t networkId);
        void OnPlayerSpawn(uint64_t networkId);
        void OnPlayerDespawn(uint64_t networkId);
        void ResetForMission();
        void Update();

        struct WeaponInfo {
            const char *name;
            const char *kind; // "melee", "firearm" or "throwable"
            uint16_t magazine;
        };
        static std::optional<WeaponInfo> Weapon(uint8_t weaponId);
        struct InventoryEntry {
            uint8_t weaponId;
            uint16_t loaded;
            uint16_t reserve;
        };
        bool GiveWeapon(uint64_t networkId, uint8_t weaponId, uint16_t loaded, uint16_t reserve);
        // Validates the complete replacement before publishing one state revision.
        bool ReplaceInventory(uint64_t networkId, const std::vector<InventoryEntry> &entries, std::optional<uint8_t> selected);
        bool RemoveWeapon(uint64_t networkId, uint8_t weaponId);
        bool SetWeaponAmmo(uint64_t networkId, uint8_t weaponId, uint16_t loaded, uint16_t reserve);
        bool SelectWeapon(uint64_t networkId, uint8_t weaponId);
        // Moves a held weapon into the replicated pickup world. Returns zero
        // without changing inventory when a pickup cannot be created.
        uint64_t DropWeapon(uint64_t networkId, uint8_t weaponId);
        bool SetHealth(uint64_t networkId, float health, uint64_t sourceId = 0,
                       std::optional<uint8_t> weaponId = std::nullopt, DamageCause cause = DamageCause::Script);
        // Merges a world weapon into the player's inventory. Returns false
        // when nothing could be taken; leftover ammunition stays on the item.
        bool TakeWeapon(uint64_t networkId, uint8_t weaponId, uint16_t loaded, uint16_t reserve, uint16_t &leftLoaded, uint16_t &leftReserve);
        // Drop(playerId, weaponId, loaded, reserve, position, yaw) fires for
        // an accepted drop and for the weapon a player held at death.
        void SetDropCallback(std::function<uint64_t(uint64_t, uint8_t, uint16_t, uint16_t, const glm::vec3 &, float)> callback) {
            _dropCallback = std::move(callback);
        }
        void SetDroppedCallback(std::function<void(uint64_t, uint64_t)> callback) {
            _droppedCallback = std::move(callback);
        }
        struct CarImpactEvidence {
            float damage = 0.0f;
            glm::vec3 origin {0.0f};
            glm::vec3 trajectory {0.0f};
            std::chrono::steady_clock::time_point acceptedAt;
        };
        // Inspect before checking the car's historical pose. Consume only
        // after that check; human hits and car hits share the pellet mask.
        std::optional<CarImpactEvidence> InspectCarImpact(const Shared::Car::HitReport &report, MafiaNet::PeerGuid sender) const;
        bool ConsumeCarImpact(const Shared::Car::HitReport &report, MafiaNet::PeerGuid sender);
        void SetTransitionCallback(std::function<void(uint64_t, uint64_t, std::optional<uint8_t>, DamageCause, float, float, uint16_t)> callback) {
            _transitionCallback = std::move(callback);
        }
        void SetActionCallback(std::function<void(const Shared::Combat::Event &)> callback) {
            _actionCallback = std::move(callback);
        }
        const Shared::Combat::State *GetState(uint64_t networkId) const;
        // Script world effects, damaging players from their server positions
        // with the retail explosion falloff and fire tick.
        void ApplyExplosion(uint64_t sourceId, uint64_t missionGeneration, const glm::vec3 &position, float radius, float damage);
        void StartFire(uint64_t sourceId, uint64_t missionGeneration, const glm::vec3 &position, std::chrono::milliseconds life, float radius, float damage);

      private:
        struct Session {
            struct FireRecord {
                Shared::Combat::Intent intent;
                std::chrono::steady_clock::time_point acceptedAt;
                uint16_t consumedPellets = 0;
            };
            Shared::Combat::State state;
            uint32_t lastSequence = 0;
            bool hasSequence      = false;
            std::chrono::steady_clock::time_point lastAction;
            std::deque<FireRecord> fires;
            struct MeleeRecord {
                uint32_t sequence = 0;
                uint8_t weaponId  = 0;
                bool heavy        = false;
                std::chrono::steady_clock::time_point acceptedAt;
                bool used = false;
            };
            std::deque<MeleeRecord> melee;
            std::chrono::steady_clock::time_point lastMelee;
            std::chrono::steady_clock::time_point meleeStartedAt;
            uint32_t meleeStartSequence = 0;
            uint8_t meleeStartWeapon = 0;
            std::chrono::steady_clock::time_point grenadeStartedAt;
            std::chrono::steady_clock::time_point grenadeReleasedAt;
            uint32_t grenadeStartSequence = 0;
            uint32_t grenadeReleaseSequence = 0;
            uint8_t grenadeWeapon = 0;
            glm::vec3 grenadeDirection {0.0f};
            std::chrono::steady_clock::time_point pendingFallUntil;
        };

        void OnIntent(const Shared::Combat::Intent &intent, MafiaNet::PeerGuid sender);
        void OnHitReport(const Shared::Combat::HitReport &report, MafiaNet::PeerGuid sender);
        void OnMeleeHit(const Shared::Combat::HitReport &report, Shared::Entities::PlayerEntity &attacker,
                        Shared::Entities::PlayerEntity &target, Session &session);
        void PublishDamage(const Shared::Combat::HitReport &report, float damage, uint8_t hitType, std::optional<uint8_t> weaponId);
        void OnFatalFallReport(const Shared::Combat::FatalFallReport &report, MafiaNet::PeerGuid sender);
        void OnDetonationReport(const Shared::Combat::DetonationReport &report, MafiaNet::PeerGuid sender);
        void OnVehicleImpact(const Shared::Combat::VehicleImpact &report, MafiaNet::PeerGuid sender);
        void ApplyVehicleImpact(const Shared::Combat::VehicleImpact &report, MafiaNet::PeerGuid sender,
                                std::chrono::steady_clock::time_point receivedAt);
        void ApplyAreaDamage(uint64_t sourceId, uint64_t missionGeneration, const glm::vec3 &position, float radius, bool explosion, float damage,
                             std::optional<uint8_t> weaponId);
        void UpdateThrows();

        // A thrown grenade or Molotov lives on after its thrower's life ends.
        struct ThrowRecord {
            uint64_t throwerId = 0;
            uint64_t missionGeneration = 0;
            uint32_t sequence = 0;
            uint8_t grenadeType = 0;
            glm::vec3 origin {0.0f};
            float speed = 0.0f;
            std::chrono::steady_clock::time_point acceptedAt;
        };
        struct FireArea {
            uint64_t sourceId = 0;
            std::optional<uint8_t> weaponId;
            uint64_t missionGeneration = 0;
            glm::vec3 position {0.0f};
            std::chrono::steady_clock::time_point startedAt;
            std::chrono::steady_clock::time_point lastTick;
            // A Molotov unless a script fire chose otherwise.
            std::chrono::milliseconds life {5000};
            float radius = 2.5f;
            float damage = 50.0f;
        };
        std::deque<ThrowRecord> _throws;
        std::deque<FireArea> _fires;
        struct PendingVehicleImpact {
            Shared::Combat::VehicleImpact report;
            MafiaNet::PeerGuid sender;
            std::chrono::steady_clock::time_point receivedAt;
        };
        std::deque<PendingVehicleImpact> _pendingVehicleImpacts;
        void Publish(Session &session, MafiaNet::PeerGuid except = MafiaNet::UNASSIGNED_PEER_GUID);
        void Emit(const Shared::Combat::Intent &intent, const Session &session);
        Session *FindCurrent(uint64_t networkId);

        Player::PlayerService *_players = nullptr;
        Car::CarService *_cars = nullptr;
        std::unordered_map<uint64_t, Session> _sessions;
        std::map<std::pair<uint64_t, uint64_t>, std::chrono::steady_clock::time_point> _vehicleImpacts;
        std::function<void(uint64_t, uint64_t, std::optional<uint8_t>, DamageCause, float, float, uint16_t)> _transitionCallback;
        std::function<void(const Shared::Combat::Event &)> _actionCallback;
        std::function<uint64_t(uint64_t, uint8_t, uint16_t, uint16_t, const glm::vec3 &, float)> _dropCallback;
        std::function<void(uint64_t, uint64_t)> _droppedCallback;
        uint64_t DropWeapon(Session &session, Shared::Entities::PlayerEntity &player, uint8_t weaponId);
    };
} // namespace Mafia1Online::Features::Combat
