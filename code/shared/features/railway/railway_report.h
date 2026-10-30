#pragma once

#include "shared/net/quantize.h"

#include <networking/rpc/rpc.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Railway {
    // Trams and metro trains (scene2.bin type 8, C_railway). Every client
    // loads the same records in the same order; an index is a position in
    // that order and nameHash (FNV-1a of the frame name) proves it matches.
    inline constexpr uint8_t kMaxRailways = 64;
    // C_railway::LoadData wagons behind the body; freeride metros have one.
    inline constexpr uint8_t kMaxWagons = 4;
    // Players are sent the railways this close to them in every snapshot,
    // and every other one (seen from afar, the metro's raised track most of
    // all) in one snapshot of kFarSnapshotDivider, a slice of them at a time.
    inline constexpr float kStreamRange = 600.0f;
    inline constexpr uint32_t kFarSnapshotDivider = 5;

    inline uint32_t NameHash(const char *name) {
        uint32_t hash = 2166136261u;
        for (; name && *name; ++name) {
            hash = (hash ^ static_cast<uint8_t>(*name)) * 16777619u;
        }
        return hash;
    }

    struct Vector {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
        }
    };

    // What C_railway::Update places the frames and dynamic collision from:
    // the body at C_actor +0x24/+0x30 and each wagon from the arrays at
    // +0xbc/+0xc0.
    struct State {
        uint8_t index = 0;
        uint32_t nameHash = 0;
        // E_railway_state (+0xdc) and speed (+0xd4), for the doors and sounds.
        uint8_t state = 0;
        float speed = 0.0f;
        // rail_doors angle at +0x228 (0 shut, pi/2 open), which AI drives
        // (Tick 0x496900) and Update applies to the door frames.
        float doorAngle = 0.0f;
        Vector position;
        Vector direction;
        uint8_t wagonCount = 0;
        std::array<Vector, kMaxWagons> wagonPositions {};
        std::array<Vector, kMaxWagons> wagonDirections {};

        // About 20 bytes plus 10 a wagon (39 plus 24 before): the body at
        // 1/64 m, wagons as offsets from it at 1/512 m, directions as yaw
        // and pitch, speed at 1/16 m/s.
        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->SerializeBitsFromIntegerRange(write, index, uint8_t {0}, static_cast<uint8_t>(kMaxRailways - 1));
            stream->Serialize(write, nameHash);
            stream->Serialize(write, state);
            Net::SerializeFixed(stream, write, speed, -32.0f, 32.0f, 10);
            Net::SerializeFixed(stream, write, doorAngle, 0.0f, 1.6f, 8);
            glm::vec3 body {position.x, position.y, position.z};
            glm::vec3 facing {direction.x, direction.y, direction.z};
            Net::SerializeWorldPosition(stream, write, body);
            Net::SerializeDirection(stream, write, facing, 12, 11);
            if (!write) {
                position  = {body.x, body.y, body.z};
                direction = {facing.x, facing.y, facing.z};
            }
            stream->SerializeBitsFromIntegerRange(write, wagonCount, uint8_t {0}, kMaxWagons);
            if (wagonCount > kMaxWagons) {
                wagonCount = 0;
                return;
            }
            for (uint8_t i = 0; i < wagonCount; ++i) {
                glm::vec3 offset {wagonPositions[i].x - body.x, wagonPositions[i].y - body.y, wagonPositions[i].z - body.z};
                glm::vec3 wagonFacing {wagonDirections[i].x, wagonDirections[i].y, wagonDirections[i].z};
                Net::SerializeFixed(stream, write, offset.x, -128.0f, 128.0f, 17);
                Net::SerializeFixed(stream, write, offset.y, -128.0f, 128.0f, 17);
                Net::SerializeFixed(stream, write, offset.z, -128.0f, 128.0f, 17);
                Net::SerializeDirection(stream, write, wagonFacing, 12, 11);
                if (!write) {
                    wagonPositions[i]  = {body.x + offset.x, body.y + offset.y, body.z + offset.z};
                    wagonDirections[i] = {wagonFacing.x, wagonFacing.y, wagonFacing.z};
                }
            }
        }
    };

    // Simulation host -> server at kSnapshotHz with every railway; server ->
    // each player with those within kStreamRange. hostTimeMs and
    // hostTimeFraction (1/256 ms past it) date the poses by the host's
    // frame clock (World::FrameClock) at the step that produced them, for
    // the players' interpolation.
    struct Snapshot {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::RailwaySnapshot");
        static constexpr int kSnapshotHz = 10;

        uint64_t missionGeneration = 0;
        // The host's process id (Simulation::HostSession) and its clock.
        uint32_t hostSession = 0;
        uint32_t hostTimeMs = 0;
        uint8_t hostTimeFraction = 0;
        uint8_t count = 0;
        std::array<State, kMaxRailways> railways {};

        // The stamp of a clock in ms, and the clock back from one.
        void SetHostTime(double milliseconds) {
            const double whole = std::floor(milliseconds);
            hostTimeMs = static_cast<uint32_t>(static_cast<int64_t>(whole));
            hostTimeFraction = static_cast<uint8_t>(std::clamp((milliseconds - whole) * 256.0, 0.0, 255.0));
        }
        [[nodiscard]] double HostTimeFraction() const {
            return static_cast<double>(hostTimeFraction) / 256.0;
        }

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, hostSession);
            stream->Serialize(write, hostTimeMs);
            stream->Serialize(write, hostTimeFraction);
            stream->Serialize(write, count);
            if (count > kMaxRailways) {
                count = 0;
                return;
            }
            for (uint8_t i = 0; i < count; ++i) {
                railways[i].Serialize(stream, write);
            }
        }
    };

    // Retail C_human::Use_Actor (0x582180) locks a railway while the human
    // steps aboard (C_railway::Lock 0x497980) and Intern_UseRailway unlocks
    // it, so it cannot depart mid-step. A player's step locks the host's.
    //
    // Player -> server -> host: the local player wants to step aboard.
    struct BoardRequest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::RailwayBoardRequest");

        uint64_t missionGeneration = 0;
        uint8_t index = 0;
        uint64_t playerId = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, index);
            stream->Serialize(write, playerId);
        }
    };

    // Host -> server -> player: the railway is held for the step, or it is
    // already leaving. Stepping off is held the same way: retail Use_Actor
    // locks for both, and Intern_UseRailway(false) unlocks.
    struct BoardAnswer {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::RailwayBoardAnswer");

        uint64_t missionGeneration = 0;
        uint8_t index = 0;
        uint64_t playerId = 0;
        bool accepted = false;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, index);
            stream->Serialize(write, playerId);
            stream->Serialize(write, accepted);
        }
    };

    // Player -> server -> host: the step ended (aboard or interrupted); the
    // host releases its lock, at the latest after kBoardLockLimit.
    struct BoardDone {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::RailwayBoardDone");

        uint64_t missionGeneration = 0;
        uint8_t index = 0;
        uint64_t playerId = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, index);
            stream->Serialize(write, playerId);
        }
    };
    inline constexpr auto kBoardLockLimitMs = 5000;
    // Host: one stop is held for players' steps at most this long in all,
    // so nobody keeps a railway at its stop by asking over and over.
    inline constexpr auto kMaxStopHoldMs = 20000;
    // Server: a player asks to step aboard or off at most this often.
    inline constexpr auto kBoardRequestIntervalMs = 1000;
    // A player steps aboard from this close to the railway's body or wagon.
    inline constexpr float kBoardReach = 30.0f;

    // Where AI keeps the doorway use objects: C_railway::isInStop (0x496db0;
    // station arrival to the station wait, or a tram, not a metro, standing
    // at exactly 0 in collision or semaphore braking, a rail human stop or
    // following another railway) plus the schedule wait (5), which keeps the
    // station's doorways until it departs.
    inline bool OffersEntries(uint8_t state, float speed, bool metro) {
        if (state >= 1 && state <= 5) {
            return true;
        }
        return (state == 7 || state == 10 || state == 11 || state == 12) && !metro && speed == 0.0f;
    }

    // Host: a stop whose departure waits for the lock count (+0x288) a step
    // holds. Collision braking (7) departs after its wait regardless
    // (reM C_railway.cpp:3132), so a step is not started there; the rail
    // human stop (11) leaves only by the host's own resume, which waits too.
    inline bool HoldsForStep(uint8_t state, float speed, bool metro) {
        return state != 7 && OffersEntries(state, speed, metro);
    }

    // A player's position aboard, relative to the body (wagon 0) or a wagon:
    // the basis is the railway direction with the world up, so every client
    // rebuilds it from its own copy of the railway.
    inline constexpr uint8_t kNoRailway = 0xff;

    // One passenger of a railway (reM S_rail_human, the driver left out):
    // where its model stands in a part's basis, and its ranims animation.
    struct Passenger {
        uint8_t index = 0;
        // The host's id for it, unique while it rides.
        uint16_t id = 0;
        uint8_t part = 0;
        Vector local;
        float yaw = 0.0f;
        uint8_t animation = 0;
        // Sent on its first reports and then now and again (kModelEvery);
        // empty otherwise, as players keep the model they made for the id.
        std::string model;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, index);
            stream->Serialize(write, id);
            stream->Serialize(write, part);
            local.Serialize(stream, write);
            stream->Serialize(write, yaw);
            stream->Serialize(write, animation);
            stream->Serialize(write, model);
        }
    };

    // Simulation host -> server -> players near the railways, kPassengersHz:
    // every passenger of the listed railways, so one missing is gone. Players
    // show them as models; the host's walkers become them at the doors
    // (RailwayInStation 0x450a60) and walk away from them (AddFreePerson).
    struct Passengers {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::RailwayPassengers");
        static constexpr int kPassengersHz = 5;
        static constexpr uint8_t kMaxPassengers = 128;
        static constexpr size_t kMaxModelLength = 63;
        // A passenger's model rides with its first kModelFirstReports reports
        // and one in kModelEvery after (a lost first report costs 2 s).
        static constexpr uint8_t kModelFirstReports = 3;
        static constexpr uint32_t kModelEvery = 10;

        uint64_t missionGeneration = 0;
        uint8_t railwayCount = 0;
        std::array<uint8_t, kMaxRailways> railways {};
        uint8_t count = 0;
        std::array<Passenger, kMaxPassengers> passengers {};

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, railwayCount);
            if (railwayCount > kMaxRailways) {
                railwayCount = 0;
                count = 0;
                return;
            }
            for (uint8_t i = 0; i < railwayCount; ++i) {
                stream->Serialize(write, railways[i]);
            }
            stream->Serialize(write, count);
            if (count > kMaxPassengers) {
                count = 0;
                return;
            }
            for (uint8_t i = 0; i < count; ++i) {
                passengers[i].Serialize(stream, write);
            }
        }
    };

    // Simulation host -> server: one of its railways ran into a player's
    // replica there (C_railway::Collide -> C_human::Collision, railway
    // branch: HIT_TYPE_MELEE_STRIKE). The host is trusted with it as with
    // its NPCs' hits; the server checks the railway is near the player.
    struct PlayerHit {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::RailwayPlayerHit");

        uint64_t missionGeneration = 0;
        uint8_t index = 0;
        uint64_t targetNetworkId = 0;
        uint64_t targetSpawnGeneration = 0;
        float damage = 0.0f;
        Vector direction;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, index);
            stream->Serialize(write, targetNetworkId);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, damage);
            direction.Serialize(stream, write);
        }
    };

    // A player's bullet that hit a railway on that player's own screen.
    // Players draw each railway a render delay plus a trip behind the host,
    // so the host's replay of the bullet against its own railway misses a
    // passenger on a moving one. The shooter reports the hit in the yaw frame
    // of the part it struck (YawFrame; 0 the body, n wagon n - 1);
    // the server checks the shot against the shooter's accepted fire and the
    // railway's place, sets the damage and forwards it to the simulation
    // host, which replays C_railway::Hit at the same place in that part.
    struct ShotHit {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::RailwayShotHit");

        uint64_t missionGeneration = 0;
        uint64_t shooterId = 0;
        uint64_t shooterSpawnGeneration = 0;
        uint32_t shotSequence = 0;
        uint8_t pelletIndex = 0;
        uint8_t index = 0;
        uint8_t part = 0;
        // The world shot direction (unit) and hit point, as the shooter's
        // TickShoot passed them, for the server's check against the fire.
        Vector direction;
        Vector hit;
        // The same in the part's YawFrame (FrameLocal).
        Vector localDirection;
        Vector localHit;
        // Server -> host only: the bullet's damage after range falloff.
        float damage = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, shooterId);
            stream->Serialize(write, shooterSpawnGeneration);
            stream->Serialize(write, shotSequence);
            stream->Serialize(write, pelletIndex);
            stream->Serialize(write, index);
            stream->Serialize(write, part);
            direction.Serialize(stream, write);
            hit.Serialize(stream, write);
            localDirection.Serialize(stream, write);
            localHit.Serialize(stream, write);
            stream->Serialize(write, damage);
        }
    };

    // A railway part frame's world matrix as I3D keeps it: rows right, up,
    // forward (scaled) and the origin.
    struct FrameMatrix {
        Vector right;
        Vector up;
        Vector forward;
        Vector origin;
    };

    // A world point (or, with point false, a direction) in the frame's own
    // coordinates: the inverse of the matrix for its orthogonal rows, as
    // RideOf takes a rider's pose. False for a degenerate matrix.
    inline bool FrameLocal(const FrameMatrix &frame, const Vector &world, bool point, Vector &local) {
        const auto dot = [](const Vector &a, const Vector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        const float rr = dot(frame.right, frame.right);
        const float uu = dot(frame.up, frame.up);
        const float ff = dot(frame.forward, frame.forward);
        if (!(rr > 1e-6f) || !(uu > 1e-6f) || !(ff > 1e-6f)) {
            return false;
        }
        const Vector offset = point ? Vector {world.x - frame.origin.x, world.y - frame.origin.y, world.z - frame.origin.z} : world;
        local = {dot(offset, frame.right) / rr, dot(offset, frame.up) / uu, dot(offset, frame.forward) / ff};
        return std::isfinite(local.x) && std::isfinite(local.y) && std::isfinite(local.z);
    }

    // The way back, on another client's copy of the frame.
    inline Vector FrameWorld(const FrameMatrix &frame, const Vector &local, bool point) {
        Vector world {frame.right.x * local.x + frame.up.x * local.y + frame.forward.x * local.z, frame.right.y * local.x + frame.up.y * local.y + frame.forward.y * local.z,
                      frame.right.z * local.x + frame.up.z * local.y + frame.forward.z * local.z};
        if (point) {
            world.x += frame.origin.x;
            world.y += frame.origin.y;
            world.z += frame.origin.z;
        }
        return world;
    }

    // A reported hit no farther than this from its part's origin: half a
    // metro wagon with room for the shot's entry point on its hull.
    inline constexpr float kShotHitPartReach = 20.0f;

    // A part's yaw frame: its reported position (C_actor +0x24 for the body,
    // the wagon arrays +0xbc for a wagon) with unit rows built from its
    // direction and the world up, as passengers' rides are (Passenger). The
    // part's frames hang rigidly from that pose (C_railway::Update places
    // them from it), so a point kept in this frame lands on the same spot of
    // the model on every copy, and the server can build it from the State
    // it holds. Right is up x forward, as the client's MakeBasis.
    inline FrameMatrix YawFrame(const Vector &origin, const Vector &direction) {
        float fx = direction.x;
        float fy = direction.y;
        float fz = direction.z;
        float length = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (!(length > 0.0001f) || !std::isfinite(length)) {
            fx = 0.0f;
            fy = 0.0f;
            fz = 1.0f;
            length = 1.0f;
        }
        fx /= length;
        fy /= length;
        fz /= length;
        // up (0, 1, 0) x forward.
        float rx = fz;
        float rz = -fx;
        const float rightLength = std::sqrt(rx * rx + rz * rz);
        if (rightLength > 0.0001f) {
            rx /= rightLength;
            rz /= rightLength;
        }
        else {
            rx = 1.0f;
            rz = 0.0f;
        }
        // forward x right.
        const Vector up {fy * rz, fz * rx - fx * rz, -fy * rx};
        return {{rx, 0.0f, rz}, up, {fx, fy, fz}, origin};
    }

    // How far the reported spot may be from where the server's newest pose
    // of the part puts it. The shooter draws the railway a render delay and
    // a trip behind (lagSeconds); along the track that is the speed times
    // it, across it and up only a curve's bend, plus the slack of the hull
    // point itself.
    inline constexpr float kShotHitSlack = 2.0f;
    inline constexpr float kShotHitCurveSlack = 2.5f;
    inline constexpr float kShotHitDirectionMatch = 0.95f;

    // The server's check of a ShotHit against the part's newest yaw frame:
    // the local spot, put on that frame, lies where the shot's validated
    // world hit is, and the local direction is the shot's.
    inline bool ShotHitMatchesPart(const FrameMatrix &part, const ShotHit &shot, float speed, float lagSeconds) {
        const auto dot = [](const Vector &a, const Vector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        const float localLength = std::sqrt(dot(shot.localHit, shot.localHit));
        const float directionLength = std::sqrt(dot(shot.localDirection, shot.localDirection));
        if (!std::isfinite(localLength) || localLength > kShotHitPartReach || !std::isfinite(directionLength) || directionLength < 0.8f || directionLength > 1.2f ||
            !std::isfinite(speed) || !std::isfinite(lagSeconds)) {
            return false;
        }
        const Vector placed = FrameWorld(part, shot.localHit, true);
        const Vector residual {shot.hit.x - placed.x, shot.hit.y - placed.y, shot.hit.z - placed.z};
        const float along = std::abs(dot(residual, part.forward));
        const float across = std::abs(dot(residual, part.right));
        const float vertical = std::abs(dot(residual, part.up));
        if (!(along <= std::abs(speed) * lagSeconds + kShotHitSlack) || !(across <= kShotHitCurveSlack) || !(vertical <= kShotHitSlack)) {
            return false;
        }
        const Vector direction = FrameWorld(part, shot.localDirection, false);
        const float worldLength = std::sqrt(dot(shot.direction, shot.direction));
        const float placedLength = std::sqrt(dot(direction, direction));
        return worldLength > 0.0001f && placedLength > 0.0001f && dot(direction, shot.direction) / (worldLength * placedLength) >= kShotHitDirectionMatch;
    }
} // namespace Mafia1Online::Shared::Railway
