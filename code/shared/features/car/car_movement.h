#pragma once

#include <networking/rpc/rpc.h>

#include <array>
#include <cstdint>

namespace Mafia1Online::Shared::Car {
    struct Movement {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarMovement");
        static constexpr uint32_t kReplicatedLightMask = 0x00000001 | 0x00000002 | 0x00000080 | 0x00000100 | 0x00000800 |
                                                         0x00001000 | 0x00008000 | 0x00010000;

        uint64_t networkId         = 0;
        uint64_t missionGeneration = 0;
        uint64_t transformRevision  = 0;
        uint64_t dynamicsCommandRevision = 0;
        uint32_t sequence          = 0;
        // The controller's MafiaNet::GetTime() when it read this pose. Only
        // differences are used, to space the observers' interpolation samples.
        uint32_t clockMs           = 0;
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

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, transformRevision);
            stream->Serialize(write, dynamicsCommandRevision);
            stream->Serialize(write, sequence);
            stream->Serialize(write, clockMs);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, qw);
            stream->Serialize(write, qx);
            stream->Serialize(write, qy);
            stream->Serialize(write, qz);
            stream->Serialize(write, velocityX);
            stream->Serialize(write, velocityY);
            stream->Serialize(write, velocityZ);
            stream->Serialize(write, angularVelocityX);
            stream->Serialize(write, angularVelocityY);
            stream->Serialize(write, angularVelocityZ);
            stream->Serialize(write, steeringInput);
            stream->Serialize(write, fuel);
            stream->Serialize(write, fuelTankCapacity);
            stream->Serialize(write, engineRotations);
            stream->Serialize(write, gear);
            stream->Serialize(write, maximumGear);
            stream->Serialize(write, lightState);
            stream->Serialize(write, hornOn);
            stream->Serialize(write, powerInput);
            stream->Serialize(write, brakeInput);
            stream->Serialize(write, handbrakeInput);
            stream->Serialize(write, clutchInput);
            stream->Serialize(write, speedLimited);
            stream->Serialize(write, skidMask);
            for (auto &door : doorTargets) {
                stream->Serialize(write, door);
            }
        }
    };
} // namespace Mafia1Online::Shared::Car
