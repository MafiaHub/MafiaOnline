#include "map_object_service.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>

#include <chrono>
#include <cmath>

namespace Mafia1Online::Features::MapObject {
    namespace {
        using Shared::Entities::MapObjectEntity;
        namespace Motion = Shared::MapObject;

        bool Finite(const glm::vec3 &value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool Normalize(glm::quat &rotation) {
            const float length = glm::length(rotation);
            if (!std::isfinite(length) || length <= 0.0001f) {
                return false;
            }
            rotation /= length;
            return true;
        }

        float Elapsed(const MapObjectEntity &object) {
            return std::chrono::duration<float>(std::chrono::steady_clock::now() - object.motionStartedAt).count();
        }
    } // namespace

    MapObjectEntity *MapObjectService::Create(const std::string &model, const glm::vec3 &position, const glm::quat &rotation, const Options &options,
                                              uint64_t missionGeneration, std::string owner) {
        glm::quat pose = rotation;
        if (_owners.size() >= Motion::kMaxObjects || !Motion::ValidModelName(model) || !Finite(position) || !Normalize(pose) || !Finite(options.scale) ||
            options.scale.x < Motion::kMinScale || options.scale.y < Motion::kMinScale || options.scale.z < Motion::kMinScale ||
            options.scale.x > Motion::kMaxScale || options.scale.y > Motion::kMaxScale || options.scale.z > Motion::kMaxScale ||
            (options.material != MapObjectEntity::kAutomaticMaterial && !Motion::ValidMaterial(options.material))) {
            return nullptr;
        }
        auto *object              = Framework::CoreModules::GetReplication()->CreateEntity<MapObjectEntity>();
        object->model             = model;
        object->missionGeneration = missionGeneration;
        object->collision         = options.collision;
        object->material          = options.material;
        object->scale             = options.scale;
        // Every client streams the whole map; the virtual world still applies.
        object->streaming.alwaysVisible = true;
        object->SetVirtualWorld(options.virtualWorld);
        Rest(*object, position, pose);
        _owners[object->GetNetworkID()] = std::move(owner);
        return object;
    }

    MapObjectEntity *MapObjectService::Find(uint64_t id) const {
        if (!_owners.contains(id)) {
            return nullptr;
        }
        return Framework::CoreModules::GetReplication()->GetEntity<MapObjectEntity>(id);
    }

    bool MapObjectService::Destroy(uint64_t id) {
        auto *object = Find(id);
        _owners.erase(id);
        if (!object) {
            return false;
        }
        Framework::CoreModules::GetReplication()->DestroyEntity(object);
        return true;
    }

    std::vector<uint64_t> MapObjectService::List() const {
        std::vector<uint64_t> ids;
        ids.reserve(_owners.size());
        for (const auto &[id, owner] : _owners) {
            ids.push_back(id);
        }
        return ids;
    }

    void MapObjectService::Rest(MapObjectEntity &object, const glm::vec3 &position, const glm::quat &rotation) {
        object.position              = position;
        object.rotation              = rotation;
        object.motion                = {};
        object.motion.startPosition  = position;
        object.motion.startRotation  = rotation;
        object.motion.targetPosition = position;
        object.motion.targetRotation = rotation;
        object.motionStartedAt       = std::chrono::steady_clock::now();
        ++object.motionRevision;
    }

    bool MapObjectService::SetPosition(uint64_t id, const glm::vec3 &position) {
        auto *object = Find(id);
        if (!object || !Finite(position)) {
            return false;
        }
        Rest(*object, position, object->rotation);
        return true;
    }

    bool MapObjectService::SetRotation(uint64_t id, const glm::quat &rotation) {
        auto *object  = Find(id);
        glm::quat pose = rotation;
        if (!object || !Normalize(pose)) {
            return false;
        }
        Rest(*object, object->position, pose);
        return true;
    }

    bool MapObjectService::SetVirtualWorld(uint64_t id, uint32_t world) {
        auto *object = Find(id);
        if (!object) {
            return false;
        }
        object->SetVirtualWorld(world);
        return true;
    }

    bool MapObjectService::MoveTo(uint64_t id, const glm::vec3 &position, float speed, std::optional<glm::quat> rotation) {
        auto *object = Find(id);
        glm::quat target = rotation.value_or(object ? object->rotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        if (!object || !Finite(position) || !std::isfinite(speed) || speed <= 0.0f || speed > Motion::kMaxSpeed || !Normalize(target)) {
            return false;
        }
        // Start from where the running move has the object now.
        const glm::vec3 start      = object->position;
        const glm::quat startPose  = object->rotation;
        Rest(*object, start, startPose);
        // Shortest turn.
        if (glm::dot(startPose, target) < 0.0f) {
            target = -target;
        }
        object->motion.targetPosition = position;
        object->motion.targetRotation = target;
        object->motion.speed          = speed;
        object->motion.moving         = true;
        return true;
    }

    bool MapObjectService::Stop(uint64_t id) {
        auto *object = Find(id);
        if (!object) {
            return false;
        }
        if (object->motion.moving) {
            const auto pose = Motion::SampleMotion(object->motion, Elapsed(*object));
            Rest(*object, pose.position, pose.rotation);
        }
        return true;
    }

    bool MapObjectService::IsMoving(uint64_t id) const {
        const auto *object = Find(id);
        return object && object->motion.moving;
    }

    void MapObjectService::Update() {
        std::vector<uint64_t> arrived;
        for (const auto &[id, owner] : _owners) {
            auto *object = Find(id);
            if (!object || !object->motion.moving) {
                continue;
            }
            const auto pose  = Motion::SampleMotion(object->motion, Elapsed(*object));
            object->position = pose.position;
            object->rotation = pose.rotation;
            if (pose.done) {
                // The revision stays: clients finish the same segment and
                // rest where it ends.
                object->motion.startPosition = pose.position;
                object->motion.startRotation = pose.rotation;
                object->motion.speed         = 0.0f;
                object->motion.moving        = false;
                arrived.push_back(id);
            }
        }
        if (_moved) {
            for (uint64_t id : arrived) {
                // A handler may destroy objects that arrived after it.
                if (Find(id)) {
                    _moved(id);
                }
            }
        }
    }

    void MapObjectService::ResetForMission() {
        for (uint64_t id : List()) {
            Destroy(id);
        }
    }

    void MapObjectService::DestroyOwnedBy(const std::string &owner) {
        if (owner.empty()) {
            return;
        }
        std::vector<uint64_t> owned;
        for (const auto &[id, name] : _owners) {
            if (name == owner) {
                owned.push_back(id);
            }
        }
        for (uint64_t id : owned) {
            Destroy(id);
        }
    }
} // namespace Mafia1Online::Features::MapObject
