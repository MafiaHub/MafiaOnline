#pragma once

#include <cstdint>

namespace Mafia1Online::Shared::Combat {
    // G_Inventory has one hand item, five small weapon slots, one coat weapon
    // slot and four ordinary item slots. These flags are from the supported
    // retail predmety.def S_item records and G_Inventory::HaveSpaceFor.
    inline constexpr uint32_t kWeaponItemFlags = 0x24u;
    inline constexpr uint32_t kLargeItemFlag = 0x200000u;
    inline constexpr unsigned kHandWeaponSlots = 1;
    inline constexpr unsigned kSmallWeaponSlots = 5;
    inline constexpr unsigned kCoatWeaponSlots = 1;
    inline constexpr unsigned kOrdinaryItemSlots = 4;

    constexpr bool IsLargeWeapon(uint8_t itemId) {
        return itemId == 4 || (itemId >= 10 && itemId <= 14);
    }

    // Stock predmety.def marks these firearms with ITEM_FLAG_DISCARD_MAG.
    // G_Inventory::Nabij replaces their loaded magazine using reserve rounds,
    // even when the replacement magazine holds fewer rounds than the old one.
    constexpr bool DiscardsMagazine(uint8_t itemId) {
        return (itemId >= 6 && itemId <= 10) || (itemId >= 12 && itemId <= 14);
    }

    struct ReloadedAmmo {
        uint16_t loaded;
        uint16_t reserve;
        bool operator==(const ReloadedAmmo &) const = default;
    };

    // The pump shotgun loads one shell per native animation event. The other
    // firearms complete their magazine update at the terminal reload event.
    constexpr ReloadedAmmo ReloadWeaponAmmo(uint8_t itemId, uint16_t capacity, uint16_t loaded, uint16_t reserve) {
        if (itemId < 6 || itemId > 14 || capacity == 0 || reserve == 0) {
            return {loaded, reserve};
        }
        if (itemId == 11) {
            return loaded < capacity ? ReloadedAmmo {static_cast<uint16_t>(loaded + 1), static_cast<uint16_t>(reserve - 1)} : ReloadedAmmo {loaded, reserve};
        }
        if (DiscardsMagazine(itemId)) {
            const uint16_t replacement = reserve < capacity ? reserve : capacity;
            return {replacement, static_cast<uint16_t>(reserve - replacement)};
        }
        const uint16_t space = loaded < capacity ? static_cast<uint16_t>(capacity - loaded) : 0;
        const uint16_t moved = reserve < space ? reserve : space;
        return {static_cast<uint16_t>(loaded + moved), static_cast<uint16_t>(reserve - moved)};
    }
} // namespace Mafia1Online::Shared::Combat
