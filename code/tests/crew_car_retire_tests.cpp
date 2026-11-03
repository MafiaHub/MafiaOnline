// Whether the simulation host may retire a car retail police or mafia code
// made (TrafficHost::UpdateCrewCarRetirement), and how.
#include "features/traffic/crew_car_retire.h"

#include <cstdio>

using namespace Mafia1Online::Features::Traffic;

namespace {
    int failures = 0;

#define CHECK(condition)                                                                                                                                                                                                                                                               \
    do {                                                                                                                                                                                                                                                                               \
        if (!(condition)) {                                                                                                                                                                                                                                                            \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                                                                                                                                                \
            ++failures;                                                                                                                                                                                                                                                                \
        }                                                                                                                                                                                                                                                                              \
    } while (false)

    void Free() {
        // Nothing holds it: into the police retirement list with its crew.
        CHECK(JudgeCrewCar({}) == CrewCarVerdict::Retire);
        // A police car back in traffic leaves with its element first.
        CrewCarFacts traffic;
        traffic.dynamicElement = true;
        CHECK(JudgeCrewCar(traffic) == CrewCarVerdict::Deactivate);
    }

    void Cases() {
        // Every case that runs through the car or its crew keeps it, however
        // else it stands.
        const auto withCase = [](auto set) {
            CrewCarFacts facts;
            facts.dynamicElement = true;
            set(facts);
            return JudgeCrewCar(facts);
        };
        CHECK(withCase([](CrewCarFacts &facts) {
            facts.pursuit = true;
        }) == CrewCarVerdict::KeepPursuit);
        CHECK(withCase([](CrewCarFacts &facts) {
            facts.activeRoadBlock = true;
        }) == CrewCarVerdict::KeepRoadBlock);
        CHECK(withCase([](CrewCarFacts &facts) {
            facts.boardCase = true;
        }) == CrewCarVerdict::KeepCase);
        CHECK(withCase([](CrewCarFacts &facts) {
            facts.crimeRecord = true;
        }) == CrewCarVerdict::KeepCase);
        CHECK(withCase([](CrewCarFacts &facts) {
            facts.recruitedOnDuty = true;
        }) == CrewCarVerdict::KeepCase);

        // A pursuit outranks the rest; a case outranks who sits in it.
        CrewCarFacts all;
        all.pursuit = all.activeRoadBlock = all.boardCase = all.foreignOccupant = all.otherElement = true;
        CHECK(JudgeCrewCar(all) == CrewCarVerdict::KeepPursuit);
        all.pursuit = false;
        CHECK(JudgeCrewCar(all) == CrewCarVerdict::KeepRoadBlock);
        all.activeRoadBlock = false;
        CHECK(JudgeCrewCar(all) == CrewCarVerdict::KeepCase);
        all.boardCase = false;
        CHECK(JudgeCrewCar(all) == CrewCarVerdict::KeepOccupant);
        all.foreignOccupant = false;
        CHECK(JudgeCrewCar(all) == CrewCarVerdict::KeepElement);
    }

    void Holders() {
        // A player's replica or a scripted NPC in a seat keeps it.
        CrewCarFacts seated;
        seated.foreignOccupant = true;
        CHECK(JudgeCrewCar(seated) == CrewCarVerdict::KeepOccupant);
        // A waiting traffic element is not broken up, even next to an active
        // one holding the car.
        CrewCarFacts waiting;
        waiting.otherElement   = true;
        waiting.dynamicElement = true;
        CHECK(JudgeCrewCar(waiting) == CrewCarVerdict::KeepElement);
        CHECK(CrewCarVerdictName(CrewCarVerdict::KeepCase).find("case") != std::string_view::npos);
    }
} // namespace

int main() {
    Free();
    Cases();
    Holders();
    if (failures == 0) {
        std::printf("Crew car retirement tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
