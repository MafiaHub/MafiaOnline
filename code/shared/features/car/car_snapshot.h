#pragma once

#include "car_damage_state.h"
#include "car_mesh_validation.h"

#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>
#include <vector>

namespace Mafia1Online::Shared::Car {
    // Versioned durable condition. Position, motion, occupants and network IDs
    // belong to the current spawn, never to a garage save.
    struct Snapshot {
        std::string model;
        float fuel = 0, fuelTankCapacity = 0, health = 100, opacity = 1;
        uint32_t damageFlags = 0, detachedParts = 0, radarColor = 0, lightState = 0;
        uint8_t seatCount = 4;
        bool engineOn = false, sirenOn = false, hornOn = false, speedLimited = false;
        int32_t gear = 0, maximumGear = 0;
        float steeringInput = 0;
        std::array<uint8_t, 8> doorTargets {};
        DamageState damage;
        std::vector<MeshVertex> mesh;

        template <typename F>
        void Fields(F &&field) {
            field(fuel);
            field(fuelTankCapacity);
            field(health);
            field(opacity);
            field(damageFlags);
            field(detachedParts);
            field(radarColor);
            field(lightState);
            field(seatCount);
            field(engineOn);
            field(sirenOn);
            field(hornOn);
            field(speedLimited);
            field(gear);
            field(maximumGear);
            field(steeringInput);
            for (auto &door : doorTargets) field(door);
            damage.Fields(field);
        }

        std::string Encode() const {
            auto copy   = *this;
            auto values = nlohmann::json::array();
            copy.Fields([&](auto &value) {
                values.push_back(value);
            });
            auto vertices = nlohmann::json::array();
            for (const auto &v : mesh) vertices.push_back({v.zone, v.lod, v.displacement});
            return nlohmann::json {{"version", 1}, {"model", model}, {"condition", values}, {"mesh", vertices}}.dump();
        }

        static bool Decode(const std::string &text, Snapshot &out) {
            if (text.size() > 1024 * 1024)
                return false;
            try {
                const auto json = nlohmann::json::parse(text);
                if (json.at("version") != 1)
                    return false;
                Snapshot value;
                value.model        = json.at("model").get<std::string>();
                const auto &values = json.at("condition");
                if (!values.is_array())
                    return false;
                size_t index = 0;
                bool valid   = true;
                value.Fields([&](auto &field) {
                    using T          = std::remove_reference_t<decltype(field)>;
                    const auto &item = values.at(index++);
                    if constexpr (std::is_same_v<T, bool>) {
                        if (!item.is_boolean()) {
                            valid = false;
                            return;
                        }
                    }
                    else {
                        if (!item.is_number()) {
                            valid = false;
                            return;
                        }
                        const double number = item.get<double>();
                        if (!std::isfinite(number) || number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max()) {
                            valid = false;
                            return;
                        }
                        if constexpr (std::is_integral_v<T>) {
                            if (std::trunc(number) != number) {
                                valid = false;
                                return;
                            }
                        }
                    }
                    field = item.get<T>();
                });
                if (!valid || index != values.size())
                    return false;
                const auto &vertices = json.at("mesh");
                if (!vertices.is_array() || vertices.size() > kMaxMeshVertices)
                    return false;
                for (const auto &v : vertices) {
                    if (!v.is_array() || v.size() != 3)
                        return false;
                    for (const auto &n : v)
                        if (!n.is_number_unsigned())
                            return false;
                    if (v[0].get<uint64_t>() > 63 || v[1].get<uint64_t>() > 1 || v[2].get<uint64_t>() > UINT32_MAX)
                        return false;
                    value.mesh.push_back({v[0].get<uint8_t>(), v[1].get<uint8_t>(), v[2].get<uint32_t>()});
                }
                const auto &d = value.damage;
                if (value.fuel < 0 || value.fuel > value.fuelTankCapacity || value.fuelTankCapacity > 10000 || value.health < 0 || value.health > 100000 || value.opacity < 0 || value.opacity > 1 || value.seatCount == 0 || value.seatCount > 8 || value.maximumGear < 0
                    || value.maximumGear > 32 || value.gear < -1 || value.gear > value.maximumGear || std::abs(value.steeringInput) > 10 || d.lightCount > DamageState::kMaxLights || d.zoneCount > DamageState::kMaxZones || d.wheelCount > DamageState::kMaxWheels
                    || d.engineHealth < 0 || d.engineHealth > 100000 || d.gearboxHealth < 0 || d.gearboxHealth > 100000 || d.bodyDamage < 0 || d.bodyDamage > 100000 || d.fuelTankHealth < 0 || d.fuelTankHealth > 100000 || d.engineDamagePower < -1 || d.engineDamagePower > 1.5f
                    || d.engineDestroyed > 1 || d.burning > 1 || d.burnTimer > 600000 || d.burnDuration > 600000)
                    return false;
                for (const auto &l : d.lights)
                    if ((l.flags & ~1u) || l.damage < 0 || l.damage > 100000)
                        return false;
                for (const auto &z : d.zones)
                    if ((z.flags & ~3u) || z.crackLevel < 0 || z.crackLevel > 100000)
                        return false;
                for (const auto &w : d.wheels)
                    if ((w.flags & ~DamageState::kWheelDamageFlags) || w.health < 0 || w.health > 100000 || std::abs(w.deformAngle) > 3.2f)
                        return false;
                if (!ValidateMeshVertexSet(value.mesh, d.zoneCount))
                    return false;
                out = std::move(value);
                return true;
            }
            catch (const nlohmann::json::exception &) {
                return false;
            }
        }
    };
} // namespace Mafia1Online::Shared::Car
