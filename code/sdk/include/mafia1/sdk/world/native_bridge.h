#pragma once

#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/sound/native_sound.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::World {
    // C_sound_table::ConstructSound(I3D_sound*, unsigned id) on g_SoundManager:
    // loads a random wave of the sound table entry into the sound, ret 8.
    inline constexpr uintptr_t kConstructSound = 0x55f820;
    inline constexpr uintptr_t kSoundManager = 0x646f50;
    // g_collision::DynInsert / DynErase(tDynamicCollObject*) on g_pCollision
    // (0x647f48), ret 4. Insert returns while the object already has a grid
    // cell (+0x0c) and Erase while it has none, so both are idempotent.
    inline constexpr uintptr_t kDynInsert = 0x5c36c0;
    inline constexpr uintptr_t kDynErase = 0x5c38b0;
    inline constexpr uintptr_t kCollision = 0x647f48;

    // reM S_bridge_runtime_entry (0x24): one of the two steamers (lodriver.i3d
    // "most_parnik") the right bridge moves along its four routes in Update.
    // Their position is the segment index plus the progress within it; a
    // speed of 0 keeps Update from advancing or wrapping them (it divides the
    // progress past 1 by the step).
    struct NativeBridgeSteamer {
        uint32_t route;
        uint32_t point;
        uint32_t previousPoint;
        float progress;
        Scene::NativeFrame *model;
        float speed;
        std::byte _unused18[0x0c];
    };
    static_assert(sizeof(NativeBridgeSteamer) == 0x24);
    static_assert(offsetof(NativeBridgeSteamer, progress) == 0x0c);
    static_assert(offsetof(NativeBridgeSteamer, speed) == 0x14);

    // reM S_bridge_runtime at 0x646f80 (g_sBridgeRuntime), shared by both
    // bridges: AI writes bridgeOpen[side], Update rebuilds bridgeRequests from
    // the steamers in its request zones and stops them at a closed bridge.
    struct NativeBridgeRuntime {
        static constexpr uintptr_t kAddress = 0x646f80;
        static constexpr uint32_t kSteamerCount = 2;
        static constexpr uint32_t kRouteCount = 4;
        // Six segments between the seven points of each route.
        static constexpr uint32_t kSegmentCount = 6;

        int32_t reservations[5];
        int32_t bridgeOpen[2];
        int32_t bridgeRequests[2];
        int32_t _unused24;
        NativeBridgeSteamer steamers[kSteamerCount];

        static NativeBridgeRuntime &Get() {
            return *reinterpret_cast<NativeBridgeRuntime *>(kAddress);
        }

        // reM g_aBridgeSegmentLengths[4][6] at 0x646ff0: each segment's length
        // in metres, filled by GameInit; Update advances the progress by
        // speed / length.
        static constexpr uintptr_t kSegmentLengths = 0x646ff0;

        static float SegmentLength(uint32_t route, uint32_t point) {
            return reinterpret_cast<const float(*)[kSegmentCount]>(kSegmentLengths)[route][point];
        }
    };
    static_assert(offsetof(NativeBridgeRuntime, bridgeOpen) == 0x14);
    static_assert(offsetof(NativeBridgeRuntime, bridgeRequests) == 0x1c);
    static_assert(offsetof(NativeBridgeRuntime, steamers) == 0x28);

    // A drawbridge (scene2.bin type 20, reM C_bridge, vtable 0x625588): two
    // leaves, its own frame (C_actor +0x68) and the counterpart named with the
    // last character incremented (+0x90). C_actor::Tick (0x4064f0) runs AI
    // (vtable +0x34, 0x567fb0), the phase machine that turns the leaves, or,
    // network controlled, the empty NetDirect (+0x3c, 0x4ab130); then Update
    // (+0x38, 0x568540) moves the leaves' collision primitives with their
    // frames, blinks the signal lights and, on the right bridge, moves the
    // steamers. GameInit (+0x64) builds the sounds, the dynamic collision and,
    // on the right bridge only, the steamers, which it then warms up by
    // running AI and Update 60 times for a second each; GameDone (+0x68)
    // releases them.
    struct NativeBridge {
        // reM E_bridge_phase, with its timer at +0x150: closed until a steamer
        // requests the bridge (AI counts the 120 s closed timer down but never
        // tests it), signaling (4 s) until no traffic car is on it, preparing
        // until its sound ends (at once without a sound driver: IsPlaying,
        // LS3DF 0x10057c40, is false with no voice), opening (30 s), open
        // (10 s, longer while requested), closing (20 s).
        enum class Phase : int32_t {
            Closed = 0,
            Signaling = 1,
            PreparingToOpen = 2,
            Opening = 3,
            Open = 4,
            Closing = 5,
        };
        static constexpr int32_t kPhaseCount = 6;
        // E_bridge_traffic_signal_phase at +0x138: traffic reads it as the
        // bridge road's semaphore axis (GetTrafficAxis).
        static constexpr int32_t kTrafficSignalRed = 1;
        static constexpr int32_t kTrafficSignalGreen = 7;
        // AI restarts the signal light timer (+0x154) at 600 ms on signaling.
        static constexpr int32_t kSignalIntervalMs = 600;

        static constexpr uint32_t kActorType = 20;
        static constexpr uintptr_t kAI = 0x567fb0;
        static constexpr uintptr_t kUpdate = 0x568540;
        // void __thiscall(C_bridge*), plain ret.
        static constexpr uintptr_t kGameInit = 0x569510;
        static constexpr uintptr_t kGameDone = 0x56a600;
        // bool __thiscall(C_bridge*, C_actor*, const S_vector &position,
        // const S_vector &direction, const S_vector &, float, I3D_frame*),
        // ret 18h; vtable +0x78. Kills a player (actor type 2) it runs into.
        static constexpr uintptr_t kCollision = 0x56a7e0;

        static constexpr size_t kCounterpartFrameOffset = 0x90;
        static constexpr size_t kMotionSoundOffset = 0x94;
        static constexpr size_t kRequestSoundOffset = 0x98;
        // tDynamicCollObject* over watercity.nadparnik1/2; AI inserts it as
        // the leaves start opening and erases it once closed.
        static constexpr size_t kDynamicCollisionOffset = 0xb4;
        static constexpr size_t kTrafficSignalOffset = 0x138;
        static constexpr size_t kSideOffset = 0x140;
        static constexpr size_t kPhaseOffset = 0x144;
        static constexpr size_t kTargetAngleOffset = 0x148;
        static constexpr size_t kCurrentAngleOffset = 0x14c;
        static constexpr size_t kPhaseTimerOffset = 0x150;
        static constexpr size_t kSignalTimerOffset = 0x154;
        static constexpr size_t kReverseDirectionOffset = 0x174;
        static constexpr size_t kOpenAngleOffset = 0x178;
        // Sound table ids AI constructs into the motion sound per phase, and
        // the signal sound into the request sound.
        static constexpr size_t kOpeningSoundOffset = 0x17c;
        static constexpr size_t kOpenedSoundOffset = 0x180;
        static constexpr size_t kClosingSoundOffset = 0x184;
        static constexpr size_t kClosedSoundOffset = 0x188;
        static constexpr size_t kPrepareSoundOffset = 0x18c;
        static constexpr size_t kSignalSoundOffset = 0x190;

        [[nodiscard]] Player::NativeActor &Actor() {
            return *reinterpret_cast<Player::NativeActor *>(this);
        }
        // The frame's first character 'L' makes the left bridge (side 0);
        // the right one (1) moves the steamers.
        [[nodiscard]] bool IsRight() const {
            return Field<int32_t>(kSideOffset) != 0;
        }
        [[nodiscard]] Phase GetPhase() const {
            return Field<Phase>(kPhaseOffset);
        }
        [[nodiscard]] int32_t PhaseTimer() const {
            return Field<int32_t>(kPhaseTimerOffset);
        }
        // Radians, 0 closed to the open angle (+0x178, 50 degrees).
        [[nodiscard]] float Angle() const {
            return Field<float>(kTargetAngleOffset);
        }
        [[nodiscard]] float OpenAngle() const {
            return Field<float>(kOpenAngleOffset);
        }

        // What AI does on entering a phase, without its timers' decisions:
        // the sounds, the road signal, the light timer, the runtime open flag
        // and the dynamic collision. Sounds that play once are played only
        // when the phase follows the previous one, as AI would have.
        void EnterPhase(Phase phase) {
            const Phase from = GetPhase();
            const bool next = static_cast<int32_t>(phase) == (static_cast<int32_t>(from) + 1) % kPhaseCount;
            auto *motion = Field<Sound::NativeSound *>(kMotionSoundOffset);
            auto *request = Field<Sound::NativeSound *>(kRequestSoundOffset);
            motion->vtable->setOnUpdate(motion, false, true);
            switch (phase) {
            case Phase::PreparingToOpen:
                if (next) {
                    Construct(motion, kPrepareSoundOffset);
                    motion->SetLoop(false);
                    motion->vtable->setOnUpdate(motion, true, true);
                }
                break;
            case Phase::Opening:
            case Phase::Closing:
                Construct(motion, phase == Phase::Opening ? kOpeningSoundOffset : kClosingSoundOffset);
                motion->SetLoop(true);
                motion->Frame()->SetOn(true);
                break;
            case Phase::Open:
            case Phase::Closed:
                if (next) {
                    Construct(motion, phase == Phase::Open ? kOpenedSoundOffset : kClosedSoundOffset);
                    motion->SetLoop(false);
                    motion->Frame()->SetOn(true);
                }
                break;
            default: break;
            }
            // Signaling starts the request bell; it rings until closed.
            if (phase == Phase::Closed) {
                request->Frame()->SetOn(false);
            }
            else if (from == Phase::Closed) {
                request->vtable->setOnUpdate(request, false, true);
                Construct(request, kSignalSoundOffset);
                request->SetLoop(true);
                request->Frame()->SetOn(true);
                // Update blinks the lights while it runs, and turns them off
                // and stops once closed.
                Field<int32_t>(kSignalTimerOffset) = kSignalIntervalMs;
            }
            Field<int32_t>(kTrafficSignalOffset) = phase == Phase::Closed ? kTrafficSignalGreen : kTrafficSignalRed;
            if (void *collision = Field<void *>(kDynamicCollisionOffset)) {
                const bool raised = phase >= Phase::Opening && phase <= Phase::Closing;
                using Call = void(__thiscall *)(void *, void *);
                // Qualified: the class's own kCollision (the hook target) hides
                // the namespace's g_collision instance.
                reinterpret_cast<Call>(raised ? kDynInsert : kDynErase)(reinterpret_cast<void *>(World::kCollision), collision);
            }
            Field<Phase>(kPhaseOffset) = phase;
        }

        // AI's open flag for the steamers' stops, which AI rewrites each tick.
        void SetRuntimeOpen(bool open) {
            NativeBridgeRuntime::Get().bridgeOpen[IsRight() ? 1 : 0] = open ? 1 : 0;
        }
        void SetPhaseTimer(int32_t timer) {
            Field<int32_t>(kPhaseTimerOffset) = timer;
        }

        // AI's tail: both leaves turn about their local z axis in opposite
        // directions (+0x174 swaps them), and Update then moves their
        // collision primitives.
        void PlaceLeaves(float angle) {
            Field<float>(kTargetAngleOffset) = angle;
            if (Field<float>(kCurrentAngleOffset) == angle) {
                return;
            }
            const float own = Field<bool>(kReverseDirectionOffset) ? angle : -angle;
            auto *frame = Actor().Frame();
            auto *counterpart = Field<Scene::NativeFrame *>(kCounterpartFrameOffset);
            frame->SetDirection({0.0f, 0.0f, 1.0f}, own);
            counterpart->SetDirection({0.0f, 0.0f, 1.0f}, -own);
            frame->Update();
            counterpart->Update();
            Field<float>(kCurrentAngleOffset) = angle;
        }

      private:
        void Construct(Sound::NativeSound *sound, size_t idOffset) {
            using Call = bool(__thiscall *)(void *, Sound::NativeSound *, uint32_t);
            reinterpret_cast<Call>(kConstructSound)(reinterpret_cast<void *>(kSoundManager), sound, Field<uint32_t>(idOffset));
        }

        template <typename T>
        T &Field(size_t offset) const {
            return *reinterpret_cast<T *>(reinterpret_cast<std::byte *>(const_cast<NativeBridge *>(this)) + offset);
        }
    };
} // namespace Mafia1Online::SDK::World
