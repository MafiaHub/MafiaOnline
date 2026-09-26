#pragma once

#include <networking/rpc/rpc.h>

#include <array>
#include <cstdint>

namespace Mafia1Online::Shared::Combat {
    constexpr uint8_t kWeaponSlots = 32;
    constexpr uint8_t kMaximumShotPellets = 10;

    struct PelletTrajectory {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        bool staticHit = false;
        float hitX = 0.0f;
        float hitY = 0.0f;
        float hitZ = 0.0f;
        float normalX = 0.0f;
        float normalY = 0.0f;
        float normalZ = 0.0f;
        uint8_t materialId = 0;
    };

    enum class Action : uint8_t {
        Equip  = 1,
        Fire   = 2,
        Reload = 3,
        Drop   = 4,
        Throw  = 5,
        Aim    = 6,
        Crouch = 7,
        // One melee attack: fists (0), knuckleduster, knife or bat.
        Melee  = 8,
        // Do_Shoot(true) begins the native charge and windup before release.
        MeleeStart = 9,
        MeleeCancel = 10,
        ThrowStart = 11,
        ThrowRelease = 12,
        ThrowCancel = 13,
    };

    // Retail Do_Shoot combo index 0..3 for a quick hit, or this value for
    // the heavy finisher a held button releases.
    constexpr int8_t kHeavyMelee = -1;
    constexpr uint16_t kMaximumMeleeHoldMs = 5000;
    constexpr uint16_t kMeleeNetworkGraceMs = 1000;
    // Do_Shoot releases a grenade only after more than 400 ms and caps its
    // impulse at the charge reached after 2000 ms.
    constexpr uint16_t kMinimumThrowHoldMs = 401;
    constexpr uint16_t kMaximumThrowChargeMs = 2000;

    struct WeaponAmmo {
        uint16_t loaded  = 0;
        uint16_t reserve = 0;
    };

    // A complete, versioned state replaces all earlier states for this life.
    // Reliable delivery also repairs a missed action or a late join.
    struct State {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CombatState");

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration   = 0;
        uint32_t revision          = 0;
        uint32_t inventoryMask     = 0;
        uint8_t selectedWeapon     = 0;
        float health               = 0.0f;
        bool alive                 = false;
        bool spawned               = false;
        uint16_t deathAnimation    = 0;
        bool aiming                = false;
        bool crouching             = false;
        float aimDirectionX        = 0.0f;
        float aimDirectionY        = 0.0f;
        float aimDirectionZ        = 0.0f;
        float poseTargetOffsetX    = 0.0f;
        float poseTargetOffsetY    = 0.0f;
        float poseTargetOffsetZ    = 0.0f;
        std::array<WeaponAmmo, kWeaponSlots> ammo {};

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
            stream->Serialize(write, revision);
            stream->Serialize(write, inventoryMask);
            stream->Serialize(write, selectedWeapon);
            stream->Serialize(write, health);
            stream->Serialize(write, alive);
            stream->Serialize(write, spawned);
            stream->Serialize(write, deathAnimation);
            stream->Serialize(write, aiming);
            stream->Serialize(write, crouching);
            stream->Serialize(write, aimDirectionX);
            stream->Serialize(write, aimDirectionY);
            stream->Serialize(write, aimDirectionZ);
            stream->Serialize(write, poseTargetOffsetX);
            stream->Serialize(write, poseTargetOffsetY);
            stream->Serialize(write, poseTargetOffsetZ);
            for (auto &slot : ammo) {
                stream->Serialize(write, slot.loaded);
                stream->Serialize(write, slot.reserve);
            }
        }
    };

    // Only the controller may submit an action. The server checks the life,
    // inventory, cadence and ammunition before publishing a result.
    struct Intent {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CombatIntent");

        uint64_t networkId             = 0;
        uint64_t missionGeneration     = 0;
        uint64_t spawnGeneration       = 0;
        uint32_t sequence              = 0;
        Action action                  = Action::Equip;
        uint8_t weaponId               = 0;
        bool aiming                    = false;
        bool crouching                 = false;
        uint64_t targetNetworkId       = 0;
        uint64_t targetSpawnGeneration = 0;
        float directionX               = 0.0f;
        float directionY               = 0.0f;
        float directionZ               = 0.0f;
        // Native C_game::NewShoot input and queued pellet trajectories for
        // Fire only. The server bounds these before forwarding a shot.
        float originX                  = 0.0f;
        float originY                  = 0.0f;
        float originZ                  = 0.0f;
        float nativeDirectionX         = 0.0f;
        float nativeDirectionY         = 0.0f;
        float nativeDirectionZ         = 0.0f;
        uint8_t pelletCount            = 0;
        std::array<float, kMaximumShotPellets * 2> scatterRandom {};
        std::array<PelletTrajectory, kMaximumShotPellets> pellets {};
        float poseTargetOffsetX         = 0.0f;
        float poseTargetOffsetY         = 0.0f;
        float poseTargetOffsetZ         = 0.0f;
        int8_t meleeIndex               = 0;
        uint16_t meleeHoldMs             = 0;
        // Throw: native NewGrenade type; origin and native direction carry
        // the spawn position and impulse.
        uint8_t grenadeType             = 0;
        uint16_t throwHoldMs             = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
            stream->Serialize(write, sequence);
            uint8_t kind = static_cast<uint8_t>(action);
            stream->Serialize(write, kind);
            if (!write) {
                action = static_cast<Action>(kind);
            }
            stream->Serialize(write, weaponId);
            stream->Serialize(write, aiming);
            stream->Serialize(write, crouching);
            stream->Serialize(write, targetNetworkId);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
            if (action == Action::Fire) {
                stream->Serialize(write, originX);
                stream->Serialize(write, originY);
                stream->Serialize(write, originZ);
                stream->Serialize(write, nativeDirectionX);
                stream->Serialize(write, nativeDirectionY);
                stream->Serialize(write, nativeDirectionZ);
                stream->Serialize(write, pelletCount);
                for (auto &value : scatterRandom) {
                    stream->Serialize(write, value);
                }
                for (auto &pellet : pellets) {
                    stream->Serialize(write, pellet.x);
                    stream->Serialize(write, pellet.y);
                    stream->Serialize(write, pellet.z);
                    stream->Serialize(write, pellet.staticHit);
                    stream->Serialize(write, pellet.hitX);
                    stream->Serialize(write, pellet.hitY);
                    stream->Serialize(write, pellet.hitZ);
                    stream->Serialize(write, pellet.normalX);
                    stream->Serialize(write, pellet.normalY);
                    stream->Serialize(write, pellet.normalZ);
                    stream->Serialize(write, pellet.materialId);
                }
            }
            stream->Serialize(write, poseTargetOffsetX);
            stream->Serialize(write, poseTargetOffsetY);
            stream->Serialize(write, poseTargetOffsetZ);
            if (action == Action::Melee) {
                stream->Serialize(write, meleeIndex);
                stream->Serialize(write, meleeHoldMs);
            }
            if (action == Action::Throw) {
                stream->Serialize(write, grenadeType);
                stream->Serialize(write, originX);
                stream->Serialize(write, originY);
                stream->Serialize(write, originZ);
                stream->Serialize(write, nativeDirectionX);
                stream->Serialize(write, nativeDirectionY);
                stream->Serialize(write, nativeDirectionZ);
            }
            if (action == Action::ThrowRelease) {
                stream->Serialize(write, throwHoldMs);
            }
        }
    };

    // Discrete actions are ordered with states, and include the server's own
    // revision so clients can drop events from an obsolete life.
    struct Event {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CombatEvent");

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t spawnGeneration   = 0;
        uint32_t revision          = 0;
        Action action              = Action::Equip;
        uint8_t weaponId           = 0;
        uint64_t targetNetworkId   = 0;
        float directionX           = 0.0f;
        float directionY           = 0.0f;
        float directionZ           = 0.0f;
        float originX              = 0.0f;
        float originY              = 0.0f;
        float originZ              = 0.0f;
        float nativeDirectionX     = 0.0f;
        float nativeDirectionY     = 0.0f;
        float nativeDirectionZ     = 0.0f;
        uint8_t pelletCount        = 0;
        std::array<float, kMaximumShotPellets * 2> scatterRandom {};
        std::array<PelletTrajectory, kMaximumShotPellets> pellets {};
        int8_t meleeIndex          = 0;
        uint16_t meleeHoldMs       = 0;
        uint8_t grenadeType        = 0;
        uint16_t throwHoldMs        = 0;
        // The accepted intent's sequence; a Detonation refers to it.
        uint32_t sequence          = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, spawnGeneration);
            stream->Serialize(write, revision);
            uint8_t kind = static_cast<uint8_t>(action);
            stream->Serialize(write, kind);
            if (!write) {
                action = static_cast<Action>(kind);
            }
            stream->Serialize(write, weaponId);
            stream->Serialize(write, targetNetworkId);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
            if (action == Action::Fire) {
                stream->Serialize(write, originX);
                stream->Serialize(write, originY);
                stream->Serialize(write, originZ);
                stream->Serialize(write, nativeDirectionX);
                stream->Serialize(write, nativeDirectionY);
                stream->Serialize(write, nativeDirectionZ);
                stream->Serialize(write, pelletCount);
                for (auto &value : scatterRandom) {
                    stream->Serialize(write, value);
                }
                for (auto &pellet : pellets) {
                    stream->Serialize(write, pellet.x);
                    stream->Serialize(write, pellet.y);
                    stream->Serialize(write, pellet.z);
                    stream->Serialize(write, pellet.staticHit);
                    stream->Serialize(write, pellet.hitX);
                    stream->Serialize(write, pellet.hitY);
                    stream->Serialize(write, pellet.hitZ);
                    stream->Serialize(write, pellet.normalX);
                    stream->Serialize(write, pellet.normalY);
                    stream->Serialize(write, pellet.normalZ);
                    stream->Serialize(write, pellet.materialId);
                }
            }
            if (action == Action::Melee) {
                stream->Serialize(write, meleeIndex);
                stream->Serialize(write, meleeHoldMs);
            }
            if (action == Action::Throw) {
                stream->Serialize(write, sequence);
                stream->Serialize(write, grenadeType);
                stream->Serialize(write, originX);
                stream->Serialize(write, originY);
                stream->Serialize(write, originZ);
                stream->Serialize(write, nativeDirectionX);
                stream->Serialize(write, nativeDirectionY);
                stream->Serialize(write, nativeDirectionZ);
            }
            if (action == Action::ThrowRelease) {
                stream->Serialize(write, throwHoldMs);
            }
        }
    };
} // namespace Mafia1Online::Shared::Combat
