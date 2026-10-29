#pragma once

#include "shared/features/fire/fire_entity.h"

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::Features::Player {
    class PlayerService;
}
namespace Mafia1Online::Features::Car {
    class CarService;
}
namespace Mafia1Online::Features::Combat {
    class CombatService;
}

namespace Mafia1Online::Features::Fire {
    // Script fires and the water jets that put them out. The server owns
    // every intensity: a jet whose target lies in a fire's area lowers it,
    // a fire nobody douses grows back at its growth rate, and players
    // standing in a burning one take fire damage through CombatService the
    // way C_fire::Tick deals it. Clients only draw.
    class FireService final {
      public:
        static constexpr size_t kMaxFires = 64;
        static constexpr size_t kMaxJets  = 32;
        // A jet reaches no further than this from its nozzle.
        static constexpr float kJetReach = 45.0f;
        // Intensity per second one jet at the reference pressure takes off a
        // fire of the reference radius and toughness 1.
        static constexpr float kDousePerSecond   = 0.14f;
        static constexpr float kReferencePressure = 18.0f;
        static constexpr float kReferenceRadius   = 3.0f;

        enum class Reason : uint8_t { Doused, Script, BurnedOut };

        struct Options {
            float radius = 3.0f;
            float height = 0.0f;
            float intensity = 1.0f;
            // Damage per second to a player inside a fully burning fire.
            float damage = 20.0f;
            // 0 burns until put out.
            std::chrono::milliseconds lifetime {0};
            // Intensity per second it regains while nobody douses it.
            float growth = 0.0f;
            // How much harder than the reference it is to put out.
            float toughness = 1.0f;
            uint64_t attachedId = 0;
            uint32_t virtualWorld = 0xFFFFFFFFu; // MafiaNet::VIRTUAL_WORLD_GLOBAL
        };

        struct JetOptions {
            float pressure = 18.0f;
            // The player credited for what a vehicle's jet puts out.
            uint64_t creditId = 0;
            uint32_t virtualWorld = 0xFFFFFFFFu;
        };

        struct Contribution {
            uint64_t playerId = 0;
            float amount = 0.0f;
        };

        void Init(Player::PlayerService &players, Car::CarService &cars, Combat::CombatService &combat);
        void Shutdown();

        Shared::Entities::FireEntity *Create(const glm::vec3 &position, const Options &options, uint64_t missionGeneration, std::string owner);
        Shared::Entities::FireEntity *Find(uint64_t id) const;
        std::vector<uint64_t> List() const;
        bool Destroy(uint64_t id);
        // Lowers the intensity by amount, credited to a player (0: nobody).
        // Fires the extinguished callback when it reaches 0. The remaining
        // intensity, or a negative value for an unknown fire.
        float Douse(uint64_t id, float amount, uint64_t playerId);
        bool SetIntensity(uint64_t id, float intensity);
        bool SetRadius(uint64_t id, float radius);
        bool SetPosition(uint64_t id, const glm::vec3 &position);
        bool AttachTo(uint64_t id, uint64_t vehicleId);
        bool Extinguish(uint64_t id);
        std::vector<Contribution> Contributors(uint64_t id) const;

        Shared::Entities::WaterJetEntity *CreateJet(uint64_t sourceId, bool sourceIsVehicle, const JetOptions &options, uint64_t missionGeneration, std::string owner);
        Shared::Entities::WaterJetEntity *FindJet(uint64_t id) const;
        std::vector<uint64_t> ListJets() const;
        bool DestroyJet(uint64_t id);
        // nullopt stops the water.
        bool AimJet(uint64_t id, std::optional<glm::vec3> target);
        bool SetJetPressure(uint64_t id, float pressure);

        void Update();
        void ResetForMission();
        void DestroyOwnedBy(const std::string &owner);

        // (fire, reason, contributors) once a fire's intensity reaches 0.
        void SetExtinguishedCallback(std::function<void(uint64_t, Reason, const std::vector<Contribution> &)> callback) {
            _extinguished = std::move(callback);
        }

        // Nozzle position of a jet's source, nullopt while it has none.
        std::optional<glm::vec3> Nozzle(const Shared::Entities::WaterJetEntity &jet) const;

      private:
        struct FireRecord {
            std::string owner;
            float damage = 0.0f;
            float growth = 0.0f;
            float toughness = 1.0f;
            float pendingGrowth = 0.0f;
            std::chrono::steady_clock::time_point startedAt;
            std::chrono::milliseconds lifetime {0};
            std::chrono::steady_clock::time_point lastDoused {};
            std::chrono::steady_clock::time_point lastDamage {};
            std::unordered_map<uint64_t, float> contributions;
            bool out = false;
        };
        struct JetRecord {
            std::string owner;
            uint64_t creditId = 0;
        };

        void Out(uint64_t id, Reason reason);

        Player::PlayerService *_players = nullptr;
        Car::CarService *_cars = nullptr;
        Combat::CombatService *_combat = nullptr;
        std::unordered_map<uint64_t, FireRecord> _fires;
        std::unordered_map<uint64_t, JetRecord> _jets;
        std::chrono::steady_clock::time_point _lastUpdate {};
        uint32_t _nextSeed = 0x9e3779b9u;
        std::function<void(uint64_t, Reason, const std::vector<Contribution> &)> _extinguished;
    };
} // namespace Mafia1Online::Features::Fire
