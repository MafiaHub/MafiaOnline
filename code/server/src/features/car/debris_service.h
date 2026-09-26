#pragma once

#include "shared/features/car/car_debris.h"
#include "shared/features/car/car_debris_entity.h"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::Features::Car {
    class CarService;

    class DebrisService final {
      public:
        Shared::Entities::CarDebrisEntity *ApplySpawn(const Shared::Car::DebrisSpawnReport &report,
                                                      uint64_t senderGuid, uint64_t missionGeneration,
                                                      const CarService &cars);
        bool ApplyMovement(const Shared::Car::DebrisMovement &movement, uint64_t senderGuid);
        bool ApplyGone(const Shared::Car::DebrisGone &gone, uint64_t senderGuid);
        void NoteExploded(uint64_t carId);
        void TransferController(uint64_t formerGuid, uint64_t replacementGuid);
        void AssignUncontrolled(uint64_t controllerGuid);
        void PruneMissingCars(const CarService &cars);
        void ClearForCar(uint64_t carId);
        void ResetForMission();

      private:
        struct Motion {
            uint32_t sequence = 0;
            bool hasSequence = false;
            std::chrono::steady_clock::time_point lastUpdate;
        };
        std::unordered_set<uint64_t> _ids;
        std::unordered_map<uint64_t, Motion> _motion;
        struct CreatorSequence { uint64_t guid = 0; uint32_t sequence = 0; };
        std::unordered_map<uint64_t, CreatorSequence> _lastCreatorSequence;
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _explodedAt;
    };
} // namespace Mafia1Online::Features::Car
