#pragma once

#include <networking/rpc/rpc.h>

#include <array>
#include <cstdint>

namespace Mafia1Online::Shared::Car {
    // C_car::Drop_Out creates an independent C_DropOut actor. These are its
    // exact model-dependent parameters, carried as eight floats: wheel uses
    // all eight; box uses weight, direction xyz and friction only.
    struct DebrisParameters {
        std::array<float, 8> values {};

        template <typename F> void Fields(F &&field) {
            for (auto &value : values) { field(value); }
        }
    };

    struct DebrisSpawnReport {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarDebrisSpawnReport");
        uint64_t carId = 0;
        uint64_t missionGeneration = 0;
        uint32_t localSequence = 0;
        uint8_t type = 0;
        uint8_t partIndex = 0;
        DebrisParameters parameters;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, carId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, localSequence);
            stream->Serialize(write, type);
            stream->Serialize(write, partIndex);
            parameters.Fields([&](auto &value) { stream->Serialize(write, value); });
            stream->Serialize(write, x); stream->Serialize(write, y); stream->Serialize(write, z);
            stream->Serialize(write, qw); stream->Serialize(write, qx);
            stream->Serialize(write, qy); stream->Serialize(write, qz);
        }
    };

    struct DebrisMovement {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarDebrisMovement");
        uint64_t debrisId = 0;
        uint64_t missionGeneration = 0;
        uint32_t sequence = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, debrisId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, sequence);
            stream->Serialize(write, x); stream->Serialize(write, y); stream->Serialize(write, z);
            stream->Serialize(write, qw); stream->Serialize(write, qx);
            stream->Serialize(write, qy); stream->Serialize(write, qz);
        }
    };

    struct DebrisGone {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarDebrisGone");
        uint64_t debrisId = 0;
        uint64_t missionGeneration = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, debrisId);
            stream->Serialize(write, missionGeneration);
        }
    };
} // namespace Mafia1Online::Shared::Car
