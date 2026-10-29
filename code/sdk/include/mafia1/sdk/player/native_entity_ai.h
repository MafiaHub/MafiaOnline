#pragma once

#include <mafia1/sdk/player/native_actor.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Player {
    // The calls C_entity's own script commands make to drive its retail AI
    // (reM C_entity::Process 0x51fda0, checked against IDA). Each command
    // builds actions with C_entity::m_ActionManager (+0x1164), pushes them and
    // forces the AI into the idle state's full substate, where actionsTick
    // runs them:
    //   Entity_ActionFollow (0x520c04-0x520cdc): NewFollow(leader, distance,
    //     19, flags, 0, 0) as job, NewTurnTo(leader, follow uid) as idle,
    //     g_pAIFollowManager.AddFollower(this, leader), ForceAI(0, 1, 0, 0).
    //   EntityMoveToFrame (0x521f3d-0x521f67): NewMove(position, 0, 19, 2,
    //     radius, 0, 0) as job, ForceAI(0, 1, 0, 0).
    //   Entity_MoveToCar (0x52063f-0x520672): NewMoveToCar(car, 200, 0.3,
    //     seat, run, 19, 0, 0) as job, ForceAI(0, 1, 0, 0); the following
    //     Enemy_UseCar calls Use_Actor(car, 1, seat, 0) within 3 of a car
    //     going slower than 0.1 (reM C_entity.cpp 3431).
    //   Entity_CarMoveTo (0x520a36-0x520b04): C_sledovacka::AddCarMoveTo
    //     on the car the entity sits in, then ActionStatus until it arrives
    //     (1), fails (2) or is blocked (3); RemoveCar ends it.
    //   Entity_ActionFire (0x5213a3-0x5214ae): WeaponChoose, then in a car
    //     NewJob(NewFire(target, -1, flags, 0)), on foot NewJob(NewFollow(
    //     target, WeaponGetBestRange, 19, 8, 0, 0)), NewIdle(NewTurnTo),
    //     NewModifier(NewFire(target, -1, flags, follow uid)) and
    //     AddModifier(NewCarJump(1, follow uid)); ForceAI(0, 1, 0, 0). flags
    //     is ((m_uAIFlags & 0x8000) | 0x10000) >> 14.
    //   Entity_ActionsClear: m_ActionManager.Clear(false).
    // Every move action finds its way over the mission's pedestrian paths
    // (ai_wayfind::FindWaySHORTEST over check.bin); a point with no path
    // node in reach gets none. Retail walks a human straight to a point with
    // AI_human_goto_target (0x446470), the step ai_path::Tick and
    // C_entity::scriptTick (0x5236a6) take towards each waypoint once a
    // frame: it turns the human towards the point, Go_Front walks or runs it
    // with its own animation and collision, and on the last waypoint it
    // stands it on the point. The entity's own AI must not run meanwhile:
    // with no job, actionsTick stands it (Do_Breath) every frame. The entity
    // script's set_state SLEEP (DecodeParams 0x51c4a4) is ForceAI(idle, 2):
    // C_entity::AI then runs only the program and returns.
    // Every ai_action_manager call is __thiscall on the manager; ret sizes
    // are those of the retail functions.
    namespace EntityAi {
        inline constexpr uintptr_t kNewMove       = 0x524C80; // ret 1Ch
        inline constexpr uintptr_t kNewFollow     = 0x524D30; // ret 18h
        inline constexpr uintptr_t kNewTurnTo     = 0x524DC0; // ret 8
        inline constexpr uintptr_t kNewFire       = 0x524E20; // ret 10h
        inline constexpr uintptr_t kNewMoveToCar  = 0x525480; // ret 20h
        inline constexpr uintptr_t kNewCarJump    = 0x5263E0; // ret 8
        inline constexpr uintptr_t kAddJob        = 0x526AF0; // ret 4
        inline constexpr uintptr_t kAddModifier   = 0x526E50; // ret 4
        inline constexpr uintptr_t kAddIdle       = 0x5271B0; // ret 4
        inline constexpr uintptr_t kNewJob        = 0x526CA0; // ret 4
        inline constexpr uintptr_t kNewModifier   = 0x527000; // ret 4
        inline constexpr uintptr_t kNewIdle       = 0x527360; // ret 4
        inline constexpr uintptr_t kClear         = 0x524650; // ret 4
        // void __thiscall C_entity::ForceAI(state, substate, priority, float), ret 10h.
        inline constexpr uintptr_t kForceAI       = 0x523D30;
        // void __thiscall C_entity::WeaponChoose(), plain ret.
        inline constexpr uintptr_t kWeaponChoose  = 0x527C70;
        // bool __thiscall C_entity::WeaponChoose(unsigned itemId), ret 4.
        inline constexpr uintptr_t kWeaponChooseItem = 0x528370;
        // float __thiscall C_entity::WeaponGetBestRange(), fld [ecx+11D8h].
        inline constexpr uintptr_t kWeaponGetBestRange = 0x5285D0;
        // g_pAIFollowManager (0x646d6c): AddFollower(follower, leader) ret 8,
        // DelFollower(follower) ret 4.
        inline constexpr uintptr_t kFollowManager = 0x646D6C;
        inline constexpr uintptr_t kAddFollower   = 0x532320;
        inline constexpr uintptr_t kDelFollower   = 0x532190;
        // C_sledovacka at C_game +0x2b18: AddCarMoveTo(car, const S_vector &,
        // bool direct, bool span, unsigned flags) ret 14h, ActionStatus(car)
        // ret 4 (ARRIVED when the car is not in its list), RemoveCar(car)
        // ret 4. C_game::InvalidateActor (0x5afe40) drops a removed car's
        // record (VypicujMiToyotu) and every entity's actions and followships
        // aimed at a removed actor.
        inline constexpr uintptr_t kGameSledovacka = 0x2B18;
        inline constexpr uintptr_t kCarMoveTo      = 0x4A1220;
        inline constexpr uintptr_t kCarActionStatus = 0x4A8670;
        inline constexpr uintptr_t kCarRemove      = 0x4A1C30;
        inline constexpr uint32_t kCarActive  = 0;
        inline constexpr uint32_t kCarArrived = 1;
        // bool __thiscall G_Inventory::Insert(S_GameItem *, bool), ret 8, on
        // C_human +0x480; HUMAN_ADDWEAPON follows it with ChangeWeaponModel
        // and, for an entity, WeaponChoose(selected item).
        inline constexpr uintptr_t kInventoryInsert = 0x6092B0;

        // bool __fastcall AI_human_goto_target(C_human *, S_vector &target,
        // unsigned frameTime, bool run, unsigned stuckCounter, bool
        // finalWaypoint, bool turningNode), ret 14h; true while it moves.
        inline constexpr uintptr_t kGotoTarget    = 0x446470;

        // C_entity fields (reM C_entity.h layout asserts, IDA ForceAI).
        inline constexpr uintptr_t kProcessInterval = 0xAFC;
        inline constexpr uintptr_t kAiFlags         = 0xF08;
        inline constexpr uintptr_t kAiState         = 0xF40;
        inline constexpr uintptr_t kAiSubstate      = 0xF44;
        inline constexpr uintptr_t kActionManager   = 0x1164;
        // C_human fields: m_uBehavior (C_entity::GameInit maps it to the
        // behaviour class), m_InitialProperties (GameInit copies them into
        // m_Properties) and its health and morale/aggressivity.
        inline constexpr uintptr_t kHumanBehaviour      = 0x5FC;
        inline constexpr uintptr_t kInitialHealth       = 0x604;
        inline constexpr uintptr_t kInitialAggressivity = 0x620;
        inline constexpr uintptr_t kInitialMorale       = 0x63C;
        inline constexpr uintptr_t kHumanInventory      = 0x480;

        // reM E_human_behavior masks.
        inline constexpr uint32_t kBehaviourGuard        = 2;
        inline constexpr uint32_t kBehaviourReactsOnHit  = 8;
        inline constexpr uint32_t kBehaviourMovesAway    = 16;

        // ForceAI arguments every movement command uses (idle state, full
        // substate, default priority) and C_entity's script path type.
        inline constexpr uint32_t kStateIdle   = 0;
        inline constexpr uint32_t kSubstateFull = 1;
        inline constexpr uint32_t kSubstateSleep = 2;
        inline constexpr uint32_t kScriptPathType = 19;
        // ai_action_move m_uMoveState: run, arrive within the radius.
        inline constexpr uint32_t kMoveRun    = 0x01;
        inline constexpr uint32_t kMoveArrive = 0x02;
        // Entity_ActionFire's follow state (forced run).
        inline constexpr uint32_t kFireFollowState = 8;
        inline constexpr float kMoveToCarNear = 200.0f;
        inline constexpr float kMoveToCarFar  = 0.3f;

        struct Action {
            uint32_t type;
            uint32_t uid;
            uint32_t sourceJobUid;
            uint32_t parentJobUid;
            uint32_t startTime;
            uint32_t payloadSize;
            uint32_t state;
            uint32_t result;
            void *payload;
        };
        static_assert(sizeof(Action) == 0x24);

        // ai_action_manager: VC6 vectors of jobs, idles and modifiers
        // (allocator, first, last, end), then the uid sequence.
        struct ActionManager {
            void *jobsAllocator;
            Action **jobsFirst;
            Action **jobsLast;
            Action **jobsEnd;
            std::byte _idlesAndModifiers[0x20];
            uint32_t sequence;
        };
        static_assert(sizeof(ActionManager) == 0x34);

        // reM S_GameItem.
        struct GameItem {
            uint16_t itemId;
            uint16_t weaponEffectLatch;
            int32_t ammoLoaded;
            int32_t ammoHidden;
            void *usingObject;
        };
        static_assert(sizeof(GameItem) == 0x10);

        struct Vector {
            float x;
            float y;
            float z;
        };

        template <typename T>
        T &Field(NativeActor *entity, uintptr_t offset) {
            return *reinterpret_cast<T *>(reinterpret_cast<std::byte *>(entity) + offset);
        }

        inline ActionManager *Manager(NativeActor *entity) {
            return &Field<ActionManager>(entity, kActionManager);
        }

        // Whether the job of this uid is still in the job stack.
        inline bool HasJob(NativeActor *entity, uint32_t uid) {
            auto *manager = Manager(entity);
            for (auto **job = manager->jobsFirst; manager->jobsFirst && job < manager->jobsLast; ++job) {
                if (*job && (*job)->uid == uid) {
                    return true;
                }
            }
            return false;
        }

        inline Action *NewMove(NativeActor *entity, const Vector &position, uint32_t moveState, float radius) {
            using Call = Action *(__thiscall *)(ActionManager *, const Vector *, uint32_t, uint32_t, uint32_t, float, void *, uint32_t);
            return reinterpret_cast<Call>(kNewMove)(Manager(entity), &position, 0, kScriptPathType, moveState, radius, nullptr, 0);
        }
        inline Action *NewFollow(NativeActor *entity, NativeActor *leader, float distance, uint32_t followState) {
            using Call = Action *(__thiscall *)(ActionManager *, NativeActor *, float, uint32_t, uint32_t, void *, uint32_t);
            return reinterpret_cast<Call>(kNewFollow)(Manager(entity), leader, distance, kScriptPathType, followState, nullptr, 0);
        }
        inline Action *NewTurnTo(NativeActor *entity, NativeActor *target, uint32_t sourceJobUid) {
            using Call = Action *(__thiscall *)(ActionManager *, NativeActor *, uint32_t);
            return reinterpret_cast<Call>(kNewTurnTo)(Manager(entity), target, sourceJobUid);
        }
        inline Action *NewFire(NativeActor *entity, NativeActor *target, uint32_t fireFlags, uint32_t sourceJobUid) {
            using Call = Action *(__thiscall *)(ActionManager *, NativeActor *, float, uint32_t, uint32_t);
            return reinterpret_cast<Call>(kNewFire)(Manager(entity), target, -1.0f, fireFlags, sourceJobUid);
        }
        inline Action *NewMoveToCar(NativeActor *entity, NativeActor *car, uint32_t seat, bool run) {
            using Call = Action *(__thiscall *)(ActionManager *, NativeActor *, float, float, uint32_t, uint32_t, uint32_t, void *, uint32_t);
            return reinterpret_cast<Call>(kNewMoveToCar)(Manager(entity), car, kMoveToCarNear, kMoveToCarFar, seat, run ? 1u : 0u, kScriptPathType, nullptr, 0);
        }
        inline Action *NewCarJump(NativeActor *entity, uint32_t flags, uint32_t sourceJobUid) {
            using Call = Action *(__thiscall *)(ActionManager *, uint32_t, uint32_t);
            return reinterpret_cast<Call>(kNewCarJump)(Manager(entity), flags, sourceJobUid);
        }
        inline void Push(uintptr_t function, NativeActor *entity, Action *action) {
            using Call = void(__thiscall *)(ActionManager *, Action *);
            reinterpret_cast<Call>(function)(Manager(entity), action);
        }
        inline void ClearActions(NativeActor *entity) {
            using Call = void(__thiscall *)(ActionManager *, bool);
            reinterpret_cast<Call>(kClear)(Manager(entity), false);
        }
        // ForceAI does nothing while the entity's script process interval is
        // 0.75 or more (0x523d30); an entity without a program has it at 0
        // after its first C_program::Process.
        inline void ForceIdle(NativeActor *entity) {
            using Call = void(__thiscall *)(NativeActor *, uint32_t, uint32_t, uint32_t, float);
            reinterpret_cast<Call>(kForceAI)(entity, kStateIdle, kSubstateFull, 0, 0.0f);
        }
        // set_state SLEEP: only the entity's program runs; its AI leaves the
        // human alone.
        inline void Sleep(NativeActor *entity) {
            using Call = void(__thiscall *)(NativeActor *, uint32_t, uint32_t, uint32_t, float);
            reinterpret_cast<Call>(kForceAI)(entity, kStateIdle, kSubstateSleep, 0, 0.0f);
        }
        inline bool Sleeping(NativeActor *entity) {
            return Field<uint32_t>(entity, kAiState) == kStateIdle && Field<uint32_t>(entity, kAiSubstate) == kSubstateSleep;
        }
        // One frame's step towards target, as C_entity::scriptTick walks its
        // script path: false once it stands there.
        inline bool GotoTarget(NativeActor *human, const Vector &target, uint32_t frameTime, bool run) {
            using Call = bool(__fastcall *)(NativeActor *, Vector *, uint32_t, bool, uint32_t, bool, bool);
            // Retail writes the human's height into the target on arrival.
            Vector point = target;
            return reinterpret_cast<Call>(kGotoTarget)(human, &point, frameTime, run, 0, true, true);
        }
        // In the idle state's full substate, where actionsTick runs jobs.
        inline bool RunsActions(NativeActor *entity) {
            return Field<uint32_t>(entity, kAiState) == kStateIdle && Field<uint32_t>(entity, kAiSubstate) == kSubstateFull;
        }
        inline bool Idle(NativeActor *entity) {
            return Field<uint32_t>(entity, kAiState) == kStateIdle;
        }
        inline void WeaponChoose(NativeActor *entity) {
            reinterpret_cast<void(__thiscall *)(NativeActor *)>(kWeaponChoose)(entity);
        }
        inline void WeaponChoose(NativeActor *entity, uint32_t itemId) {
            reinterpret_cast<bool(__thiscall *)(NativeActor *, uint32_t)>(kWeaponChooseItem)(entity, itemId);
        }
        inline float WeaponBestRange(NativeActor *entity) {
            return reinterpret_cast<float(__thiscall *)(NativeActor *)>(kWeaponGetBestRange)(entity);
        }
        inline uint32_t FireFlags(NativeActor *entity) {
            return ((Field<uint32_t>(entity, kAiFlags) & 0x8000u) | 0x10000u) >> 14;
        }
        inline void AddFollower(NativeActor *follower, NativeActor *leader) {
            using Call = void(__thiscall *)(uintptr_t, NativeActor *, NativeActor *);
            reinterpret_cast<Call>(kAddFollower)(kFollowManager, follower, leader);
        }
        inline void DelFollower(NativeActor *follower) {
            using Call = void(__thiscall *)(uintptr_t, NativeActor *);
            reinterpret_cast<Call>(kDelFollower)(kFollowManager, follower);
        }
        inline bool CarMoveTo(void *game, NativeActor *car, const Vector &target) {
            using Call = bool(__thiscall *)(void *, NativeActor *, const Vector *, bool, bool, uint32_t);
            // Routed over the road graph, stopping at the end.
            return reinterpret_cast<Call>(kCarMoveTo)(static_cast<std::byte *>(game) + kGameSledovacka, car, &target, false, true, 0);
        }
        inline uint32_t CarStatus(void *game, NativeActor *car) {
            using Call = uint32_t(__thiscall *)(void *, NativeActor *);
            return reinterpret_cast<Call>(kCarActionStatus)(static_cast<std::byte *>(game) + kGameSledovacka, car);
        }
        inline void CarRemove(void *game, NativeActor *car) {
            using Call = void(__thiscall *)(void *, NativeActor *);
            reinterpret_cast<Call>(kCarRemove)(static_cast<std::byte *>(game) + kGameSledovacka, car);
        }
        inline bool InsertItem(NativeActor *human, uint16_t itemId, int32_t loaded, int32_t reserve) {
            GameItem item {itemId, 0, loaded, reserve, nullptr};
            using Call = bool(__thiscall *)(void *, GameItem *, bool);
            return reinterpret_cast<Call>(kInventoryInsert)(reinterpret_cast<std::byte *>(human) + kHumanInventory, &item, false);
        }
    } // namespace EntityAi
} // namespace Mafia1Online::SDK::Player
