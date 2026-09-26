#include <utils/safe_win32.h>

#include "debris_hooks.h"

#include "core/application.h"
#include "features/car/debris_service.h"

#include <MinHook.h>
#include <core_modules.h>
#include <mafia1/sdk/core/game.h>

namespace Mafia1Online::Features::Car {
    namespace {
        using DropOut = bool(__fastcall *)(SDK::Car::NativeCar *, SDK::Scene::NativeFrame *, void *, int, int);
        using AddTemporaryActor = void(__thiscall *)(SDK::Core::Game::NativeGame *, SDK::Player::NativeActor *);
        DropOut gDropOutOriginal = nullptr;
        AddTemporaryActor gAddTemporaryOriginal = nullptr;

        struct Capture {
            SDK::Player::NativeActor *actor = nullptr;
        };
        thread_local Capture *gCapture = nullptr;
        thread_local unsigned gReplayDepth = 0;
        thread_local SDK::Player::NativeActor *gReplayActor = nullptr;

        void __fastcall AddTemporaryActorHook(SDK::Core::Game::NativeGame *game, void *, SDK::Player::NativeActor *actor) {
            gAddTemporaryOriginal(game, actor);
            if (gCapture && actor->GetType() == SDK::Player::NativeActor::Type::DropOut) {
                gCapture->actor = actor;
            }
        }

        bool __fastcall DropOutHook(SDK::Car::NativeCar *car, SDK::Scene::NativeFrame *source,
                                    void *parameters, int type, int partIndex) {
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            if (!gReplayDepth && !application->Debris().AllowNativeDropOut(application->World(), car)) {
                return false;
            }
            Capture capture;
            Capture *previous = gCapture;
            gCapture = &capture;
            const bool result = gDropOutOriginal(car, source, parameters, type, partIndex);
            gCapture = previous;
            if (result && capture.actor) {
                if (gReplayDepth) {
                    gReplayActor = capture.actor;
                }
                else {
                    application->Debris().OnNativeDropOut(application->World(), car, capture.actor,
                                                          type, partIndex, parameters);
                }
            }
            return result;
        }
    } // namespace

    bool InstallDebrisHooks() {
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Core::Game::kAddTemporaryActor),
                          reinterpret_cast<void *>(&AddTemporaryActorHook),
                          reinterpret_cast<void **>(&gAddTemporaryOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(SDK::Core::Game::kAddTemporaryActor)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Core::Game::kAddTemporaryActor));
            return false;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Car::kDropOut),
                          reinterpret_cast<void *>(&DropOutHook),
                          reinterpret_cast<void **>(&gDropOutOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(SDK::Car::kDropOut)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDropOut));
            MH_DisableHook(reinterpret_cast<void *>(SDK::Core::Game::kAddTemporaryActor));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Core::Game::kAddTemporaryActor));
            return false;
        }
        return true;
    }

    void UninstallDebrisHooks() {
        MH_DisableHook(reinterpret_cast<void *>(SDK::Car::kDropOut));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Car::kDropOut));
        MH_DisableHook(reinterpret_cast<void *>(SDK::Core::Game::kAddTemporaryActor));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Core::Game::kAddTemporaryActor));
    }

    SDK::Player::NativeActor *ApplyAuthoritativeDropOut(SDK::Car::NativeCar *car,
                                                        SDK::Scene::NativeFrame *source,
                                                        void *parameters,
                                                        SDK::Car::NativeDropOutType type,
                                                        int partIndex) {
        ++gReplayDepth;
        gReplayActor = nullptr;
        const bool created = car->DropOut(source, parameters, type, partIndex);
        --gReplayDepth;
        auto *actor = created ? gReplayActor : nullptr;
        gReplayActor = nullptr;
        return actor;
    }
} // namespace Mafia1Online::Features::Car
