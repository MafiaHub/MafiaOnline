#pragma once

#include <networking/rpc/rpc.h>

#include <array>
#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Pedestrian {
    // Simulation host -> server. A key names one walker life for the whole
    // mission generation and is never reused within it.

    struct Spawned {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianSpawned");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        std::string model;
        // Index among the mission's pedestrian generators in actor order,
        // which every client loads identically from scene2.bin; kNoGenerator
        // for a walker no pool owns (a traffic driver who parked and got
        // out, AddFreePersonFromCar; a released officer's walker copy).
        static constexpr uint8_t kNoGenerator = 0xff;
        uint8_t generator = 0;
        // C_traffic_element walk style (+0x1b4), rerolled by every activation.
        uint8_t walkStyle = 0;
        // Rain umbrella in the right hand (C_traffic_element +0x4c).
        bool umbrella = false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, model);
            stream->Serialize(write, generator);
            stream->Serialize(write, walkStyle);
            stream->Serialize(write, umbrella);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, yaw);
        }
    };

    struct Retired {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianRetired");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
        }
    };

    struct WalkerPose {
        uint32_t key = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        // Ground speed in m/s; replicas pick their locomotion from it.
        float speed = 0.0f;
        bool dead = false;
        // C_traffic_element's current animation (+0x160) and the flag SetAnim
        // passes with it (+0x16c).
        int16_t animation = 0;
        bool animationFlag = false;
        // Still holding the umbrella; a hit drops it for good.
        bool umbrella = false;
        // Fade in or out (C_traffic_element +0xbc), 0 to 255.
        uint8_t alpha = 255;
    };

    // Newest poses of up to kMaxWalkers walkers; a sample for an unknown
    // key is ignored.
    struct Snapshot {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PedestrianSnapshot");
        static constexpr uint8_t kMaxWalkers = 32;

        uint64_t missionGeneration = 0;
        uint8_t count = 0;
        std::array<WalkerPose, kMaxWalkers> walkers {};

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, count);
            if (count > kMaxWalkers) {
                count = 0;
                return;
            }
            for (uint8_t i = 0; i < count; ++i) {
                auto &walker = walkers[i];
                stream->Serialize(write, walker.key);
                stream->Serialize(write, walker.x);
                stream->Serialize(write, walker.y);
                stream->Serialize(write, walker.z);
                stream->Serialize(write, walker.yaw);
                stream->Serialize(write, walker.speed);
                stream->Serialize(write, walker.dead);
                stream->Serialize(write, walker.animation);
                stream->Serialize(write, walker.animationFlag);
                stream->Serialize(write, walker.umbrella);
                stream->Serialize(write, walker.alpha);
            }
        }
    };
} // namespace Mafia1Online::Shared::Pedestrian
