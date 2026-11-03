#pragma once

#include <cstdint>
#include <string_view>

namespace Mafia1Online::Features::Npc {
    // Why an officer's police board case (ai_police_action) ended, from what
    // the host saw the frame it went. The board names no reason itself:
    // HosiKoncime winds a case down (mode 7) when the offender is lost,
    // MamHoZmrdaUzHoNechte closes every case on an offender at a grab, and
    // ZinvalidujAktora closes those of a removed actor.
    enum class PoliceCaseEnd : uint8_t {
        Despawned,
        OfficerDead,
        Grabbed,
        OtherOfficerGrabbed,
        TargetGone,
        TargetDead,
        MovedToOtherPlayer,
        RecordCleared,
        LostOffender,
        Closed,
    };

    struct PoliceCaseFacts {
        bool despawned = false;
        bool officerDead = false;
        // This officer, or another, holds the offender for an arrest or fine
        // (the grab closes every case on the offender; the catch itself is
        // logged when it completes).
        bool grabbedHere = false;
        bool grabbedElsewhere = false;
        // The offender is no longer a recorded person (left, respawned, on
        // police duty), or is dead.
        bool targetGone = false;
        bool targetDead = false;
        // The officer took a case on another offender in the same frame.
        bool newOffender = false;
        // The offender's record is at level 0 (settled or cleared by script).
        bool recordCleared = false;
        // The case was winding down (mode 7) when last seen.
        bool windingDown = false;
    };

    constexpr PoliceCaseEnd ClassifyPoliceCaseEnd(const PoliceCaseFacts &facts) {
        if (facts.despawned) {
            return PoliceCaseEnd::Despawned;
        }
        if (facts.officerDead) {
            return PoliceCaseEnd::OfficerDead;
        }
        if (facts.grabbedHere) {
            return PoliceCaseEnd::Grabbed;
        }
        if (facts.grabbedElsewhere) {
            return PoliceCaseEnd::OtherOfficerGrabbed;
        }
        if (facts.targetGone) {
            return PoliceCaseEnd::TargetGone;
        }
        if (facts.targetDead) {
            return PoliceCaseEnd::TargetDead;
        }
        if (facts.newOffender) {
            return PoliceCaseEnd::MovedToOtherPlayer;
        }
        if (facts.recordCleared) {
            return PoliceCaseEnd::RecordCleared;
        }
        if (facts.windingDown) {
            return PoliceCaseEnd::LostOffender;
        }
        return PoliceCaseEnd::Closed;
    }

    constexpr std::string_view PoliceCaseEndName(PoliceCaseEnd end) {
        switch (end) {
        case PoliceCaseEnd::Despawned: return "officer despawned";
        case PoliceCaseEnd::OfficerDead: return "officer dead";
        case PoliceCaseEnd::Grabbed: return "grab";
        case PoliceCaseEnd::OtherOfficerGrabbed: return "another officer's grab";
        case PoliceCaseEnd::TargetGone: return "target no longer recorded";
        case PoliceCaseEnd::TargetDead: return "target dead";
        case PoliceCaseEnd::MovedToOtherPlayer: return "moved to another player";
        case PoliceCaseEnd::RecordCleared: return "record cleared";
        case PoliceCaseEnd::LostOffender: return "lost the offender (wound down)";
        case PoliceCaseEnd::Closed: return "case closed";
        }
        return "case closed";
    }

    // ai_police_action::m_uMode (see SDK::Police::kBoardMode*).
    constexpr std::string_view PoliceCaseModeName(uint32_t mode) {
        switch (mode) {
        case 0: return "kill (car)";
        case 1: return "kill (foot)";
        case 2: return "pursue (car)";
        case 3: return "pursue (foot)";
        case 4: return "arrest (car)";
        case 5: return "arrest (foot)";
        case 6: return "warn";
        case 7: return "wind down";
        }
        return "unknown";
    }
} // namespace Mafia1Online::Features::Npc
