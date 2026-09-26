#pragma once

#include "shared/features/combat/combat_protocol.h"
#include "shared/features/combat/detonation.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <utility>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::SDK::Player {
    struct NativeActor;
    struct NativeHuman;
    struct Vector3;
}

namespace Mafia1Online::SDK::Combat {
    struct NativeGrenade;
}

namespace Mafia1Online::Features::Combat {
    struct NativeShotData;
    class CombatService final {
      public:
        void RegisterRPC();
        void Update(World::WorldService &world);
        void Reset();
        void OnNativeShot(SDK::Player::NativeHuman &human, const NativeShotData &shot);
        void OnNativeMeleeStart(SDK::Player::NativeHuman &human);
        void OnNativeMeleeCancel(SDK::Player::NativeHuman &human);
        void OnNativeMelee(SDK::Player::NativeHuman &human, int8_t index, uint16_t holdMs);
        void OnNativeGrenadeStart(SDK::Player::NativeHuman &human);
        void OnNativeGrenadeRelease(SDK::Player::NativeHuman &human, uint16_t chargeMs, const SDK::Player::Vector3 &direction);
        void OnNativeGrenadeCancel(SDK::Player::NativeHuman &human);
        void OnNativeDrop(SDK::Player::NativeHuman &human, uint8_t weaponId);
        [[nodiscard]] bool IsRemoteNetworkHuman(SDK::Player::NativeActor &actor) const;
        void OnRemoteGrenadeNotify(SDK::Player::NativeActor &actor);
        [[nodiscard]] bool HasLocalThrowRelease() const;
        void OnNativeThrow(SDK::Combat::NativeGrenade *grenade, int32_t type, const SDK::Player::Vector3 &position, const SDK::Player::Vector3 &impulse);
        void OnNativeDetonation(SDK::Combat::NativeGrenade *grenade, const SDK::Player::Vector3 &position);
        void OnGrenadeDestroyed(SDK::Combat::NativeGrenade *grenade);
        // The local melee attack a native swing hit belongs to, while it can
        // still land (the server accepts one hit within 1.8 s).
        [[nodiscard]] std::optional<uint32_t> OpenLocalMelee() const;
        struct LocalPelletMatch { uint32_t shotSequence; uint8_t pelletIndex; };
        std::optional<LocalPelletMatch> MatchLocalPellet(float directionX, float directionY, float directionZ, float hitX, float hitY, float hitZ);
        [[nodiscard]] uint32_t LastLocalFireSequence() const {
            return _lastLocalFireSequence;
        }

        const Shared::Combat::State *GetState(uint64_t networkId) const;
        const Shared::Combat::State *GetLocalState(const World::WorldService &world) const;
        bool Submit(uint64_t networkId, Shared::Combat::Action action, uint8_t weaponId, uint64_t targetNetworkId, uint64_t targetSpawnGeneration, float directionX, float directionY, float directionZ, bool aiming = false,
                    bool crouching = false, float poseTargetOffsetX = 0.0f, float poseTargetOffsetY = 0.0f, float poseTargetOffsetZ = 0.0f, const NativeShotData *shot = nullptr);
        bool PopEvent(Shared::Combat::Event &event);

      private:
        void OnState(const Shared::Combat::State &state);
        void OnEvent(const Shared::Combat::Event &event);
        void OnDetonation(const Shared::Combat::Detonation &detonation);
        void ApplyNativeStates(World::WorldService &world);
        void ReplayEvents(World::WorldService &world);
        void UpdateRemoteMelee(World::WorldService &world);
        void UpdateRemoteGrenades(World::WorldService &world);
        void UpdateLocalAim(World::WorldService &world);
        void UpdateLocalCrouch(World::WorldService &world);
        void UpdateLocalActions(World::WorldService &world);
        bool SubmitAim(uint64_t networkId, uint8_t weaponId, bool aiming, float directionX, float directionY, float directionZ, float poseTargetOffsetX, float poseTargetOffsetY, float poseTargetOffsetZ);

        struct AppliedNative {
            uint64_t nativeGeneration  = 0;
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration   = 0;
            uint32_t revision          = 0;
            uint32_t inventoryMask     = 0;
            uint8_t selectedWeapon     = 0;
            std::array<Shared::Combat::WeaponAmmo, Shared::Combat::kWeaponSlots> ammo {};
            bool poseInitialized       = false;
            float poseTargetOffsetX    = 0.0f;
            float poseTargetOffsetY    = 0.0f;
            float poseTargetOffsetZ    = 0.0f;
            std::chrono::steady_clock::time_point poseUpdatedAt {};
        };

        struct EventKey {
            uint64_t networkId         = 0;
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration   = 0;
            uint32_t revision          = 0;

            bool operator==(const EventKey &) const = default;
        };

        struct EventKeyHash {
            size_t operator()(const EventKey &key) const;
        };

        struct PendingEvent {
            Shared::Combat::Event event;
            std::chrono::steady_clock::time_point received;
        };

        std::unordered_map<uint64_t, Shared::Combat::State> _states;
        std::unordered_map<uint64_t, AppliedNative> _applied;
        std::deque<PendingEvent> _events;
        std::unordered_set<EventKey, EventKeyHash> _seenEvents;
        std::deque<EventKey> _seenOrder;
        struct LocalAim {
            uint64_t networkId         = 0;
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration   = 0;
            uint64_t nativeGeneration  = 0;
            bool sent                  = false;
            bool aiming                = false;
            float directionX           = 0.0f;
            float directionY           = 0.0f;
            float directionZ           = 0.0f;
            float poseTargetOffsetX     = 0.0f;
            float poseTargetOffsetY     = 0.0f;
            float poseTargetOffsetZ     = 0.0f;
            std::chrono::steady_clock::time_point lastSent {};
        } _localAim;
        struct LocalCrouch {
            uint64_t networkId         = 0;
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration   = 0;
            uint64_t nativeGeneration  = 0;
            bool sent                  = false;
            bool crouching             = false;
        } _localCrouch;
        struct LocalAction {
            uint64_t networkId         = 0;
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration   = 0;
            uint64_t nativeGeneration  = 0;
            uint32_t baseRevision      = 0;
            Shared::Combat::Action action = Shared::Combat::Action::Equip;
            uint8_t weaponId           = 0;
            uint16_t loaded            = 0;
            uint16_t reserve           = 0;
            std::chrono::steady_clock::time_point sentAt {};
        } _localAction;
        World::WorldService *_world     = nullptr;
        uint32_t _sequence              = 0;
        uint32_t _lastLocalFireSequence = 0;
        uint32_t _lastLocalMeleeSequence = 0;
        struct LocalThrow {
            uint64_t networkId = 0;
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration = 0;
            uint32_t sequence = 0;
        };
        // Native grenade pointers stay valid until their destructor hook
        // removes them from these maps.
        std::unordered_map<SDK::Combat::NativeGrenade *, LocalThrow> _localGrenades;
        std::map<std::pair<uint64_t, uint32_t>, SDK::Combat::NativeGrenade *> _remoteGrenades;
        struct RemoteMelee {
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration = 0;
            uint64_t nativeGeneration = 0;
            uint8_t weaponId = 0;
            std::chrono::steady_clock::time_point startedAt {};
        };
        std::unordered_map<uint64_t, RemoteMelee> _remoteMelee;
        struct GrenadeCharge {
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration = 0;
            uint64_t nativeGeneration = 0;
            uint8_t weaponId = 0;
            std::chrono::steady_clock::time_point startedAt {};
        };
        std::unordered_map<uint64_t, GrenadeCharge> _remoteGrenadeCharges;
        std::unordered_map<uint64_t, GrenadeCharge> _remoteGrenadeReleases;
        GrenadeCharge _localGrenadeCharge;
        uint64_t _localGrenadeNetworkId = 0;
        std::chrono::steady_clock::time_point _localGrenadeReleasedAt {};
        uint16_t _submitThrowHoldMs = 0;
        struct SubmitThrow {
            uint8_t type = 0;
            float originX = 0.0f, originY = 0.0f, originZ = 0.0f;
            float impulseX = 0.0f, impulseY = 0.0f, impulseZ = 0.0f;
        };
        const SubmitThrow *_submitThrow = nullptr;
        std::chrono::steady_clock::time_point _lastLocalMeleeAt;
        int8_t _submitMeleeIndex = 0;
        uint16_t _submitMeleeHoldMs = 0;
        struct LocalFire {
            uint32_t sequence = 0;
            uint64_t missionGeneration = 0;
            uint64_t spawnGeneration = 0;
            float originX = 0.0f;
            float originY = 0.0f;
            float originZ = 0.0f;
            uint8_t pelletCount = 0;
            uint16_t reportedPellets = 0;
            std::array<Shared::Combat::PelletTrajectory, Shared::Combat::kMaximumShotPellets> pellets {};
            std::chrono::steady_clock::time_point sentAt {};
        };
        std::deque<LocalFire> _localFires;
    };
} // namespace Mafia1Online::Features::Combat
