#pragma once

#include <mafia1/sdk/core/game.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Core::Mission {
    // Valid only after the retail image hash and fixed base have been checked.
    inline constexpr uintptr_t kTick  = 0x5407E0;
    inline constexpr uintptr_t kClose = 0x5405E0;
    inline constexpr uintptr_t kOpen = 0x5409D0;
    inline constexpr uintptr_t kCreateActor = 0x53f7d0;
    inline constexpr uintptr_t kFindActorByName = 0x540490;
    inline constexpr uintptr_t kGlobalProgramGameInit = 0x540520;
    inline constexpr uintptr_t kGlobalProgramGameDone = 0x540550;
    inline constexpr uintptr_t kGlobalProgramRun = 0x540580;
    inline constexpr uintptr_t kGlobalMissionPointer = 0x63788C;
    struct NativeMission {
        [[nodiscard]] Game::NativeGame *Game() const { return _game; }
        [[nodiscard]] SDK::Scene::NativeScene *GetScene() const { return _scene; }
        [[nodiscard]] Player::NativeActor *CreateActor(Player::NativeActor::Type type) {
            using Call = Player::NativeActor *(__thiscall *)(NativeMission *, uint32_t);
            return reinterpret_cast<Call>(kCreateActor)(this, static_cast<uint32_t>(type));
        }
        // reM C_mission::FindActorByName searches the mission's actor list,
        // including actors whose scene sector is not currently active.
        [[nodiscard]] Player::NativeActor *FindActorByName(const char *name) const {
            using Call = Player::NativeActor *(__thiscall *)(const NativeMission *, const char *);
            return reinterpret_cast<Call>(kFindActorByName)(this, name);
        }
        void ResetEvents() {
            _currentEvent = 0;
            _previousEvent = 0;
        }

        std::byte _unused00[0x10];
        SDK::Scene::NativeScene *_scene;
        std::byte _unused14[0x10];
        Game::NativeGame *_game;
        std::byte _unused28[0x48];
        int _currentEvent;
        int _previousEvent;
    };
    static_assert(sizeof(NativeMission) == 0x78);
    static_assert(offsetof(NativeMission, _scene) == 0x10);
    static_assert(offsetof(NativeMission, _game) == 0x24);
    static_assert(offsetof(NativeMission, _currentEvent) == 0x70);
    static_assert(offsetof(NativeMission, _previousEvent) == 0x74);

    inline NativeMission *Get() {
        return *reinterpret_cast<NativeMission **>(kGlobalMissionPointer);
    }

    inline Game::NativeGame *GetGame(const void *mission) {
        return static_cast<const NativeMission *>(mission)->Game();
    }

    inline int Open(void *mission, const char *name) {
        using Call = int(__thiscall *)(void *, const char *, bool, uint32_t, bool);
        return reinterpret_cast<Call>(kOpen)(mission, name, false, 0xffffffffu, true);
    }

    inline void Close(void *mission) {
        using Call = void(__thiscall *)(void *);
        reinterpret_cast<Call>(kClose)(mission);
    }

    inline void ResetEvents(void *mission) {
        static_cast<NativeMission *>(mission)->ResetEvents();
    }
} // namespace Mafia1Online::SDK::Core::Mission
