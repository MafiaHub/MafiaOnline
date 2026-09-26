#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Car {
    // Retail C_Vehicle version-9 save records. Only the damage mesh section is
    // read; the whole native loader is never called on a multiplayer car.
#pragma pack(push, 1)
    struct NativeVehicleStateHeader {
        uint32_t version;
        uint32_t totalSize;
        std::byte _unused08[0x160 - 0x08];
        int32_t lightCount;
        int32_t zoneCount;
        int32_t seatCount;
        int32_t wheelCount;
        uint8_t bodyDynamicCollisionDisabled;
    };
    struct NativeVehicleStateLight {
        int32_t codedPosition;
        uint32_t flags;
        float damage;
    };
    struct NativeVehicleStateZone {
        int32_t codedPosition;
        uint16_t flags;
        float crackLevel;
        int32_t primaryCount;
        int32_t secondaryCount;
    };
    struct NativeVehicleStateVertex { uint32_t displacement; };
#pragma pack(pop)
    static_assert(sizeof(NativeVehicleStateHeader) == 0x171);
    static_assert(offsetof(NativeVehicleStateHeader, lightCount) == 0x160);
    static_assert(offsetof(NativeVehicleStateHeader, zoneCount) == 0x164);
    static_assert(sizeof(NativeVehicleStateLight) == 0x0c);
    static_assert(sizeof(NativeVehicleStateZone) == 0x12);
    static_assert(sizeof(NativeVehicleStateVertex) == 0x04);
} // namespace Mafia1Online::SDK::Car
