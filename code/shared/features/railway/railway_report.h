#pragma once

#include <networking/rpc/rpc.h>

#include <array>
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
    // all) in one snapshot of kFarSnapshotDivider.
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

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, index);
            stream->Serialize(write, nameHash);
            stream->Serialize(write, state);
            stream->Serialize(write, speed);
            stream->Serialize(write, doorAngle);
            position.Serialize(stream, write);
            direction.Serialize(stream, write);
            stream->Serialize(write, wagonCount);
            if (wagonCount > kMaxWagons) {
                wagonCount = 0;
                return;
            }
            for (uint8_t i = 0; i < wagonCount; ++i) {
                wagonPositions[i].Serialize(stream, write);
                wagonDirections[i].Serialize(stream, write);
            }
        }
    };

    // Simulation host -> server at kSnapshotHz with every railway; server ->
    // each player with those within kStreamRange. hostTimeMs dates the
    // poses on the host's clock for the players' interpolation.
    struct Snapshot {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::RailwaySnapshot");
        static constexpr int kSnapshotHz = 10;

        uint64_t missionGeneration = 0;
        // The host's process id (Simulation::HostSession) and its clock.
        uint32_t hostSession = 0;
        uint32_t hostTimeMs = 0;
        uint8_t count = 0;
        std::array<State, kMaxRailways> railways {};

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, hostSession);
            stream->Serialize(write, hostTimeMs);
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
} // namespace Mafia1Online::Shared::Railway
