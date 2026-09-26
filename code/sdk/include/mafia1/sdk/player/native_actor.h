#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Mafia1Online::SDK::Player {
    inline constexpr uintptr_t kActorDestructor = 0x405d90;

    struct Vector3 {
        float x;
        float y;
        float z;
    };
    static_assert(sizeof(Vector3) == 12);

    struct NativeActor;
    struct NativeActorVTable {
        void *_unused00[0x44 / sizeof(void *)];
        uint16_t(__thiscall *release)(NativeActor *);
        bool(__thiscall *initialize)(NativeActor *, Scene::NativeFrame *);
    };
    static_assert(offsetof(NativeActorVTable, release) == 0x44);
    static_assert(offsetof(NativeActorVTable, initialize) == 0x48);

    // Borrowed retail C_actor view. The owning C_mission/C_game can destroy it
    // during mission close, actor removal, or shutdown; never retain it there.
    struct NativeActor {
        enum class Type : uint32_t {
            Player = 2,
            Car    = 4,
            Door   = 6,
            DropOut = 17,
            Entity = 27,
        };

        [[nodiscard]] Type GetType() const {
            return _type;
        }
        [[nodiscard]] bool IsPlayer() const {
            return _type == Type::Player;
        }
        [[nodiscard]] bool IsAlive() const {
            return _alive;
        }
        [[nodiscard]] bool IsDead() const {
            return _dead;
        }
        [[nodiscard]] bool IsNetworkControlled() const {
            return _networkControlled;
        }
        [[nodiscard]] const Vector3 &Position() const {
            return _position;
        }
        [[nodiscard]] const Vector3 &Direction() const {
            return _direction;
        }
        [[nodiscard]] Scene::NativeFrame *Frame() const {
            return _frame;
        }

        // These virtual calls are valid only for an actor created by the
        // mission factory while the matching native mission remains open.
        bool Initialize(Scene::NativeFrame *frame) {
            return static_cast<NativeActorVTable *>(_vtable)->initialize(this, frame);
        }
        uint16_t Release() {
            return static_cast<NativeActorVTable *>(_vtable)->release(this);
        }

        void SetNetworkControlled(bool controlled) {
            _networkControlled = controlled;
        }
        void RestoreLivingState() {
            _alive = true;
            _dead  = false;
        }
        void SetStoredTransform(const Vector3 &position, const Vector3 &direction) {
            _position  = position;
            _direction = direction;
        }

        void *_vtable;
        std::byte _unused04[0x0c];
        Type _type;
        std::byte _unused14[0x08];
        int32_t _actorState;
        bool _networkControlled;
        std::byte _unused21[3];
        Vector3 _position;
        Vector3 _direction;
        std::byte _unused3c[0x20];
        bool _spawnFrameOn;
        bool _alive;
        bool _dead;
        bool _visible;
        std::byte _unused60[8];
        Scene::NativeFrame *_frame;
        bool _removeFrame;
        std::byte _unused6d[3];
    };

    static_assert(sizeof(void *) == 4, "NativeActor is a 32-bit retail Game.exe view");
    static_assert(sizeof(NativeActor) == 0x70);
    static_assert(offsetof(NativeActor, _type) == 0x10);
    static_assert(offsetof(NativeActor, _networkControlled) == 0x20);
    static_assert(offsetof(NativeActor, _actorState) == 0x1c);
    static_assert(offsetof(NativeActor, _position) == 0x24);
    static_assert(offsetof(NativeActor, _direction) == 0x30);
    static_assert(offsetof(NativeActor, _alive) == 0x5d);
    static_assert(offsetof(NativeActor, _dead) == 0x5e);
    static_assert(offsetof(NativeActor, _frame) == 0x68);
    static_assert(offsetof(NativeActor, _removeFrame) == 0x6c);
} // namespace Mafia1Online::SDK::Player
