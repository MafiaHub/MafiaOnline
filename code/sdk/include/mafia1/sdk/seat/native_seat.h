#pragma once

#include <mafia1/sdk/car/native_car.h>
#include <mafia1/sdk/player/native_actor.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Seat {
    inline constexpr uintptr_t kHumanUseActor = 0x582180;
    inline constexpr uintptr_t kHumanCanDropOutFromCar = 0x5805c0;
    inline constexpr uintptr_t kHumanInternUseCar = 0x57e020;
    inline constexpr uintptr_t kHumanFinishCarTransition = 0x5716d0;
    inline constexpr uintptr_t kHumanForceExitCar = 0x581220;
    inline constexpr uintptr_t kHumanClimbInCar = 0x581eb0;
    inline constexpr uintptr_t kHumanThrowFromCar = 0x587d70;
    // C_human::intern_ThrowMeFromCar, void __thiscall(C_human*): frees the
    // seated human's seat (+0x98, +0xac) and plays its throw-out (308-315).
    inline constexpr uintptr_t kHumanThrowMeFromCar = 0x588b40;
    // C_human::Intern_UseRailway, void __thiscall(C_human*, bool enter),
    // ret 4: the end of a railway step. false takes the human back to world
    // space and the primary sector, out of the wagon's collision list
    // (DynGetOut), unlinks the camera of the local player and Unlocks.
    inline constexpr uintptr_t kHumanInternUseRailway = 0x571b90;
    inline constexpr uintptr_t kCarGetOwner = 0x41dec0;
    inline constexpr uintptr_t kCarGetSeatProperty = 0x41dc30;
    // C_car::SetEnterProtect1, void __thiscall(C_car*, int seat, bool),
    // ret 8: sets or clears the seat's bit in m_uSeatProtectionMask, then
    // m_bPhysicsUpdatePending and SetActState(active).
    inline constexpr uintptr_t kCarSetEnterProtect1 = 0x429770;
    // C_car::m_uSeatProtectionMask: C_car::Update registers a seat's use
    // prompt only while its bit is set (0x41ffb0, removed at 0x4200d5).
    inline constexpr size_t kCarSeatProtectionMask = 0x21c4;

    enum class UseAction : int32_t {
        Enter = 1,
        Exit = 2,
    };

    struct NativeCar: Car::NativeCar {
        [[nodiscard]] Player::NativeActor *GetOwner(int seat) {
            using Call = Player::NativeActor *(__thiscall *)(NativeCar *, int);
            return reinterpret_cast<Call>(kCarGetOwner)(this, seat);
        }

        [[nodiscard]] bool HasSeat(int seat) {
            bool left = false;
            bool hasDoor = false;
            bool rear = false;
            bool open = false;
            using Call = bool(__thiscall *)(NativeCar *, int, bool *, bool *, bool *, bool *);
            return reinterpret_cast<Call>(kCarGetSeatProperty)(this, seat, &left, &hasDoor, &rear, &open);
        }

        // Use_Actor clears the seat's bit for its door animation and only
        // Intern_UseCar(bool) sets it again; while clear, the seat offers no
        // use prompt.
        [[nodiscard]] bool SeatEnterable(int seat) const {
            const auto mask = *reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(this) + kCarSeatProtectionMask);
            return ((mask >> seat) & 1u) != 0;
        }

        void SetSeatEnterable(int seat, bool enterable) {
            using Call = void(__thiscall *)(NativeCar *, int, bool);
            reinterpret_cast<Call>(kCarSetEnterProtect1)(this, seat, enterable);
        }
    };
    static_assert(sizeof(NativeCar) == sizeof(Car::NativeCar));
    static_assert(sizeof(NativeCar) >= kCarSeatProtectionMask + sizeof(uint32_t));

    // The seat state lives in C_human's prefix, shared by C_player and C_entity.
    struct NativeHuman {
        Player::NativeActor actor;
        int32_t animationId;
        int32_t animationState;
        std::byte _unused78[0x20];
        Player::NativeActor *usedActorEnter;
        Player::NativeActor *usedActorLeave;
        NativeCar *currentCar;
        NativeCar *controlOverrideCar;
        NativeCar *currentCar2;
        int32_t seatId;
        std::byte _unusedB0[0x410 - 0xb0];
        // 1 while a Use_Actor door animation holds the car's boarding lock.
        int32_t transitionState;

        [[nodiscard]] bool IsSeatedIn(const NativeCar *car, int seat) const {
            return usedActorEnter == static_cast<const Player::NativeActor *>(car) && seatId == seat;
        }

        [[nodiscard]] bool CanDropOut(int seat) {
            using Call = bool(__thiscall *)(NativeHuman *, int);
            return reinterpret_cast<Call>(kHumanCanDropOutFromCar)(this, seat);
        }

        [[nodiscard]] bool PlaceInCar(NativeCar *car, int seat) {
            using Call = void(__thiscall *)(NativeHuman *, NativeCar *, int);
            reinterpret_cast<Call>(kHumanInternUseCar)(this, car, seat);
            return IsSeatedIn(car, seat) && car->GetOwner(seat) == &actor;
        }

        // Use_Actor Enter reserves the seat and takes C_Vehicle::LockVehicle
        // until its door animation ends. A snap must release that lock.
        [[nodiscard]] bool HasPendingEntry(const NativeCar *car) const {
            return transitionState == 1 && !usedActorEnter && usedActorLeave == static_cast<const Player::NativeActor *>(car);
        }

        // C_human::Intern_UseCar(bool) is the retail door-animation completion:
        // it unlocks the vehicle, links the seat and clears the reservation.
        [[nodiscard]] bool FinishPendingEntry(NativeCar *car, int seat) {
            using Call = void(__thiscall *)(NativeHuman *, bool);
            reinterpret_cast<Call>(kHumanFinishCarTransition)(this, true);
            return IsSeatedIn(car, seat) && car->GetOwner(seat) == &actor;
        }

        void ForceExitCar() {
            using Call = void(__thiscall *)(NativeHuman *);
            reinterpret_cast<Call>(kHumanForceExitCar)(this);
        }

        [[nodiscard]] bool ClimbToPairedSeat() {
            using Call = bool(__thiscall *)(NativeHuman *);
            return reinterpret_cast<Call>(kHumanClimbInCar)(this);
        }

        void UseCar(NativeCar *car, UseAction action, int seat) {
            using Call = void(__thiscall *)(NativeHuman *, NativeCar *, int, int, int);
            reinterpret_cast<Call>(kHumanUseActor)(this, car, static_cast<int>(action), seat, 0);
        }

        [[nodiscard]] bool TrySteal(NativeCar *car, int seat) {
            using Call = bool(__thiscall *)(NativeHuman *, NativeCar *, int);
            return reinterpret_cast<Call>(kHumanThrowFromCar)(this, car, seat);
        }

        // Retail's Corovacka branch of Do_ThrowCocotFromCar seats the new
        // driver (Intern_UseCar) and calls this on it.
        void ThrowOutOfSeat() {
            reinterpret_cast<void(__thiscall *)(NativeHuman *)>(kHumanThrowMeFromCar)(this);
        }

        // Aboard a railway, +0x98 holds it as for a car.
        void LeaveRailwayNow() {
            reinterpret_cast<void(__thiscall *)(NativeHuman *, bool)>(kHumanInternUseRailway)(this, false);
        }
    };
    static_assert(offsetof(NativeHuman, usedActorEnter) == 0x98);
    static_assert(offsetof(NativeHuman, usedActorLeave) == 0x9c);
    static_assert(offsetof(NativeHuman, seatId) == 0xac);
    static_assert(offsetof(NativeHuman, transitionState) == 0x410);
} // namespace Mafia1Online::SDK::Seat
