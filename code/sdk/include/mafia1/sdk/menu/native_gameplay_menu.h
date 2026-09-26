#pragma once

#include "native_menu.h"
#include <mafia1/sdk/player/native_inventory.h>
#include <mafia1/sdk/world/native_using_object.h>

#include <span>

namespace Mafia1Online::SDK::Menu {
    inline constexpr uintptr_t kInventoryVtable    = 0x627584;
    inline constexpr uintptr_t kItemPickupVtable   = 0x625920;
    inline constexpr uint32_t kPickupCancelled     = 167;
    inline constexpr uint32_t kInventoryCancelled  = 0xffffffffu;
    inline constexpr int kInventoryControl         = 24;
    inline constexpr int kInteractControl          = 10;
    inline constexpr int kAlternateInteractControl = 11;
    inline constexpr int kFireControl              = 12;
    inline constexpr int kAlternateFireControl     = 13;

    // VC6 vector views borrow storage from the current native player call.
    template <typename T>
    struct NativeMenuVector {
        void *allocator;
        T *begin;
        T *end;
        T *capacity;
        std::span<T> Items() const {
            return begin ? std::span<T>(begin, end) : std::span<T>();
        }
    };

    struct NativeInventoryMenu {
        NativeMenu base;
        std::byte _unused1c[0x30 - sizeof(NativeMenu)];
        Player::NativeInventory *inventory;
        World::NativeItemVector *dropped;

        void Select(uint32_t index, uint32_t slot) {
            using Call = bool(__thiscall *)(Player::NativeInventory *, uint32_t, uint32_t, World::NativeItemVector *);
            reinterpret_cast<Call>(0x607bc0)(inventory, index, slot, dropped);
        }
        void Drop(Player::NativeGameItem *item) {
            using Call = bool(__thiscall *)(Player::NativeInventory *, Player::NativeGameItem *, World::NativeItemVector *);
            reinterpret_cast<Call>(0x6095e0)(inventory, item, dropped);
        }
    };
    static_assert(offsetof(NativeInventoryMenu, inventory) == 0x30);
    static_assert(sizeof(NativeInventoryMenu) == 0x38);

    struct NativePickupMenu {
        NativeMenu base;
        std::byte _unused1c[0x30 - sizeof(NativeMenu)];
        Player::NativeInventory *inventory;
        NativeMenuVector<Player::NativeInventory *> *inventories;
        World::NativeItemVector *gameItems;
        World::NativeItemVector *dropped;
        World::NativeItemVector *removed;
        NativeMenuVector<void *> itemAddresses;

        // OnClick only reads this address vector; it appends outcomes to the
        // caller's native vectors. Restore our borrowed view before Destroy.
        uint32_t Select(std::span<void *> items, uint32_t index) {
            const auto savedAddresses = itemAddresses;
            const auto savedResult    = GetLoopResult();
            itemAddresses             = {nullptr, items.data(), items.data() + items.size(), items.data() + items.size()};
            using Call                = int(__thiscall *)(NativePickupMenu *, uint32_t);
            reinterpret_cast<Call>(0x5e3480)(this, 256 + index);
            const auto result                              = GetLoopResult();
            *reinterpret_cast<uint32_t *>(kMenuLoopResult) = savedResult;
            itemAddresses                                  = savedAddresses;
            return result;
        }
    };
    static_assert(offsetof(NativePickupMenu, inventory) == 0x30);
    static_assert(offsetof(NativePickupMenu, itemAddresses) == 0x44);
    static_assert(sizeof(NativePickupMenu) == 0x54);

    inline const char *GameplayText(uint32_t id) {
        using Call = const char *(__thiscall *)(void *, uint32_t);
        return reinterpret_cast<Call>(0x60fb40)(reinterpret_cast<void *>(0x6d8714), id);
    }
} // namespace Mafia1Online::SDK::Menu
