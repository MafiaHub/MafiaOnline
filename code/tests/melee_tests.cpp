// Melee hits on the simulation host's walkers: the wire messages and the
// pure rules the attacker and the server apply to them.
#include "shared/features/combat/hit_report.h"
#include "shared/features/combat/melee_rules.h"
#include "shared/features/pedestrian/pedestrian_control.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace Mafia1Online;

namespace {
    int failures = 0;

#define CHECK(condition)                                                                                                                                 \
    do {                                                                                                                                                 \
        if (!(condition)) {                                                                                                                              \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                                  \
            ++failures;                                                                                                                                  \
        }                                                                                                                                                \
    } while (false)

    bool Near(float a, float b, float tolerance = 1e-5f) {
        return std::abs(a - b) <= tolerance;
    }

    template <typename T>
    T RoundTrip(T value) {
        MafiaNet::BitStream stream;
        value.Serialize(&stream, true);
        T read;
        read.Serialize(&stream, false);
        return read;
    }

    void Messages() {
        Shared::Combat::HitReport report;
        report.kind = Shared::Combat::HitReport::Kind::Melee;
        report.shooterNetworkId = 41;
        report.targetNetworkId = 9001;
        report.missionGeneration = 3;
        report.shooterSpawnGeneration = 2;
        report.targetSpawnGeneration = 0;
        report.shotSequence = 77;
        report.bodyPart = Shared::Combat::kWalkerMeleeBodyPart;
        report.directionX = 0.6f;
        report.directionZ = -0.8f;
        report.hitX = -1520.25f;
        report.hitY = 1.4f;
        report.hitZ = 610.5f;
        const auto readReport = RoundTrip(report);
        CHECK(readReport.kind == Shared::Combat::HitReport::Kind::Melee);
        CHECK(readReport.shooterNetworkId == 41 && readReport.targetNetworkId == 9001 && readReport.missionGeneration == 3);
        CHECK(readReport.shooterSpawnGeneration == 2 && readReport.targetSpawnGeneration == 0 && readReport.shotSequence == 77);
        CHECK(readReport.bodyPart == 5 && Near(readReport.directionX, 0.6f) && Near(readReport.directionZ, -0.8f));
        CHECK(Near(readReport.hitX, -1520.25f) && Near(readReport.hitY, 1.4f) && Near(readReport.hitZ, 610.5f));

        Shared::Pedestrian::MeleeHit hit;
        hit.missionGeneration = 3;
        hit.key = 12;
        hit.attackerId = 41;
        hit.weaponId = 4;
        hit.heavy = true;
        hit.damage = 10.0f;
        hit.directionX = 0.6f;
        hit.directionY = 0.0f;
        hit.directionZ = -0.8f;
        hit.x = -1520.25f;
        hit.y = 1.4f;
        hit.z = 610.5f;
        const auto readHit = RoundTrip(hit);
        CHECK(readHit.missionGeneration == 3 && readHit.key == 12 && readHit.attackerId == 41);
        CHECK(readHit.weaponId == 4 && readHit.heavy && Near(readHit.damage, 10.0f));
        CHECK(Near(readHit.directionX, 0.6f) && Near(readHit.directionY, 0.0f) && Near(readHit.directionZ, -0.8f));
        CHECK(Near(readHit.x, -1520.25f) && Near(readHit.y, 1.4f) && Near(readHit.z, 610.5f));
        hit.heavy = false;
        hit.weaponId = 0;
        CHECK(!RoundTrip(hit).heavy && RoundTrip(hit).weaponId == 0);
    }

    void Reach() {
        using Shared::Combat::MeleeHitPlausible;
        const glm::vec3 attacker(0.0f, 0.0f, 0.0f);
        const glm::vec3 facing(0.0f, 0.0f, 1.0f);
        const glm::vec3 target(0.0f, 0.0f, 1.2f);
        const glm::vec3 chest(0.0f, 1.3f, 1.0f);
        CHECK(MeleeHitPlausible(attacker, facing, target, chest));
        // At the reach limit, and past it.
        CHECK(MeleeHitPlausible(attacker, facing, {0.0f, 0.0f, 2.6f}, {0.0f, 1.3f, 2.4f}));
        CHECK(!MeleeHitPlausible(attacker, facing, {0.0f, 0.0f, 3.0f}, {0.0f, 1.3f, 2.8f}));
        // A walker that walked on: the lag allowance covers it.
        CHECK(MeleeHitPlausible(attacker, facing, {0.0f, 0.0f, 3.0f}, {0.0f, 1.3f, 1.0f}, 0.75f));
        CHECK(!MeleeHitPlausible(attacker, facing, {0.0f, 0.0f, 3.0f}, {0.0f, 1.3f, 1.0f}));
        CHECK(!MeleeHitPlausible(attacker, facing, {0.0f, 0.0f, 5.0f}, {0.0f, 1.3f, 1.0f}, 2.0f));
        // On another floor, or the hit below the feet or over the head.
        CHECK(!MeleeHitPlausible(attacker, facing, {0.0f, 2.0f, 1.2f}, {0.0f, 3.3f, 1.0f}));
        CHECK(!MeleeHitPlausible(attacker, facing, target, {0.0f, -0.8f, 1.0f}));
        CHECK(!MeleeHitPlausible(attacker, facing, target, {0.0f, 2.8f, 1.0f}));
        // A hit far from the target's body.
        CHECK(!MeleeHitPlausible(attacker, facing, target, {2.0f, 1.3f, 1.0f}));
        // Behind the attacker; beside it is still a swing.
        CHECK(!MeleeHitPlausible(attacker, facing, {0.0f, 0.0f, -1.2f}, {0.0f, 1.3f, -1.0f}));
        CHECK(MeleeHitPlausible(attacker, facing, {1.2f, 0.0f, 0.0f}, {1.0f, 1.3f, 0.0f}));
        // Standing on the attacker: no facing to check.
        CHECK(MeleeHitPlausible(attacker, facing, {0.0f, 0.0f, -0.1f}, {0.0f, 1.3f, 0.0f}));
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(!MeleeHitPlausible(attacker, facing, target, {nan, 1.3f, 1.0f}));
        CHECK(!MeleeHitPlausible(attacker, facing, {nan, 0.0f, 1.2f}, chest));
        CHECK(!MeleeHitPlausible(attacker, facing, target, chest, nan));
        CHECK(!MeleeHitPlausible(attacker, facing, target, chest, -1.0f));
    }

    void Lag() {
        using Shared::Combat::WalkerMeleeLagAllowance;
        CHECK(Near(WalkerMeleeLagAllowance({0.0f, 0.0f, 0.0f}), 0.0f));
        CHECK(Near(WalkerMeleeLagAllowance({0.9f, 0.0f, 1.2f}), 0.75f));
        // Vertical speed does not move a walker along the street.
        CHECK(Near(WalkerMeleeLagAllowance({0.0f, 9.0f, 0.0f}), 0.0f));
        CHECK(Near(WalkerMeleeLagAllowance({30.0f, 0.0f, 0.0f}), Shared::Combat::kWalkerMeleeMaximumLag));
        CHECK(Near(WalkerMeleeLagAllowance({std::numeric_limits<float>::infinity(), 0.0f, 0.0f}), 0.0f));
    }

    void Boxing() {
        using Shared::Combat::HostBooksBoxing;
        // A player's replayed swing on the host books nothing; its validated
        // hit does, and the host's own NPCs book as in retail.
        CHECK(!HostBooksBoxing(true, false));
        CHECK(HostBooksBoxing(true, true));
        CHECK(HostBooksBoxing(false, false));
        CHECK(HostBooksBoxing(false, true));
    }

    void Gate() {
        Shared::Combat::MeleeReportGate gate;
        CHECK(!gate.Admit(0));
        CHECK(gate.Admit(5));
        CHECK(!gate.Admit(5));
        CHECK(gate.Admit(6));
        CHECK(!gate.Admit(6));
        gate.Reset();
        CHECK(gate.Admit(6));
    }
} // namespace

int main() {
    Messages();
    Reach();
    Lag();
    Boxing();
    Gate();
    if (failures == 0) {
        std::printf("melee tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
