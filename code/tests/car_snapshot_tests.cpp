#include "shared/features/car/car_snapshot.h"
#include <cstdio>
using namespace Mafia1Online::Shared::Car;
int main() {
    Snapshot source;
    source.model                 = "taxi00.i3d";
    source.fuelTankCapacity      = 50;
    source.fuel                  = 12.75f;
    source.engineOn              = true;
    source.damage.engineHealth   = 327;
    source.damage.gearboxHealth  = 450;
    source.damage.bodyDamage     = 72;
    source.damage.fuelTankHealth = 60;
    source.damage.zoneCount      = 3;
    source.damage.lightCount     = 2;
    source.damage.wheelCount     = 4;
    source.damage.zones[1]       = {1, 0.45f};
    source.damage.wheels[0]      = {0xC0000400, 3, 0.2f};
    source.damage.lights[1]      = {1, 0.6f};
    source.mesh                  = {{1, 0, (27U << 18) | 0x321}, {1, 1, (9U << 18) | 0x123}};
    Snapshot restored;
    if (!Snapshot::Decode(source.Encode(), restored) || restored.Encode() != source.Encode())
        return 1;
    auto json            = nlohmann::json::parse(source.Encode());
    json["condition"][0] = 51;
    if (Snapshot::Decode(json.dump(), restored))
        return 2;
    json            = nlohmann::json::parse(source.Encode());
    json["mesh"][1] = json["mesh"][0];
    if (Snapshot::Decode(json.dump(), restored))
        return 3;
    json               = nlohmann::json::parse(source.Encode());
    json["mesh"][0][0] = 64;
    if (Snapshot::Decode(json.dump(), restored))
        return 4;
    json                 = nlohmann::json::parse(source.Encode());
    json["condition"][8] = 260; // Must not wrap the byte-sized seat count.
    if (Snapshot::Decode(json.dump(), restored))
        return 5;
    json                 = nlohmann::json::parse(source.Encode());
    json["condition"][0] = "12";
    if (Snapshot::Decode(json.dump(), restored))
        return 6;
    if (Snapshot::Decode("{}", restored) || Snapshot::Decode("{broken", restored))
        return 7;
    if (restored.Encode() != source.Encode())
        return 8; // Invalid input is atomic.
    source.damage.burning      = 1;
    source.damage.burnTimer    = 23;
    source.damage.burnDuration = 9000;
    if (!Snapshot::Decode(source.Encode(), restored) || restored.damage != source.damage)
        return 9;
    std::puts("Vehicle condition and deformation snapshots passed");
}
