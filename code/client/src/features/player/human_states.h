#pragma once

#include <cstdint>

namespace Mafia1Online::Features::Player {
    // What a human replica plays for a replicated native locomotion state
    // (C_human m_iAnimState).
    struct HumanAnimation {
        int16_t state;
        // Played once when the replicated state changes to it.
        bool oneShot;
    };

    // The low nine bits name the state (HUMAN_ANIM_STATE_MASK); the upper
    // bits select the weapon/crouch variant, which PersonAnim (0x573e50)
    // picks again from each human's own replicated stance.
    inline HumanAnimation ClassifyHumanState(int16_t state) {
        const auto base = static_cast<int16_t>(state & 0x1ff);
        switch (base) {
        // The breath, the runs and walks, and gliding (102).
        case 1:
        case 5:
        case 6:
        case 7:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
        case 102:
        // Turning on the spot: AI_human_turn_to_point (ai_path) sets left1
        // (8), right1 (9) or turn1 (10, also C_human::Turn_Turn 0x58ac60)
        // while the AI turns a standing body.
        case 8:
        case 9:
        case 10:
        // Panic (anim_info "panika ..."): an entity's freeze (AI_ACTION_FREEZE,
        // Do_ShockDown) holds 62, 65 or 68 crouched, on all fours or lying
        // until Do_ShockUp; standing panic, the panic runs and looking round.
        case 55:
        case 56:
        case 57:
        case 58:
        case 59:
        case 60:
        case 62:
        case 65:
        case 68:
        case 70:
        case 71:
        case 72:
        case 73:
        case 74:
        case 75:
        case 76:
        case 77:
        // Cover (C_entity::actionTick): Do_CornerBreath (0x58a960) holds
        // "enm roh ... stat" (78, 79) behind the corner, Do_CornerFireAimed
        // (0x58ab80) leans out aimed (88-92) while the AI shoots.
        case 78:
        case 79:
        case 88:
        case 89:
        case 90:
        case 91:
        case 92: return {base, false};
        // Jumps and the hard landing play once over the streamed arc.
        case 95:
        case 97:
        case 98:
        case 99:
        case 100:
        case 101:
        case 103:
        // The startles (Do_ShockStart) and a freeze's way down and back up.
        case 47:
        case 48:
        case 49:
        case 50:
        case 51:
        case 52:
        case 53:
        case 54:
        case 61:
        case 63:
        case 64:
        case 66:
        case 67:
        case 69:
        // A peek round the corner (Do_CornerView 0x58a9a0, 80-82) and a shot
        // round it (Do_CornerFire 0x58aa60, 83-87): timed actions (work state
        // 5) for the clip's length less 250 ms, after which the state stays.
        case 80:
        case 81:
        case 82:
        case 83:
        case 84:
        case 85:
        case 86:
        case 87:
        // A car's side jump (C_human::Do_SideJump 0x587040, GAME_ANIM_DODGE_*
        // 322-329): once, as the owner's C_human::Update ends it on idle.
        case 322:
        case 323:
        case 324:
        case 325:
        case 326:
        case 327:
        case 328:
        case 329: return {base, true};
        default: break;
        }
        return {1, false};
    }

    // The C_human call that puts a body in a cover state, so a replica
    // takes it with the work state, action time and aim the host's did.
    // Each takes the corner side (E_corner_view 0-2) and refuses a rifle,
    // an item or, for side 2, a standing body (IDA 0x58a9a0-0x58ac38);
    // the side's alternative clip (84, 86, 89, 91) is chosen by the call
    // from the weapon in hand.
    enum class CornerCall : uint8_t {
        None,
        View,
        Fire,
        FireAimed,
    };
    struct CornerAction {
        CornerCall call = CornerCall::None;
        int32_t view = 0;
    };
    inline CornerAction CornerActionFor(int32_t state) {
        switch (state & 0x1ff) {
        case 80: return {CornerCall::View, 0};
        case 81: return {CornerCall::View, 1};
        case 82: return {CornerCall::View, 2};
        case 83:
        case 84: return {CornerCall::Fire, 0};
        case 85:
        case 86: return {CornerCall::Fire, 1};
        case 87: return {CornerCall::Fire, 2};
        case 88:
        case 89: return {CornerCall::FireAimed, 0};
        case 90:
        case 91: return {CornerCall::FireAimed, 1};
        case 92: return {CornerCall::FireAimed, 2};
        default: return {};
        }
    }
} // namespace Mafia1Online::Features::Player
