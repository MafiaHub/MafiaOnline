#include <utils/safe_win32.h>

#include "death_service.h"

#include "core/application.h"
#include "features/script/visual_scripting.h"
#include "shared/features/combat/hit_report.h"

#include <MinHook.h>
#include <core_modules.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_death.h>
#include <mafia1/sdk/seat/native_seat.h>

#include <algorithm>
#include <array>

namespace Mafia1Online::Features::Death {
    namespace {
        using SDK::Player::NativeActor;
        using SDK::Player::NativeHitType;
        using SDK::Player::NativeHuman;
        using SDK::Player::Vector3;
        using NativeFrame = SDK::Scene::NativeFrame;

        using HitCall              = bool(__thiscall *)(NativeHuman *, NativeHitType, const Vector3 &, const Vector3 &, const Vector3 &, float, NativeActor *, uint32_t, NativeFrame *);
        using DeathCall            = void(__thiscall *)(NativeHuman *);
        using SetActStateCall      = void(__thiscall *)(NativeActor *, int32_t);
        using CarAirborneDeathCall = void(__thiscall *)(NativeHuman *, void *);
        using MovementCall         = void(__thiscall *)(NativeHuman *, uint32_t);
        using AnimationCall        = int(__fastcall *)(int, bool, const Vector3 &, const Vector3 &);

        HitCall gHitOriginal                           = nullptr;
        DeathCall gDeathOriginal                       = nullptr;
        SetActStateCall gSetActStateOriginal           = nullptr;
        DeathCall gForceDeathOriginal                  = nullptr;
        CarAirborneDeathCall gCarAirborneDeathOriginal = nullptr;
        AnimationCall gAnimationOriginal               = nullptr;
        MovementCall gMovementOriginal                 = nullptr;
        DeathService *gService                         = nullptr;
        thread_local unsigned gAuthoritativeDepth      = 0;
        thread_local uint16_t gAuthoritativeAnimation  = 0;
        thread_local unsigned gMovementDepth           = 0;

        bool IsSolidLocalHuman(const NativeHuman &human) {
            return Scripting::IsSolidLocalHumanActor(&human.Actor());
        }

        void RestoreSolidLocalHuman(NativeHuman &human) {
            human.SetHealth(std::max(human.Health(), 1.0f));
            human.Actor().RestoreLivingState();
        }

        bool __fastcall HitHook(NativeHuman *human, void *, NativeHitType type, const Vector3 &direction, const Vector3 &position, const Vector3 &normal, float damage, NativeActor *attacker, uint32_t bodyPart, NativeFrame *frame) {
            // These local scene actors have no server-side health or death
            // state. Native Hit would kill a shop attendant only on this client.
            if (IsSolidLocalHuman(*human)) {
                return true;
            }
            if (gAuthoritativeDepth != 0 || !gService || !gService->Protect(human->Actor())) {
                return gHitOriginal(human, type, direction, position, normal, damage, attacker, bodyPart, frame);
            }
            gService->OnSuppressedHit(*human, type, direction, position, attacker, bodyPart);
            // TickShoot treats a skeleton hit as consumed even when Hit returns
            // false; returning true also prevents callers that honor the result
            // from retrying the same protected target.
            return true;
        }

        void __fastcall DeathHook(NativeHuman *human, void *) {
            if (IsSolidLocalHuman(*human)) {
                RestoreSolidLocalHuman(*human);
                return;
            }
            if (gAuthoritativeDepth == 0 && gService && gService->Protect(human->Actor())) {
                if (gMovementDepth != 0) {
                    gService->OnSuppressedFatalFall(*human, Shared::Combat::FatalFallReport::Cause::Fall);
                }
                gService->RestoreLiving(human->Actor());
                return;
            }
            gDeathOriginal(human);
            if (SDK::Player::CurrentPlayer() == &human->Actor()) {
                auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
                application->Cars().OnLocalHumanDeath(application->World(), &human->Actor());
            }
        }

        void __fastcall SetActStateHook(NativeActor *actor, void *, int32_t state) {
            if (state == 2 && Scripting::IsSolidLocalHumanActor(actor)) {
                actor->RestoreLivingState();
                return;
            }
            if (state == 2 && gAuthoritativeDepth == 0 && gService && gService->Protect(*actor)) {
                gService->RestoreLiving(*actor);
                return;
            }
            if (state == 2 && actor->GetType() == NativeActor::Type::Car) {
                // C_car::Update deactivates a car falling faster than -85 on
                // every client. A network car keeps running until the server
                // commits a terminal outcome from its simulation controller.
                auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
                if (!application->Cars().AllowNativeDeactivation(application->World(), actor)) {
                    return;
                }
            }
            gSetActStateOriginal(actor, state);
        }

        void __fastcall ForceDeathHook(NativeHuman *human, void *) {
            if (IsSolidLocalHuman(*human)) {
                RestoreSolidLocalHuman(*human);
                return;
            }
            if (gAuthoritativeDepth == 0 && gService && gService->Protect(human->Actor())) {
                return;
            }
            gForceDeathOriginal(human);
        }

        void __fastcall CarAirborneDeathHook(NativeHuman *human, void *, void *car) {
            if (IsSolidLocalHuman(*human)) {
                RestoreSolidLocalHuman(*human);
                return;
            }
            if (gAuthoritativeDepth == 0 && gService && gService->Protect(human->Actor())) {
                return;
            }
            gCarAirborneDeathOriginal(human, car);
        }

        void __fastcall MovementHook(NativeHuman *human, void *, uint32_t frameMs) {
            auto *game               = SDK::Core::Mission::Get()->Game();
            const bool local         = SDK::Player::CurrentPlayer() == &human->Actor();
            const bool drowning      = human->FatalCollisionProcessed();
            const bool fallTriggered = game->PlayerDeathTriggered();
            ++gMovementDepth;
            gMovementOriginal(human, frameMs);
            --gMovementDepth;
            if (!gService || !gService->Protect(human->Actor())) {
                return;
            }
            using Cause = Shared::Combat::FatalFallReport::Cause;
            if (local) {
                // Water only zeroes health and sinks the human; its Death comes
                // about five seconds later. A fall volume never calls Death.
                if (!drowning && human->FatalCollisionProcessed()) {
                    gService->OnSuppressedFatalFall(*human, Cause::Drowned);
                }
                else if (!fallTriggered && game->PlayerDeathTriggered()) {
                    gService->OnSuppressedFatalFall(*human, Cause::FallVolume);
                }
            }
            else if (human->FallSpeed() > 40.0f) {
                // Only the owner reports a death. Keep an observer's copy from
                // reaching Movement's fatal fall speed, whose suppressed Death
                // still clears C_entity's action and fight state every frame.
                human->SetFallSpeed(40.0f);
            }
        }

        int __fastcall DeathAnimationHook(int bodyPart, bool crouched, const Vector3 &hitDirection, const Vector3 &facing) {
            if (gAuthoritativeDepth != 0 && gAuthoritativeAnimation >= 131 && gAuthoritativeAnimation <= 136) {
                return gAuthoritativeAnimation;
            }
            return gAnimationOriginal(bodyPart, crouched, hitDirection, facing);
        }

        constexpr std::array<uintptr_t, 7> kAddresses {
            SDK::Player::kHumanHit,
            SDK::Player::kHumanDeath,
            SDK::Player::kActorSetActState,
            SDK::Player::kHumanForceDeath,
            SDK::Player::kHumanCarAirborneDeath,
            SDK::Player::kHumanMovement,
            SDK::Player::kDeathAnimationSelection,
        };
    } // namespace

    bool InstallDeathHooks(DeathService &service) {
        gService = &service;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanHit), reinterpret_cast<void *>(&HitHook), reinterpret_cast<void **>(&gHitOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanDeath), reinterpret_cast<void *>(&DeathHook), reinterpret_cast<void **>(&gDeathOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kActorSetActState), reinterpret_cast<void *>(&SetActStateHook), reinterpret_cast<void **>(&gSetActStateOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanForceDeath), reinterpret_cast<void *>(&ForceDeathHook), reinterpret_cast<void **>(&gForceDeathOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanCarAirborneDeath), reinterpret_cast<void *>(&CarAirborneDeathHook), reinterpret_cast<void **>(&gCarAirborneDeathOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanMovement), reinterpret_cast<void *>(&MovementHook), reinterpret_cast<void **>(&gMovementOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kDeathAnimationSelection), reinterpret_cast<void *>(&DeathAnimationHook), reinterpret_cast<void **>(&gAnimationOriginal)) != MH_OK) {
            UninstallDeathHooks();
            return false;
        }
        for (uintptr_t address : kAddresses) {
            if (MH_EnableHook(reinterpret_cast<void *>(address)) != MH_OK) {
                UninstallDeathHooks();
                return false;
            }
        }
        return true;
    }

    void UninstallDeathHooks() {
        gService = nullptr;
        for (uintptr_t address : kAddresses) {
            MH_DisableHook(reinterpret_cast<void *>(address));
            MH_RemoveHook(reinterpret_cast<void *>(address));
        }
        gHitOriginal              = nullptr;
        gDeathOriginal            = nullptr;
        gSetActStateOriginal      = nullptr;
        gForceDeathOriginal       = nullptr;
        gCarAirborneDeathOriginal = nullptr;
        gMovementOriginal         = nullptr;
        gAnimationOriginal        = nullptr;
    }

    bool ReplayAuthoritativeDeath(NativeHuman &human, uint16_t animationId) {
        if (!gHitOriginal) {
            return false;
        }
        const auto direction = human.Actor().Direction();
        const auto position  = human.Actor().Position();
        const Vector3 normal {0.0f, 1.0f, 0.0f};
        // C_human::Hit routes a seated human to HitInCar, which applies only
        // Generic and CarImpact damage and picks the retail in-car death
        // animation for the seat itself.
        const auto &seatView = *reinterpret_cast<const SDK::Seat::NativeHuman *>(&human);
        const bool seated    = seatView.usedActorEnter && seatView.usedActorEnter->GetType() == NativeActor::Type::Car && seatView.seatId >= 0;
        ++gAuthoritativeDepth;
        gAuthoritativeAnimation = animationId;
        const bool accepted     = gHitOriginal(&human, seated ? NativeHitType::Generic : NativeHitType::Direct, direction, position, normal, 10000.0f, nullptr, 5, nullptr);
        gAuthoritativeAnimation = 0;
        --gAuthoritativeDepth;
        return accepted;
    }

    bool ReplayAuthoritativeDamage(NativeHuman &human, const Shared::Combat::DamageEvent &event) {
        if (!gHitOriginal || human.Actor().IsDead()) {
            return false;
        }
        const Vector3 direction {event.directionX, event.directionY, event.directionZ};
        const auto position = human.Actor().Position();
        const Vector3 normal {0.0f, 1.0f, 0.0f};
        const float reactionDamage = std::clamp(event.damage, 1.0f, 5.0f);
        human.SetHealth(100.0f);
        ++gAuthoritativeDepth;
        // The in-car marker stands for the zero body part C_car::Hit forwards.
        const uint32_t bodyPart = event.bodyPart == Shared::Combat::HitReport::kInCarBodyPart ? 0 : event.bodyPart;
        // Car impact enters the retail hit path as HIT_TYPE_SNIPER. The
        // protocol's hitType 4 is separate from the native enum value.
        const auto hitType = event.hitType == 1 ? NativeHitType::HardKnockdown :
                             event.hitType == 4 ? NativeHitType::Sniper : NativeHitType::Generic;
        const bool accepted = gHitOriginal(&human, hitType, direction, position, normal, reactionDamage, nullptr, bodyPart, nullptr);
        --gAuthoritativeDepth;
        return accepted;
    }
} // namespace Mafia1Online::Features::Death
