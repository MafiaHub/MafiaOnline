#pragma once

#include "shared/features/car/car_entity.h"
#include "shared/features/car/car_hit_report.h"
#include "shared/features/car/car_mesh_checkpoint.h"
#include "shared/features/car/seat_action.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Mafia1Online::Shared::Car {
    struct Movement;
    struct EngineState;
    struct DamageReport;
} // namespace Mafia1Online::Shared::Car
namespace Mafia1Online::Shared::Entities {
    class PlayerEntity;
}

namespace Mafia1Online::Features::Car {
    // All methods run on the server simulation thread. The replication manager
    // owns the returned entity; this service only tracks its network ID.
    class CarService final {
      public:
        Shared::Entities::CarEntity *Spawn(std::string_view model, glm::vec3 position, glm::quat rotation, uint64_t missionGeneration, uint64_t controllerGuid);
        Shared::Entities::CarEntity *Find(uint64_t networkId) const;
        bool Despawn(uint64_t networkId);
        void ResetForMission();
        bool ApplyMovement(const Shared::Car::Movement &movement, uint64_t senderGuid);
        bool ApplyEngineState(const Shared::Car::EngineState &state, uint64_t senderGuid);
        bool ApplyDamageReport(const Shared::Car::DamageReport &report, uint64_t senderGuid);
        bool ApplyMeshReportChunk(const Shared::Car::MeshReportChunk &chunk, uint64_t senderGuid);
        bool ValidateHitGeometry(const Shared::Car::HitReport &report,
                                 std::chrono::steady_clock::time_point acceptedAt) const;
        // Match an impact against a recent accepted car pose and its native
        // body bounds. The returned speed belongs to that matching pose.
        std::optional<float> ValidateVehicleImpactGeometry(uint64_t carId, uint64_t missionGeneration,
                                                            const glm::vec3 &contact, const glm::vec3 &targetPosition,
                                                            std::chrono::steady_clock::time_point receivedAt) const;
        void SendMeshCheckpoint(uint64_t networkId, uint64_t recipientGuid) const;
        void TransferController(uint64_t formerGuid, uint64_t replacementGuid);
        void AssignUncontrolled(uint64_t controllerGuid);
        void SetController(uint64_t networkId, uint64_t controllerGuid);

        bool SetTransform(uint64_t networkId, glm::vec3 position, glm::vec3 velocity, glm::quat rotation);
        bool SetEngineOn(uint64_t networkId, bool on);
        bool SetFuel(uint64_t networkId, float fuel);
        bool SetLights(uint64_t networkId, uint32_t lightState);
        bool SetHorn(uint64_t networkId, bool on);
        bool SetSiren(uint64_t networkId, bool on);
        bool SetRadarColor(uint64_t networkId, uint32_t argb);
        bool SetOpacity(uint64_t networkId, float opacity);
        bool Repair(uint64_t networkId);
        std::string SaveState(uint64_t networkId) const;
        bool RestoreState(uint64_t networkId, const std::string &snapshot);
        bool SetDamage(uint64_t networkId, float health, uint32_t damageFlags, uint32_t detachedParts);
        bool SetMechanicalDamage(uint64_t networkId, float engineHealth, float gearboxHealth, float bodyDamage, int32_t fuelTankHealth);
        bool SetTerminalState(uint64_t networkId, Shared::Entities::CarEntity::TerminalState state);
        bool SetSeatCount(uint64_t networkId, uint8_t count);
        bool RecordSeatOutcome(uint64_t networkId, uint8_t seat, uint64_t playerId, uint64_t playerGeneration, Shared::Entities::CarEntity::SeatResult result);
        struct SeatLocation { uint64_t carId; uint8_t seat; };
        std::optional<SeatLocation> SeatForPlayer(uint64_t playerId, uint64_t playerGeneration) const;
        std::optional<Shared::Car::SeatEvent> ApplySeatIntent(const Shared::Car::SeatIntent &intent, Shared::Entities::PlayerEntity &player, uint64_t missionGeneration);
        void ClearOccupant(uint64_t playerId, uint64_t playerGeneration);
        // The occupant an accepted StealBegin just evicted, once.
        struct Eviction { uint64_t playerId; uint64_t playerGeneration; };
        std::optional<Eviction> TakeEviction();

        template <typename Fn>
        void ForEach(const Fn &fn) const {
            const std::vector<uint64_t> ids(_ids.begin(), _ids.end());
            for (uint64_t id : ids) {
                if (auto *car = Find(id)) {
                    fn(car);
                }
            }
        }

      private:
        struct HitPose {
            std::chrono::steady_clock::time_point observedAt;
            glm::vec3 position;
            glm::quat rotation;
            glm::vec3 velocity;
            uint64_t controllerGuid;
        };
        void RecordHitPose(uint64_t carId, const Shared::Entities::CarEntity &car);
        void ResetMeshSender(uint64_t networkId);
        struct MotionState {
            uint32_t lastSequence       = 0;
            bool hasSequence            = false;
            uint32_t lastEngineSequence = 0;
            bool hasEngineSequence      = false;
            uint32_t lastDamageSequence = 0;
            bool hasDamageSequence      = false;
            std::chrono::steady_clock::time_point lastMovement;
        };
        std::unordered_set<uint64_t> _ids;
        std::unordered_map<uint64_t, MotionState> _motion;
        std::unordered_map<uint64_t, uint32_t> _lastSeatIntent;
        std::optional<Eviction> _lastEviction;
        struct MeshAssembly {
            uint64_t ownerGuid = 0;
            uint32_t lastSequence = 0;
            bool hasSequence = false;
            uint32_t pendingSequence = 0;
            uint16_t pendingCount = 0;
            uint16_t pendingTotal = 0;
            std::vector<Shared::Car::MeshVertex> pending;
            std::vector<uint8_t> received;
            std::vector<Shared::Car::MeshVertex> checkpoint;
            uint64_t sourceGuid = 0;
        };
        std::unordered_map<uint64_t, MeshAssembly> _mesh;
        std::unordered_map<uint64_t, uint64_t> _authoredDamageRevision;
        std::unordered_map<uint64_t, std::deque<HitPose>> _hitPoses;
        uint64_t _seatEventSequence = 0;
    };
} // namespace Mafia1Online::Features::Car
