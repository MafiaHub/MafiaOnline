#pragma once

#include <mafianet/virtual_world.h>
#include "shared/features/combat/aim_update.h"
#include "shared/features/combat/combat_protocol.h"
#include "shared/features/combat/damage_event.h"
#include "shared/features/combat/detonation.h"
#include "shared/features/combat/fatal_fall_report.h"
#include "shared/features/combat/hit_report.h"
#include "shared/features/combat/vehicle_impact.h"
#include "shared/features/car/car_hit_report.h"

#include <mafianet/peer_interface.h>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Framework::Networking::Replication {
    class NetworkEntity;
}

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
    enum class DamageCause : uint8_t { Script, Firearm, Melee, Explosion, Fire, Fall, Drowning, Vehicle, Railway };

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
        // A bullet impact the shooter reported on something the server keeps
        // no health for (a car, a railway): one unused pellet of an accepted
        // firearm shot of the last 2 s, flying along that pellet within its
        // native range. The evidence carries the damage after range falloff.
        struct ShotClaim {
            uint64_t shooterId = 0;
            uint64_t missionGeneration = 0;
            uint64_t shooterSpawnGeneration = 0;
            uint32_t shotSequence = 0;
            uint8_t pelletIndex = 0;
            glm::vec3 direction {0.0f};
            glm::vec3 hit {0.0f};
        };
        std::optional<CarImpactEvidence> InspectShot(const ShotClaim &claim, MafiaNet::PeerGuid sender) const;
        bool ConsumeShot(const ShotClaim &claim, MafiaNet::PeerGuid sender);
        void SetTransitionCallback(std::function<void(uint64_t, uint64_t, std::optional<uint8_t>, DamageCause, float, float, uint16_t)> callback) {
            _transitionCallback = std::move(callback);
        }
        void SetActionCallback(std::function<void(const Shared::Combat::Event &)> callback) {
            _actionCallback = std::move(callback);
        }
        const Shared::Combat::State *GetState(uint64_t networkId) const;
        // Server NPCs players can hit, such as the simulation host's
        // NPCs. A player's validated hit on one goes to hit() with the
        // NPC's native Hit damage before its body part scale; hitType is 0
        // bullet, 1 melee knockdown, 4 car impact or 5 car bump (Npc::Hit).
        struct NpcTarget {
            glm::vec3 position {0.0f};
            glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
            float health = 0.0f;
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration = 0;
            bool alive = false;
        };
        struct NpcTargets {
            std::function<std::optional<NpcTarget>(uint64_t)> find;
            std::function<void(uint64_t npcId, uint64_t attackerId, uint8_t hitType, uint8_t bodyPart, float damage, const glm::vec3 &direction, const glm::vec3 &position,
                               uint8_t weaponId)> hit;
        };
        void SetNpcTargets(NpcTargets targets) {
            _npcTargets = std::move(targets);
        }
        // Script world effects, damaging players from their server positions
        // with the retail explosion falloff and fire tick.
        void ApplyExplosion(uint64_t sourceId, uint64_t missionGeneration, const glm::vec3 &position, float radius, float damage);
        // A car the server just committed as exploded: C_car::CarExplosion
        // (0x421d60) passes radius 10 and damage 200 to NewExplosion. Every
        // client replays it natively, where players' own hits are suppressed.
        void ApplyCarExplosion(uint64_t missionGeneration, const glm::vec3 &position) {
            ApplyExplosion(0, missionGeneration, position, 10.0f, 200.0f);
        }
        // A fire hurts only players in its virtual world (every world by default).
        void StartFire(uint64_t sourceId, uint64_t missionGeneration, const glm::vec3 &position, std::chrono::milliseconds life, float radius, float damage,
                       uint32_t virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL);
        // A hit the simulation host's NPC landed on a player's replica, with
        // the native Hit damage before the body part scale. A bullet (hit
        // type 0) is scaled like a player's shot; bodyPart 0 means through
        // the car the target sits in. A melee knockdown (1) applies as given.
        void ApplyNpcHit(uint64_t sourceId, uint64_t targetId, uint64_t missionGeneration, uint64_t targetSpawnGeneration, uint8_t hitType, uint8_t bodyPart, float damage,
                         const glm::vec3 &direction, uint8_t weaponId);
        // A tram or metro train of the simulation host ran into a player on
        // foot there; damage is C_human::Collision's (|v| - 2.5) * 4.2.
        // Returns whether the hit was applied.
        bool ApplyRailwayHit(uint64_t targetId, uint64_t missionGeneration, uint64_t targetSpawnGeneration, float damage, const glm::vec3 &direction);

        // One body's relayed aim: once it stops moving, the aim it settled
        // on goes out once more, reliably, so a lost last update cannot
        // leave observers pointing where it was.
        struct AimRelay {
            static constexpr std::chrono::milliseconds kSettleAfter {150};
            std::chrono::steady_clock::time_point lastRelay;
            MafiaNet::PeerGuid except = MafiaNet::UNASSIGNED_PEER_GUID;
            bool settlePending        = false;
        };

        // Sends `state`'s current aim, unreliably, to the peers that stream
        // `body` (but `except`), and counts it into state.aimSequence so a
        // later State tells receivers which relay it includes. Arms `relay`.
        static void RelayAim(Framework::Networking::Replication::NetworkEntity &body, Shared::Combat::State &state, MafiaNet::PeerGuid except, AimRelay &relay);
        // Per tick: once `relay`'s aim has not moved for kSettleAfter, sends
        // it reliably (unordered: the sequence drops it behind a newer one).
        static void SettleAim(Framework::Networking::Replication::NetworkEntity &body, Shared::Combat::State &state, AimRelay &relay,
                              std::chrono::steady_clock::time_point now);

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
            // The controller's AimUpdate count, apart from its intents'.
            uint16_t lastAimUpdate = 0;
            bool hasAimUpdate      = false;
            AimRelay aimRelay;
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
        void OnAimUpdate(const Shared::Combat::AimUpdate &update, MafiaNet::PeerGuid sender);
        void OnHitReport(const Shared::Combat::HitReport &report, MafiaNet::PeerGuid sender);
        struct HitTarget {
            glm::vec3 position;
            glm::quat rotation;
            float health;
            bool seated;
        };
        struct ValidatedHit {
            uint8_t weaponId;
            // Before the body part scale.
            float damage;
        };
        // A reported bullet or melee impact against the shooter's accepted
        // action; marks it used and returns the weapon and its damage.
        std::optional<ValidatedHit> ValidateBullet(const Shared::Combat::HitReport &report, Session &session, const HitTarget &target);
        std::optional<ValidatedHit> ValidateMelee(const Shared::Combat::HitReport &report, const Shared::Entities::PlayerEntity &attacker, Session &session, const HitTarget &target);
        void OnNpcHitReport(const Shared::Combat::HitReport &report, Shared::Entities::PlayerEntity &shooter, Session &session);
        void ApplyNpcVehicleImpact(const Shared::Combat::VehicleImpact &report, MafiaNet::PeerGuid sender, std::chrono::steady_clock::time_point receivedAt);
        // One contact per car and target each 500 ms (C_Vehicle reports it
        // every physics step while the bodies touch); an impact may follow a
        // bump at once (VehicleContactDue).
        bool AdmitVehicleContact(uint64_t carId, uint64_t targetId, Shared::Combat::VehicleContact kind);
        void PublishDamage(const Shared::Combat::HitReport &report, float damage, uint8_t hitType, std::optional<uint8_t> weaponId);
        void OnFatalFallReport(const Shared::Combat::FatalFallReport &report, MafiaNet::PeerGuid sender);
        void OnDetonationReport(const Shared::Combat::DetonationReport &report, MafiaNet::PeerGuid sender);
        void OnVehicleImpact(const Shared::Combat::VehicleImpact &report, MafiaNet::PeerGuid sender);
        void ApplyVehicleImpact(const Shared::Combat::VehicleImpact &report, MafiaNet::PeerGuid sender,
                                std::chrono::steady_clock::time_point receivedAt);
        void ApplyAreaDamage(uint64_t sourceId, uint64_t missionGeneration, const glm::vec3 &position, float radius, bool explosion, float damage,
                             std::optional<uint8_t> weaponId, uint32_t virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL);
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
            uint32_t virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
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
        NpcTargets _npcTargets;
        std::unordered_map<uint64_t, Session> _sessions;
        std::map<std::pair<uint64_t, uint64_t>, std::chrono::steady_clock::time_point> _vehicleImpacts;
        struct LastVehicleContact {
            std::chrono::steady_clock::time_point at;
            Shared::Combat::VehicleContact kind = Shared::Combat::VehicleContact::Impact;
        };
        std::map<std::pair<uint64_t, uint64_t>, LastVehicleContact> _vehicleContacts;
        // Contacts each driver's client reported in the last second.
        std::unordered_map<MafiaNet::PeerGuid, Shared::Combat::VehicleContactRate> _vehicleContactRates;
        std::function<void(uint64_t, uint64_t, std::optional<uint8_t>, DamageCause, float, float, uint16_t)> _transitionCallback;
        std::function<void(const Shared::Combat::Event &)> _actionCallback;
        std::function<uint64_t(uint64_t, uint8_t, uint16_t, uint16_t, const glm::vec3 &, float)> _dropCallback;
        std::function<void(uint64_t, uint64_t)> _droppedCallback;
        uint64_t DropWeapon(Session &session, Shared::Entities::PlayerEntity &player, uint8_t weaponId);
    };
} // namespace Mafia1Online::Features::Combat
