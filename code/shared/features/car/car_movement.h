#pragma once

#include "shared/net/quantize.h"

#include <networking/rpc/rpc.h>

#include <array>
#include <cstdint>

namespace Mafia1Online::Shared::Car {
    struct Movement {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarMovement");
        static constexpr uint32_t kReplicatedLightMask = 0x00000001 | 0x00000002 | 0x00000080 | 0x00000100 | 0x00000800 |
                                                         0x00001000 | 0x00008000 | 0x00010000;

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t transformRevision  = 0;
        uint64_t dynamicsCommandRevision = 0;
        uint32_t sequence          = 0;
        // The controller's frame clock (World::FrameClock) at the physics
        // step that produced this pose, in ms and 1/256 ms past it. Only
        // differences are used, to space the observers' interpolation samples.
        uint32_t clockMs           = 0;
        uint8_t clockFraction      = 0;
        float x                    = 0.0f;
        float y                    = 0.0f;
        float z                    = 0.0f;
        float qw                   = 1.0f;
        float qx                   = 0.0f;
        float qy                   = 0.0f;
        float qz                   = 0.0f;
        float velocityX            = 0.0f;
        float velocityY            = 0.0f;
        float velocityZ            = 0.0f;
        float angularVelocityX     = 0.0f;
        float angularVelocityY     = 0.0f;
        float angularVelocityZ     = 0.0f;
        float steeringInput        = 0.0f;
        float fuel                 = 0.0f;
        float fuelTankCapacity     = 0.0f;
        float engineRotations      = 0.0f;
        int32_t gear               = 0;
        int32_t maximumGear        = 0;
        uint32_t lightState        = 0;
        bool hornOn                = false;
        // Resolved native pedal values, each in [0, 1].
        float powerInput           = 0.0f;
        float brakeInput           = 0.0f;
        float handbrakeInput       = 0.0f;
        float clutchInput          = 1.0f;
        bool speedLimited          = false;
        uint8_t skidMask           = 0;
        // Door target per seat, 0 closed to 255 fully open; 0 without a door.
        std::array<uint8_t, 8> doorTargets {};

        // Fuel, tank, gearbox, limiter and doors change seldom: they ride
        // only when one changed or a second has passed (and always until the
        // server has the car's dynamics), and the server keeps its own copy
        // otherwise.
        bool slowFields = true;

        // About 50 bytes (147 before), 70 with the slow fields: ids
        // compressed, the pose quantized as CarEntity's transform.
        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->SerializeCompressed(write, networkId);
            stream->SerializeCompressed(write, missionGeneration);
            stream->SerializeCompressed(write, transformRevision);
            stream->SerializeCompressed(write, dynamicsCommandRevision);
            stream->Serialize(write, sequence);
            stream->Serialize(write, clockMs);
            stream->Serialize(write, clockFraction);
            glm::vec3 position {x, y, z};
            glm::quat rotation {qw, qx, qy, qz};
            // As fine as CarEntity relays it: coarser here would put the
            // 1/64 m, 10-bit noise back into every observer's pose.
            Net::SerializeWorldPosition(stream, write, position, Net::kCarPlaneBits, Net::kCarHeightBits);
            Net::SerializeQuat(stream, write, rotation, 12);
            Net::SerializeFixed(stream, write, velocityX, -128.0f, 128.0f, 16);
            Net::SerializeFixed(stream, write, velocityY, -128.0f, 128.0f, 16);
            Net::SerializeFixed(stream, write, velocityZ, -128.0f, 128.0f, 16);
            Net::SerializeFixed(stream, write, angularVelocityX, -32.0f, 32.0f, 12);
            Net::SerializeFixed(stream, write, angularVelocityY, -32.0f, 32.0f, 12);
            Net::SerializeFixed(stream, write, angularVelocityZ, -32.0f, 32.0f, 12);
            if (!write) {
                x  = position.x;
                y  = position.y;
                z  = position.z;
                qw = rotation.w;
                qx = rotation.x;
                qy = rotation.y;
                qz = rotation.z;
            }
            Net::SerializeFixed(stream, write, steeringInput, -2.0f, 2.0f, 15);
            Net::SerializeFixed(stream, write, engineRotations, 0.0f, 20000.0f, 15);
            stream->Serialize(write, gear);
            stream->Serialize(write, lightState);
            stream->Serialize(write, hornOn);
            Net::SerializeFixed(stream, write, powerInput, 0.0f, 1.0f, 8);
            Net::SerializeFixed(stream, write, brakeInput, 0.0f, 1.0f, 8);
            Net::SerializeFixed(stream, write, handbrakeInput, 0.0f, 1.0f, 8);
            Net::SerializeFixed(stream, write, clutchInput, 0.0f, 1.0f, 8);
            stream->Serialize(write, skidMask);
            stream->Serialize(write, slowFields);
            if (slowFields) {
                stream->Serialize(write, fuel);
                stream->Serialize(write, fuelTankCapacity);
                stream->Serialize(write, maximumGear);
                stream->Serialize(write, speedLimited);
                for (auto &door : doorTargets) {
                    stream->Serialize(write, door);
                }
            }
        }
    };
} // namespace Mafia1Online::Shared::Car
