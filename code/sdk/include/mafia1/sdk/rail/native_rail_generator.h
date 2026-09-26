#pragma once

#include <cstdint>

namespace Mafia1Online::SDK::Rail {
    // Opaque: the mod only intercepts the AI vmethod that assigns tram and
    // metro pool vehicles to tracks.
    struct NativeRailGenerator {
        static constexpr uintptr_t kAI = 0x597bf0;
        static constexpr uintptr_t kVtable = 0x6259e8;
    };
} // namespace Mafia1Online::SDK::Rail
