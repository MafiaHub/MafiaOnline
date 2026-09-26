#include "debris_service.h"

#include "car_service.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Car {
    namespace {
        bool Finite(glm::vec3 value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool Finite(glm::quat value) {
            return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool ValidParameters(const Shared::Car::DebrisSpawnReport &report) {
            const auto &v = report.parameters.values;
            if (report.type < 1 || report.type > 7 ||
                !std::all_of(v.begin(), v.end(), [](float value) { return std::isfinite(value); })) {
                return false;
            }
            if (report.type == 1) {
                return v[0] > 0.0f && v[0] <= 5.0f && v[1] > 0.0f && v[1] <= 10.0f &&
                       v[2] > 0.0f && v[2] <= 1000.0f &&
                       std::abs(v[3]) <= 150.0f && std::abs(v[4]) <= 150.0f && std::abs(v[5]) <= 150.0f &&
                       v[6] >= 0.0f && v[6] <= 10000.0f && v[7] >= 0.0f && v[7] <= 1000.0f;
            }
            return v[0] > 0.0f && v[0] <= 1000.0f &&
                   std::abs(v[1]) <= 150.0f && std::abs(v[2]) <= 150.0f && std::abs(v[3]) <= 150.0f &&
                   v[4] >= 0.0f && v[4] <= 10.0f && v[5] == 0.0f && v[6] == 0.0f && v[7] == 0.0f;
        }
    } // namespace

    Shared::Entities::CarDebrisEntity *DebrisService::ApplySpawn(const Shared::Car::DebrisSpawnReport &report,
                                                                  uint64_t senderGuid, uint64_t missionGeneration,
                                                                  const CarService &cars) {
        const auto *car = cars.Find(report.carId);
        const glm::vec3 position(report.x, report.y, report.z);
        const glm::quat rotation(report.qw, report.qx, report.qy, report.qz);
        const auto exploded = _explodedAt.find(report.carId);
        const bool explosionDebris = car && car->terminalState == Shared::Entities::CarEntity::TerminalState::Exploded &&
                                     exploded != _explodedAt.end() &&
                                     std::chrono::steady_clock::now() - exploded->second <= std::chrono::seconds(2);
        if (!car || senderGuid == 0 || report.localSequence == 0 || _ids.size() >= 2048 ||
            car->simulationControllerGuid != senderGuid || car->missionGeneration != missionGeneration ||
            report.missionGeneration != missionGeneration ||
            (car->terminalState != Shared::Entities::CarEntity::TerminalState::Active && !explosionDebris) ||
            !ValidParameters(report) || !Finite(position) || !Finite(rotation) ||
            glm::length(rotation) < 0.5f || glm::length(rotation) > 2.0f ||
            glm::distance(position, car->position) > 20.0f ||
            (report.type == 1 && (report.partIndex >= Shared::Car::DamageState::kMaxWheels ||
                                  (car->nativeDamageValid && report.partIndex >= car->nativeDamage.wheelCount))) ||
            (report.type != 1 && (report.partIndex >= Shared::Car::DamageState::kMaxZones ||
                                  (car->nativeDamageValid && report.partIndex >= car->nativeDamage.zoneCount)))) {
            return nullptr;
        }
        auto &sequence = _lastCreatorSequence[report.carId];
        if (sequence.guid == senderGuid && static_cast<int32_t>(report.localSequence - sequence.sequence) <= 0) {
            return nullptr;
        }
        size_t carDebrisCount = 0;
        auto *replication = Framework::CoreModules::GetReplication();
        for (uint64_t id : _ids) {
            if (auto *existing = replication->GetEntity<Shared::Entities::CarDebrisEntity>(id);
                existing && existing->carId == report.carId) {
                if (++carDebrisCount >= 128) {
                    return nullptr;
                }
            }
        }
        auto *debris = replication->CreateEntity<Shared::Entities::CarDebrisEntity>();
        debris->carId = report.carId;
        debris->missionGeneration = missionGeneration;
        debris->creatorGuid = senderGuid;
        debris->controllerGuid = senderGuid;
        debris->creatorSequence = report.localSequence;
        debris->type = report.type;
        debris->partIndex = report.partIndex;
        debris->parameters = report.parameters;
        debris->position = position;
        debris->rotation = glm::normalize(rotation);
        debris->streaming.alwaysVisible = true;
        debris->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        _ids.insert(debris->GetNetworkID());
        sequence = {senderGuid, report.localSequence};
        return debris;
    }

    bool DebrisService::ApplyMovement(const Shared::Car::DebrisMovement &movement, uint64_t senderGuid) {
        if (_ids.find(movement.debrisId) == _ids.end()) {
            return false;
        }
        auto *debris = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarDebrisEntity>(movement.debrisId);
        const glm::vec3 position(movement.x, movement.y, movement.z);
        const glm::quat rotation(movement.qw, movement.qx, movement.qy, movement.qz);
        if (!debris || senderGuid == 0 || debris->controllerGuid != senderGuid ||
            debris->missionGeneration != movement.missionGeneration || !Finite(position) || !Finite(rotation) ||
            glm::length(rotation) < 0.5f || glm::length(rotation) > 2.0f ||
            std::abs(position.x) > 50000.0f || std::abs(position.y) > 50000.0f || std::abs(position.z) > 50000.0f) {
            return false;
        }
        auto &motion = _motion[movement.debrisId];
        if (motion.hasSequence && static_cast<int32_t>(movement.sequence - motion.sequence) <= 0) {
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        const float elapsed = motion.hasSequence ? std::clamp(std::chrono::duration<float>(now - motion.lastUpdate).count(), 0.0f, 3.0f) : 0.0f;
        if (glm::distance(position, debris->position) > (motion.hasSequence ? 4.0f + 120.0f * elapsed : 20.0f)) {
            return false;
        }
        debris->position = position;
        debris->rotation = glm::normalize(rotation);
        motion = {movement.sequence, true, now};
        return true;
    }

    bool DebrisService::ApplyGone(const Shared::Car::DebrisGone &gone, uint64_t senderGuid) {
        if (_ids.find(gone.debrisId) == _ids.end()) {
            return false;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        auto *debris = replication->GetEntity<Shared::Entities::CarDebrisEntity>(gone.debrisId);
        if (!debris || senderGuid == 0 || debris->controllerGuid != senderGuid ||
            debris->missionGeneration != gone.missionGeneration) {
            return false;
        }
        _ids.erase(gone.debrisId);
        _motion.erase(gone.debrisId);
        replication->DestroyEntity(debris);
        return true;
    }

    void DebrisService::NoteExploded(uint64_t carId) {
        _explodedAt[carId] = std::chrono::steady_clock::now();
    }

    void DebrisService::TransferController(uint64_t formerGuid, uint64_t replacementGuid) {
        if (formerGuid == 0 || formerGuid == replacementGuid) {
            return;
        }
        for (auto it = _lastCreatorSequence.begin(); it != _lastCreatorSequence.end();) {
            if (it->second.guid == formerGuid) { it = _lastCreatorSequence.erase(it); }
            else { ++it; }
        }
        for (uint64_t id : _ids) {
            if (auto *debris = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarDebrisEntity>(id);
                debris && debris->controllerGuid == formerGuid) {
                debris->controllerGuid = replacementGuid;
                _motion.erase(id);
            }
        }
    }

    void DebrisService::AssignUncontrolled(uint64_t controllerGuid) {
        if (controllerGuid == 0) {
            return;
        }
        for (uint64_t id : _ids) {
            if (auto *debris = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarDebrisEntity>(id);
                debris && debris->controllerGuid == 0) {
                debris->controllerGuid = controllerGuid;
                _motion.erase(id);
            }
        }
    }

    void DebrisService::PruneMissingCars(const CarService &cars) {
        auto *replication = Framework::CoreModules::GetReplication();
        std::unordered_set<uint64_t> removedCars;
        for (auto it = _ids.begin(); it != _ids.end();) {
            auto *debris = replication->GetEntity<Shared::Entities::CarDebrisEntity>(*it);
            if (!debris || !cars.Find(debris->carId)) {
                if (debris) { removedCars.insert(debris->carId); }
                _motion.erase(*it);
                it = _ids.erase(it);
                if (debris) { replication->DestroyEntity(debris); }
            }
            else { ++it; }
        }
        for (uint64_t carId : removedCars) {
            _lastCreatorSequence.erase(carId);
            _explodedAt.erase(carId);
        }
    }

    void DebrisService::ClearForCar(uint64_t carId) {
        auto *replication = Framework::CoreModules::GetReplication();
        for (auto it = _ids.begin(); it != _ids.end();) {
            auto *debris = replication->GetEntity<Shared::Entities::CarDebrisEntity>(*it);
            if (!debris || debris->carId == carId) {
                _motion.erase(*it);
                it = _ids.erase(it);
                if (debris) { replication->DestroyEntity(debris); }
            }
            else { ++it; }
        }
        _lastCreatorSequence.erase(carId);
        _explodedAt.erase(carId);
    }

    void DebrisService::ResetForMission() {
        auto *replication = Framework::CoreModules::GetReplication();
        for (uint64_t id : _ids) {
            if (auto *debris = replication->GetEntity<Shared::Entities::CarDebrisEntity>(id)) {
                replication->DestroyEntity(debris);
            }
        }
        _ids.clear();
        _motion.clear();
        _lastCreatorSequence.clear();
        _explodedAt.clear();
    }
} // namespace Mafia1Online::Features::Car
