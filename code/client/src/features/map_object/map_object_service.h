#pragma once

#include "collision_template.h"
#include "static_collision.h"

#include "shared/features/map_object/map_object_motion.h"

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
    struct NativeScene;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}
namespace Mafia1Online::Shared::Entities {
    class MapObjectEntity;
}

namespace Mafia1Online::Features::MapObject {
    // Builds the server's map objects as native model frames in the mission
    // scene and gives them collision in the static tree, then plays their
    // moves. Replication already leaves out objects of other virtual worlds.
    class MapObjectService final {
      public:
        MapObjectService();
        ~MapObjectService();
        // Hooks g_collision::DeleteCollsLevel so no patched cell outlives the
        // tree it belongs to.
        bool Install();
        void Uninstall();
        void Update(World::WorldService &world);
        // While the mission scene and collision tree still exist.
        void OnMissionClosing();
        void Reset();
        // The DeleteCollsLevel hook, before the tree is freed.
        void OnCollisionDeleting();

      private:
        struct Object {
            SDK::Scene::NativeFrame *frame = nullptr;
            SDK::Scene::NativeScene *scene = nullptr;
            uint32_t body                  = 0;
            bool collides                  = false;
            uint8_t material               = 0;
            const ModelCollision *collision = nullptr;
            std::vector<SDK::Scene::NativeFrame *> partFrames;
            std::vector<MeshPart> parts;
            glm::vec3 modelMin {0.0f};
            glm::vec3 modelMax {0.0f};
            glm::vec3 scale {1.0f};
            uint32_t revision = 0;
            Shared::MapObject::MotionSegment motion;
            std::chrono::steady_clock::time_point startedAt;
            bool settled = false; // the rest pose of this revision is applied
        };

        void Create(World::WorldService &world, Shared::Entities::MapObjectEntity &entity);
        void Remove(Object &object);
        void Sync(Object &object, const Shared::Entities::MapObjectEntity &entity);
        void Animate(Object &object);
        void Place(Object &object, const glm::vec3 &position, const glm::quat &rotation);
        const ModelCollision *Template(World::WorldService &world, SDK::Scene::NativeScene &scene, const std::string &model);
        void AttachTree();

        std::unordered_map<uint64_t, Object> _objects;
        std::unordered_set<uint64_t> _failed;
        StaticCollision _collision;
        CollisionTemplates _templates;
        bool _templatesTried = false;
        std::unordered_map<std::string, std::unique_ptr<ModelCollision>> _templateCache;
        uint64_t _missionGeneration = 0;
        bool _installed = false;
    };
} // namespace Mafia1Online::Features::MapObject
