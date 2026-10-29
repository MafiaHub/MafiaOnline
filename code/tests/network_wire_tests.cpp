// Round trips of the quantized wire forms, the sender sample clock and the
// railway rider blend.
#include "client/src/features/car/car_pose_buffer.h"
#include "client/src/features/player/ride_blend.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/car/car_movement.h"
#include "shared/features/combat/aim_update.h"
#include "shared/features/combat/combat_protocol.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/pedestrian/pedestrian_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_movement.h"
#include "shared/features/railway/railway_report.h"
#include "shared/net/quantize.h"
#include "shared/net/sample_clock.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

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

    bool Near(float a, float b, float tolerance) {
        return std::abs(a - b) <= tolerance;
    }

    bool Near(const glm::vec3 &a, const glm::vec3 &b, float tolerance) {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
    }

    template <typename T>
    T RoundTrip(T value) {
        MafiaNet::BitStream stream;
        value.Serialize(&stream, true);
        T read;
        read.Serialize(&stream, false);
        return read;
    }

    template <typename T>
    void TransformRoundTrip(T &source, T &target) {
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        source.SerializeTransform(writer);
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        target.SerializeTransform(reader);
        CHECK(writer.Good() && reader.Good());
    }

    void FixedValues() {
        for (const float value : {-8192.0f, -3027.1f, -0.004f, 0.0f, 0.3f, 1148.7f, 3462.4f, 8192.0f}) {
            MafiaNet::BitStream stream;
            float written = value;
            Shared::Net::SerializeFixed(&stream, true, written, -8192.0f, 8192.0f, 20);
            float read = 0.0f;
            Shared::Net::SerializeFixed(&stream, false, read, -8192.0f, 8192.0f, 20);
            // A step of 16384 / (2^20 - 1), a hair over 1/64 m.
            CHECK(Near(read, value, 0.0079f));
            CHECK(read == Shared::Net::QuantizeFixed(value, -8192.0f, 8192.0f, 20));
        }
        // Out of range and not finite: the escape carries the raw float.
        for (const float value : {9000.5f, -1.0e7f, std::numeric_limits<float>::infinity()}) {
            MafiaNet::BitStream stream;
            float written = value;
            Shared::Net::SerializeFixed(&stream, true, written, -8192.0f, 8192.0f, 20);
            float read = 0.0f;
            Shared::Net::SerializeFixed(&stream, false, read, -8192.0f, 8192.0f, 20);
            CHECK(read == value);
        }
        MafiaNet::BitStream stream;
        float nan = std::numeric_limits<float>::quiet_NaN();
        Shared::Net::SerializeFixed(&stream, true, nan, 0.0f, 1.0f, 8);
        float read = 0.0f;
        Shared::Net::SerializeFixed(&stream, false, read, 0.0f, 1.0f, 8);
        CHECK(std::isnan(read));
    }

    void Angles() {
        for (const float angle : {-3.14159f, -1.0f, 0.0f, 0.5f, 3.1f, 3.5f, 7.0f}) {
            MafiaNet::BitStream stream;
            float written = angle;
            Shared::Net::SerializeAngle(&stream, true, written, 10);
            float read = 0.0f;
            Shared::Net::SerializeAngle(&stream, false, read, 10);
            const float difference = std::remainder(read - angle, glm::two_pi<float>());
            CHECK(std::abs(difference) <= glm::pi<float>() / 1024.0f + 1e-5f);
        }
        const glm::vec3 direction = glm::normalize(glm::vec3(0.3f, -0.4f, -0.8f));
        MafiaNet::BitStream stream;
        glm::vec3 written = direction;
        Shared::Net::SerializeDirection(&stream, true, written, 12, 11);
        glm::vec3 read {0.0f};
        Shared::Net::SerializeDirection(&stream, false, read, 12, 11);
        CHECK(glm::dot(read, direction) > std::cos(glm::radians(0.2f)));
    }

    void Quaternions() {
        const glm::quat cases[] = {glm::identity<glm::quat>(), glm::angleAxis(2.0f, glm::normalize(glm::vec3(0.2f, 1.0f, -0.3f))),
                                   glm::quat(-0.9f, 0.1f, 0.3f, 0.2f), glm::angleAxis(-3.1f, glm::vec3(0.0f, 0.0f, 1.0f))};
        for (glm::quat rotation : cases) {
            rotation = glm::normalize(rotation);
            MafiaNet::BitStream stream;
            glm::quat written = rotation;
            Shared::Net::SerializeQuat(&stream, true, written, 10);
            glm::quat read;
            Shared::Net::SerializeQuat(&stream, false, read, 10);
            // The same rotation, either sign, within a fifth of a degree.
            CHECK(std::abs(glm::dot(read, rotation)) > std::cos(glm::radians(0.2f) / 2.0f));
        }
    }

    void PlayerTransform() {
        Shared::Entities::PlayerEntity source;
        source.position     = {-3027.1f, 121.7f, 3462.4f};
        source.rotation     = glm::angleAxis(1.2f, glm::vec3(0.0f, 1.0f, 0.0f));
        source.velocity     = {3.0f, -0.5f, 70.0f};
        source.locomotion   = 0x123;
        source.sampleClock  = 65530;
        source.railway      = 7;
        source.railwayPart  = 2;
        source.railwayLocal = {0.4f, 1.2f, -6.75f};
        source.railwayYaw   = -2.0f;
        Shared::Entities::PlayerEntity target;
        TransformRoundTrip(source, target);
        CHECK(Near(target.position, source.position, 1.0f / 64.0f));
        CHECK(Near(Shared::Net::YawOf(target.rotation), 1.2f, 0.01f));
        CHECK(Near(target.velocity.x, 3.0f, 1.0f / 32.0f) && target.velocity.z == 70.0f);
        CHECK(target.locomotion == 0x123 && target.sampleClock == 65530);
        CHECK(target.railway == 7 && target.railwayPart == 2 && Near(target.railwayLocal, source.railwayLocal, 1.0f / 1024.0f) && Near(target.railwayYaw, -2.0f, 0.01f));

        source.railway = Shared::Railway::kNoRailway;
        source.position = {20000.0f, -900.0f, 5.0f};
        TransformRoundTrip(source, target);
        CHECK(target.railway == Shared::Railway::kNoRailway && target.railwayLocal == glm::vec3(0.0f));
        CHECK(target.position.x == 20000.0f && target.position.y == -900.0f);
    }

    void WalkerTransforms() {
        Shared::Entities::PedestrianEntity walker;
        walker.position    = {12.3f, 4.5f, -67.8f};
        walker.rotation    = glm::angleAxis(-0.7f, glm::vec3(0.0f, 1.0f, 0.0f));
        walker.velocity    = walker.rotation * glm::vec3(0.0f, 0.0f, 1.4f);
        walker.sampleClock = 1234;
        Shared::Entities::PedestrianEntity walkerRead;
        TransformRoundTrip(walker, walkerRead);
        CHECK(Near(walkerRead.position, walker.position, 1.0f / 64.0f));
        CHECK(Near(walkerRead.velocity, walker.velocity, 0.05f));
        CHECK(walkerRead.sampleClock == 1234);

        Shared::Entities::NpcEntity npc;
        npc.position   = {1.0f, 2.0f, 3.0f};
        npc.locomotion = -5;
        Shared::Entities::NpcEntity npcRead;
        TransformRoundTrip(npc, npcRead);
        CHECK(npcRead.locomotion == -5 && Near(npcRead.position, npc.position, 1.0f / 64.0f));
    }

    void CarTransform() {
        Shared::Entities::CarEntity car;
        car.position        = {500.25f, 30.0f, -700.5f};
        car.rotation        = glm::normalize(glm::quat(0.7f, 0.1f, 0.7f, -0.05f));
        car.velocity        = {25.0f, -1.0f, 140.0f};
        car.angularVelocity = {0.1f, -2.0f, 0.3f};
        car.poseClockMs     = 0xdeadbeef;
        car.poseClockSource = 0x1234567890ull;
        car.steeringInput   = -0.5f;
        car.engineRotations = 4321.0f;
        car.gear            = 3;
        car.powerInput      = 1.0f;
        car.clutchInput     = 0.5f;
        car.skidMask        = 0x5;
        Shared::Entities::CarEntity read;
        TransformRoundTrip(car, read);
        CHECK(Near(read.position, car.position, 1.0f / 256.0f));
        // Smallest three at 12 bits: within a twentieth of a degree.
        const glm::quat difference = read.rotation * glm::inverse(car.rotation);
        CHECK(2.0f * std::atan2(glm::length(glm::vec3(difference.x, difference.y, difference.z)), std::abs(difference.w)) < glm::radians(0.05f));
        CHECK(Near(read.velocity.x, 25.0f, 1.0f / 256.0f) && read.velocity.z == 140.0f);
        CHECK(read.poseClockMs == 0xdeadbeef && read.poseClockSource == Shared::Entities::CarEntity::ClockSourceTag(car.poseClockSource) && read.poseClockSource != 0);
        CHECK(read.gear == 3 && read.skidMask == 0x5 && Near(read.clutchInput, 0.5f, 1.0f / 63.0f) && read.powerInput == 1.0f);
        CHECK(Near(read.steeringInput, -0.5f, 0.001f) && read.engineRotations == 4321.0f);
    }

    void Reports() {
        Shared::Player::Movement movement;
        movement.networkId   = 77;
        movement.sequence    = 9;
        movement.clockMs     = 4000;
        movement.x           = -100.0f;
        movement.yaw         = 2.0f;
        movement.railway     = 3;
        movement.railwayPart = 1;
        movement.railwayZ    = -40.0f;
        const auto movementRead = RoundTrip(movement);
        CHECK(movementRead.networkId == 77 && movementRead.clockMs == 4000 && Near(movementRead.x, -100.0f, 0.0079f) && Near(movementRead.yaw, 2.0f, 0.002f));
        CHECK(movementRead.railway == 3 && movementRead.railwayPart == 1 && movementRead.railwayZ == -40.0f);

        Shared::Car::Movement car;
        car.networkId    = 5;
        car.fuel         = 12.5f;
        car.maximumGear  = 5;
        car.doorTargets  = {255, 0, 7};
        car.slowFields   = false;
        auto carRead     = RoundTrip(car);
        CHECK(carRead.networkId == 5 && !carRead.slowFields && carRead.fuel == 0.0f && carRead.maximumGear == 0);
        car.slowFields = true;
        carRead        = RoundTrip(car);
        CHECK(carRead.slowFields && carRead.fuel == 12.5f && carRead.maximumGear == 5 && carRead.doorTargets == car.doorTargets);

        Shared::Railway::State railway;
        railway.index            = 63;
        railway.speed            = -12.0f;
        railway.position         = {100.0f, 20.0f, -300.0f};
        railway.direction        = {0.0f, 0.0f, 1.0f};
        railway.wagonCount       = 2;
        railway.wagonPositions[1] = {100.0f, 20.5f, -330.0f};
        railway.wagonDirections[1] = {0.0f, 0.0f, 1.0f};
        const auto railwayRead   = RoundTrip(railway);
        CHECK(railwayRead.index == 63 && railwayRead.wagonCount == 2 && Near(railwayRead.speed, -12.0f, 0.04f));
        CHECK(Near(railwayRead.wagonPositions[1].z, -330.0f, 0.01f) && Near(railwayRead.wagonPositions[1].y, 20.5f, 0.01f));

        Shared::Combat::AimUpdate aim;
        aim.networkId        = 42;
        aim.sequence         = 65535;
        aim.aiming           = true;
        aim.direction        = glm::normalize(glm::vec3(1.0f, 0.2f, 0.0f));
        aim.poseTargetOffset = {3.0f, 1.5f, -9.0f};
        const auto aimRead   = RoundTrip(aim);
        CHECK(aimRead.networkId == 42 && aimRead.aiming && glm::dot(aimRead.direction, aim.direction) > 0.9999f && Near(aimRead.poseTargetOffset, aim.poseTargetOffset, 0.001f));
        CHECK(Shared::Combat::NewerSequence(0, 65535) && !Shared::Combat::NewerSequence(65535, 0));

        // A pistol shot carries one pellet, not ten.
        Shared::Combat::Event shot;
        shot.action              = Shared::Combat::Action::Fire;
        shot.pelletCount         = 1;
        shot.scatterRandom[1]    = 0.25f;
        shot.pellets[0].x        = 4.0f;
        shot.pellets[0].staticHit = true;
        shot.pellets[0].hitY     = 2.0f;
        MafiaNet::BitStream one;
        shot.Serialize(&one, true);
        shot.pelletCount = Shared::Combat::kMaximumShotPellets;
        MafiaNet::BitStream ten;
        shot.Serialize(&ten, true);
        CHECK(one.GetNumberOfBytesUsed() + 150 < ten.GetNumberOfBytesUsed());
        Shared::Combat::Event shotRead;
        shotRead.Serialize(&one, false);
        CHECK(shotRead.pelletCount == 1 && shotRead.scatterRandom[1] == 0.25f && shotRead.pellets[0].x == 4.0f && shotRead.pellets[0].hitY == 2.0f);
    }

    void SampleClocks() {
        // 50 ms samples whose 16-bit clock wraps, arriving 40-60 ms late.
        Shared::Net::SampleClockMapper mapper;
        double previous = 0.0;
        for (int i = 0; i < 400; ++i) {
            const uint64_t sent  = 60000 + static_cast<uint64_t>(i) * 50;
            const double arrival = 1.0e6 + static_cast<double>(sent) + 40.0 + (i * 7 % 21);
            const double mapped  = mapper.Map(Shared::Net::SampleClock(sent), arrival);
            if (i > 60) {
                // Steady spacing across the wrap: the late arrivals do not move it.
                CHECK(std::abs(mapped - previous - 50.0) < 1.01);
                CHECK(mapped <= arrival);
            }
            previous = mapped;
        }
    }

    void HeldSampleClocks() {
        // The server keeps a relayed pose's clock while its wire form holds.
        Shared::Entities::PedestrianEntity walker;
        // On the 1/64 m grid, so a millimetre stays in the same step.
        walker.position    = {Shared::Net::QuantizeFixed(10.0f, -Shared::Net::kWorldHalfExtent, Shared::Net::kWorldHalfExtent, Shared::Net::kWorldPlaneBits), 2.0f, -5.0f};
        walker.sampleClock = 100;
        Shared::Net::RelayPose(walker, 200, [&] { walker.position.x += 0.001f; }); // Under 1/64 m.
        CHECK(walker.sampleClock == 100);
        Shared::Net::RelayPose(walker, 300, [&] { walker.position.x += 0.5f; });
        CHECK(walker.sampleClock == 300);
        // A held clock sent again (an idle refresh) maps to the time it had,
        // however late it arrives, and the next new one still maps normally.
        Shared::Net::SampleClockMapper mapper;
        const double first = mapper.Map(1000, 5000.0);
        CHECK(mapper.Map(1000, 9000.0) == first);
        CHECK(std::abs(mapper.Map(1050, 5050.0) - (first + 50.0)) < 0.01);
    }

    Features::Car::CarPoseSample CarAt(double time, float x, float speed) {
        Features::Car::CarPoseSample pose;
        pose.time     = time;
        pose.position = {x, 0.0f, 0.0f};
        pose.velocity = {speed, 0.0f, 0.0f};
        return pose;
    }

    void CarResumeAfterHeartbeat() {
        // Parked, its unchanged pose not resent for 3 s, then pulling away.
        Features::Car::CarPoseBuffer buffer;
        for (double t = 1000.0; t <= 1500.0; t += 33.0) {
            buffer.Push(CarAt(t, 0.0f, 0.0f), t, false);
        }
        buffer.Push(CarAt(4500.0, 0.05f, 3.0f), 4500.0, false);
        Features::Car::CarPoseSample out;
        // Still parked until one interval before the first moving pose, not
        // creeping (or overshooting on its tangent) across the pause.
        CHECK(buffer.Sample(4400.0, out) && out.position.x == 0.0f);
        CHECK(buffer.Sample(4480.0, out) && out.position.x >= 0.0f && out.position.x <= 0.05f);
        CHECK(Near(buffer.IntervalMs(), 33.0f, 1.0f));
    }

    // A car at 20 m/s that brakes to a stop from 2.5 s, rendered every 16
    // ms; `interval` gives the spacing of the poses sent at a time, `late`
    // how late each arrives (in order). Returns the largest one-frame
    // backward step of the shown car: an extrapolation the braking car's
    // poses land behind is taken back over ~200 ms, a few cm a frame, where
    // jumping to them would step back up to 0.5 a (0.15 s)^2 = 0.22 m.
    template <typename Interval, typename Late>
    float DriveCar(Features::Car::CarPoseBuffer &buffer, double until, Interval interval, Late late, float *maxStep) {
        double sent       = 1000.0;
        float lastX       = -1.0f;
        float maxBackward = 0.0f;
        *maxStep          = 0.0f;
        std::vector<std::pair<double, Features::Car::CarPoseSample>> inFlight;
        for (double now = 1000.0; now < until; now += 16.0) {
            while (sent <= now) {
                // Braking hard from 2.5 s: the newest pose then lags the
                // extrapolation an observer runs.
                const float speed = sent < 2500.0 ? 20.0f : std::max(0.0f, 20.0f - static_cast<float>(sent - 2500.0) * 0.02f);
                const float x     = sent < 2500.0 ? static_cast<float>(sent - 1000.0) * 0.02f
                                                  : 30.0f + (20.0f + speed) * 0.5f * static_cast<float>(std::min(sent - 2500.0, 1000.0)) * 0.001f;
                inFlight.push_back({sent + late(sent), CarAt(sent, x, speed)});
                sent += interval(sent);
            }
            for (auto it = inFlight.begin(); it != inFlight.end();) {
                if (it->first <= now) {
                    buffer.Push(it->second, it->first, false);
                    it = inFlight.erase(it);
                }
                else {
                    ++it;
                }
            }
            const double renderTime = buffer.AdvanceDelay(now, 16.0f);
            Features::Car::CarPoseSample shown;
            if (!buffer.Present(renderTime, 0.016f, shown)) {
                continue;
            }
            if (lastX >= 0.0f) {
                maxBackward = std::max(maxBackward, lastX - shown.position.x);
                *maxStep    = std::max(*maxStep, shown.position.x - lastX);
            }
            lastX = shown.position.x;
        }
        return maxBackward;
    }

    void CarBandChange() {
        // 30 Hz, then the 7.5 Hz band from 2 s: the delay follows the interval
        // and the car never steps back.
        Features::Car::CarPoseBuffer buffer;
        float maxStep        = 0.0f;
        const float backward = DriveCar(buffer, 6000.0, [](double t) { return t < 2000.0 ? 33.0 : 133.0; }, [](double) { return 20.0; }, &maxStep);
        CHECK(backward < 0.05f);
        CHECK(maxStep < 0.6f);
        CHECK(buffer.IntervalMs() > 110.0f && buffer.DelayMs() > 120.0f);
    }

    void CarLateCorrection() {
        // A 250 ms hiccup as the braking car stops holds its poses back, then
        // delivers them at once: it underruns, extrapolates at the 4.7 m/s it
        // had past where it stopped, and the poses land behind it.
        Features::Car::CarPoseBuffer buffer;
        float maxStep        = 0.0f;
        const float backward = DriveCar(
            buffer, 5000.0, [](double) { return 33.0; }, [](double t) { return t >= 3300.0 && t < 3550.0 ? 3560.0 - t : 10.0; }, &maxStep);
        CHECK(backward < 0.05f);
        CHECK(maxStep < 0.6f);
        // A correction decays: 7% after 200 ms, so at most a few centimetres
        // a frame are ever taken back.
        Features::Car::CarPoseBuffer snap;
        snap.Push(CarAt(1000.0, 0.0f, 10.0f), 1000.0, false);
        snap.Push(CarAt(1033.0, 0.33f, 10.0f), 1033.0, false);
        Features::Car::CarPoseSample shown;
        CHECK(snap.Present(1150.0, 0.016f, shown) && Near(shown.position.x, 1.5f, 0.01f)); // Extrapolated 117 ms.
        snap.Push(CarAt(1150.0, 0.5f, 0.0f), 1150.0, false);                              // It had stopped.
        CHECK(snap.Present(1150.0, 0.016f, shown) && shown.position.x > 1.2f);
        CHECK(snap.Present(1150.0, 0.2f, shown) && shown.position.x < 0.6f);
        // A jump beyond 5 m is applied at once.
        CHECK(snap.Present(1250.0, 0.016f, shown) && shown.position.x < 0.6f);
        snap.Push(CarAt(1200.0, 20.0f, 0.0f), 1200.0, false);
        CHECK(snap.Present(1250.0, 0.016f, shown) && Near(shown.position.x, 20.0f, 0.001f));
    }

    void RideBlend() {
        using Features::Player::RemoteRide;
        std::deque<std::pair<MafiaNet::Time, RemoteRide>> rides;
        rides.push_back({1000, RemoteRide {}});
        rides.push_back({1050, RemoteRide {4, 1, {0.0f, 0.0f, 2.0f}, 0.0f}});
        rides.push_back({1100, RemoteRide {4, 1, {0.0f, 0.0f, 4.0f}, 3.0f}});
        rides.push_back({1150, RemoteRide {4, 1, {0.0f, 0.0f, 6.0f}, -3.0f}});
        RemoteRide ride;
        float weight = 0.0f;
        // Before boarding: nowhere near a ride.
        CHECK(!Features::Player::RideAt(rides, 1020.0, ride, weight));
        // Boarding blends in over kRideBlendMs from the first sample aboard.
        CHECK(Features::Player::RideAt(rides, 1075.0, ride, weight) && Near(weight, 25.0f / 150.0f, 1e-4f));
        CHECK(Near(ride.local.z, 3.0f, 1e-4f) && Near(ride.yaw, 1.5f, 1e-4f));
        // The short way round from 3 to -3 radians, through pi.
        CHECK(Features::Player::RideAt(rides, 1125.0, ride, weight) && std::abs(ride.yaw) > 3.1f);
        CHECK(Features::Player::RideAt(rides, 1300.0, ride, weight) && weight == 1.0f && ride.local.z == 6.0f);
        // Stepping off eases back out from the last offset aboard.
        rides.push_back({1200, RemoteRide {}});
        CHECK(Features::Player::RideAt(rides, 1260.0, ride, weight) && Near(weight, 1.0f - 60.0f / 150.0f, 1e-4f) && ride.local.z == 6.0f);
        CHECK(!Features::Player::RideAt(rides, 1400.0, ride, weight));
        // Aboard from the start of the history: no blend.
        rides.erase(rides.begin());
        CHECK(Features::Player::RideAt(rides, 1060.0, ride, weight) && weight == 1.0f);
    }
} // namespace

int main() {
    FixedValues();
    Angles();
    Quaternions();
    PlayerTransform();
    WalkerTransforms();
    CarTransform();
    Reports();
    SampleClocks();
    HeldSampleClocks();
    CarResumeAfterHeartbeat();
    CarBandChange();
    CarLateCorrection();
    RideBlend();
    if (failures == 0) {
        std::printf("network wire tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
