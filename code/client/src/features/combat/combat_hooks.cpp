#include <utils/safe_win32.h>

#include "combat_hooks.h"

#include "combat_service.h"

#include <MinHook.h>
#include <mafia1/sdk/combat/native_grenade.h>
#include <mafia1/sdk/core/game.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>

#include <algorithm>
#include <initializer_list>
#include <cmath>

namespace Mafia1Online::Features::Combat {
    namespace {
        using NativeGame  = SDK::Core::Game::NativeGame;
        using NativeActor = SDK::Player::NativeActor;
        using NativeHuman = SDK::Player::NativeHuman;
        using Vector3     = SDK::Player::Vector3;
        using NativeFrame = SDK::Scene::NativeFrame;

        using ShootingCall = void(__thiscall *)(NativeHuman *);
        using NewShootCall = bool(__thiscall *)(NativeGame *, NativeActor *, Vector3, Vector3, float, int, NativeFrame *, int);
        using RandomCall = float(__cdecl *)();
        using DoShootCall = bool(__thiscall *)(NativeHuman *, bool, const Vector3 *);
        using SpecialStrokeCall = NativeHuman *(__fastcall *)(NativeHuman *, bool);
        using NativeGrenade = SDK::Combat::NativeGrenade;
        using NewGrenadeCall = NativeGrenade *(__thiscall *)(NativeGame *, int32_t, Vector3 *, Vector3 *, bool, void *);
        using GrenadeAICall = void(__thiscall *)(NativeGrenade *, uint32_t);
        using NewExplosionCall = void(__thiscall *)(NativeGame *, void *, Vector3 *, float, float, bool, bool, bool, int32_t);
        using NewFireCall = void(__thiscall *)(NativeGame *, Vector3 *, void *, int32_t, float, float, bool);
        using GrenadeDestructorCall = void *(__thiscall *)(NativeGrenade *, uint32_t);
        using HumanNotifyCall = uint32_t(__stdcall *)(NativeFrame *, uint32_t, uint32_t, uint32_t);
        constexpr uint32_t kThrowGrenadeNote = 41;

        ShootingCall gShootingOriginal     = nullptr;
        NewShootCall gNewShootOriginal     = nullptr;
        RandomCall gRandomOriginal         = nullptr;
        DoShootCall gDoShootOriginal       = nullptr;
        SpecialStrokeCall gSpecialStrokeOriginal = nullptr;
        NewGrenadeCall gNewGrenadeOriginal = nullptr;
        GrenadeAICall gGrenadeAIOriginal = nullptr;
        NewExplosionCall gNewExplosionOriginal = nullptr;
        NewFireCall gNewFireOriginal = nullptr;
        GrenadeDestructorCall gGrenadeDestructorOriginal = nullptr;
        HumanNotifyCall gHumanNotifyOriginal = nullptr;
        thread_local NativeGrenade *gTickingGrenade = nullptr;
        thread_local NativeActor *gGrenadeNotifyActor = nullptr;
        CombatService *gService            = nullptr;
        thread_local unsigned gReplayDepth = 0;
        thread_local NativeShotData gLocalShot;
        thread_local NativeHuman *gCapturingHuman = nullptr;
        thread_local const Shared::Combat::Event *gReplayEvent = nullptr;
        thread_local NativeActor *gReplayActor = nullptr;
        thread_local unsigned gScatterCaptureRemaining = 0;
        thread_local unsigned gScatterReplayRemaining = 0;
        thread_local unsigned gScatterIndex = 0;
        thread_local bool gReplayProjectileQueued = false;

        float __cdecl RandomHook() {
            const float actual = gRandomOriginal();
            if (gScatterCaptureRemaining != 0) {
                gLocalShot.scatterRandom[gScatterIndex++] = actual;
                --gScatterCaptureRemaining;
            }
            if (gScatterReplayRemaining != 0 && gReplayEvent) {
                --gScatterReplayRemaining;
                return gReplayEvent->scatterRandom[gScatterIndex++];
            }
            return actual;
        }

        // A melee release only attacks when Do_Shoot changes the combo, the
        // attack animation, the work state or loads an overlay; a release
        // often does nothing, and notes toggle the armed flag on their own.
        bool __fastcall DoShootHook(NativeHuman *human, void *, bool pressed, const Vector3 *target) {
            const bool captureMelee = gReplayDepth == 0 && gService && human->Actor().IsPlayer() && human->HoldsMeleeItem();
            const bool captureThrow = gReplayDepth == 0 && gService && human->Actor().IsPlayer() && human->HoldsThrowable();
            if (captureThrow) {
                const bool chargingBefore = human->GrenadeCharging();
                const auto *mission = SDK::Core::Mission::Get();
                const uint32_t gameTime = mission && mission->Game() ? mission->Game()->GameTime() : 0;
                const uint16_t chargeMs = !pressed && chargingBefore
                    ? static_cast<uint16_t>(std::min<uint32_t>(gameTime - human->GrenadeChargeStartTime(), Shared::Combat::kMaximumThrowChargeMs)) : 0;
                const bool result = gDoShootOriginal(human, pressed, target);
                if (pressed && !chargingBefore && human->GrenadeCharging()) {
                    gService->OnNativeGrenadeStart(*human);
                }
                else if (!pressed && chargingBefore && !human->GrenadeCharging()) {
                    if (human->AnimationState() == SDK::Player::kHumanThrowGrenadeAnimation) {
                        const auto impulse = human->ShootTarget();
                        const float length = std::sqrt(impulse.x * impulse.x + impulse.y * impulse.y + impulse.z * impulse.z);
                        if (length > 0.0001f) {
                            gService->OnNativeGrenadeRelease(*human, chargeMs, {impulse.x / length, impulse.y / length, impulse.z / length});
                        }
                    }
                    else {
                        gService->OnNativeGrenadeCancel(*human);
                    }
                }
                return result;
            }
            if (!captureMelee) {
                return gDoShootOriginal(human, pressed, target);
            }
            const bool armedBefore = human->MeleeArmed();
            const auto before = human->Melee();
            const auto *mission = SDK::Core::Mission::Get();
            const uint32_t gameTime = mission && mission->Game() ? mission->Game()->GameTime() : 0;
            const uint16_t holdMs = !pressed && armedBefore ? static_cast<uint16_t>(std::min<uint32_t>(gameTime - human->MeleeAttackTime(), Shared::Combat::kMaximumMeleeHoldMs)) : 0;
            const bool result = gDoShootOriginal(human, pressed, target);
            const auto after  = human->Melee();
            if (pressed && !armedBefore && human->MeleeArmed()) {
                gService->OnNativeMeleeStart(*human);
            }
            if (!pressed && armedBefore && (after.animationState != before.animationState || after.canWork != before.canWork || after.weaponChangeTime != before.weaponChangeTime || after.actionTimeRemaining != before.actionTimeRemaining)) {
                const bool heavy = after.index == Shared::Combat::kHeavyMelee;
                gService->OnNativeMelee(*human, heavy ? Shared::Combat::kHeavyMelee : static_cast<int8_t>(std::clamp(before.index, 0, 3)), holdMs);
            }
            else if (!pressed && armedBefore) {
                gService->OnNativeMeleeCancel(*human);
            }
            return result;
        }

        // Do_HardHead and Do_Castrate subtract health directly, bypassing
        // C_human::Hit and server authority. Returning no target makes a
        // heavy fist attack fall through to the plain finisher, the same
        // animation on every client.
        NativeHuman *__fastcall SpecialStrokeHook(NativeHuman *, bool) {
            return nullptr;
        }

        uint32_t __stdcall HumanNotifyHook(NativeFrame *frame, uint32_t message, uint32_t note, uint32_t argument) {
            NativeActor *previous = gGrenadeNotifyActor;
            gGrenadeNotifyActor = nullptr;
            if (note == kThrowGrenadeNote && frame && frame->Parent()) {
                auto *actor = frame->Parent()->ActorOwner();
                gGrenadeNotifyActor = actor && (actor->GetType() == NativeActor::Type::Player || actor->GetType() == NativeActor::Type::Entity) ? actor : nullptr;
            }
            const auto result = gHumanNotifyOriginal(frame, message, note, argument);
            gGrenadeNotifyActor = previous;
            return result;
        }

        // The native notify is owned by a known human. The accepted Throw
        // event creates the remote display projectile with the thrower's
        // exact spawn position and impulse, so suppress the notify's duplicate.
        NativeGrenade *__fastcall NewGrenadeHook(NativeGame *game, void *, int32_t type, Vector3 *position, Vector3 *impulse, bool activate, void *frame) {
            if (gReplayDepth == 0 && gService && gGrenadeNotifyActor && gService->IsRemoteNetworkHuman(*gGrenadeNotifyActor)) {
                gService->OnRemoteGrenadeNotify(*gGrenadeNotifyActor);
                if (frame) {
                    static_cast<NativeFrame *>(frame)->Release();
                }
                return nullptr;
            }
            const bool localThrow = gReplayDepth == 0 && gService && gGrenadeNotifyActor == SDK::Player::CurrentPlayer()
                && activate && position && impulse && gService->HasLocalThrowRelease();
            auto *grenade = gNewGrenadeOriginal(game, type, position, impulse, activate, frame);
            if (localThrow && grenade) {
                gService->OnNativeThrow(grenade, type, *position, *impulse);
            }
            return grenade;
        }

        void __fastcall GrenadeAIHook(NativeGrenade *grenade, void *, uint32_t frameTime) {
            NativeGrenade *previous = gTickingGrenade;
            gTickingGrenade = grenade;
            gGrenadeAIOriginal(grenade, frameTime);
            gTickingGrenade = previous;
        }

        void __fastcall NewExplosionHook(NativeGame *game, void *, void *source, Vector3 *position, float radius, float damage, bool affectWorld, bool large, bool smoke, int32_t sound) {
            if (gTickingGrenade && source == gTickingGrenade && position && gService) {
                gService->OnNativeDetonation(gTickingGrenade, *position);
            }
            gNewExplosionOriginal(game, source, position, radius, damage, affectWorld, large, smoke, sound);
        }

        void __fastcall NewFireHook(NativeGame *game, void *, Vector3 *position, void *parent, int32_t life, float radius, float damage, bool damageActors) {
            // C_grenade::AI converts the fire position into its hit actor's
            // frame; report the grenade's own world position instead.
            if (gTickingGrenade && gService && gTickingGrenade->Actor().Frame()) {
                gService->OnNativeDetonation(gTickingGrenade, gTickingGrenade->Actor().Frame()->ValidWorldPosition());
            }
            gNewFireOriginal(game, position, parent, life, radius, damage, damageActors);
        }

        void *__fastcall GrenadeDestructorHook(NativeGrenade *grenade, void *, uint32_t flags) {
            if (gService) {
                gService->OnGrenadeDestroyed(grenade);
            }
            return gGrenadeDestructorOriginal(grenade, flags);
        }

        void __fastcall ShootingHook(NativeHuman *human, void *) {
            const float before = human->ShotCount();
            const bool captureLocal = gReplayDepth == 0 && gService && human->Actor().IsPlayer();
            NativeHuman *previousHuman = gCapturingHuman;
            NativeShotData previousShot = gLocalShot;
            if (captureLocal) {
                gCapturingHuman = human;
                gLocalShot = {};
            }
            gShootingOriginal(human);
            if (captureLocal && gLocalShot.captured && human->ShotCount() > before) {
                gService->OnNativeShot(*human, gLocalShot);
            }
            if (captureLocal) {
                gCapturingHuman = previousHuman;
                gLocalShot = previousShot;
            }
        }

        bool __fastcall NewShootHook(NativeGame *game, void *, NativeActor *actor, Vector3 position, Vector3 direction, float damage, int particleEffectId, NativeFrame *frame, int shotCount) {
            if (gReplayDepth != 0 && gReplayEvent && actor == gReplayActor) {
                const auto &event = *gReplayEvent;
                const Vector3 source {event.originX, event.originY, event.originZ};
                const Vector3 trajectory {event.nativeDirectionX, event.nativeDirectionY, event.nativeDirectionZ};
                const auto before = game->ShootRecords().size();
                gScatterIndex = 0;
                gScatterReplayRemaining = event.pelletCount > 1 ? event.pelletCount * 2 : 0;
                const bool queued = gNewShootOriginal(game, actor, source, trajectory, 0.0f, particleEffectId, frame, event.pelletCount);
                const auto records = game->ShootRecords();
                gReplayProjectileQueued = queued && gScatterReplayRemaining == 0 && records.size() == before + event.pelletCount;
                gScatterReplayRemaining = 0;
                if (gReplayProjectileQueued) {
                    for (size_t index = 0; index < event.pelletCount; ++index) {
                        const auto &record = records[before + index];
                        const auto &pellet = event.pellets[index];
                        const float difference = std::abs(record.direction.x - pellet.x) + std::abs(record.direction.y - pellet.y) + std::abs(record.direction.z - pellet.z);
                        const float impactDifference = record.hit ? std::abs(record.hitPosition.x - pellet.hitX) + std::abs(record.hitPosition.y - pellet.hitY) + std::abs(record.hitPosition.z - pellet.hitZ) : 0.0f;
                        const float normalDifference = record.hit ? std::abs(record.hitNormal.x - pellet.normalX) + std::abs(record.hitNormal.y - pellet.normalY) + std::abs(record.hitNormal.z - pellet.normalZ) : 0.0f;
                        const float originDifference = std::abs(record.startPosition.x - event.originX) + std::abs(record.startPosition.y - event.originY) + std::abs(record.startPosition.z - event.originZ);
                        if (difference > 0.01f || originDifference > 0.01f || record.hit != pellet.staticHit || (record.hit && (impactDifference > 0.01f || normalDifference > 0.05f || record.materialId != pellet.materialId))) {
                            gReplayProjectileQueued = false;
                            break;
                        }
                    }
                }
                if (!gReplayProjectileQueued) {
                    game->DiscardShootRecordsFrom(before);
                }
                return queued;
            }
            const bool capture = gReplayDepth == 0 && gCapturingHuman && actor == &gCapturingHuman->Actor() && shotCount >= 1 && shotCount <= Shared::Combat::kMaximumShotPellets;
            const auto before = capture ? game->ShootRecords().size() : 0;
            if (capture) {
                gLocalShot = {};
                gLocalShot.originX = position.x;
                gLocalShot.originY = position.y;
                gLocalShot.originZ = position.z;
                gLocalShot.directionX = direction.x;
                gLocalShot.directionY = direction.y;
                gLocalShot.directionZ = direction.z;
                gLocalShot.pelletCount = static_cast<uint8_t>(shotCount);
                gScatterIndex = 0;
                gScatterCaptureRemaining = shotCount > 1 ? shotCount * 2 : 0;
            }
            const bool fired = gNewShootOriginal(game, actor, position, direction, damage, particleEffectId, frame, shotCount);
            if (capture) {
                const auto records = game->ShootRecords();
                gLocalShot.captured = fired && gScatterCaptureRemaining == 0 && records.size() == before + static_cast<size_t>(shotCount);
                gScatterCaptureRemaining = 0;
                if (gLocalShot.captured) {
                    for (size_t index = 0; index < static_cast<size_t>(shotCount); ++index) {
                        const auto &record = records[before + index];
                        auto &pellet = gLocalShot.pellets[index];
                        pellet.x = record.direction.x;
                        pellet.y = record.direction.y;
                        pellet.z = record.direction.z;
                        pellet.staticHit = record.hit;
                        if (record.hit) {
                            pellet.hitX = record.hitPosition.x;
                            pellet.hitY = record.hitPosition.y;
                            pellet.hitZ = record.hitPosition.z;
                            pellet.normalX = record.hitNormal.x;
                            pellet.normalY = record.hitNormal.y;
                            pellet.normalZ = record.hitNormal.z;
                            pellet.materialId = record.materialId;
                        }
                    }
                }
            }
            return fired;
        }
    } // namespace

    bool InstallCombatHooks(CombatService &service) {
        gService = &service;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanShooting), reinterpret_cast<void *>(&ShootingHook), reinterpret_cast<void **>(&gShootingOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Core::Game::kNewShoot), reinterpret_cast<void *>(&NewShootHook), reinterpret_cast<void **>(&gNewShootOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Core::Game::kRandomFloat), reinterpret_cast<void *>(&RandomHook), reinterpret_cast<void **>(&gRandomOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanDoShoot), reinterpret_cast<void *>(&DoShootHook), reinterpret_cast<void **>(&gDoShootOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanCanSpecialStroke), reinterpret_cast<void *>(&SpecialStrokeHook), reinterpret_cast<void **>(&gSpecialStrokeOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanNotify), reinterpret_cast<void *>(&HumanNotifyHook), reinterpret_cast<void **>(&gHumanNotifyOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Combat::kNewGrenade), reinterpret_cast<void *>(&NewGrenadeHook), reinterpret_cast<void **>(&gNewGrenadeOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Combat::kGrenadeAI), reinterpret_cast<void *>(&GrenadeAIHook), reinterpret_cast<void **>(&gGrenadeAIOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Combat::kNewExplosion), reinterpret_cast<void *>(&NewExplosionHook), reinterpret_cast<void **>(&gNewExplosionOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Combat::kNewFire), reinterpret_cast<void *>(&NewFireHook), reinterpret_cast<void **>(&gNewFireOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Combat::kGrenadeDestructor), reinterpret_cast<void *>(&GrenadeDestructorHook), reinterpret_cast<void **>(&gGrenadeDestructorOriginal)) != MH_OK) {
            UninstallCombatHooks();
            return false;
        }
        for (uintptr_t address : {SDK::Player::kHumanShooting, SDK::Core::Game::kNewShoot, SDK::Core::Game::kRandomFloat, SDK::Player::kHumanDoShoot, SDK::Player::kHumanCanSpecialStroke, SDK::Player::kHumanNotify,
                                  SDK::Combat::kNewGrenade, SDK::Combat::kGrenadeAI, SDK::Combat::kNewExplosion, SDK::Combat::kNewFire, SDK::Combat::kGrenadeDestructor}) {
            if (MH_EnableHook(reinterpret_cast<void *>(address)) != MH_OK) {
                UninstallCombatHooks();
                return false;
            }
        }
        return true;
    }

    void UninstallCombatHooks() {
        gService = nullptr;
        for (uintptr_t address : {SDK::Player::kHumanShooting, SDK::Core::Game::kNewShoot, SDK::Core::Game::kRandomFloat, SDK::Player::kHumanDoShoot, SDK::Player::kHumanCanSpecialStroke, SDK::Player::kHumanNotify,
                                  SDK::Combat::kNewGrenade, SDK::Combat::kGrenadeAI, SDK::Combat::kNewExplosion, SDK::Combat::kNewFire, SDK::Combat::kGrenadeDestructor}) {
            MH_DisableHook(reinterpret_cast<void *>(address));
            MH_RemoveHook(reinterpret_cast<void *>(address));
        }
        gShootingOriginal = nullptr;
        gNewShootOriginal = nullptr;
        gRandomOriginal = nullptr;
        gDoShootOriginal = nullptr;
        gSpecialStrokeOriginal = nullptr;
        gHumanNotifyOriginal = nullptr;
        gNewGrenadeOriginal = nullptr;
        gGrenadeAIOriginal = nullptr;
        gNewExplosionOriginal = nullptr;
        gNewFireOriginal = nullptr;
        gGrenadeDestructorOriginal = nullptr;
    }

    SDK::Combat::NativeGrenade *ReplayNativeThrow(int32_t type, const SDK::Player::Vector3 &origin, const SDK::Player::Vector3 &impulse) {
        auto *mission = SDK::Core::Mission::Get();
        if (!mission || !mission->Game()) {
            return nullptr;
        }
        Vector3 position = origin;
        Vector3 push     = impulse;
        ++gReplayDepth;
        auto *grenade = gNewGrenadeOriginal(mission->Game(), type, &position, &push, true, nullptr);
        --gReplayDepth;
        if (grenade) {
            grenade->Disarm();
            grenade->IgnoreDynamicCollision();
        }
        return grenade;
    }

    void ReplayDetonation(int32_t type, const SDK::Player::Vector3 &position) {
        auto *mission = SDK::Core::Mission::Get();
        if (!mission || !mission->Game()) {
            return;
        }
        ++gReplayDepth;
        if (type == SDK::Combat::kGrenadeType) {
            SDK::Combat::NewExplosion(mission->Game(), position);
        }
        else {
            SDK::Combat::NewMolotovFire(mission->Game(), position);
        }
        --gReplayDepth;
    }

    bool ReplayNativeMeleeStart(NativeHuman &human) {
        ++gReplayDepth;
        gDoShootOriginal(&human, true, nullptr);
        --gReplayDepth;
        return human.MeleeArmed();
    }

    void ReplayNativeMeleeHold(NativeHuman &human) {
        ++gReplayDepth;
        gDoShootOriginal(&human, true, nullptr);
        --gReplayDepth;
    }

    void ReplayNativeMeleeCancel(NativeHuman &human) {
        human.CancelMeleeRelease();
    }

    bool ReplayNativeMelee(NativeHuman &human, int8_t index, uint16_t holdMs, uint32_t gameTime) {
        const auto before = human.Melee();
        ++gReplayDepth;
        if (!human.MeleeArmed()) {
            gDoShootOriginal(&human, true, nullptr);
        }
        if (!human.MeleeArmed()) {
            --gReplayDepth;
            return false;
        }
        human.PrepareMeleeRelease(index, gameTime, holdMs);
        gDoShootOriginal(&human, false, nullptr);
        --gReplayDepth;
        const auto after = human.Melee();
        return after.animationState != before.animationState || after.canWork != before.canWork || after.weaponChangeTime != before.weaponChangeTime || after.actionTimeRemaining != before.actionTimeRemaining;
    }

    bool ReplayNativeGrenadeStart(NativeHuman &human) {
        ++gReplayDepth;
        gDoShootOriginal(&human, true, nullptr);
        --gReplayDepth;
        return human.GrenadeCharging();
    }

    void ReplayNativeGrenadeHold(NativeHuman &human) {
        ++gReplayDepth;
        gDoShootOriginal(&human, true, nullptr);
        --gReplayDepth;
    }

    bool ReplayNativeGrenadeRelease(NativeHuman &human, uint16_t chargeMs, uint32_t gameTime, const Vector3 &direction) {
        if (!human.GrenadeCharging()) {
            return false;
        }
        human.PrepareGrenadeRelease(gameTime, chargeMs, direction);
        ++gReplayDepth;
        gDoShootOriginal(&human, false, nullptr);
        --gReplayDepth;
        return !human.GrenadeCharging() && human.AnimationState() == SDK::Player::kHumanThrowGrenadeAnimation;
    }

    void ReplayNativeGrenadeCancel(NativeHuman &human, uint32_t gameTime) {
        if (!human.GrenadeCharging()) {
            return;
        }
        human.PrepareGrenadeRelease(gameTime, 0, human.ShootTarget());
        ++gReplayDepth;
        gDoShootOriginal(&human, false, nullptr);
        --gReplayDepth;
    }

    NativeShotReplayResult ReplayNativeShot(NativeHuman &human, const Shared::Combat::Event &event) {
        const float before = human.ShotCount();
        const auto cursor = human.PrepareNetworkShot();
        gReplayEvent = &event;
        gReplayActor = &human.Actor();
        gReplayProjectileQueued = false;
        ++gReplayDepth;
        human.Shoot();
        --gReplayDepth;
        gReplayEvent = nullptr;
        gReplayActor = nullptr;
        if (human.ShotCount() <= before) {
            human.RestoreShotEventCursor(cursor);
            return NativeShotReplayResult::Deferred;
        }
        return gReplayProjectileQueued ? NativeShotReplayResult::Played : NativeShotReplayResult::Rejected;
    }

} // namespace Mafia1Online::Features::Combat
