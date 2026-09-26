#pragma once

#include <mafia1/sdk/player/native_actor.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Player {
    struct NativeActor;

    // Retail G_Camera is embedded in C_game at +0x4c. Its SetPlayer method
    // ends the current camera mode, swaps the borrowed human, then begins it.
    struct NativeCamera {
        void SetPlayer(NativeActor *actor) {
            using Call = void(__thiscall *)(NativeCamera *, NativeActor *);
            reinterpret_cast<Call>(0x5ed170)(this, actor);
        }
        [[nodiscard]] NativeActor *Player() const { return _player; }
        // Retail SetCar ends the old mode and begins the chosen car or
        // pedestrian mode. The car is borrowed and must be detached first.
        void SetCar(NativeActor *actor) {
            using Call = void(__thiscall *)(NativeCamera *, NativeActor *);
            reinterpret_cast<Call>(0x5ed190)(this, actor);
        }
        [[nodiscard]] NativeActor *Car() const { return _car; }
        // G_Camera::LockAt takes two borrowed S_vector references and copies
        // the pose into the active camera; it retains neither argument.
        void LockAt(const Vector3 &position, const Vector3 &direction) {
            using Call = void(__thiscall *)(NativeCamera *, const Vector3 &, const Vector3 &);
            reinterpret_cast<Call>(0x5f39f0)(this, position, direction);
        }
        void Unlock() {
            using Call = void(__thiscall *)(NativeCamera *, bool);
            reinterpret_cast<Call>(0x5f3fd0)(this, false);
        }
        // C_human::Death switches G_Camera to its fixed death mode. SetPlayer
        // changes the human but preserves that mode, so a new life must also
        // restore the retail pedestrian camera. SetCar(nullptr) clears the
        // previous car and free-look state; Link(nullptr) clears any borrowed
        // scripted frame and selects the profile's pedestrian mode.
        void ResetForPedestrian() {
            SetCar(nullptr);
            using Call = void(__thiscall *)(NativeCamera *, void *);
            reinterpret_cast<Call>(0x5ed1e0)(this, nullptr); // G_Camera::Link
        }
        [[nodiscard]] uint32_t Mode() const {
            return _mode;
        }
        // reM G_Camera::SetMode at 0x5ed400. Selecting the behind mode while
        // spectating keeps mouse camera mode from turning the remote human.
        void SetMode(uint32_t mode, bool force = true) {
            using Call = void(__thiscall *)(NativeCamera *, uint32_t, bool);
            reinterpret_cast<Call>(0x5ed400)(this, mode, force);
        }
        [[nodiscard]] float Pitch() const { return _pitch; }
        void SetPitch(float pitch) { _pitch = pitch; }

        std::byte _unused00[8];
        NativeActor *_player;
        NativeActor *_car;
        uint32_t _mode;
        std::byte _unused14[0x14];
        float _pitch;
        std::byte _unused2c[0x6c];
    };
    static_assert(sizeof(NativeCamera) == 0x98);
    static_assert(offsetof(NativeCamera, _player) == 0x08);
    static_assert(offsetof(NativeCamera, _car) == 0x0c);
    static_assert(offsetof(NativeCamera, _mode) == 0x10);
    static_assert(offsetof(NativeCamera, _pitch) == 0x28);
} // namespace Mafia1Online::SDK::Player
