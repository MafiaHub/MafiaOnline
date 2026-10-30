#pragma once

#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Mafia1Online::SDK::World {
    // reM C_web_connection (8 bytes): the node it leads to and its type. A
    // railway runs along the links without WCT_OneWay (0x80) in the low
    // byte, its back path along those with it (C_railway::Place,
    // CreateForwardVector, CreateBackVector).
    struct NativeWebConnection {
        static constexpr uint16_t kOneWay = 0x80;
        uint16_t endPoint;
        uint16_t type;
        float length;
    };
    static_assert(sizeof(NativeWebConnection) == 8);

    // reM C_web_node (packed, 30 bytes).
#pragma pack(push, 1)
    struct NativeWebNode {
        Player::Vector3 position;
        uint16_t type;
        uint16_t id;
        uint32_t radius;
        uint32_t checkData;
        NativeWebConnection *links;
        uint8_t linkCapacity;
        uint8_t linkCount;
    };
#pragma pack(pop)
    static_assert(sizeof(NativeWebNode) == 30);
    static_assert(offsetof(NativeWebNode, links) == 0x18);
    static_assert(offsetof(NativeWebNode, linkCount) == 0x1d);

    // reM C_web_path, the mission's at C_mission +0x2c (C_railway::Place
    // reads it there, 0x48f8bb): nodes at +4, their count at +0xc.
    struct NativeWebPath {
        static constexpr size_t kMissionOffset = 0x2c;
        static constexpr size_t kNodesOffset = 0x04;
        static constexpr size_t kNodeCountOffset = 0x0c;

        // The loaded mission's; null without one.
        [[nodiscard]] static const NativeWebPath *Get() {
            const auto *mission = Core::Mission::Get();
            return mission ? *reinterpret_cast<NativeWebPath *const *>(reinterpret_cast<const std::byte *>(mission) + kMissionOffset) : nullptr;
        }

        [[nodiscard]] uint32_t NodeCount() const {
            return *reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(this) + kNodeCountOffset);
        }
        // Null past the nodes.
        [[nodiscard]] const NativeWebNode *Node(uint32_t index) const {
            const auto *nodes = *reinterpret_cast<NativeWebNode *const *>(reinterpret_cast<const std::byte *>(this) + kNodesOffset);
            return nodes && index < NodeCount() ? nodes + index : nullptr;
        }
        // The node a railway heading for `index` takes next: the last link
        // without kOneWay (C_railway::Place's fallback, 0x48faa3).
        [[nodiscard]] std::optional<uint32_t> RailwayNext(uint32_t index) const {
            return LastLink(index, false);
        }
        // The node behind it: the last link with kOneWay (CreateBackVector).
        [[nodiscard]] std::optional<uint32_t> RailwayPrevious(uint32_t index) const {
            return LastLink(index, true);
        }

      private:
        [[nodiscard]] std::optional<uint32_t> LastLink(uint32_t index, bool oneWay) const {
            const auto *node = Node(index);
            if (!node || !node->links) {
                return std::nullopt;
            }
            for (int link = node->linkCount - 1; link >= 0; --link) {
                if (((node->links[link].type & NativeWebConnection::kOneWay) != 0) == oneWay) {
                    return node->links[link].endPoint < NodeCount() ? std::optional<uint32_t>(node->links[link].endPoint) : std::nullopt;
                }
            }
            return std::nullopt;
        }
    };
} // namespace Mafia1Online::SDK::World
