// The reason the host logs when an officer's police board case ends.
#include "features/npc/police_case.h"

#include <cstdio>

using namespace Mafia1Online::Features::Npc;

namespace {
    int failures = 0;

#define CHECK(condition)                                                                                                                                 \
    do {                                                                                                                                                 \
        if (!(condition)) {                                                                                                                              \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                  \
            ++failures;                                                                                                                                  \
        }                                                                                                                                                \
    } while (false)

    void Reasons() {
        CHECK(ClassifyPoliceCaseEnd({}) == PoliceCaseEnd::Closed);

        PoliceCaseFacts lost;
        lost.windingDown = true;
        CHECK(ClassifyPoliceCaseEnd(lost) == PoliceCaseEnd::LostOffender);

        // A cleared record winds every case down too; the clear is the cause.
        auto cleared = lost;
        cleared.recordCleared = true;
        CHECK(ClassifyPoliceCaseEnd(cleared) == PoliceCaseEnd::RecordCleared);

        // The grab settles the record and closes every case on the offender.
        auto grab = cleared;
        grab.grabbedElsewhere = true;
        CHECK(ClassifyPoliceCaseEnd(grab) == PoliceCaseEnd::OtherOfficerGrabbed);
        grab.grabbedHere = true;
        CHECK(ClassifyPoliceCaseEnd(grab) == PoliceCaseEnd::Grabbed);

        PoliceCaseFacts moved;
        moved.newOffender = true;
        moved.recordCleared = true;
        CHECK(ClassifyPoliceCaseEnd(moved) == PoliceCaseEnd::MovedToOtherPlayer);

        PoliceCaseFacts gone;
        gone.targetGone = true;
        gone.newOffender = true;
        CHECK(ClassifyPoliceCaseEnd(gone) == PoliceCaseEnd::TargetGone);

        PoliceCaseFacts dead;
        dead.targetDead = true;
        CHECK(ClassifyPoliceCaseEnd(dead) == PoliceCaseEnd::TargetDead);

        // The officer's own end comes first.
        auto despawned = grab;
        despawned.officerDead = true;
        CHECK(ClassifyPoliceCaseEnd(despawned) == PoliceCaseEnd::OfficerDead);
        despawned.despawned = true;
        CHECK(ClassifyPoliceCaseEnd(despawned) == PoliceCaseEnd::Despawned);

        CHECK(PoliceCaseModeName(5) == "arrest (foot)");
        CHECK(PoliceCaseModeName(7) == "wind down");
        CHECK(PoliceCaseModeName(42) == "unknown");
        CHECK(PoliceCaseEndName(PoliceCaseEnd::LostOffender) == "lost the offender (wound down)");
    }
} // namespace

int main() {
    Reasons();
    if (failures != 0) {
        std::printf("%d police case check(s) failed\n", failures);
        return 1;
    }
    std::printf("Police case tests passed\n");
    return 0;
}
