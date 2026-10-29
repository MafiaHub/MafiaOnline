#pragma once

#include <networking/rpc/rpc.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace Mafia1Online::Shared::Pumper {
    // Gas station attendants (scene2.bin type 25, C_pumper). Every client
    // loads them in the same order; an index is a position in that order and
    // nameHash (FNV-1a of the frame name) proves it matches. The Free Ride
    // scenes have three.
    inline constexpr uint8_t kMaxPumpers = 8;
    // E_pumper_state; Panic (17) never happens here, panics are blocked.
    inline constexpr uint8_t kStateCount = 18;
    inline constexpr uint8_t kIdle = 1;
    inline constexpr uint8_t kFueling = 5;
    inline constexpr uint8_t kBusyDialogue = 6;
    inline constexpr uint8_t kRequestDialogue = 7;
    inline constexpr uint8_t kRequestReply = 8;
    inline constexpr uint8_t kBusyReply = 9;
    inline constexpr uint8_t kPumpReady = 10;
    // anim_info indices are small; the special ones run 0x10000-0x10008.
    inline constexpr int32_t kMaxAnimation = 0x10008;

    inline uint32_t NameHash(const char *name) {
        uint32_t hash = 2166136261u;
        for (; name && *name; ++name) {
            hash = (hash ^ static_cast<uint8_t>(*name)) * 16777619u;
        }
        return hash;
    }

    // reM C_pumper::IsPumping: walking, or past idle up to PUMP_READY. Retail
    // ignores Start in none of them; a service may begin only outside them.
    constexpr bool Pumping(uint8_t state) {
        return state == 0 || (state > kIdle && state <= kPumpReady);
    }

    constexpr const char *StateName(uint8_t state) {
        switch (state) {
        case 0: return "walking";
        case 1: return "idle";
        case 2: return "turning";
        case 3: return "turning";
        case 4: return "done";
        case 5: return "fueling";
        case 6: return "done";
        case 7: return "greeting";
        case 8: return "greeting";
        case 9: return "done";
        case 10: return "fueling";
        case 11:
        case 12: return "idle";
        case 13:
        case 14:
        case 15:
        case 16: return "smoking";
        default: return "unknown";
        }
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

    // What every player needs to replay an attendant: the state, the
    // animation SetAnim plays (+0xd8, +0xdc, +0xdd) and the actor's position
    // and direction (+0x24, +0x30); carId is the network car he serves.
    struct State {
        uint8_t index = 0;
        uint32_t nameHash = 0;
        uint8_t state = kIdle;
        int32_t animation = -1;
        bool finite = false;
        bool track = false;
        Vector position;
        Vector direction;
        uint64_t carId = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, index);
            stream->Serialize(write, nameHash);
            stream->Serialize(write, state);
            stream->Serialize(write, animation);
            stream->Serialize(write, finite);
            stream->Serialize(write, track);
            position.Serialize(stream, write);
            direction.Serialize(stream, write);
            stream->Serialize(write, carId);
        }
    };

    inline bool Finite(const Vector &vector, float limit) {
        return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z) && std::fabs(vector.x) <= limit && std::fabs(vector.y) <= limit &&
               std::fabs(vector.z) <= limit;
    }

    inline bool Valid(const State &state) {
        return state.index < kMaxPumpers && state.state < kStateCount && state.state != 17 && state.animation >= -1 && state.animation <= kMaxAnimation &&
               Finite(state.position, 100000.0f) && Finite(state.direction, 100.0f);
    }

    // Simulation host -> server at kSnapshotHz, and at once (reliable) when
    // an attendant changes state or animation; server -> every spawned player.
    struct Snapshot {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PumperSnapshot");
        static constexpr int kSnapshotHz = 10;

        uint64_t missionGeneration = 0;
        uint32_t hostSession = 0;
        uint32_t hostTimeMs = 0;
        uint8_t count = 0;
        std::array<State, kMaxPumpers> pumpers {};

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, hostSession);
            stream->Serialize(write, hostTimeMs);
            stream->Serialize(write, count);
            if (count > kMaxPumpers) {
                count = 0;
                return;
            }
            for (uint8_t i = 0; i < count; ++i) {
                pumpers[i].Serialize(stream, write);
            }
        }
    };

    // Enumerations travel as their byte; an unknown one reads as max, which
    // every handler drops.
    template <typename Kind>
    void SerializeKind(MafiaNet::BitStream *stream, bool write, Kind &kind) {
        auto value = static_cast<uint8_t>(kind);
        stream->Serialize(write, value);
        if (!write) {
            kind = static_cast<Kind>(value <= static_cast<uint8_t>(Kind::Last) ? value : static_cast<uint8_t>(0xff));
        }
    }

    // Player -> server: the retail "use the pump" choice (C_player::
    // Do_AB_OwnerCar -> C_pumper::Start) for the car the player drives, or
    // the withdrawal of an accepted one whose native Start then failed.
    struct Request {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PumperRequest");
        enum class Kind : uint8_t {
            Serve = 0,
            Cancel = 1,
            Last = Cancel,
        };

        uint64_t missionGeneration = 0;
        uint8_t index = 0;
        uint32_t nameHash = 0;
        uint64_t carId = 0;
        Kind kind = Kind::Serve;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, index);
            stream->Serialize(write, nameHash);
            stream->Serialize(write, carId);
            SerializeKind(stream, write, kind);
        }
    };

    // Server -> the customer: Accepted runs the native Start (greeting, car
    // aligned to the pump, controls locked, camera on the attendant);
    // Release runs the retail release (car forward, engine on, controls
    // unlocked, camera back) when the attendant is back or the service ends.
    // Server -> the simulation host: Serve starts the attendant on the car,
    // Abort sends him back.
    struct Command {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PumperCommand");
        enum class Kind : uint8_t {
            Accepted = 0,
            Refused = 1,
            Release = 2,
            Serve = 3,
            Abort = 4,
            Last = Abort,
        };

        uint64_t missionGeneration = 0;
        uint8_t index = 0;
        uint64_t carId = 0;
        Kind kind = Kind::Refused;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, index);
            stream->Serialize(write, carId);
            SerializeKind(stream, write, kind);
        }
    };

    // Simulation host -> server: the fuel animation's event 43 (EndPumpFuel,
    // where retail fills the tank), and the end of a service: the attendant
    // back at his start node (completed) or the service given up.
    struct Progress {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PumperProgress");
        enum class Kind : uint8_t {
            Fuelled = 0,
            Finished = 1,
            Abandoned = 2,
            Last = Abandoned,
        };

        uint64_t missionGeneration = 0;
        uint8_t index = 0;
        uint64_t carId = 0;
        Kind kind = Kind::Finished;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, index);
            stream->Serialize(write, carId);
            SerializeKind(stream, write, kind);
        }
    };

    // The fuel a service puts in at price per litre, given what the player
    // can pay: every litre the tank takes, or as many as the money buys.
    struct Charge {
        float litres = 0.0f;
        uint32_t cost = 0;
    };
    inline Charge ChargeFor(float fuel, float capacity, double pricePerLitre, uint32_t money) {
        Charge charge;
        if (!std::isfinite(fuel) || !std::isfinite(capacity) || capacity <= fuel) {
            return charge;
        }
        charge.litres = capacity - fuel;
        if (!(pricePerLitre > 0.0) || !std::isfinite(pricePerLitre)) {
            return charge;
        }
        const double cost = std::ceil(static_cast<double>(charge.litres) * pricePerLitre);
        if (cost <= static_cast<double>(money)) {
            charge.cost = static_cast<uint32_t>(cost);
            return charge;
        }
        charge.litres = static_cast<float>(static_cast<double>(money) / pricePerLitre);
        charge.cost = money;
        return charge;
    }
} // namespace Mafia1Online::Shared::Pumper
