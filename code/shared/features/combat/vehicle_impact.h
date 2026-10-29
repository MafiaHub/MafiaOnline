#pragma once

#include <networking/rpc/rpc.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>

namespace Mafia1Online::Shared::Combat {
    // C_human::Collision 0x5784a0, car branch: a contact at 3 m/s or more
    // hits the human (HIT_TYPE_SNIPER, (|v| - 2.5) * 4.2); a slower one only
    // pushes it out of the car and starts a side jump (Do_SideJump 0x587040,
    // animation states 322-329).
    inline constexpr float kVehicleImpactMinSpeed = 3.0f;
    // The slowest contact the server takes as a bump: below it the car is
    // parked against the human, which retail does not dodge either (a resting
    // body contact never reaches the approaching-contact callback).
    inline constexpr float kVehicleBumpMinSpeed = 0.5f;
    inline constexpr float kVehicleImpactMaxSpeed = 120.0f;
    // What a bump replays as the contact velocity: below the impact speed,
    // so C_human::Collision takes its side-jump branch and never Hit.
    inline constexpr float kVehicleBumpReplaySpeed = 2.9f;
    // Per driver: contacts the server checks per second, bumps and impacts
    // together. One car touches a crowd in a burst; more is a flood.
    inline constexpr size_t kVehicleContactsPerSecond = 8;

    enum class VehicleContact : uint8_t {
        Impact = 0,
        Bump = 1,
    };

    inline bool KnownVehicleContact(VehicleContact kind) {
        return kind == VehicleContact::Impact || kind == VehicleContact::Bump;
    }

    // The slowest server-side car speed that backs a reported contact kind.
    // A bump the server saw faster (the driver braked into it) still only
    // dodges: it never harms, so the driver's word on the branch stands.
    inline float VehicleContactMinSpeed(VehicleContact kind) {
        return kind == VehicleContact::Bump ? kVehicleBumpMinSpeed : kVehicleImpactMinSpeed;
    }
    inline bool VehicleContactSpeedValid(VehicleContact kind, float speed) {
        return std::isfinite(speed) && speed >= VehicleContactMinSpeed(kind) && speed <= kVehicleImpactMaxSpeed;
    }

    // The driver's contact speed: what C_human::Collision would take.
    // Nothing (no report) below a bump or for a contact that separates.
    inline bool ClassifyVehicleContact(float speed, float approach, VehicleContact &kind) {
        if (!std::isfinite(speed) || !std::isfinite(approach) || approach >= 0.0f || speed < kVehicleBumpMinSpeed) {
            return false;
        }
        kind = speed < kVehicleImpactMinSpeed ? VehicleContact::Bump : VehicleContact::Impact;
        return true;
    }

    // The car's velocity scaled into the side-jump branch: the direction
    // decides which of the eight dodges plays, the length only the branch.
    inline glm::vec3 BumpReplayDirection(const glm::vec3 &velocity) {
        const float speed = glm::length(velocity);
        if (!std::isfinite(speed) || speed < 0.0001f) {
            return glm::vec3(0.0f);
        }
        return speed < kVehicleBumpReplaySpeed ? velocity : velocity * (kVehicleBumpReplaySpeed / speed);
    }

    // One contact per car and human each 500 ms (C_Vehicle reports it every
    // physics step while the bodies touch); an impact still follows a bump
    // at once, as the car speeds up into the human it nudged.
    inline constexpr std::chrono::milliseconds kVehicleContactRepeat {500};
    inline bool VehicleContactDue(std::chrono::steady_clock::duration since, VehicleContact previous, VehicleContact next) {
        return since >= kVehicleContactRepeat || (previous == VehicleContact::Bump && next == VehicleContact::Impact);
    }

    // A sliding one-second window over a sender's contacts.
    class VehicleContactRate final {
      public:
        using Clock = std::chrono::steady_clock;
        bool Admit(Clock::time_point now, size_t limit = kVehicleContactsPerSecond) {
            while (!_recent.empty() && now - _recent.front() >= std::chrono::seconds(1)) {
                _recent.pop_front();
            }
            if (_recent.size() >= limit) {
                return false;
            }
            _recent.push_back(now);
            return true;
        }

      private:
        std::deque<Clock::time_point> _recent;
    };

    // The car simulation owner observed a body contact with a remote player
    // or a simulation host NPC. The server calculates damage from its own car
    // speed and checks both poses.
    struct VehicleImpact {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::VehicleImpact");

        uint64_t carId = 0;
        uint64_t targetId = 0;
        uint64_t missionGeneration = 0;
        uint64_t targetSpawnGeneration = 0;
        float contactX = 0.0f;
        float contactY = 0.0f;
        float contactZ = 0.0f;
        VehicleContact kind = VehicleContact::Impact;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto kindValue = static_cast<uint8_t>(kind);
            stream->Serialize(write, carId);
            stream->Serialize(write, targetId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, contactX);
            stream->Serialize(write, contactY);
            stream->Serialize(write, contactZ);
            stream->Serialize(write, kindValue);
            if (!write) {
                kind = static_cast<VehicleContact>(kindValue);
            }
        }
    };

    // Server -> the bumped player's own client: a car the server checked
    // touched it below the impact speed. That client runs C_player::Collision
    // on its own player, which side-jumps (and respects the police lock); the
    // dodge reaches everyone else as its locomotion state.
    struct VehicleBump {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::VehicleBump");

        uint64_t carId = 0;
        uint64_t missionGeneration = 0;
        uint64_t targetSpawnGeneration = 0;
        float contactX = 0.0f;
        float contactY = 0.0f;
        float contactZ = 0.0f;
        // BumpReplayDirection of the server's car velocity.
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, carId);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, contactX);
            stream->Serialize(write, contactY);
            stream->Serialize(write, contactZ);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
        }
    };
} // namespace Mafia1Online::Shared::Combat
