#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Mafia1Online::Shared::World {
    struct MissionActorRecord {
        std::string frame;
        uint32_t type = 0;
    };

    // scene2.bin uses packed {u16 type, u32 sizeIncludingHeader} chunks.
    // Only walk the actor hierarchy; model/sound/navigation payloads stay opaque.
    namespace MissionChunks {
        inline uint32_t U32(std::span<const uint8_t> bytes) {
            return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
        }
        template <typename F>
        bool Each(std::span<const uint8_t> bytes, F &&visit) {
            while (!bytes.empty()) {
                if (bytes.size() < 6)
                    return false;
                const uint16_t type = uint16_t(bytes[0]) | uint16_t(bytes[1]) << 8;
                const uint32_t size = U32(bytes.subspan(2));
                if (size < 6 || size > bytes.size() || !visit(type, bytes.subspan(6, size - 6)))
                    return false;
                bytes = bytes.subspan(size);
            }
            return true;
        }
    } // namespace MissionChunks

    inline bool ReadMissionActors(std::span<const uint8_t> bytes, std::vector<MissionActorRecord> &out) {
        if (bytes.size() < 6 || bytes[0] != 0x53 || bytes[1] != 0x4c || MissionChunks::U32(bytes.subspan(2)) != bytes.size())
            return false;
        std::vector<MissionActorRecord> actors;
        const bool valid = MissionChunks::Each(bytes.subspan(6), [&](uint16_t type, auto data) {
            if (type != 0xae20)
                return true;
            return MissionChunks::Each(data, [&](uint16_t recordType, auto record) {
                if (recordType != 0xae21)
                    return true;
                MissionActorRecord actor;
                bool hasType = false;
                if (!MissionChunks::Each(record, [&](uint16_t field, auto value) {
                        if (field == 0xae22) {
                            if (value.size() != 4)
                                return false;
                            actor.type = MissionChunks::U32(value);
                            hasType    = true;
                        }
                        else if (field == 0xae23) {
                            if (value.empty() || value.size() > 256 || value.back() != 0)
                                return false;
                            actor.frame.assign(reinterpret_cast<const char *>(value.data()), value.size() - 1);
                            if (actor.frame.find('\0') != std::string::npos)
                                return false;
                        }
                        return true;
                    }))
                    return false;
                if (!hasType || actor.frame.empty() || actors.size() >= 100000)
                    return false;
                actors.push_back(std::move(actor));
                return true;
            });
        });
        if (valid)
            out = std::move(actors);
        return valid;
    }

    // Doors need their native actors for interaction and multiplayer door sync.
    inline bool LoadMissionActor(uint32_t type) {
        return type == 6;
    }

    // Retain scenery even when its actor behaviour is suppressed. Doors retain
    // both their geometry and their native actor.
    inline bool KeepMissionActorGeometry(uint32_t type) {
        switch (type) {
        case 1:
        case 6:
        case 9:
        case 20:
        case 24:
        case 25:
        case 34: return true;
        default: return false;
        }
    }
} // namespace Mafia1Online::Shared::World
