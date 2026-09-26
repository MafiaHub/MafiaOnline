#pragma once

#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/player/native_camera.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace Mafia1Online::SDK::Core::Game {
    inline constexpr uintptr_t kTick                 = 0x5a51c0;
    inline constexpr uintptr_t kSetTrafficVisible    = 0x5a8470;
    inline constexpr uintptr_t kAddTemporaryActor    = 0x5a77c0;
    inline constexpr uintptr_t kRemoveTemporaryActor = 0x5a79a0;
    inline constexpr uintptr_t kNewShoot             = 0x5a84a0;
    inline constexpr uintptr_t kRandomFloat          = 0x408470;

    struct NativeShootRecord {
        Player::Vector3 startPosition;
        Player::Vector3 direction;
        float directionMagnitude;
        Player::Vector3 position;
        float damage;
        Player::NativeActor *actor;
        void *frame;
        void *collision;
        Player::Vector3 hitPosition;
        Player::Vector3 hitNormal;
        bool hit;
        std::byte _padding51[3];
        int32_t impactSoundId;
        uint8_t materialId;
        std::byte _padding59[3];
    };
    static_assert(sizeof(NativeShootRecord) == 0x5c);
    static_assert(offsetof(NativeShootRecord, direction) == 0x0c);
    static_assert(offsetof(NativeShootRecord, hitPosition) == 0x38);
    static_assert(offsetof(NativeShootRecord, hit) == 0x50);

    struct NativeShootQueue {
        uint32_t allocator;
        NativeShootRecord *begin;
        NativeShootRecord *end;
        NativeShootRecord *capacity;
    };
    static_assert(sizeof(NativeShootQueue) == 0x10);
    static_assert(offsetof(NativeShootQueue, begin) == 0x04);
    static_assert(offsetof(NativeShootQueue, end) == 0x08);
    static_assert(offsetof(NativeShootQueue, capacity) == 0x0c);

    // Borrowed retail C_game view, valid only while its C_mission is open and
    // initialized.
    struct NativeGame {
        enum class State : uint32_t {
            Running = 0,
            Exit    = 1,
            Restart = 2,
            Load    = 3,
        };

        [[nodiscard]] bool IsInitialized() const {
            return _initialized;
        }
        [[nodiscard]] State GetState() const {
            return _state;
        }
        [[nodiscard]] Player::NativeActor *Player() const {
            return _player;
        }
        [[nodiscard]] Player::NativeCamera &Camera() {
            return _camera;
        }
        [[nodiscard]] uint32_t StartupTickCount() const {
            // reM C_game layout: startup tick count at +0x2b14.
            return *reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(this) + 0x2b14);
        }
        void SetCameraRotRepair() {
            reinterpret_cast<void(__thiscall *)(NativeGame *)>(0x5ba010)(this);
        }
        void RecomputeLightCache() {
            reinterpret_cast<void(__thiscall *)(NativeGame *)>(0x5b5d40)(this);
        }
        [[nodiscard]] bool HasPlayerFocus(Player::NativeActor *actor) const {
            return _player == actor && _human == actor && _camera._player == actor;
        }
        [[nodiscard]] bool HasControlledHuman(Player::NativeActor *actor) const {
            return _player == actor && _human == actor;
        }
        [[nodiscard]] bool PlayerDeathTriggered() const {
            return _playerDeathTriggered;
        }
        // reM C_game::m_uGameTime, the millisecond clock melee timing reads.
        [[nodiscard]] uint32_t GameTime() const {
            return _gameTime;
        }
        [[nodiscard]] int PlayerDeathMenuTimer() const {
            return _playerDeathMenuTimer;
        }
        [[nodiscard]] std::span<NativeShootRecord> ShootRecords() {
            return _shootRecords.begin ? std::span<NativeShootRecord>(_shootRecords.begin, _shootRecords.end) : std::span<NativeShootRecord>();
        }
        void DiscardShootRecordsFrom(size_t count) {
            const auto records = ShootRecords();
            if (count <= records.size()) {
                _shootRecords.end = _shootRecords.begin ? _shootRecords.begin + count : nullptr;
            }
        }

        // C_game::Tick may open GM_Menu_gamefailed if either the trigger flag
        // or a dead player with an expired timer is present. Scripts own the
        // replacement respawn decision during a mod-managed mission.
        void ClearPlayerDeathTriggered() {
            _playerDeathTriggered = false;
        }
        void KeepPlayerDeathMenuPending() {
            _playerDeathMenuTimer = 7000;
        }
        void SetTrafficVisible(bool visible) {
            using Call = void(__thiscall *)(NativeGame *, bool);
            reinterpret_cast<Call>(kSetTrafficVisible)(this, visible);
        }
        void AddTemporaryActor(Player::NativeActor *actor) {
            using Call = void(__thiscall *)(NativeGame *, Player::NativeActor *);
            reinterpret_cast<Call>(kAddTemporaryActor)(this, actor);
        }
        void RemoveTemporaryActor(Player::NativeActor *actor) {
            using Call = void(__thiscall *)(NativeGame *, Player::NativeActor *);
            reinterpret_cast<Call>(kRemoveTemporaryActor)(this, actor);
        }
        // AddTemporaryActor invokes GameInit. Attach only after that call so
        // camera Begin sees an initialized human. The player pointer is set
        // first because C_human::GameInit tests it for local-only resources.
        void SetPlayerForInitialization(Player::NativeActor *actor) {
            _player = actor;
        }
        void FocusPlayer(Player::NativeActor *actor) {
            // C_game::SetHuman performs the retail human/camera handoff.
            using Call = void(__thiscall *)(NativeGame *, Player::NativeActor *);
            reinterpret_cast<Call>(0x5a07e0)(this, actor);
        }
        void ClearPlayer(Player::NativeActor *actor) {
            if (_camera._player == actor) {
                _camera.SetPlayer(nullptr);
            }
            if (_human == actor) {
                _human = nullptr;
            }
            if (_player == actor) {
                _player = nullptr;
            }
        }

        std::byte _unused00[0x40];
        bool _initialized;
        std::byte _unused41[3];
        void *_mission;
        State _state;
        Player::NativeCamera _camera;
        Player::NativeActor *_player;
        Player::NativeActor *_human;
        std::byte _unusedEC[0xe8];
        NativeShootQueue _shootRecords;
        std::byte _unused1e4[0x28f4];
        bool _playerDeathTriggered;
        std::byte _unused2ad9[0x2b0c - 0x2ad9];
        uint32_t _gameTime;
        std::byte _unused2b10[0x2fd4 - 0x2b10];
        int _playerDeathMenuTimer;
    };

    static_assert(sizeof(NativeGame) == 0x2fd8);
    static_assert(offsetof(NativeGame, _initialized) == 0x40);
    static_assert(offsetof(NativeGame, _mission) == 0x44);
    static_assert(offsetof(NativeGame, _state) == 0x48);
    static_assert(offsetof(NativeGame, _player) == 0xe4);
    static_assert(offsetof(NativeGame, _human) == 0xe8);
    static_assert(offsetof(NativeGame, _shootRecords) == 0x1d4);
    static_assert(offsetof(NativeGame, _playerDeathTriggered) == 0x2ad8);
    static_assert(offsetof(NativeGame, _gameTime) == 0x2b0c);
    static_assert(offsetof(NativeGame, _playerDeathMenuTimer) == 0x2fd4);
} // namespace Mafia1Online::SDK::Core::Game
