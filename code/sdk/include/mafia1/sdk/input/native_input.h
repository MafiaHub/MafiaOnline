#pragma once

#include <cstdint>

namespace Mafia1Online::SDK::Input {
    inline constexpr uintptr_t kNativeInput         = 0x647c30;
    inline constexpr uintptr_t kUpdate              = 0x4f0620;
    inline constexpr uintptr_t kGetState            = 0x4f01a0;
    inline constexpr uintptr_t kIGraphReadKeyOffset = 0x71960;
    inline constexpr uintptr_t kTranslatedKeyOffset = 0x1c546c;

    struct NativeInput {
        int GetState(float **state, bool pressedOnly) {
            using Call = int(__thiscall *)(NativeInput *, float **, bool);
            return reinterpret_cast<Call>(kGetState)(this, state, pressedOnly);
        }
    };

    inline NativeInput &Instance() {
        return *reinterpret_cast<NativeInput *>(kNativeInput);
    }

    inline int GetState(void *input, float **state, bool pressedOnly) {
        return static_cast<NativeInput *>(input)->GetState(state, pressedOnly);
    }
} // namespace Mafia1Online::SDK::Input
