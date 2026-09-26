#include "shared/features/world/mission_actors.h"
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace Mafia1Online::Shared::World;
using Bytes = std::vector<uint8_t>;

static Bytes Chunk(uint16_t type, const Bytes &body) {
    const auto size = static_cast<uint32_t>(body.size() + 6);
    Bytes result {uint8_t(type), uint8_t(type >> 8), uint8_t(size), uint8_t(size >> 8), uint8_t(size >> 16), uint8_t(size >> 24)};
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

int main(int argc, char **argv) {
    Bytes actor     = Chunk(0xae23, {'c', 'a', 'r', 0});
    const auto type = Chunk(0xae22, {4, 0, 0, 0});
    actor.insert(actor.end(), type.begin(), type.end());
    auto bytes = Chunk(0x4c53, Chunk(0xae20, Chunk(0xae21, actor)));
    std::vector<MissionActorRecord> records;
    if (!ReadMissionActors(bytes, records) || records.size() != 1 || records[0].frame != "car" || records[0].type != 4)
        return 1;
    bytes.pop_back();
    if (ReadMissionActors(bytes, records) || records.size() != 1)
        return 2;
    bytes = Chunk(0x4c53, Chunk(0xae20, {0x21, 0xae, 0xff, 0xff, 0xff, 0xff}));
    if (ReadMissionActors(bytes, records))
        return 3;
    bytes = Chunk(0x4c53, Chunk(0xae20, Chunk(0xae21, Chunk(0xae23, {'b', 'a', 'd'}))));
    if (ReadMissionActors(bytes, records))
        return 4;
    if (KeepMissionActorGeometry(4) || KeepMissionActorGeometry(8) || KeepMissionActorGeometry(27) || !KeepMissionActorGeometry(6) || !KeepMissionActorGeometry(20))
        return 5;
    for (uint32_t typeId = 0; typeId <= 35; ++typeId) {
        if (LoadMissionActor(typeId) != (typeId == 6))
            return 7;
    }
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        const Bytes scene((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!ReadMissionActors(scene, records) || records.empty())
            return 6;
        std::printf("Read %zu mission actor records\n", records.size());
    }
    std::puts("Mission actor parsing and geometry policy passed");
}
