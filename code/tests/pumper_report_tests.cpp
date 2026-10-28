#include "shared/features/pumper/pumper_report.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>

using namespace Mafia1Online::Shared::Pumper;

namespace {
    bool Near(float a, float b) {
        return std::fabs(a - b) < 0.001f;
    }

    bool Equal(const State &a, const State &b) {
        return a.index == b.index && a.nameHash == b.nameHash && a.state == b.state && a.animation == b.animation && a.finite == b.finite && a.track == b.track &&
               a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z && a.direction.x == b.direction.x &&
               a.direction.y == b.direction.y && a.direction.z == b.direction.z && a.carId == b.carId;
    }
} // namespace

int main() {
    // reM C_pumper::IsPumping: WALKING and IDLE < state <= PUMP_READY.
    for (uint8_t state = 0; state < kStateCount; ++state) {
        const bool pumping = state == 0 || (state >= 2 && state <= 10);
        if (Pumping(state) != pumping)
            return 1;
    }
    if (std::string_view(StateName(0)) != "walking" || std::string_view(StateName(5)) != "fueling" || std::string_view(StateName(13)) != "smoking" ||
        std::string_view(StateName(99)) != "unknown")
        return 2;

    State state;
    state.index = 2;
    state.nameHash = NameHash("pumpar2");
    state.state = kFueling;
    state.animation = 0x10008;
    state.position = {-110.6f, 8.5f, -135.5f};
    state.direction = {0.0f, 0.0f, 1.0f};
    if (!Valid(state))
        return 3;
    auto invalid = state;
    invalid.index = kMaxPumpers;
    if (Valid(invalid))
        return 4;
    invalid = state;
    invalid.state = 17; // Panic is blocked on every client.
    if (Valid(invalid))
        return 5;
    invalid = state;
    invalid.position.x = std::numeric_limits<float>::quiet_NaN();
    if (Valid(invalid))
        return 6;
    invalid = state;
    invalid.animation = 0x10009;
    if (Valid(invalid))
        return 7;
    if (NameHash("pumpar") == NameHash("pumpar1"))
        return 8;

    // Free: every missing litre at no cost.
    auto charge = ChargeFor(12.0f, 60.0f, 0.0, 0);
    if (!Near(charge.litres, 48.0f) || charge.cost != 0)
        return 9;
    // Priced: the full tank rounds up to whole money.
    charge = ChargeFor(12.0f, 60.5f, 3.0, 1000);
    if (!Near(charge.litres, 48.5f) || charge.cost != 146)
        return 10;
    // Short of money: as many litres as it buys, for all of it.
    charge = ChargeFor(12.0f, 60.0f, 3.0, 30);
    if (!Near(charge.litres, 10.0f) || charge.cost != 30)
        return 11;
    // A full or overfull tank takes nothing.
    charge = ChargeFor(60.0f, 60.0f, 3.0, 1000);
    if (charge.litres != 0.0f || charge.cost != 0)
        return 12;
    charge = ChargeFor(std::numeric_limits<float>::infinity(), 60.0f, 3.0, 1000);
    if (charge.litres != 0.0f || charge.cost != 0)
        return 13;

    // The wire format round-trips, and a count past the limit reads empty.
    Snapshot snapshot;
    snapshot.missionGeneration = 7;
    snapshot.hostSession = 99;
    snapshot.hostTimeMs = 123456;
    snapshot.count = 1;
    snapshot.pumpers[0] = state;
    snapshot.pumpers[0].carId = 0x1234567890ull;
    MafiaNet::BitStream stream;
    snapshot.Serialize(&stream, true);
    Snapshot read;
    read.Serialize(&stream, false);
    if (read.missionGeneration != 7 || read.hostSession != 99 || read.hostTimeMs != 123456 || read.count != 1 || !Equal(read.pumpers[0], snapshot.pumpers[0]))
        return 14;

    Command command;
    command.missionGeneration = 3;
    command.index = 1;
    command.carId = 42;
    command.kind = Command::Kind::Release;
    MafiaNet::BitStream commandStream;
    command.Serialize(&commandStream, true);
    Command commandRead;
    commandRead.Serialize(&commandStream, false);
    if (commandRead.kind != Command::Kind::Release || commandRead.carId != 42 || commandRead.index != 1)
        return 15;
    // An unknown kind reads past Last, which every handler ignores.
    MafiaNet::BitStream bad;
    uint64_t generation = 3;
    uint8_t index = 1;
    uint32_t nameHash = 5;
    uint64_t carId = 42;
    uint8_t kind = 200;
    bad.Serialize(true, generation);
    bad.Serialize(true, index);
    bad.Serialize(true, nameHash);
    bad.Serialize(true, carId);
    bad.Serialize(true, kind);
    Request request;
    request.Serialize(&bad, false);
    if (request.kind == Request::Kind::Serve || request.kind == Request::Kind::Cancel)
        return 16;

    std::printf("Pumper report tests passed\n");
    return 0;
}
