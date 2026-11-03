#include "shared/features/taxi/taxi_protocol.h"

#include <cstdio>

using namespace Mafia1Online::Shared::Taxi;

int main() {
    if (!KnownWave(kWaveNone) || !KnownWave(kWaveVariants) || KnownWave(kWaveVariants + 1))
        return 1;

    Drivers drivers;
    drivers.playerNetworkIds = {7, 42, 0xffffffffffull};
    MafiaNet::BitStream driverStream;
    drivers.Serialize(&driverStream, true);
    Drivers driversRead;
    driversRead.Serialize(&driverStream, false);
    if (driversRead.playerNetworkIds != drivers.playerNetworkIds)
        return 2;

    // A count past the limit reads as nobody, not a huge allocation.
    MafiaNet::BitStream bad;
    uint16_t count = Drivers::kMaxDrivers + 1;
    bad.Serialize(true, count);
    Drivers badRead;
    badRead.playerNetworkIds = {1};
    badRead.Serialize(&bad, false);
    if (!badRead.playerNetworkIds.empty())
        return 3;

    Hail hail;
    hail.missionGeneration = 3;
    hail.walkerKey = 77;
    hail.driverId = 1001;
    hail.carId = 2002;
    hail.wave = 2;
    MafiaNet::BitStream hailStream;
    hail.Serialize(&hailStream, true);
    Hail hailRead;
    hailRead.Serialize(&hailStream, false);
    if (hailRead.missionGeneration != 3 || hailRead.walkerKey != 77 || hailRead.driverId != 1001 || hailRead.carId != 2002 || hailRead.wave != 2)
        return 4;

    Pickup pickup;
    pickup.missionGeneration = 3;
    pickup.walkerKey = 77;
    pickup.driverId = 1001;
    pickup.carId = 2002;
    MafiaNet::BitStream pickupStream;
    pickup.Serialize(&pickupStream, true);
    Pickup pickupRead;
    pickupRead.Serialize(&pickupStream, false);
    if (pickupRead.walkerKey != 77 || pickupRead.driverId != 1001 || pickupRead.carId != 2002)
        return 5;

    HailEnded ended;
    ended.missionGeneration = 3;
    ended.walkerKey = 77;
    MafiaNet::BitStream endedStream;
    ended.Serialize(&endedStream, true);
    HailEnded endedRead;
    endedRead.Serialize(&endedStream, false);
    if (endedRead.missionGeneration != 3 || endedRead.walkerKey != 77)
        return 6;

    std::puts("taxi protocol tests passed");
    return 0;
}
