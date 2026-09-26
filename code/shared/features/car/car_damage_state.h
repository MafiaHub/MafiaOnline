#pragma once

#include <networking/rpc/rpc.h>

#include <array>
#include <cstdint>

namespace Mafia1Online::Shared::Car {
    // Fixed bounds cover stock car layouts without trusting a client-supplied
    // allocation count. A model beyond these limits is rejected as a whole.
    struct DamageState {
        static constexpr uint8_t kMaxLights = 64;
        static constexpr uint8_t kMaxZones = 64;
        static constexpr uint8_t kMaxWheels = 16;
        // Persistent wheel bits: flat tyre, destroyed and broken. DAMAGED lasts
        // one native tick; its lasting effect is deformAngle.
        static constexpr uint32_t kWheelDamageFlags = 0xC0000400;

        struct Light {
            uint32_t flags = 0;
            float damage = 0.0f;
            bool operator==(const Light &) const = default;
        };
        struct Zone {
            uint16_t flags = 0;
            float crackLevel = 0.0f;
            bool operator==(const Zone &) const = default;
        };
        struct Wheel {
            uint32_t flags = 0;
            float health = 0.0f;
            float deformAngle = 0.0f;
            bool operator==(const Wheel &) const = default;
        };

        uint8_t lightCount = 0;
        uint8_t zoneCount = 0;
        uint8_t wheelCount = 0;
        std::array<Light, kMaxLights> lights {};
        std::array<Zone, kMaxZones> zones {};
        std::array<Wheel, kMaxWheels> wheels {};
        float engineHealth = 0.0f;
        float engineDamagePower = 1.0f;
        uint8_t engineDestroyed = 0;
        float gearboxHealth = 0.0f;
        float bodyDamage = 0.0f;
        int32_t fuelTankHealth = 0;
        uint8_t burning = 0;
        uint32_t burnTimer = 0;
        uint32_t burnDuration = 0;

        bool operator==(const DamageState &) const = default;

        template <typename F> void Fields(F &&field) {
            field(lightCount);
            field(zoneCount);
            field(wheelCount);
            for (auto &light : lights) { field(light.flags); field(light.damage); }
            for (auto &zone : zones) { field(zone.flags); field(zone.crackLevel); }
            for (auto &wheel : wheels) { field(wheel.flags); field(wheel.health); field(wheel.deformAngle); }
            field(engineHealth);
            field(engineDamagePower);
            field(engineDestroyed);
            field(gearboxHealth);
            field(bodyDamage);
            field(fuelTankHealth);
            field(burning);
            field(burnTimer);
            field(burnDuration);
        }
    };

    struct DamageReport {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::CarDamageReport");
        uint64_t networkId = 0;
        uint64_t missionGeneration = 0;
        uint32_t sequence = 0;
        // The damage revision the controller had applied when it observed
        // this state; a report older than a server-authored revision is stale.
        uint64_t baseRevision = 0;
        DamageState state;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, networkId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, sequence);
            stream->Serialize(write, baseRevision);
            state.Fields([&](auto &value) { stream->Serialize(write, value); });
        }
    };
} // namespace Mafia1Online::Shared::Car
