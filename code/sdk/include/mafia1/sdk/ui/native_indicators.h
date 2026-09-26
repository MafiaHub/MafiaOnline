#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::UI {
    inline constexpr uintptr_t kIndicators = 0x6bf980;
    inline constexpr uintptr_t kPlayerSetAmmo = 0x5f8910;
    inline constexpr uintptr_t kRadarSetPlayerCar = 0x5fa500;
    inline constexpr uintptr_t kRadarAddCar       = 0x5fa510;
    inline constexpr uintptr_t kRadarRemoveCar    = 0x5fa720;
    inline constexpr uintptr_t kMapEnable         = 0x5f9d10;
    inline constexpr uintptr_t kParheliaSetFov    = 0x604890;
    // G_IndicatorsClass::m_uFlags: the dashboard radar is drawn around
    // m_pRadarPlayerCar while this bit is set.
    inline constexpr uint32_t kRadarFlag = 0x80000;
    inline constexpr uint32_t kTimerFlag   = 0x8;
    inline constexpr uint32_t kCompassFlag = 0x80;

    // Game-owned HUD singleton in the verified retail executable. Its
    // lifetime spans the initialized game UI; callers never own this view.
    struct NativeIndicators {
        static NativeIndicators &Get() {
            return *reinterpret_cast<NativeIndicators *>(kIndicators);
        }

        void SetAmmo(uint32_t loaded, uint32_t reserve) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, uint32_t, uint32_t)>(kPlayerSetAmmo)(this, loaded, reserve);
        }

        // Radar cars are {C_car*, ARGB} pairs drawn as the car's rotated
        // bounding box. Adding a listed car updates its colour; C_car's
        // destructor and GameDone remove it.
        void RadarAddCar(void *car, uint32_t argb) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, void *, uint32_t)>(kRadarAddCar)(this, car, argb);
        }
        void RadarRemoveCar(void *car) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, void *)>(kRadarRemoveCar)(this, car);
        }
        // RadarRemoveCar does not clear the radar's centre car, which DrawAll
        // dereferences while the radar flag is set.
        void ReleaseRadarPlayerCar(void *car) {
            if (RadarPlayerCar() != car) {
                return;
            }
            Flags() &= ~kRadarFlag;
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, void *)>(kRadarSetPlayerCar)(this, nullptr);
        }
        // Enables the held-TAB city map overlay; retail does it only from
        // the ENABLEMAP mission script opcode, which the mod does not run.
        void MapEnable(bool enabled) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, bool)>(kMapEnable)(this, enabled);
        }
        // Stores the raw frame at +0x4278 without a reference; DrawAll
        // reads its world position while kCompassFlag is set, so clear both
        // before the frame is released.
        void CompassSetDestination(void *frame) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, void *)>(0x5fa020)(this, frame);
        }
        void AddFlag(uint32_t flag) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, uint32_t)>(0x47a080)(this, flag);
        }
        void RemoveFlag(uint32_t flag) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, uint32_t)>(0x47a0a0)(this, flag);
        }
        bool TestFlag(uint32_t flag) {
            return reinterpret_cast<bool(__thiscall *)(NativeIndicators *, uint32_t)>(0x47a0c0)(this, flag);
        }
        // The watch face runs in real time from this clock time.
        void TimerSetTime(uint32_t hours, uint32_t minutes, uint32_t seconds) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, uint32_t, uint32_t, uint32_t)>(0x5f7500)(this, hours, minutes, seconds);
        }
        // The countdown wedge; Tick counts it down and calls
        // C_game::Timer_TimeOut, which ignores a null timer program.
        void TimerSetInterval(uint32_t seconds) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, uint32_t)>(0x5f7540)(this, seconds);
        }
        uint32_t TimerGetInterval() {
            uint32_t seconds = 0;
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, uint32_t *)>(0x5f7620)(this, &seconds);
            return seconds;
        }
        // toColor true fades the screen into rgb, false fades it back out;
        // zero milliseconds is instant.
        void FadeInOut(bool toColor, uint32_t milliseconds, uint32_t rgb) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, bool, uint32_t, uint32_t)>(0x5fa370)(this, toColor, milliseconds, rgb);
        }
        // CAMERA_SETFOV uses this helper so double/triple screen modes also
        // update their aspect ratio and effective field of view.
        void ParheliaSetFov(void *camera, float radians) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, void *, float)>(kParheliaSetFov)(this, camera, radians);
        }
        // Large white centred text; copies at most 127 characters.
        void RaceFlashText(const char *text, float seconds) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, const char *, float)>(0x5fafc0)(this, text, seconds);
        }
        // Five line console, five seconds per line; colour is 0xRRGGBB.
        void ConsoleAddText(const char *text, uint32_t rgb) {
            reinterpret_cast<void(__thiscall *)(NativeIndicators *, const char *, uint32_t)>(0x5f9d50)(this, text, rgb);
        }
        bool MapEnabled() const {
            return *reinterpret_cast<const bool *>(reinterpret_cast<const std::byte *>(this) + 0x46ec);
        }

      private:
        uint32_t &Flags() {
            return *reinterpret_cast<uint32_t *>(reinterpret_cast<std::byte *>(this) + 0x40a4);
        }
        void *RadarPlayerCar() const {
            return *reinterpret_cast<void *const *>(reinterpret_cast<const std::byte *>(this) + 0x44a8);
        }
    };
} // namespace Mafia1Online::SDK::UI
