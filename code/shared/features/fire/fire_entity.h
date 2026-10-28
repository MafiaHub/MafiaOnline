#pragma once

#include <networking/replication/network_entity.h>

#include <glm/vec3.hpp>

#include <cstdint>

namespace Mafia1Online::Shared::Entities {
    // A script fire (Fire.create) that burns until it is put out. The server
    // owns its intensity, which water jets and scripts lower; every client
    // in range draws it from the retail fire effects (tables\effects.tbl
    // "Fire" and "Fire_no_smoke") at flame points the seed places within
    // radius and height, lit by one retail fire light (C_game's
    // SpawnFireFrameSlot pool) and heard through the retail fire loop
    // (sound table entry 20, as C_fire::Init builds it). The lower the
    // intensity, the fewer flame points burn; at 0 only smoke is left.
    class FireEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::Fire";
        static constexpr float kMinRadius = 0.5f;
        static constexpr float kMaxRadius = 15.0f;
        static constexpr float kMaxHeight = 12.0f;

        uint64_t missionGeneration = 0;
        // Places the flame points the same way on every client.
        uint32_t seed = 0;
        float radius = 3.0f;
        // Flame points rise up to this far above the position (a facade).
        float height = 0.0f;
        // 0 (put out, smoke only) to 1 (fully ablaze).
        float intensity = 1.0f;
        // A water jet is hitting it: clients add the retail steam.
        bool dousing = false;
        // Network ID of the vehicle it burns on, 0 for none. The server
        // moves the fire with it.
        uint64_t attachedId = 0;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(missionGeneration);
            fields.Field(seed);
            fields.Field(radius);
            fields.Field(height);
            fields.Field(intensity);
            fields.Field(dousing);
            fields.Field(attachedId);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(radius);
            fields.Field(height);
            fields.Field(intensity);
            fields.Field(dousing);
            fields.Field(attachedId);
        }
    };

    // A fire hose or a truck's water cannon (WaterJet.create). Every client
    // in range draws the retail hose effect (tables\effects.tbl 89
    // "hadice": 100 drops a second along the parent frame's x axis, falling
    // with gravity) from the source's nozzle to the target, with the retail
    // hydrant loop (sound table entry 327). The server decides what it puts
    // out: fires whose area holds the target.
    class WaterJetEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::WaterJet";
        static constexpr float kMinPressure = 8.0f;
        static constexpr float kMaxPressure = 30.0f;

        uint64_t missionGeneration = 0;
        // The player or vehicle that holds the nozzle (network ID).
        uint64_t sourceId = 0;
        bool sourceIsVehicle = false;
        bool active = false;
        glm::vec3 target {0.0f};
        // Muzzle speed in m/s; the arc reaches about pressure^2 / g.
        float pressure = 18.0f;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(missionGeneration);
            fields.Field(sourceId);
            fields.Field(sourceIsVehicle);
            fields.Field(active);
            fields.Field(target);
            fields.Field(pressure);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(active);
            fields.Field(target);
            fields.Field(pressure);
        }
    };
} // namespace Mafia1Online::Shared::Entities
