#pragma once

#include "shared/features/map_object/map_object_entity.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::Features::MapObject {
    // Script-placed world objects. The server owns every pose and plays each
    // move itself, so scripts read where an object is and hear when a move
    // ends; clients play the same segment for their visuals and collision.
    class MapObjectService final {
      public:
        struct Options {
            uint32_t virtualWorld = 0xFFFFFFFFu; // MafiaNet::VIRTUAL_WORLD_GLOBAL
            bool collision        = true;
            uint8_t material      = Shared::Entities::MapObjectEntity::kAutomaticMaterial;
            glm::vec3 scale {1.0f};
        };

        // Null at the object limit or for an invalid model, pose or option.
        // owner is the creating resource, whose stop destroys the object.
        Shared::Entities::MapObjectEntity *Create(const std::string &model, const glm::vec3 &position, const glm::quat &rotation, const Options &options,
                                                  uint64_t missionGeneration, std::string owner);
        Shared::Entities::MapObjectEntity *Find(uint64_t id) const;
        bool Destroy(uint64_t id);
        std::vector<uint64_t> List() const;

        // Teleports stop a running move without raising mapObjectMoved.
        bool SetPosition(uint64_t id, const glm::vec3 &position);
        bool SetRotation(uint64_t id, const glm::quat &rotation);
        bool SetVirtualWorld(uint64_t id, uint32_t world);
        bool MoveTo(uint64_t id, const glm::vec3 &position, float speed, std::optional<glm::quat> rotation);
        bool Stop(uint64_t id);
        bool IsMoving(uint64_t id) const;

        // Advances moves; raises the moved callback after the loop.
        void Update();
        void ResetForMission();
        void DestroyOwnedBy(const std::string &owner);

        void SetMovedCallback(std::function<void(uint64_t)> callback) {
            _moved = std::move(callback);
        }

      private:
        // Rest at the current pose and start a new playback revision.
        void Rest(Shared::Entities::MapObjectEntity &object, const glm::vec3 &position, const glm::quat &rotation);

        std::unordered_map<uint64_t, std::string> _owners;
        std::function<void(uint64_t)> _moved;
    };
} // namespace Mafia1Online::Features::MapObject
