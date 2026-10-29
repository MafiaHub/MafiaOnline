#pragma once

#include "shared/features/car/car_entity.h"

#include <networking/rpc/rpc.h>

#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Traffic {
    // Simulation host <-> server. A key names one host-driven car for the
    // whole mission generation and is never reused within it. Its physics,
    // damage and terminal outcomes travel as the ordinary car reports
    // (Movement, DamageReport, ...) with the host as simulation controller.
    using Kind = Entities::CarEntity::TrafficKind;
    using CrewState = Entities::CarEntity::CrewState;

    inline CrewState ReadCrewState(uint8_t value) {
        return value <= static_cast<uint8_t>(CrewState::Entering) ? static_cast<CrewState>(value) : CrewState::None;
    }

    // At most this many cars the host drives at once (reported and not yet
    // retired or taken by a player). The host reports no more and the
    // server takes no more, so both agree on every car.
    inline constexpr size_t kMaxHostCars = 192;

    // Host -> server: a car the host's retail world started driving, a
    // traffic pool car or a car an NPC crew took (Krles, police dispatch).
    struct Spawned {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TrafficSpawned");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        Kind kind = Kind::Civil;
        std::string model;
        std::string driverModel;
        std::string passengerModel;
        CrewState driverState = CrewState::None;
        CrewState passengerState = CrewState::None;
        uint8_t seatCount = 4;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;
        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
        uint32_t lightState = 0;
        bool sirenOn = false;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto kindValue = static_cast<uint8_t>(kind);
            auto driver = static_cast<uint8_t>(driverState);
            auto passenger = static_cast<uint8_t>(passengerState);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, kindValue);
            stream->Serialize(write, model);
            stream->Serialize(write, driverModel);
            stream->Serialize(write, passengerModel);
            stream->Serialize(write, driver);
            stream->Serialize(write, passenger);
            stream->Serialize(write, seatCount);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, qw);
            stream->Serialize(write, qx);
            stream->Serialize(write, qy);
            stream->Serialize(write, qz);
            stream->Serialize(write, vx);
            stream->Serialize(write, vy);
            stream->Serialize(write, vz);
            stream->Serialize(write, lightState);
            stream->Serialize(write, sirenOn);
            if (!write) {
                kind = kindValue <= static_cast<uint8_t>(Kind::Mafia) ? static_cast<Kind>(kindValue) : Kind::None;
                driverState = ReadCrewState(driver);
                passengerState = ReadCrewState(passenger);
            }
        }
    };

    // Host -> server: the host no longer has the car (despawned far from
    // every player, freed back to a native pool, or destroyed).
    struct Retired {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TrafficRetired");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
        }
    };

    // Host -> server: the traffic crew changed (died in the seat, got out,
    // a pedestrian climbed in with its own model) or the native siren did.
    struct Crew {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TrafficCrew");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        std::string driverModel;
        std::string passengerModel;
        CrewState driverState = CrewState::None;
        CrewState passengerState = CrewState::None;
        bool sirenOn = false;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto driver = static_cast<uint8_t>(driverState);
            auto passenger = static_cast<uint8_t>(passengerState);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, driverModel);
            stream->Serialize(write, passengerModel);
            stream->Serialize(write, driver);
            stream->Serialize(write, passenger);
            stream->Serialize(write, sirenOn);
            if (!write) {
                driverState = ReadCrewState(driver);
                passengerState = ReadCrewState(passenger);
            }
        }
    };

    // Host -> server: a road block barrier (C_irenka "barrier.i3d" from the
    // police manager's pool) stands at this pose; a key names it until it
    // goes back to the pool.
    struct BarrierPlaced {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TrafficBarrierPlaced");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, qw);
            stream->Serialize(write, qx);
            stream->Serialize(write, qy);
            stream->Serialize(write, qz);
        }
    };

    struct BarrierRemoved {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TrafficBarrierRemoved");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
        }
    };

    // How far from a traffic car's origin a player may steal or enter it.
    inline constexpr float kStealReach = 8.0f;
    // Retail offers a car's seats only while it is nearly still (C_car::
    // Update 0x41fac0: m_fSpeed below 0.3); the host's reported velocity
    // lags its car, so a steal is taken up to this speed.
    inline constexpr float kStealMaxSpeed = 1.0f;

    // Player -> server: the local player started into the driver's seat of a
    // traffic car whose driver still sits in it, as retail's steal does
    // (C_player::Do_AB_OwnerNULL -> Do_ThrowCocotFromCar).
    struct StealIntent {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TrafficStealIntent");

        uint64_t missionGeneration = 0;
        uint64_t carId = 0;
        uint64_t playerId = 0;
        uint64_t spawnGeneration = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, carId);
            stream->Serialize(write, playerId);
            stream->Serialize(write, spawnGeneration);
        }
    };

    // Server -> the thief: its StealIntent was not taken (the car moved
    // away, another player took it, its driver got out). The thief's
    // client played the pull-out already; the driver model comes back.
    struct StealRefused {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TrafficStealRefused");

        uint64_t missionGeneration = 0;
        uint64_t carId = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, carId);
        }
    };

    // Server -> host: the car is a player's now. With steal set the host
    // first throws the traffic driver out at that player's replica; then it
    // swaps the pool car for an ordinary one (SebraliMiKaaaruSvine) and stops
    // driving it.
    struct Takeover {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TrafficTakeover");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t playerId = 0;
        uint64_t playerSpawnGeneration = 0;
        bool steal = false;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, playerId);
            stream->Serialize(write, playerSpawnGeneration);
            stream->Serialize(write, steal);
        }
    };
} // namespace Mafia1Online::Shared::Traffic
