#include <utils/safe_win32.h>

#include "seat_hooks.h"
#include "seat_service.h"

#include <MinHook.h>
#include <mafia1/sdk/seat/native_seat.h>

#include <initializer_list>

namespace Mafia1Online::Features::Seat {
    namespace {
        using NativeActor = SDK::Player::NativeActor;
        using NativeHuman = SDK::Seat::NativeHuman;
        using NativeCar = SDK::Seat::NativeCar;
        using UseCall = void(__thiscall *)(NativeHuman *, NativeActor *, int, int, int);
        using StealCall = bool(__thiscall *)(NativeHuman *, NativeCar *, int);

        UseCall gUseOriginal = nullptr;
        StealCall gStealOriginal = nullptr;
        SeatService *gService = nullptr;

        void __fastcall UseHook(NativeHuman *human, void *, NativeActor *target, int action, int seat, int animationSpeedState) {
            auto *car = target && target->GetType() == NativeActor::Type::Car ? static_cast<NativeCar *>(static_cast<void *>(target)) : nullptr;
            const int originalSeat = action == static_cast<int>(SDK::Seat::UseAction::Exit) ? human->seatId : seat;
            if (!car && action == static_cast<int>(SDK::Seat::UseAction::Exit) && gService) {
                car = gService->CurrentNetworkCar(human);
            }
            gUseOriginal(human, target, action, seat, animationSpeedState);
            if (car && gService) {
                gService->OnNativeUse(human, car, action, originalSeat);
            }
        }

        bool __fastcall StealHook(NativeHuman *human, void *, NativeCar *car, int seat) {
            const bool started = gStealOriginal(human, car, seat);
            if (started && gService) {
                gService->OnNativeSteal(human, car, seat);
            }
            return started;
        }
    } // namespace

    bool InstallSeatHooks(SeatService &service) {
        gService = &service;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Seat::kHumanUseActor), reinterpret_cast<void *>(&UseHook), reinterpret_cast<void **>(&gUseOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(SDK::Seat::kHumanThrowFromCar), reinterpret_cast<void *>(&StealHook), reinterpret_cast<void **>(&gStealOriginal)) != MH_OK) {
            UninstallSeatHooks();
            return false;
        }
        if (MH_EnableHook(reinterpret_cast<void *>(SDK::Seat::kHumanUseActor)) != MH_OK || MH_EnableHook(reinterpret_cast<void *>(SDK::Seat::kHumanThrowFromCar)) != MH_OK) {
            UninstallSeatHooks();
            return false;
        }
        return true;
    }

    void UninstallSeatHooks() {
        gService = nullptr;
        for (void *address : {reinterpret_cast<void *>(SDK::Seat::kHumanUseActor), reinterpret_cast<void *>(SDK::Seat::kHumanThrowFromCar)}) {
            MH_DisableHook(address);
            MH_RemoveHook(address);
        }
        gUseOriginal = nullptr;
        gStealOriginal = nullptr;
    }
} // namespace Mafia1Online::Features::Seat
