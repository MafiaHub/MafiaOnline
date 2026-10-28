#pragma once

#include <networking/rpc/rpc.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Mafia1Online::Shared::Taxi {
    // Retail taxi hailing (C_traffic_generator::GeneratePassengerForTaxi
    // 0x44f7d0, C_traffic_element::MavaNaTaxi 0x451ff0), run on the
    // simulation host for every player a script lets walkers hail. A walker
    // is named by its Pedestrian::Spawned key.

    // The retail wave: "Mavani01 HPT.i3d" to "Mavani03 HPT.i3d" as the
    // animation machine's extended animation; 0 is none.
    inline constexpr uint8_t kWaveNone = 0;
    inline constexpr uint8_t kWaveVariants = 3;

    inline bool KnownWave(uint8_t wave) {
        return wave <= kWaveVariants;
    }

    // Server -> simulation host: the players walkers may hail (retail
    // EnableTaxiDriver, per player), sent whole on each change and to a newly
    // attached host. The host still asks retail's own questions: a taxi
    // model in the driver's seat, slow enough, no passenger aboard, not
    // wanted.
    struct Drivers {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TaxiDrivers");
        static constexpr uint16_t kMaxDrivers = 256;

        std::vector<uint64_t> playerNetworkIds;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto count = static_cast<uint16_t>(std::min<size_t>(playerNetworkIds.size(), kMaxDrivers));
            stream->Serialize(write, count);
            if (count > kMaxDrivers) {
                playerNetworkIds.clear();
                return;
            }
            if (!write) {
                playerNetworkIds.assign(count, 0);
            }
            for (uint16_t i = 0; i < count; ++i) {
                stream->Serialize(write, playerNetworkIds[i]);
            }
        }
    };

    // Simulation host -> server: a walker started waving at a player's taxi.
    struct Hail {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TaxiHail");

        uint64_t missionGeneration = 0;
        uint32_t walkerKey = 0;
        uint64_t driverId = 0;
        uint64_t carId = 0;
        uint8_t wave = kWaveNone;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, walkerKey);
            stream->Serialize(write, driverId);
            stream->Serialize(write, carId);
            stream->Serialize(write, wave);
        }
    };

    // Simulation host -> server: the wave ended (EndMavaniNaTaxi: a panic, a
    // hit, the taxi gone, the driver wanted, the walker retired or turned
    // into the passenger).
    struct HailEnded {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TaxiHailEnded");

        uint64_t missionGeneration = 0;
        uint32_t walkerKey = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, walkerKey);
        }
    };

    // Simulation host -> server: the taxi stopped by its hailer, retail's
    // boarding moment (MavaNaTaxi 0x4522ed: driver speed below 0.5 within 10
    // of the walker). Retail makes the walker its taxi_pas.dat passenger
    // there; the host keeps it waving and leaves that to the script.
    struct Pickup {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TaxiPickup");

        uint64_t missionGeneration = 0;
        uint32_t walkerKey = 0;
        uint64_t driverId = 0;
        uint64_t carId = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, walkerKey);
            stream->Serialize(write, driverId);
            stream->Serialize(write, carId);
        }
    };

    // Server -> simulation host: end this walker's wave now (Taxi.decline).
    struct EndHail {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::TaxiEndHail");

        uint64_t missionGeneration = 0;
        uint32_t walkerKey = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, walkerKey);
        }
    };
} // namespace Mafia1Online::Shared::Taxi
