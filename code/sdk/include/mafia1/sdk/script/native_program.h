#pragma once

#include <cstdint>

namespace Mafia1Online::SDK::Script {
    // Opaque: the mod only intercepts the dispatch boundary while a
    // server-managed mission is active.
    struct NativeProgram {
        static constexpr uintptr_t kCallProcess = 0x46ccb0;
    };
} // namespace Mafia1Online::SDK::Script
