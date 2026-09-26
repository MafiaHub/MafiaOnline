#pragma once

#include <cstdint>

namespace Mafia1Online::SDK::Core::System {
    // Valid only after the retail image hash and fixed base have been checked.
    inline constexpr uintptr_t kClose = 0x5C0080;
    inline constexpr uintptr_t kInitSystem = 0x5BF5A0;
    inline constexpr uintptr_t kVideoMemOverflow = 0x5BEB00;
    inline constexpr int32_t kOutOfVideoMemoryError = static_cast<int32_t>(0x80000002u);
    inline constexpr uintptr_t kCollision = 0x647F48;
    inline constexpr uintptr_t kLoadCollision = 0x5C2B70;
    inline constexpr uintptr_t kGameInit = 0x5A0810;
    inline constexpr uintptr_t kGameLoop = 0x5BE750;
    inline constexpr uintptr_t kGameSetState = 0x47B630;

    inline bool LoadCollision(const char *path) {
        using Call = int(__thiscall *)(void *, const char *, bool);
        return reinterpret_cast<Call>(kLoadCollision)(reinterpret_cast<void *>(kCollision), path, true) > 0;
    }

    inline bool InitGame(void *game) {
        using Call = bool(__thiscall *)(void *);
        return reinterpret_cast<Call>(kGameInit)(game);
    }

    inline void RunGameLoop() {
        using Call = void(__cdecl *)();
        reinterpret_cast<Call>(kGameLoop)();
    }

    inline void ExitGameLoop(void *game) {
        using Call = void(__thiscall *)(void *, int);
        reinterpret_cast<Call>(kGameSetState)(game, 1);
    }
} // namespace Mafia1Online::SDK::Core::System
