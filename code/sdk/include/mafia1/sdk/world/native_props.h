#pragma once

#include <mafia1/sdk/collision/native_collision.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>

namespace Mafia1Online::SDK::World {
    // Breakable scene models. A car body (Collision_Filter_Body 0x426340),
    // a bullet (TickShoot 0x5aab7e), an explosion (NewExplosion 0x5ab509)
    // or a blunt melee hit (C_human::AI 0x571096) that meets a collision
    // primitive of the crash material calls SpawnCrashActorFromFrame:
    // __fastcall(I3D_frame *struck, const S_vector *origin, const S_vector
    // *impulse, float force), ret 8. It walks up to the struck frame's
    // FRAME_MODEL, takes it out of C_game's semaphores (DelSemaphore 0x5b6d50
    // matches S_semaphore's first field, which the loader 0x5ad900 never
    // writes) and telephones, sets
    // COLLISION_LINE_QUERY_FLAG on every collision primitive of the model's
    // subtree (0x430bb0) so nothing collides with it any more, builds the
    // "<model>_c1.i3d" pieces (CreateCrashActors), appends the model to
    // C_game::m_ObjectsToDeactivate, turns it off and drops its shadows.
    // C_game::Done (0x5a3c60) turns that list back on; nothing clears the
    // primitives' flag again.
    inline constexpr uintptr_t kSpawnCrashActorFromFrame = 0x431710;
    using SpawnCrashActorFromFrameCall = bool(__fastcall *)(Scene::NativeFrame *, const Player::Vector3 *, const Player::Vector3 *, float);
    // __fastcall(I3D_frame *model, S_vector &origin, S_vector &impulse, float
    // force, char *modelName, bool numbered, unsigned char modelIndex, int
    // parentLimit, bool activateAll, bool forwardImpact), ret 0x20. Each
    // visual of the crash model becomes a temporary C_crash_obj; a piece
    // whose properties carry 'H' starts a hydrant jet (C_game::AddHydrant).
    // Its result is ignored by SpawnCrashActorFromFrame.
    inline constexpr uintptr_t kCreateCrashActors = 0x430c50;
    using CreateCrashActorsCall = bool(__fastcall *)(Scene::NativeFrame *, Player::Vector3 *, Player::Vector3 *, float, char *, bool, uint8_t, int, bool, bool);

    // g_collision_header::m_uPackedValue: type byte 0, material byte 3.
    // Collision_Filter_Body compares the material with 41 (0x42634d).
    inline constexpr uint8_t kCrashMaterial = 41;
    inline constexpr uint32_t kCollisionPrimitiveOff = 0x20;
    inline constexpr uint32_t kFrameModel = 9;
    // ApplySegmentFrames names the city cache's pooled model instances
    // "off<group>seg<instance>"; one stands for a different placement each
    // time its segment streams in.
    inline bool IsCachePoolName(const char *name) {
        if (!name || name[0] != 'o' || name[1] != 'f' || name[2] != 'f') {
            return false;
        }
        const char *c = name + 3;
        const char *digits = c;
        while (*c >= '0' && *c <= '9') {
            ++c;
        }
        if (c == digits || c[0] != 's' || c[1] != 'e' || c[2] != 'g') {
            return false;
        }
        c += 3;
        digits = c;
        while (*c >= '0' && *c <= '9') {
            ++c;
        }
        return c != digits && *c == 0;
    }

    // I3D_frame::m_iNumChildren (+0x11c) and GetChild (vtable +0x30, as
    // mark_frame_collision_prims_recursive 0x430bb0 calls it).
    inline int32_t FrameChildCount(const Scene::NativeFrame *frame) {
        return *reinterpret_cast<const int32_t *>(reinterpret_cast<const std::byte *>(frame) + 0x11c);
    }
    inline Scene::NativeFrame *FrameChild(Scene::NativeFrame *frame, int32_t index) {
        using Call = Scene::NativeFrame *(__stdcall *)(Scene::NativeFrame *, int32_t);
        return reinterpret_cast<Call *>(frame->vtable)[0x30 / sizeof(void *)](frame, index);
    }
    // The first frame of that name in the subtree, frame included.
    inline Scene::NativeFrame *FindInSubtree(Scene::NativeFrame *frame, const char *name) {
        if (std::strcmp(frame->Name(), name) == 0) {
            return frame;
        }
        for (int32_t child = FrameChildCount(frame); child > 0;) {
            if (auto *next = FrameChild(frame, --child)) {
                if (auto *found = FindInSubtree(next, name)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    // g_collision::BuildPrimsByFrame (0x5ca5c0): __thiscall(g_collision*,
    // I3D_frame*, std::vector<g_collision_header*>*), the static primitives
    // linked to that one frame. The VC6 vector grows with the game's
    // operator new; 0x430bb0 frees it with the game's operator delete.
    inline constexpr uintptr_t kBuildPrimsByFrame = 0x5ca5c0;
    inline constexpr uintptr_t kOperatorDelete = 0x60fcf6;
    struct NativePrimitiveVector {
        uint32_t allocator = 0;
        uint32_t **first = nullptr;
        uint32_t **last = nullptr;
        uint32_t **end = nullptr;
    };
    static_assert(sizeof(NativePrimitiveVector) == 0x10);

    // Each static collision primitive's packed type/flags/material word, for
    // the frame and its whole subtree, as 0x430bb0 walks it.
    template <typename F>
    void ForEachCollisionPrimitive(Scene::NativeFrame *frame, F &&visit) {
        NativePrimitiveVector primitives;
        using Build = int(__thiscall *)(void *, Scene::NativeFrame *, NativePrimitiveVector *);
        const int count = reinterpret_cast<Build>(kBuildPrimsByFrame)(reinterpret_cast<void *>(Collision::kCollision), frame, &primitives);
        for (int i = 0; i < count; ++i) {
            visit(primitives.first[i]);
        }
        reinterpret_cast<void(__cdecl *)(void *)>(kOperatorDelete)(primitives.first);
        for (int32_t child = FrameChildCount(frame); child > 0;) {
            if (auto *next = FrameChild(frame, --child)) {
                ForEachCollisionPrimitive(next, visit);
            }
        }
    }
    inline uint8_t PrimitiveMaterial(const uint32_t *primitive) {
        return static_cast<uint8_t>(*primitive >> 24);
    }

    // C_game's lists the crash code touches.
    struct NativeCrashWorld {
        // m_ObjectsToDeactivate: a VC6 vector of I3D_frame* at +0x10
        // (_First +0x14, _Last +0x18), appended to at 0x431857.
        static constexpr size_t kDeactivatedFirstOffset = 0x14;
        static constexpr size_t kDeactivatedLastOffset = 0x18;
        // m_TemporaryActors (+0x120: _First +0x124, _Last +0x128), as
        // InternTempActAdd (0x5a7b70) reads it.
        static constexpr size_t kTemporaryFirstOffset = 0x124;
        static constexpr size_t kTemporaryLastOffset = 0x128;
        // __thiscall(C_game*, C_actor*, bool), as C_crash_obj::AI drops a
        // piece (0x431be0) before RemoveTemporaryActor.
        static constexpr uintptr_t kInvalidateActor = 0x5afe40;
        // __thiscall(C_game*): destroys every hydrant jet's particle and
        // sound (m_Hydrants +0x2c4) and empties the list.
        static constexpr uintptr_t kDelAllHydrants = 0x5b9ef0;
        // __thiscall(C_game*): rebuilds m_Telephones (+0x1c4) from the
        // scene's PHbox01/alarm01 models and the city cache, as GameInit
        // does. Police AI bounds-checks and rescans the indices it holds.
        static constexpr uintptr_t kLoadTelephones = 0x5b6e00;
        // __thiscall(C_game*): rebuilds m_Semaphores (+0x1a8) from the
        // primary sector's semafor.i3d models, as C_game::Init (0x5a1fd7)
        // does; TickSemaphores lights them from the shared phase fields.
        static constexpr uintptr_t kLoadSemaphores = 0x5add10;
        static constexpr uint32_t kCrashObjectType = 26;

        Core::Game::NativeGame *game;

        [[nodiscard]] std::span<Scene::NativeFrame *> Deactivated() const {
            auto *first = Field<Scene::NativeFrame **>(kDeactivatedFirstOffset);
            auto *last = Field<Scene::NativeFrame **>(kDeactivatedLastOffset);
            return first ? std::span<Scene::NativeFrame *>(first, last) : std::span<Scene::NativeFrame *>();
        }
        // Keeps the first count entries and the storage, as erase() does
        // for a vector of pointers.
        void TruncateDeactivated(size_t count) const {
            if (count <= Deactivated().size()) {
                Field<Scene::NativeFrame **>(kDeactivatedLastOffset) = Field<Scene::NativeFrame **>(kDeactivatedFirstOffset) + count;
            }
        }
        [[nodiscard]] std::span<Player::NativeActor *> TemporaryActors() const {
            auto *first = Field<Player::NativeActor **>(kTemporaryFirstOffset);
            auto *last = Field<Player::NativeActor **>(kTemporaryLastOffset);
            return first ? std::span<Player::NativeActor *>(first, last) : std::span<Player::NativeActor *>();
        }
        void InvalidateActor(Player::NativeActor *actor) const {
            reinterpret_cast<void(__thiscall *)(Core::Game::NativeGame *, Player::NativeActor *, bool)>(kInvalidateActor)(game, actor, false);
        }
        void DelAllHydrants() const {
            reinterpret_cast<void(__thiscall *)(Core::Game::NativeGame *)>(kDelAllHydrants)(game);
        }
        void LoadTelephones() const {
            reinterpret_cast<void(__thiscall *)(Core::Game::NativeGame *)>(kLoadTelephones)(game);
        }
        void LoadSemaphores() const {
            reinterpret_cast<int(__thiscall *)(Core::Game::NativeGame *)>(kLoadSemaphores)(game);
        }

      private:
        template <typename T>
        T &Field(size_t offset) const {
            return *reinterpret_cast<T *>(reinterpret_cast<std::byte *>(game) + offset);
        }
    };

    // A C_crash_obj piece (reM, 0x3c0 bytes): m_uCrashFlags (+0x38c) from
    // its ';...;' properties and m_fHealth (+0x390) from 'X'. Once a
    // damageable piece's health is below 0, C_crash_obj::AI (0x431be0) calls
    // NewFire(position, 0, 5000, 2, 100, true) for 'F' and NewExplosion(this,
    // position, 10, 50, true, false, true, 18) for 'E', then drops the
    // piece. CreateActor (0x4311d0) takes 2 off a new piece's health, so the
    // ';M1000N22X1EF;' piece of the stock fuel pump (xpumpa 2) goes off on
    // its first tick.
    struct NativeCrashPiece {
        static constexpr uint32_t kDamageable = 0x1;
        static constexpr uint32_t kStartFire = 0x8;
        static constexpr uint32_t kExplode = 0x20;
        static constexpr float kExplosionRadius = 10.0f;
        static constexpr int32_t kFireLifeMs = 5000;
        static constexpr float kFireRadius = 2.0f;

        [[nodiscard]] Player::NativeActor &Actor() {
            return *reinterpret_cast<Player::NativeActor *>(this);
        }
        [[nodiscard]] uint32_t &Flags() {
            return *reinterpret_cast<uint32_t *>(reinterpret_cast<std::byte *>(this) + 0x38c);
        }
        [[nodiscard]] bool GoesOff() {
            return (Flags() & kDamageable) && *reinterpret_cast<const float *>(reinterpret_cast<std::byte *>(this) + 0x390) < 0.0f;
        }
    };

    // A scene2.bin type 35 actor (reM C_box_new : C_PhThing, vtable
    // 0x6254f0): a box, railing, mail box or can. C_actor::Tick runs AI
    // (MoveBox/MoveWheel) and Update, which keeps the frame, the dynamic
    // collision and the sleep bookkeeping; a resting one goes inactive and
    // is not ticked at all.
    struct NativePhysicalProp {
        static constexpr uint32_t kActorType = 35;
        // void __thiscall(C_box_new*), vtable +0x64 and +0x68.
        static constexpr uintptr_t kGameInit = 0x567650;
        static constexpr uintptr_t kGameDone = 0x567720;
        // void __thiscall(C_PhThing*): its matrices, m_vPosition (+0x24),
        // m_vDirection (+0x30), m_vWorldRight (+0xa0) and m_vWorldUp (+0xac)
        // from the frame's world matrix.
        static constexpr uintptr_t kUpdateImportantVariables = 0x460220;
        // void __thiscall(g_collision*, tDynamicCollObject*), as Update
        // calls it (0x45abcb).
        static constexpr uintptr_t kDynUpdate = 0x5c3ac0;
        // S_matrix::SetDir3 (__stdcall import thunk), as MoveCount builds the
        // local rotation (0x45ddb0).
        static constexpr uintptr_t kMatrixSetDir3 = 0x60fc90;
        // What wakes a sleeping prop (vtable 0x6254f0 +0x78 and +0x7c; each
        // sets m_unusedWasHit and jumps to C_PhThing's, which sets it
        // ACTIVE and restarts its sleep timers). bool __thiscall(C_box_new*,
        // C_actor* actor, const S_vector&, const S_vector&, const S_vector&,
        // float, I3D_frame*), ret 0x18: a car (C_car.cpp), a human or a
        // railway pushing it. bool __thiscall(C_box_new*, E_hit_type, const
        // S_vector&, const S_vector&, const S_vector&, float, C_actor*
        // attacker, unsigned, I3D_frame*), ret 0x20: a shot, blow or blast.
        static constexpr uintptr_t kCollision = 0x567c80;
        static constexpr uintptr_t kHit = 0x567ca0;
        // C_actor::SetActState, void __thiscall(C_actor*, int), ret 4: it
        // leaves DEAD (2) alone and marks C_game's actor lists for a rebuild.
        static constexpr uintptr_t kSetActState = 0x406da0;
        static constexpr int32_t kStateActive = 0;
        static constexpr int32_t kStateInactive = 1;
        static constexpr int32_t kStateDead = 2;
        // C_PhThing::AI (0x45a690) returns before any physics past either
        // limit; Update (0x45a7a0) then puts it INACTIVE.
        static constexpr uint32_t kStationaryLimitMs = 4000;

        static constexpr size_t kActorStateOffset = 0x1c;
        static constexpr size_t kStateChangedOffset = 0x75;
        static constexpr size_t kPreviousPositionOffset = 0x90;
        static constexpr size_t kStationaryMsOffset = 0x9c;
        static constexpr size_t kWorldUpOffset = 0xac;
        static constexpr size_t kDynamicCollisionOffset = 0xb8;
        // tDynamicCollObject::m_vPosition, which Update copies before DynUpdate.
        static constexpr size_t kDynamicPositionOffset = 0xc8;
        static constexpr size_t kLinearVelocityOffset = 0x220;
        static constexpr size_t kAngularVelocityOffset = 0x22c;
        static constexpr size_t kLowMotionSecondsOffset = 0x32c;
        static constexpr size_t kLowMotionLimitOffset = 0x330;
        // m_ActorEntries: a VC6 vector of 0x14-byte S_PhThing_actor_entry,
        // _First +0x380, _Last +0x384 (FindActors 0x45a5c8).
        static constexpr size_t kActorEntriesFirstOffset = 0x380;
        static constexpr size_t kActorEntriesLastOffset = 0x384;

        // I3D_frame transform flags MoveCount rewrites (0x45ddb0): it
        // clears LMAT_SCALED, ROT_VALID, LMAT_BUILT, WMAT_VALID, WBVOL_VALID
        // and CONSTRUCTED and sets CONSTRUCTED and LMAT_BUILT.
        static constexpr uint32_t kTransformFlags = ~0xbffffec3u;

        [[nodiscard]] Player::NativeActor &Actor() {
            return *reinterpret_cast<Player::NativeActor *>(this);
        }
        [[nodiscard]] const Player::Vector3 &Up() const {
            return Field<Player::Vector3>(kWorldUpOffset);
        }
        [[nodiscard]] float LinearSpeed() const {
            return Length(Field<Player::Vector3>(kLinearVelocityOffset));
        }
        [[nodiscard]] float AngularSpeed() const {
            return Length(Field<Player::Vector3>(kAngularVelocityOffset));
        }
        [[nodiscard]] bool IsDead() const {
            return Field<int32_t>(kActorStateOffset) == kStateDead;
        }
        // The native sleep: not ticked (INACTIVE), or past the limits AI
        // returns at. Update does not zero the velocity of a prop it puts
        // to sleep (reM C_PhThing.cpp Update), so speed says nothing here.
        [[nodiscard]] bool Asleep() const {
            const int32_t state = Field<int32_t>(kActorStateOffset);
            if (state == kStateDead) {
                return false;
            }
            return state == kStateInactive || Field<float>(kLowMotionSecondsOffset) > Field<float>(kLowMotionLimitOffset) ||
                   Field<uint32_t>(kStationaryMsOffset) > kStationaryLimitMs;
        }

        // The frame transform the mission placed it with, to put it back.
        struct Transform {
            float local[16] {};
            float scale[3] {};
            float rotation[4] {};
            uint32_t flags = 0;
        };
        [[nodiscard]] Transform SaveTransform() {
            auto *frame = Actor().Frame();
            Transform transform;
            std::copy(std::begin(frame->_localMatrix), std::end(frame->_localMatrix), transform.local);
            std::copy(std::begin(frame->_scale), std::end(frame->_scale), transform.scale);
            std::copy(std::begin(frame->_rotation), std::end(frame->_rotation), transform.rotation);
            transform.flags = frame->_flags;
            return transform;
        }
        void RestoreTransform(const Transform &transform) {
            auto *frame = Actor().Frame();
            std::copy(std::begin(transform.local), std::end(transform.local), frame->_localMatrix);
            std::copy(std::begin(transform.scale), std::end(transform.scale), frame->_scale);
            std::copy(std::begin(transform.rotation), std::end(transform.rotation), frame->_rotation);
            // The world matrix and bound are rebuilt from the restored local
            // transform.
            constexpr uint32_t kRebuilt = Scene::kFrameWorldMatrixValid | Scene::kFrameWorldBoundValid;
            frame->_flags = (frame->_flags & ~kTransformFlags) | (transform.flags & kTransformFlags & ~kRebuilt);
            Settle();
        }

        // Places it as MoveCount does (local position unless
        // FRMFLAGS_POS_LOCKED, then SetDir3), at rest.
        void Place(const Player::Vector3 &position, const Player::Vector3 &direction, const Player::Vector3 &up) {
            auto *frame = Actor().Frame();
            if (!(frame->_flags & Scene::kFramePositionLocked)) {
                frame->_localMatrix[12] = position.x;
                frame->_localMatrix[13] = position.y;
                frame->_localMatrix[14] = position.z;
            }
            using SetDir3 = void(__stdcall *)(float *, const Player::Vector3 *, const Player::Vector3 *);
            reinterpret_cast<SetDir3>(kMatrixSetDir3)(frame->_localMatrix, &direction, &up);
            frame->_flags = (frame->_flags & ~kTransformFlags) | Scene::kFrameConstructed | Scene::kFrameLocalMatrixBuilt;
            Settle();
        }

      private:
        // What C_PhThing::Update does with the frame after the physics step
        // (0x45ab6b-0x45abcb), with the motion stopped, so a resting prop's
        // collision follows it too; then asleep as FindActors (0x45a560)
        // leaves a new piece: INACTIVE, or, with actors on it, ACTIVE past
        // both sleep limits (m_bStateChanged cleared, so ChangeState keeps
        // the timers). Until something wakes it, it stays where it was put.
        void Settle() {
            auto *frame = Actor().Frame();
            Field<Player::Vector3>(kLinearVelocityOffset) = {};
            Field<Player::Vector3>(kAngularVelocityOffset) = {};
            frame->Update();
            Core::Mission::Get()->GetScene()->SetFrameSectorPos(frame, frame->WorldPosition());
            reinterpret_cast<void(__thiscall *)(NativePhysicalProp *)>(kUpdateImportantVariables)(this);
            const auto position = Actor().Position();
            Field<Player::Vector3>(kPreviousPositionOffset) = position;
            Field<Player::Vector3>(kDynamicPositionOffset) = position;
            using DynUpdate = void(__thiscall *)(void *, void *);
            reinterpret_cast<DynUpdate>(kDynUpdate)(reinterpret_cast<void *>(Collision::kCollision), reinterpret_cast<std::byte *>(this) + kDynamicCollisionOffset);
            using SetActState = void(__thiscall *)(NativePhysicalProp *, int32_t);
            if (Field<std::byte *>(kActorEntriesFirstOffset) != Field<std::byte *>(kActorEntriesLastOffset)) {
                Field<float>(kLowMotionSecondsOffset) = Field<float>(kLowMotionLimitOffset) + 1.0f;
                Field<uint32_t>(kStationaryMsOffset) = kStationaryLimitMs + 1;
                Field<bool>(kStateChangedOffset) = false;
                reinterpret_cast<SetActState>(kSetActState)(this, kStateActive);
            } else {
                Field<uint32_t>(kStationaryMsOffset) = 0;
                Field<float>(kLowMotionSecondsOffset) = 0.0f;
                reinterpret_cast<SetActState>(kSetActState)(this, kStateInactive);
            }
        }
        static float Length(const Player::Vector3 &v) {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }
        template <typename T>
        T &Field(size_t offset) const {
            return *reinterpret_cast<T *>(reinterpret_cast<std::byte *>(const_cast<NativePhysicalProp *>(this)) + offset);
        }
    };
} // namespace Mafia1Online::SDK::World
