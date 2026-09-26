#pragma once

#include <mafia1/sdk/core/game.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::World {
    // reM S_GameItem.
    struct NativeGameItem {
        uint16_t itemId;
        uint16_t weaponEffectLatch;
        int32_t ammoLoaded;
        int32_t ammoHidden;
        struct NativeUsingObject *usingObject;
    };
    static_assert(sizeof(NativeGameItem) == 0x10);
    static_assert(offsetof(NativeGameItem, usingObject) == 0x0c);

    // reM S_using_object: a world pickup the use key finds. With
    // USING_OBJECT_FRAME_BOUND (2) and without OWNED (1), DelObject
    // invalidates and releases the frame but never frees the record.
    struct NativeUsingObject {
        Player::Vector3 position;
        float distanceSquared;
        void *matrix;
        uint32_t flags;
        Scene::NativeFrame *frame;
        Player::NativeActor *actor;
        Scene::NativeFrame *ownerFrame;
        bool transientOwner;
        std::byte _unused25[3];
        NativeGameItem item;
    };
    static_assert(sizeof(NativeUsingObject) == 0x38);
    static_assert(offsetof(NativeUsingObject, distanceSquared) == 0x0c);
    static_assert(offsetof(NativeUsingObject, flags) == 0x14);
    static_assert(offsetof(NativeUsingObject, actor) == 0x1c);
    static_assert(offsetof(NativeUsingObject, item) == 0x28);

    inline constexpr uint32_t kUsingObjectFrameBound = 2;

    // C_using_object at C_game+0x29ec (Do_AB_OwnerNULL, 0x5947b2).
    // AddObject 0x55dd60 and DelObject 0x55df10 are __thiscall(registry,
    // S_using_object*), ret 4. FindNearObjects 0x55e180 is __thiscall(
    // registry, const S_vector& position, const S_vector& direction,
    // std::vector<S_GameItem>*, I3D_frame*), ret 0x10, nearest first.
    inline constexpr uintptr_t kUsingObjectsOffset = 0x29ec;
    inline constexpr uintptr_t kAddObject          = 0x55DD60;
    inline constexpr uintptr_t kDelObject          = 0x55DF10;
    inline constexpr uintptr_t kFindNearObjects    = 0x55E180;

    // C_human::DropOutItems 0x57faa0: bool __thiscall(C_human*,
    // std::vector<S_GameItem>*), ret 4. Every retail weapon drop, including
    // death, holstering overflow and car entry, creates its item here.
    inline constexpr uintptr_t kDropOutItems = 0x57FAA0;

    // g_pItems (0x6d4c14): 0xbc-byte S_item records; the model name is an
    // inline 32 byte string at +0x24 (DropOutItems 0x57fb82-0x57fba3).
    inline const char *ItemModelName(uint16_t itemId) {
        const auto *items = *reinterpret_cast<const std::byte *const *>(0x6d4c14);
        return reinterpret_cast<const char *>(items + static_cast<size_t>(itemId) * 0xbc + 0x24);
    }

    // The VC6 std::vector<S_GameItem> layout FindNearObjects fills.
    struct NativeItemVector {
        void *allocatorProxy;
        NativeGameItem *begin;
        NativeGameItem *end;
        NativeGameItem *capacity;
    };

    inline void *UsingObjects(Core::Game::NativeGame *game) {
        return reinterpret_cast<std::byte *>(game) + kUsingObjectsOffset;
    }
    inline void AddUsingObject(Core::Game::NativeGame *game, NativeUsingObject *object) {
        using Call = void(__thiscall *)(void *, NativeUsingObject *);
        reinterpret_cast<Call>(kAddObject)(UsingObjects(game), object);
    }
    inline void DelUsingObject(Core::Game::NativeGame *game, NativeUsingObject *object) {
        using Call = void(__thiscall *)(void *, NativeUsingObject *);
        reinterpret_cast<Call>(kDelObject)(UsingObjects(game), object);
    }
} // namespace Mafia1Online::SDK::World
