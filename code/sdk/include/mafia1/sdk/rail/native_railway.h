#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Rail {
    // reM C_railway::Update and m_iPassengerTrafficMode. The native
    // DEACTIVATING branch turns off the model and wagons and removes their
    // dynamic collisions, radar markers, engine and curve sounds.
    struct NativeRailway {
        enum class PassengerTrafficMode : int32_t {
            Deactivating = 1,
            Inactive = 3,
        };

        static constexpr uint32_t kActorType = 8;
        static constexpr uintptr_t kAI = 0x488140;
        static constexpr uintptr_t kUpdate = 0x4894d0;
        static constexpr size_t kPassengerTrafficModeOffset = 0x2a8;

        void Deactivate() {
            auto *mode = reinterpret_cast<PassengerTrafficMode *>(reinterpret_cast<std::byte *>(this) + kPassengerTrafficModeOffset);
            *mode = PassengerTrafficMode::Deactivating;
            reinterpret_cast<void(__thiscall *)(NativeRailway *, unsigned int)>(kUpdate)(this, 0);
            *mode = PassengerTrafficMode::Inactive;
        }
    };
} // namespace Mafia1Online::SDK::Rail
