#pragma once

#include <cstdint>

namespace Mafia1Online::SDK::World {
    // reM C_bridge::GameInit and C_bridge::ShutDown. GameInit resets the
    // shutdown flag, so multiplayer applies it after native initialization.
    inline constexpr uintptr_t kBridgeGameInit = 0x569510;
    inline constexpr uintptr_t kBridgeShutDown = 0x56a7b0;

    struct NativeBridge {
        void ShutDown(bool shutDown) {
            using Call = void(__thiscall *)(NativeBridge *, bool);
            reinterpret_cast<Call>(kBridgeShutDown)(this, shutDown);
        }
    };
} // namespace Mafia1Online::SDK::World
