#pragma once

#include "shared/features/combat/weapon_inventory.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Mafia1Online::SDK::Player {
    // Retail G_Inventory/S_GameItem views. The inventory and any objects it
    // references belong to C_human; these views never own or free them.
    struct NativeGameItem {
        uint16_t itemId;
        uint16_t weaponEffectLatch;
        int32_t ammoLoaded;
        int32_t ammoReserve;
        void *usingObject;
    };
    static_assert(sizeof(NativeGameItem) == 0x10);

    inline constexpr uintptr_t kItemTablePointer    = 0x6D4C14;
    inline constexpr uintptr_t kItemTableCount      = 0x6D4C18;
    inline constexpr uint32_t kNoAutoSelectItemFlag = 0x100;
    inline constexpr uint32_t kFirearmItemFlag      = 0x20;

    struct NativeItemDefinition {
        std::byte _name[0x20];
        uint32_t flags;
        std::byte _rest[0x3c];
        int32_t magazineCapacity;
        std::byte _tail[0x58];
    };
    static_assert(sizeof(NativeItemDefinition) == 0xbc);
    static_assert(offsetof(NativeItemDefinition, flags) == 0x20);
    static_assert(offsetof(NativeItemDefinition, magazineCapacity) == 0x60);

    struct NativeStockItem {
        uint16_t itemId;
        int32_t loaded;
        int32_t reserve;
    };

    inline uint32_t StockItemFlags(uint16_t itemId) {
        const auto count  = *reinterpret_cast<const uint32_t *>(kItemTableCount);
        const auto *items = *reinterpret_cast<const NativeItemDefinition *const *>(kItemTablePointer);
        return itemId < count ? items[itemId].flags : 0;
    }

    inline uint16_t StockMagazineCapacity(uint16_t itemId) {
        const auto count  = *reinterpret_cast<const uint32_t *>(kItemTableCount);
        const auto *items = *reinterpret_cast<const NativeItemDefinition *const *>(kItemTablePointer);
        return itemId < count && items[itemId].magazineCapacity > 0 ? static_cast<uint16_t>(items[itemId].magazineCapacity) : 0;
    }

    struct NativeInventory {
        [[nodiscard]] NativeGameItem &Selected() {
            return _selected;
        }
        [[nodiscard]] const NativeGameItem &Selected() const {
            return _selected;
        }
        [[nodiscard]] uint32_t ItemMask() const {
            uint32_t mask      = 0;
            const auto include = [&mask](const NativeGameItem &item) {
                if (item.itemId < 32 && item.itemId != 0) {
                    mask |= 1u << item.itemId;
                }
            };
            include(_selected);
            for (const auto &weapon : _weapons) {
                include(weapon);
            }
            include(_coatWeapon);
            for (uint32_t i = 0; i < _itemCount && i < Shared::Combat::kOrdinaryItemSlots; ++i) {
                include(_items[i]);
            }
            return mask;
        }

        // Rejects an unrepresentable set before changing any game-owned slot.
        bool Reconcile(std::span<const NativeStockItem> stock, uint16_t selectedId) {
            NativeGameItem hand {};
            NativeGameItem weapons[Shared::Combat::kSmallWeaponSlots] {};
            NativeGameItem coat {};
            NativeGameItem items[Shared::Combat::kOrdinaryItemSlots] {};
            unsigned weaponCount = 0;
            unsigned itemCount   = 0;
            for (const auto &entry : stock) {
                NativeGameItem next {entry.itemId, 0, entry.loaded, entry.reserve, nullptr};
                if (entry.itemId == selectedId) {
                    hand = next;
                    continue;
                }
                const uint32_t flags = StockItemFlags(entry.itemId);
                if (flags & Shared::Combat::kWeaponItemFlags) {
                    if (flags & Shared::Combat::kLargeItemFlag) {
                        if (coat.itemId != 0) {
                            return false;
                        }
                        coat = next;
                    }
                    else {
                        if (weaponCount == Shared::Combat::kSmallWeaponSlots) {
                            return false;
                        }
                        weapons[weaponCount++] = next;
                    }
                }
                else {
                    if (itemCount == Shared::Combat::kOrdinaryItemSlots || flags & Shared::Combat::kLargeItemFlag) {
                        return false;
                    }
                    items[itemCount++] = next;
                }
            }
            if (selectedId != 0 && hand.itemId == 0) {
                return false;
            }
            Assign(_selected, hand);
            for (unsigned i = 0; i < Shared::Combat::kSmallWeaponSlots; ++i) {
                Assign(_weapons[i], weapons[i]);
            }
            Assign(_coatWeapon, coat);
            for (unsigned i = 0; i < Shared::Combat::kOrdinaryItemSlots; ++i) {
                Assign(_items[i], items[i]);
            }
            _itemCount = itemCount;
            if (coat.itemId != 0) {
                _canHaveBigWeapon = 1;
            }
            return true;
        }

      private:
        static void Assign(NativeGameItem &slot, const NativeGameItem &value) {
            if (slot.itemId != value.itemId) {
                slot.weaponEffectLatch = 0;
                slot.usingObject       = nullptr;
            }
            slot.itemId      = value.itemId;
            slot.ammoLoaded  = value.ammoLoaded;
            slot.ammoReserve = value.ammoReserve;
        }

      public:
        int32_t _canHaveBigWeapon;
        int32_t _itemPickUp;
        uint32_t _itemCount;
        std::byte _cycle[0x14];
        NativeGameItem _selected;
        NativeGameItem _weapons[Shared::Combat::kSmallWeaponSlots];
        NativeGameItem _coatWeapon;
        NativeGameItem _items[Shared::Combat::kOrdinaryItemSlots];
        NativeGameItem _pickupItem;
        int32_t _showMessages;
    };
    static_assert(sizeof(NativeInventory) == 0xe4);
    static_assert(offsetof(NativeInventory, _selected) == 0x20);
    static_assert(offsetof(NativeInventory, _weapons) == 0x30);
    static_assert(offsetof(NativeInventory, _coatWeapon) == 0x80);
    static_assert(offsetof(NativeInventory, _items) == 0x90);
    static_assert(offsetof(NativeInventory, _pickupItem) == 0xd0);
} // namespace Mafia1Online::SDK::Player
