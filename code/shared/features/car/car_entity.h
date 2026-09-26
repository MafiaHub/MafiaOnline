#pragma once

#include <networking/replication/network_entity.h>
#include "shared/features/car/car_damage_state.h"

#include <mafianet/string.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Entities {
    class CarEntity final: public Framework::Networking::Replication::NetworkEntity {
        static uint8_t Unit(float value) {
            return static_cast<uint8_t>(std::lround(std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f) * 255.0f));
        }

      public:
        static constexpr const char *kTypeName = "Mafia1Online::Car";
        static constexpr uint8_t kMaxSeats     = 8;

        enum class TerminalState : uint8_t {
            Active,
            Exploded,
            Submerged,
            OutOfBounds,
        };

        enum class SeatResult : uint8_t {
            None,
            Entered,
            Stolen,
            Exited,
            ExitBlocked,
        };

        // Model and mission generation are fixed for this replica's lifetime.
        std::string model;
        uint64_t missionGeneration = 0;
        // Only this client may report the car's native physics pose. Server
        // ownership of damage, seats, and terminal state is unchanged.
        uint64_t simulationControllerGuid = 0;
        uint8_t seatCount                 = 4;
        // Incremented for server script teleports so the native physics owner
        // applies the authoritative pose before sending another movement report.
        uint64_t transformRevision        = 0;
        // The pose accompanies its reliable revision. The ordinary transform
        // channel can arrive later or be lost, especially during a teleport.
        glm::vec3 forcedPosition {0.0f};
        glm::quat forcedRotation {1.0f, 0.0f, 0.0f, 0.0f};

        // The server owns damage, occupants and terminal outcomes. The transform
        // uses NetworkEntity's timestamped, unreliable pose channel.
        float health                = 100.0f;
        bool engineOn               = false;
        uint64_t engineRevision     = 0;
        bool dynamicsValid          = false;
        uint64_t dynamicsCommandRevision = 0;
        glm::vec3 angularVelocity   = glm::vec3(0.0f);
        float steeringInput         = 0.0f;
        float fuel                  = 0.0f;
        float fuelTankCapacity      = 0.0f;
        float engineRotations       = 0.0f;
        int32_t gear                = 0;
        int32_t maximumGear         = 0;
        uint32_t lightState         = 0;
        bool hornOn                 = false;
        // The controller's resolved pedals; see Shared::Car::Movement.
        float powerInput            = 0.0f;
        float brakeInput            = 0.0f;
        float handbrakeInput        = 0.0f;
        float clutchInput           = 1.0f;
        bool speedLimited           = false;
        std::array<uint8_t, kMaxSeats> doorTargets {};
        // Server owned; drivers toggle it through SirenIntent.
        bool sirenOn                = false;
        // Server owned; the ARGB colour of this car on every radar, 0 for none.
        uint32_t radarColor         = 0;
        // Server-authored visual opacity: 1 is opaque, 0 is transparent.
        float opacity               = 1.0f;
        // Full damage/deform repair command. Clients apply it to their native
        // car before the controller reports the new durable damage/mesh state.
        uint64_t repairRevision     = 0;
        uint32_t damageFlags        = 0;
        uint32_t detachedParts      = 0;
        // Native per-part observations from the simulation owner. This is a
        // durable gameplay state; mesh vertex deltas are tracked separately.
        bool nativeDamageValid       = false;
        uint64_t nativeDamageRevision = 0;
        Shared::Car::DamageState nativeDamage;
        uint64_t meshRevision = 0;
        TerminalState terminalState = TerminalState::Active;
        uint64_t terminalSequence   = 0;
        std::array<uint64_t, kMaxSeats> occupantIds {};
        std::array<uint64_t, kMaxSeats> occupantGenerations {};

        // A reliable result record lets a client distinguish a blocked exit
        // from an exit that has not been processed yet.
        uint64_t seatSequence = 0;
        uint64_t seatActorId  = 0;
        uint8_t seatIndex     = 0;
        SeatResult seatResult = SeatResult::None;

        // The pose's sample clock on the controller that reported it. It rides
        // in the transform channel, so it always matches the pose it dates.
        uint32_t poseClockMs     = 0;
        uint64_t poseClockSource = 0;

        // Per-wheel skid bits in the retail replay record's layout.
        uint8_t skidMask = 0;

        // Values that change every tick while driving ride with the pose on
        // the unreliable transform channel, quantized; the reliable state
        // channel would otherwise resend them on every serialize tick.
        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            NetworkEntity::SerializeTransform(fields);
            fields.Field(poseClockMs);
            fields.Field(poseClockSource);
            fields.Field(angularVelocity);
            auto steering  = static_cast<int16_t>(std::lround(std::clamp(steeringInput, -2.0f, 2.0f) * 16383.0f));
            auto rpm       = static_cast<uint16_t>(std::lround(std::clamp(engineRotations, 0.0f, 65535.0f)));
            auto gearValue = static_cast<int8_t>(std::clamp(gear, -1, 127));
            auto power     = Unit(powerInput);
            auto brake     = Unit(brakeInput);
            auto handbrake = Unit(handbrakeInput);
            auto clutch    = Unit(clutchInput);
            fields.Field(steering);
            fields.Field(rpm);
            fields.Field(gearValue);
            fields.Field(power);
            fields.Field(brake);
            fields.Field(handbrake);
            fields.Field(clutch);
            fields.Field(skidMask);
            if (!fields.Writing()) {
                steeringInput   = steering / 16383.0f;
                engineRotations = rpm;
                gear            = gearValue;
                powerInput      = power / 255.0f;
                brakeInput      = brake / 255.0f;
                handbrakeInput  = handbrake / 255.0f;
                clutchInput     = clutch / 255.0f;
            }
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            MafiaNet::RakString modelName(model.c_str());
            fields.Field(modelName);
            fields.Field(missionGeneration);
            fields.Field(simulationControllerGuid);
            fields.Field(seatCount);
            if (!fields.Writing()) {
                model = modelName.C_String();
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(seatCount);
            fields.Field(simulationControllerGuid);
            fields.Field(transformRevision);
            fields.Field(forcedPosition);
            fields.Field(forcedRotation);
            fields.Field(health);
            fields.Field(engineOn);
            fields.Field(engineRevision);
            fields.Field(dynamicsValid);
            fields.Field(dynamicsCommandRevision);
            fields.Field(fuel);
            fields.Field(fuelTankCapacity);
            fields.Field(maximumGear);
            fields.Field(lightState);
            fields.Field(hornOn);
            fields.Field(speedLimited);
            for (auto &door : doorTargets) {
                fields.Field(door);
            }
            fields.Field(sirenOn);
            fields.Field(radarColor);
            fields.Field(opacity);
            fields.Field(repairRevision);
            fields.Field(damageFlags);
            fields.Field(detachedParts);
            fields.Field(nativeDamageValid);
            fields.Field(nativeDamageRevision);
            nativeDamage.Fields([&](auto &value) { fields.Field(value); });
            fields.Field(meshRevision);
            auto terminal = static_cast<uint8_t>(terminalState);
            fields.Field(terminal);
            fields.Field(terminalSequence);
            for (uint8_t i = 0; i < kMaxSeats; ++i) {
                fields.Field(occupantIds[i]);
                fields.Field(occupantGenerations[i]);
            }
            fields.Field(seatSequence);
            fields.Field(seatActorId);
            fields.Field(seatIndex);
            auto seat = static_cast<uint8_t>(seatResult);
            fields.Field(seat);
            if (!fields.Writing()) {
                terminalState = terminal <= static_cast<uint8_t>(TerminalState::OutOfBounds) ? static_cast<TerminalState>(terminal) : TerminalState::Active;
                seatResult    = seat <= static_cast<uint8_t>(SeatResult::ExitBlocked) ? static_cast<SeatResult>(seat) : SeatResult::None;
            }
        }
    };
} // namespace Mafia1Online::Shared::Entities
