#pragma once

#include <mafia1/sdk/player/native_actor.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Door {
    // reM C_door::SetState, SetOpenAngle and EnableUsingObject in retail Game.exe.
    inline constexpr uintptr_t kSetState = 0x439610;
    inline constexpr uintptr_t kSetOpenAngle = 0x43b810;
    inline constexpr uintptr_t kEnableUsingObject = 0x43add0;
    inline constexpr int32_t kPromptOpen = 2000;
    inline constexpr int32_t kPromptClose = 2001;

    enum class State : uint32_t {
        Open = 0,
        Closed = 1,
        Opening = 2,
        Closing = 3,
    };

    // Borrowed C_door view. Never retain across a mission close.
    struct NativeDoor: Player::NativeActor {
        State state;
        float angle;
        std::byte _unused78[0x110 - 0x78];
        uint32_t flags;
        NativeDoor *pairedDoor;
        NativeDoor *parentDoor;
        bool interactionDisabled;
        bool opensBothDirections;
        uint8_t reverseDirection;
        std::byte _unused11f;
        float maximumAngle;
        bool initiallyOpened;
        bool locked;
        std::byte _unused126[0x164 - 0x126];
        bool useOmniSector;
        std::byte _unused165[0x168 - 0x165];
        Player::Vector3 openDirection;
        std::byte _unused174[0x1ac - 0x174];
        int32_t usePrompt;
        std::byte _unused1b0[0x1b8 - 0x1b0];

        NativeDoor *Root() {
            auto *root = this;
            while (root->parentDoor) {
                root = root->parentDoor;
            }
            return root;
        }
        void SetState(State next) {
            using Call = int(__thiscall *)(NativeDoor *, State, Player::NativeActor *, bool, bool);
            reinterpret_cast<Call>(kSetState)(this, next, nullptr, true, true);
        }
        void SetOpenAngle(float fraction) {
            using Call = void(__thiscall *)(NativeDoor *, float);
            reinterpret_cast<Call>(kSetOpenAngle)(this, fraction);
        }
        void EnableUsingObject(bool enabled) {
            using Call = void(__thiscall *)(NativeDoor *, bool);
            reinterpret_cast<Call>(kEnableUsingObject)(this, enabled);
        }
        void SetUsePrompt(bool isOpen) { usePrompt = isOpen ? kPromptClose : kPromptOpen; }
    };
    static_assert(sizeof(void *) == 4);
    static_assert(offsetof(NativeDoor, state) == 0x70);
    static_assert(offsetof(NativeDoor, flags) == 0x110);
    static_assert(offsetof(NativeDoor, pairedDoor) == 0x114);
    static_assert(offsetof(NativeDoor, parentDoor) == 0x118);
    static_assert(offsetof(NativeDoor, reverseDirection) == 0x11e);
    static_assert(offsetof(NativeDoor, maximumAngle) == 0x120);
    static_assert(offsetof(NativeDoor, locked) == 0x125);
    static_assert(offsetof(NativeDoor, useOmniSector) == 0x164);
    static_assert(offsetof(NativeDoor, openDirection) == 0x168);
    static_assert(offsetof(NativeDoor, usePrompt) == 0x1ac);
    static_assert(sizeof(NativeDoor) == 0x1b8);
} // namespace Mafia1Online::SDK::Door
