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
    // GameLoopSingle1 ticks C_cache_base_block (0x4020b0) on g_cacheBaseBlock
    // after C_mission::Tick while its open flag is set (0x5be905-0x5be911).
    // It streams cached city frames in and out around the active camera.
    inline constexpr uintptr_t kCacheBaseBlock = 0x63D268;
    inline constexpr uintptr_t kCacheBaseBlockOpen = 0x63D28C;
    inline constexpr uintptr_t kCacheBaseBlockTick = 0x4020B0;

    inline void TickCacheBaseBlock(unsigned int frameMs) {
        if (*reinterpret_cast<const bool *>(kCacheBaseBlockOpen)) {
            using Call = void(__thiscall *)(void *, unsigned int);
            reinterpret_cast<Call>(kCacheBaseBlockTick)(reinterpret_cast<void *>(kCacheBaseBlock), frameMs);
        }
    }

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
