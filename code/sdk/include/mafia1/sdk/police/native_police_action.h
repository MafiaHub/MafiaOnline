#pragma once

#include <mafia1/sdk/player/native_actor.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Police {
    // int __thiscall C_entity::actionTick(ai_action *, unsigned, unsigned,
    // unsigned *flags), ret 10h. It switches on the action type at +0x0
    // (0x50a342) and reads the payload from +0x20 (0x515252 for police).
    inline constexpr uintptr_t kEntityActionTick = 0x50A2C0;
    inline constexpr uint32_t kPoliceActionType  = 33;

    // ai_action_police, the payload of a type 33 action. Its sub-state at
    // +0x34 drives the jump table at 0x5153a9; the target at +0x58 and the
    // mode at +0x44 (reM ai_action_manager.h asserts 0x58).
    struct NativePoliceAction {
        // State 11 plays the arrest or fine animations on both humans, then
        // switches to 12 (0x517265); state 12 finishes once they end.
        static constexpr uint32_t kStateAlign = 11;
        static constexpr uint32_t kStateDone  = 12;
        // Mode 0 plays PolicajtZatykajici/PolicajtZatykany and calls
        // PlukovnikuMamZmrdaPlayera (0x5171fe, 0x517260); mode 1 plays
        // PolicajtPokutujici/PolicajtPokutovany and at state 12 calls
        // C_player::LockControls(false) on the target whatever its type
        // (0x51729d-0x5172a8). reM swaps the two animation branches.
        static constexpr uint32_t kModeArrest = 0;
        static constexpr uint32_t kModeFine   = 1;

        [[nodiscard]] uint32_t State() const { return _state; }
        [[nodiscard]] uint32_t Mode() const { return _mode; }
        [[nodiscard]] Player::NativeActor *Target() const { return _target; }

        std::byte _unused00[0x34];
        uint32_t _state;
        std::byte _unused38[0x44 - 0x38];
        uint32_t _mode;
        std::byte _unused48[0x58 - 0x48];
        Player::NativeActor *_target;
    };
    static_assert(offsetof(NativePoliceAction, _state) == 0x34);
    static_assert(offsetof(NativePoliceAction, _mode) == 0x44);
    static_assert(offsetof(NativePoliceAction, _target) == 0x58);

    struct NativeAiAction {
        [[nodiscard]] uint32_t Type() const { return _type; }
        [[nodiscard]] NativePoliceAction *Police() const {
            return _type == kPoliceActionType ? static_cast<NativePoliceAction *>(_payload) : nullptr;
        }

        uint32_t _type;
        std::byte _unused04[0x20 - 0x04];
        void *_payload;
    };
    static_assert(offsetof(NativeAiAction, _payload) == 0x20);

    // bool __thiscall C_entity::Hit(same eight arguments as C_human::Hit),
    // ret 20h, vtable slot 0x62510c. It runs C_human::Hit and then, for an
    // attacker of type 2 or 27 (or a car's seat 0 owner), C_entity::Event
    // (attacker, 0 = under attack), which turns the attacker into an enemy in
    // the NPC's logs. A cop (class 2) whose police action already targets the
    // attacker escalates it; any other attacker only gets the log entry.
    inline constexpr uintptr_t kEntityHit = 0x507ED0;
    // C_entity behaviour class (+0xf4c): 2 police, 4 gangster. ai_logs::Event
    // (0x4f6f0c) rates a type 2 player as class 0 and any type 27 entity by
    // this field; the entity's own class picks the attitude table.
    inline constexpr uintptr_t kEntityBehaviourClass  = 0xF4C;
    inline constexpr uint32_t kPlayerBehaviourClass   = 0;
    inline constexpr uint32_t kPoliceBehaviourClass   = 2;
    // The police board action number the officer serves (+0xf54, read at
    // 0x507f75).
    inline constexpr uintptr_t kEntityPoliceActionNumber = 0xF54;

    // g_PoliceNastenka and its per-offender actions (0x6c bytes each): the
    // members vector at +0x1c (first +0x1c, last +0x20) and the offender at
    // +0x60.
    inline constexpr uintptr_t kPoliceBoard = 0x646D10;
    // ai_police_action *__thiscall MamCisloDejAkci(unsigned number), ret 4.
    inline constexpr uintptr_t kBoardActionByNumber = 0x53BBF0;
    // void __thiscall ZinvalidujAktora(C_actor *), ret 4: ends the actions
    // aimed at the actor and removes it from every action's members; an
    // action left without members finishes (AkceDokoncenaPlukovniku).
    inline constexpr uintptr_t kBoardForget = 0x53C6F0;
    // char __thiscall MamProVasAkciHosi(C_actor *offender, unsigned mode,
    // std::vector<C_actor *> &officers, bool booked, void *payload), ret 14h:
    // opens an action against the offender for the officers (SpawnPolice on
    // each). It only reads the vector (first +4, last +8, VC6 allocator
    // first) and the payload only for mode 6. Mode 0 hunts to kill, on foot
    // unless the offender sits in a car. The retail ARREST_PLAYER script
    // command calls it with one officer (reM C_entity.cpp:4872).
    inline constexpr uintptr_t kBoardDispatch = 0x53BC50;
    // ai_police_action::m_uMode. MamProVasAkciHosi takes the car variant of
    // a mode and opens the foot variant (one higher) when the first officer
    // sits in no car. C_shvestky::VasekSelChlastatNaOslavu 0x49f980 picks it
    // from the record level: lethal kill, arrest arrest, minor pursue.
    inline constexpr uint32_t kBoardModeKill    = 0;
    inline constexpr uint32_t kBoardModePursue  = 2;
    inline constexpr uint32_t kBoardModeArrest  = 4;
    inline constexpr uint32_t kBoardModeWarn    = 6;
    inline constexpr uint32_t kBoardModeWindDown = 7;

    struct NativeBoardAction {
        [[nodiscard]] Player::NativeActor *Offender() const { return _offender; }
        [[nodiscard]] uint32_t Mode() const { return _mode; }
        // An officer on foot chasing, arresting or hunting its offender
        // (kill 1, pursue 3, arrest 5), not a car hunt, road block or wind
        // down.
        [[nodiscard]] bool OnFootCase() const { return _mode == 1 || _mode == 3 || _mode == 5; }

        std::byte _unused00[0x2c];
        uint32_t _mode;
        std::byte _unused30[0x60 - 0x30];
        Player::NativeActor *_offender;
    };
    static_assert(offsetof(NativeBoardAction, _mode) == 0x2c);
    static_assert(offsetof(NativeBoardAction, _offender) == 0x60);

    // Sends one officer after a new offender, as retail sends one officer
    // with ARREST_PLAYER: off its old action first, then into a new one of
    // this mode (a car mode, see kBoardModeKill).
    inline void RedirectOfficer(Player::NativeActor *officer, Player::NativeActor *offender, uint32_t mode = kBoardModeKill) {
        struct ActorVector {
            void *allocator;
            Player::NativeActor **first;
            Player::NativeActor **last;
            Player::NativeActor **end;
        };
        using Forget   = void(__thiscall *)(uintptr_t, Player::NativeActor *);
        using Dispatch = char(__thiscall *)(uintptr_t, Player::NativeActor *, uint32_t, ActorVector &, bool, void *);
        reinterpret_cast<Forget>(kBoardForget)(kPoliceBoard, officer);
        ActorVector officers {nullptr, &officer, &officer + 1, &officer + 1};
        reinterpret_cast<Dispatch>(kBoardDispatch)(kPoliceBoard, offender, mode, officers, true, nullptr);
    }

    // Ends every police action aimed at the offender; its officers go back
    // to their own AI.
    inline void CallOffOfficers(Player::NativeActor *offender) {
        using Forget = void(__thiscall *)(uintptr_t, Player::NativeActor *);
        reinterpret_cast<Forget>(kBoardForget)(kPoliceBoard, offender);
    }

    // void __thiscall AkceDokoncenaPlukovniku(ai_police_action *), ret 4:
    // closes the action and erases it from the board (m_Actions, 0x6c bytes
    // each, _First +0x14, _Last +0x18).
    inline constexpr uintptr_t kBoardFinish = 0x53C550;
    inline constexpr size_t kBoardActionSize = 0x6C;

    // Before a human is deleted. ZinvalidujAktora winds a car action down
    // (HosiKoncime) but keeps its target, which the wind-down tick still
    // reads, so every action left aimed at the human is closed as well.
    inline void ForgetOffender(Player::NativeActor *offender) {
        CallOffOfficers(offender);
        using Finish = void(__thiscall *)(uintptr_t, void *);
        for (bool finished = true; finished;) {
            finished = false;
            auto *first = *reinterpret_cast<std::byte **>(kPoliceBoard + 0x14);
            auto *last = *reinterpret_cast<std::byte **>(kPoliceBoard + 0x18);
            for (auto *action = first; first && action < last; action += kBoardActionSize) {
                if (reinterpret_cast<NativeBoardAction *>(action)->Offender() == offender) {
                    reinterpret_cast<Finish>(kBoardFinish)(kPoliceBoard, action);
                    finished = true;
                    break;
                }
            }
        }
    }

    inline NativeBoardAction *OfficerBoardAction(const Player::NativeActor *officer) {
        using Call = NativeBoardAction *(__thiscall *)(uintptr_t, uint32_t);
        const auto number = *reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(officer) + kEntityPoliceActionNumber);
        return reinterpret_cast<Call>(kBoardActionByNumber)(kPoliceBoard, number);
    }

    inline uint32_t BehaviourClass(const Player::NativeActor *entity) {
        return *reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(entity) + kEntityBehaviourClass);
    }
    inline void SetBehaviourClass(Player::NativeActor *entity, uint32_t behaviourClass) {
        *reinterpret_cast<uint32_t *>(reinterpret_cast<std::byte *>(entity) + kEntityBehaviourClass) = behaviourClass;
    }

    // void __thiscall ai_police_nastenka::PlukovnikuMamZmrdaPlayera(), plain
    // ret, on g_PoliceNastenka (0x646d10): clamps the shift timer (+0x24) to
    // 12000. Once it runs out, C_shvestky::Tick (0x49a3d3) fades out and
    // opens GM_Menu_gamefailed. Its only caller is the arrest at 0x517260.
    inline constexpr uintptr_t kPoliceBoardArrested = 0x53CBE0;
    // char __thiscall ai_police_nastenka::MakamSefe(bool busy), ret 4: stores
    // the board's busy flag (+0x2c). The police action raises it as an
    // arrest or fine starts (0x515cee) and clears it in state 12 once both
    // animations have ended (0x517291-0x517298), just before the fine's
    // LockControls; its only two callers.
    inline constexpr uintptr_t kPoliceBoardBusy = 0x53D490;

    // bool __thiscall C_player::LockControls(bool), ret 4. It writes the
    // C_player field +0xada first (0x595f0c); on a C_entity that byte lies
    // past C_human (0xa9c bytes) inside the entity's own state.
    inline constexpr uintptr_t kPlayerLockControls = 0x595F00;

    // void __thiscall G_Camera::CumNaNe(C_human *, C_human *), ret 8: pushes
    // the camera into its arrest lock mode. The arrest calls it for every
    // target (0x5171c1); only a player target is unlocked afterwards.
    inline constexpr uintptr_t kCameraArrestView = 0x5F3A70;
} // namespace Mafia1Online::SDK::Police
