// Car bumps, police arrests and railway shots: the wire messages and the
// pure rules both ends of them apply.
#include "shared/features/combat/vehicle_impact.h"
#include "shared/features/npc/npc_report.h"
#include "shared/features/npc/npc_seized.h"
#include "shared/features/railway/railway_report.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>

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

    bool Near(float a, float b, float tolerance = 1e-4f) {
        return std::abs(a - b) <= tolerance;
    }

    bool Near(const Shared::Railway::Vector &a, const Shared::Railway::Vector &b, float tolerance = 1e-3f) {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
    }

    template <typename T>
    T RoundTrip(T value, size_t *bytes = nullptr) {
        MafiaNet::BitStream stream;
        value.Serialize(&stream, true);
        if (bytes) {
            *bytes = static_cast<size_t>(stream.GetNumberOfBytesUsed());
        }
        T read;
        read.Serialize(&stream, false);
        return read;
    }

    void ImpactMessages() {
        Shared::Combat::VehicleImpact impact;
        impact.carId = 11;
        impact.targetId = 12;
        impact.missionGeneration = 3;
        impact.targetSpawnGeneration = 4;
        impact.contactX = 1.5f;
        impact.contactY = -2.0f;
        impact.contactZ = 300.25f;
        impact.kind = Shared::Combat::VehicleContact::Bump;
        size_t bytes = 0;
        const auto read = RoundTrip(impact, &bytes);
        CHECK(bytes == 45);
        CHECK(read.carId == 11 && read.targetId == 12 && read.missionGeneration == 3 && read.targetSpawnGeneration == 4);
        CHECK(read.contactX == 1.5f && read.contactY == -2.0f && read.contactZ == 300.25f);
        CHECK(read.kind == Shared::Combat::VehicleContact::Bump);
        CHECK(Shared::Combat::KnownVehicleContact(read.kind));
        CHECK(!Shared::Combat::KnownVehicleContact(static_cast<Shared::Combat::VehicleContact>(7)));

        Shared::Combat::VehicleBump bump;
        bump.carId = 21;
        bump.missionGeneration = 5;
        bump.targetSpawnGeneration = 6;
        bump.contactX = 4.0f;
        bump.directionZ = -2.5f;
        const auto bumped = RoundTrip(bump, &bytes);
        CHECK(bytes == 48);
        CHECK(bumped.carId == 21 && bumped.missionGeneration == 5 && bumped.targetSpawnGeneration == 6 && bumped.contactX == 4.0f && bumped.directionZ == -2.5f);

        Shared::Npc::Hit hit;
        hit.key = 9;
        hit.attackerId = 21;
        hit.hitType = Shared::Npc::Hit::kCarBump;
        const auto hitRead = RoundTrip(hit);
        CHECK(hitRead.hitType == 5 && hitRead.key == 9 && hitRead.attackerId == 21);
    }

    void ContactRules() {
        using Shared::Combat::VehicleContact;
        VehicleContact kind = VehicleContact::Impact;
        // Retail: below 3 m/s a side jump, from 3 a hit; never a separating
        // contact, nor a car resting against the human.
        CHECK(Shared::Combat::ClassifyVehicleContact(2.0f, -0.5f, kind) && kind == VehicleContact::Bump);
        CHECK(Shared::Combat::ClassifyVehicleContact(3.0f, -0.5f, kind) && kind == VehicleContact::Impact);
        CHECK(Shared::Combat::ClassifyVehicleContact(12.0f, -4.0f, kind) && kind == VehicleContact::Impact);
        CHECK(!Shared::Combat::ClassifyVehicleContact(2.0f, 0.0f, kind));
        CHECK(!Shared::Combat::ClassifyVehicleContact(8.0f, 0.3f, kind));
        CHECK(!Shared::Combat::ClassifyVehicleContact(0.4f, -0.3f, kind));
        CHECK(!Shared::Combat::ClassifyVehicleContact(std::numeric_limits<float>::quiet_NaN(), -1.0f, kind));
        CHECK(!Shared::Combat::ClassifyVehicleContact(2.0f, std::numeric_limits<float>::infinity(), kind));

        // The server's own car speed backs each kind.
        CHECK(Shared::Combat::VehicleContactSpeedValid(VehicleContact::Bump, 0.5f));
        CHECK(Shared::Combat::VehicleContactSpeedValid(VehicleContact::Bump, 4.0f));
        CHECK(!Shared::Combat::VehicleContactSpeedValid(VehicleContact::Bump, 0.3f));
        CHECK(!Shared::Combat::VehicleContactSpeedValid(VehicleContact::Impact, 2.9f));
        CHECK(Shared::Combat::VehicleContactSpeedValid(VehicleContact::Impact, 3.0f));
        CHECK(!Shared::Combat::VehicleContactSpeedValid(VehicleContact::Impact, 121.0f));
        CHECK(!Shared::Combat::VehicleContactSpeedValid(VehicleContact::Bump, std::numeric_limits<float>::infinity()));

        // One contact per car and human each 500 ms, but an impact follows
        // a bump at once.
        using std::chrono::milliseconds;
        CHECK(!Shared::Combat::VehicleContactDue(milliseconds(100), VehicleContact::Bump, VehicleContact::Bump));
        CHECK(Shared::Combat::VehicleContactDue(milliseconds(100), VehicleContact::Bump, VehicleContact::Impact));
        CHECK(!Shared::Combat::VehicleContactDue(milliseconds(100), VehicleContact::Impact, VehicleContact::Impact));
        CHECK(!Shared::Combat::VehicleContactDue(milliseconds(100), VehicleContact::Impact, VehicleContact::Bump));
        CHECK(Shared::Combat::VehicleContactDue(milliseconds(500), VehicleContact::Impact, VehicleContact::Bump));

        // A bump replays below 3, along the car's velocity.
        const auto slow = Shared::Combat::BumpReplayDirection({1.0f, 0.0f, 1.0f});
        CHECK(Near(slow.x, 1.0f) && Near(slow.z, 1.0f));
        const auto fast = Shared::Combat::BumpReplayDirection({0.0f, 0.0f, 9.0f});
        CHECK(glm::length(fast) < Shared::Combat::kVehicleImpactMinSpeed && Near(fast.z, Shared::Combat::kVehicleBumpReplaySpeed));
        const auto diagonal = Shared::Combat::BumpReplayDirection({6.0f, 0.0f, -8.0f});
        CHECK(Near(glm::length(diagonal), Shared::Combat::kVehicleBumpReplaySpeed) && Near(diagonal.x / diagonal.z, -0.75f));
        CHECK(glm::length(Shared::Combat::BumpReplayDirection({0.0f, 0.0f, 0.0f})) == 0.0f);
    }

    void ContactRate() {
        using Clock = std::chrono::steady_clock;
        Shared::Combat::VehicleContactRate rate;
        const auto start = Clock::now();
        for (size_t i = 0; i < Shared::Combat::kVehicleContactsPerSecond; ++i) {
            CHECK(rate.Admit(start + std::chrono::milliseconds(10 * i)));
        }
        CHECK(!rate.Admit(start + std::chrono::milliseconds(500)));
        CHECK(!rate.Admit(start + std::chrono::milliseconds(999)));
        // The first contact leaves the window a second after it came.
        CHECK(rate.Admit(start + std::chrono::milliseconds(1000)));
        CHECK(!rate.Admit(start + std::chrono::milliseconds(1005)));
        CHECK(rate.Admit(start + std::chrono::milliseconds(2500)));
    }

    void SeizedMessage() {
        Shared::Npc::Seized seized;
        seized.missionGeneration = 2;
        seized.key = 0x80000001u;
        seized.targetNetworkId = 30;
        seized.targetSpawnGeneration = 7;
        seized.outcome = Shared::Npc::Caught::Outcome::Fine;
        seized.officerNetworkId = 31;
        seized.officer = {10.0f, 1.0f, -5.0f, 3.1f};
        seized.target = {10.0f, 1.0f, -4.2f, -0.04f};
        size_t bytes = 0;
        const auto read = RoundTrip(seized, &bytes);
        // 29 before the officer and the two poses.
        CHECK(bytes == 29 + 8 + 32);
        CHECK(read.missionGeneration == 2 && read.key == 0x80000001u && read.targetNetworkId == 30 && read.targetSpawnGeneration == 7);
        CHECK(read.outcome == Shared::Npc::Caught::Outcome::Fine && read.officerNetworkId == 31);
        CHECK(read.officer.x == 10.0f && read.officer.z == -5.0f && read.officer.yaw == 3.1f);
        CHECK(read.target.z == -4.2f && read.target.yaw == -0.04f);

        // A body takes the aligned pose only from close by.
        CHECK(Shared::Npc::SeizedAlignable({10.0f, 1.0f, -6.0f}, seized.target));
        CHECK(!Shared::Npc::SeizedAlignable({10.0f, 1.0f, -8.0f}, seized.target));
        Shared::Npc::Seized::Pose broken {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f};
        CHECK(!Shared::Npc::SeizedAlignable({0.0f, 0.0f, 0.0f}, broken));
        const auto facing = Shared::Npc::Seized::Pose {0.0f, 0.0f, 0.0f, 1.5707964f}.Direction();
        CHECK(Near(facing.x, 1.0f) && Near(facing.z, 0.0f));

        CHECK(std::string_view(Shared::Npc::SeizingAnimation(Shared::Npc::Caught::Outcome::Arrest)) == "PolicajtZatykajici.I3D");
        CHECK(std::string_view(Shared::Npc::SeizingAnimation(Shared::Npc::Caught::Outcome::Fine)) == "PolicajtPokutujici.I3D");
        CHECK(std::string_view(Shared::Npc::SeizedAnimation(Shared::Npc::Caught::Outcome::Fine)) == "PolicajtPokutovany.I3D");
    }

    // A wagon frame: yawed by `yaw`, scaled as I3D keeps a scaled model, at
    // `origin`.
    Shared::Railway::FrameMatrix Wagon(float yaw, float scale, const Shared::Railway::Vector &origin) {
        const float s = std::sin(yaw);
        const float c = std::cos(yaw);
        return {{c * scale, 0.0f, -s * scale}, {0.0f, scale, 0.0f}, {s * scale, 0.0f, c * scale}, origin};
    }

    void RailwayShots() {
        using Shared::Railway::Vector;
        // The shooter's wagon, drawn behind the host's.
        const auto shooterWagon = Wagon(0.6f, 1.0f, {100.0f, 2.0f, 50.0f});
        const Vector hit {101.5f, 3.2f, 52.0f};
        const Vector direction {0.6f, 0.0f, 0.8f};
        Vector localHit;
        Vector localDirection;
        CHECK(Shared::Railway::FrameLocal(shooterWagon, hit, true, localHit));
        CHECK(Shared::Railway::FrameLocal(shooterWagon, direction, false, localDirection));
        // Back on the same frame: the same world point.
        CHECK(Near(Shared::Railway::FrameWorld(shooterWagon, localHit, true), hit));
        CHECK(Near(Shared::Railway::FrameWorld(shooterWagon, localDirection, false), direction));

        // The host's wagon is 3 m further down a turning track: the hit
        // lands at the same place on the wagon, not at the old world point.
        const auto hostWagon = Wagon(0.75f, 1.0f, {101.8f, 2.0f, 52.4f});
        const auto replayed = Shared::Railway::FrameWorld(hostWagon, localHit, true);
        Vector again;
        CHECK(Shared::Railway::FrameLocal(hostWagon, replayed, true, again) && Near(again, localHit));
        const float moved = std::hypot(replayed.x - hit.x, replayed.y - hit.y, replayed.z - hit.z);
        CHECK(moved > 2.0f);
        const auto replayedDirection = Shared::Railway::FrameWorld(hostWagon, localDirection, false);
        CHECK(Near(std::hypot(replayedDirection.x, replayedDirection.y, replayedDirection.z), 1.0f, 1e-3f));

        // A scaled frame keeps the local point in the frame's own units.
        const auto scaled = Wagon(-1.2f, 1.25f, {0.0f, 0.0f, 0.0f});
        Vector scaledLocal;
        CHECK(Shared::Railway::FrameLocal(scaled, hit, true, scaledLocal));
        CHECK(Near(Shared::Railway::FrameWorld(scaled, scaledLocal, true), hit));

        // A degenerate frame takes nothing.
        Shared::Railway::FrameMatrix flat {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {}};
        Vector unused;
        CHECK(!Shared::Railway::FrameLocal(flat, hit, true, unused));

        // The yaw frame is orthonormal, forward along the part, right handed
        // as the client's MakeBasis (right = up x forward).
        const auto yaw = Shared::Railway::YawFrame({5.0f, 1.0f, -3.0f}, {0.0f, 0.0f, 2.0f});
        CHECK(Near(yaw.forward, {0.0f, 0.0f, 1.0f}) && Near(yaw.right, {1.0f, 0.0f, 0.0f}) && Near(yaw.up, {0.0f, 1.0f, 0.0f}));
        const auto turned = Shared::Railway::YawFrame({0.0f, 0.0f, 0.0f}, {0.6f, 0.0f, -0.8f});
        CHECK(Near(turned.right.x * turned.forward.x + turned.right.z * turned.forward.z, 0.0f) && Near(turned.up, {0.0f, 1.0f, 0.0f}));

        // The server's check: the shooter's spot on its copy of the wagon,
        // put on the server's newest pose of it, must be the validated hit.
        const auto checkShot = [](const Shared::Railway::FrameMatrix &shooterPart, const Shared::Railway::FrameMatrix &serverPart, const Vector &worldHit,
                                  const Vector &worldDirection, const Vector &claimedHit, float speed) {
            Shared::Railway::ShotHit claim;
            claim.hit = worldHit;
            claim.direction = worldDirection;
            Shared::Railway::FrameLocal(shooterPart, claimedHit, true, claim.localHit);
            Shared::Railway::FrameLocal(shooterPart, worldDirection, false, claim.localDirection);
            return Shared::Railway::ShotHitMatchesPart(serverPart, claim, speed, 1.0f);
        };
        // A stopped tram: same pose on both ends.
        const auto stopped = Shared::Railway::YawFrame({200.0f, 0.0f, 100.0f}, {1.0f, 0.0f, 0.0f});
        const Vector hullHit {203.0f, 1.5f, 101.3f};
        const Vector sideways {0.0f, 0.0f, -1.0f};
        CHECK(checkShot(stopped, stopped, hullHit, sideways, hullHit, 0.0f));
        // The real shot moved 5 m down the wagon onto a seated rider.
        CHECK(!checkShot(stopped, stopped, hullHit, sideways, {208.0f, 1.5f, 101.3f}, 0.0f));
        // ...or across it, to the other side.
        CHECK(!checkShot(stopped, stopped, hullHit, sideways, {203.0f, 1.5f, 98.0f}, 0.0f));
        // ...or a different direction than the validated shot.
        {
            Shared::Railway::ShotHit claim;
            claim.hit = hullHit;
            claim.direction = sideways;
            Shared::Railway::FrameLocal(stopped, hullHit, true, claim.localHit);
            Shared::Railway::FrameLocal(stopped, {1.0f, 0.0f, 0.0f}, false, claim.localDirection);
            CHECK(!Shared::Railway::ShotHitMatchesPart(stopped, claim, 0.0f, 1.0f));
        }
        // At 10 m/s the shooter drew the tram 6 m behind the server's pose:
        // the same spot on the wagon is accepted along the track...
        const auto drawn = Shared::Railway::YawFrame({194.0f, 0.0f, 100.0f}, {1.0f, 0.0f, 0.0f});
        const Vector drawnHit {197.0f, 1.5f, 101.3f};
        CHECK(checkShot(drawn, stopped, drawnHit, sideways, drawnHit, 10.0f));
        // ...but not the same 6 m with the tram standing still.
        CHECK(!checkShot(drawn, stopped, drawnHit, sideways, drawnHit, 0.0f));
        // ...and never moved across it.
        CHECK(!checkShot(drawn, stopped, drawnHit, sideways, {197.0f, 1.5f, 97.0f}, 10.0f));
        // A local spot past the part's reach is refused outright.
        CHECK(!checkShot(stopped, stopped, {225.0f, 1.5f, 100.0f}, sideways, {225.0f, 1.5f, 100.0f}, 0.0f));

        Shared::Railway::ShotHit shot;
        shot.missionGeneration = 8;
        shot.shooterId = 40;
        shot.shooterSpawnGeneration = 2;
        shot.shotSequence = 77;
        shot.pelletIndex = 3;
        shot.index = 12;
        shot.part = 1;
        shot.direction = direction;
        shot.hit = hit;
        shot.localDirection = localDirection;
        shot.localHit = localHit;
        shot.damage = 18.5f;
        size_t bytes = 0;
        const auto read = RoundTrip(shot, &bytes);
        CHECK(bytes == 83);
        CHECK(read.missionGeneration == 8 && read.shooterId == 40 && read.shooterSpawnGeneration == 2 && read.shotSequence == 77 && read.pelletIndex == 3);
        CHECK(read.index == 12 && read.part == 1 && read.damage == 18.5f);
        CHECK(Near(read.localHit, localHit, 0.0f) && Near(read.hit, hit, 0.0f) && Near(read.localDirection, localDirection, 0.0f));
    }
} // namespace

int main() {
    ImpactMessages();
    ContactRules();
    ContactRate();
    SeizedMessage();
    RailwayShots();
    if (failures == 0) {
        std::printf("contact sync tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
