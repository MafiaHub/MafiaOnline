#pragma once

#include "shared/features/combat/combat_protocol.h"

#include <array>
#include <cstdint>

namespace Mafia1Online::SDK::Player {
    struct NativeHuman;
    struct Vector3;
}
namespace Mafia1Online::SDK::Combat {
    struct NativeGrenade;
}

namespace Mafia1Online::Features::Combat {
    class CombatService;

    struct NativeShotData {
        float originX = 0.0f;
        float originY = 0.0f;
        float originZ = 0.0f;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;
        uint8_t pelletCount = 0;
        std::array<float, Shared::Combat::kMaximumShotPellets * 2> scatterRandom {};
        std::array<Shared::Combat::PelletTrajectory, Shared::Combat::kMaximumShotPellets> pellets {};
        bool captured = false;
    };

    bool InstallCombatHooks(CombatService &service);
    void UninstallCombatHooks();
    enum class NativeShotReplayResult { Deferred, Played, Rejected };
    NativeShotReplayResult ReplayNativeShot(SDK::Player::NativeHuman &human, const Shared::Combat::Event &event);
    // Plays one server-accepted melee attack on a remote human through the
    // retail Do_Shoot release branch. False if the native state refused it.
    bool ReplayNativeMeleeStart(SDK::Player::NativeHuman &human);
    void ReplayNativeMeleeHold(SDK::Player::NativeHuman &human);
    void ReplayNativeMeleeCancel(SDK::Player::NativeHuman &human);
    bool ReplayNativeMelee(SDK::Player::NativeHuman &human, int8_t index, uint16_t holdMs, uint32_t gameTime);
    bool ReplayNativeGrenadeStart(SDK::Player::NativeHuman &human);
    void ReplayNativeGrenadeHold(SDK::Player::NativeHuman &human);
    bool ReplayNativeGrenadeRelease(SDK::Player::NativeHuman &human, uint16_t chargeMs, uint32_t gameTime, const SDK::Player::Vector3 &direction);
    void ReplayNativeGrenadeCancel(SDK::Player::NativeHuman &human, uint32_t gameTime);
    // A remote throw's display copy: the same native grenade, disarmed.
    SDK::Combat::NativeGrenade *ReplayNativeThrow(int32_t type, const SDK::Player::Vector3 &origin, const SDK::Player::Vector3 &impulse);
    // The server's detonation as a native explosion or Molotov fire. Hits on
    // protected humans and on cars this client does not simulate stay
    // suppressed by their hooks.
    void ReplayDetonation(int32_t type, const SDK::Player::Vector3 &position);
} // namespace Mafia1Online::Features::Combat
