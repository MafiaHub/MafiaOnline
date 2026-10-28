#pragma once

#include "static_collision.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::Features::MapObject {
    // missions\<name>\tree.klz as g_collision::LoadCollision (0x5c2b70)
    // reads it, before it resolves the frame links: every primitive still
    // names the frame record it belongs to.
    struct KlzFile {
        struct Record {
            uint32_t flags = 0; // 1: faces need a visual, 2: volumes
            std::string name;
        };
        struct Face {
            SDK::Collision::Header header {};
            std::array<uint16_t, 3> vertices {};
            uint16_t link = 0; // 0x7fff: standalone vertices
            glm::vec3 normal {0.0f};
        };
        // An XTOBB, AABB or OBB in world space; matrices are glm
        // column-vector transforms (the file's LS3D matrices as they are).
        struct Volume {
            SDK::Collision::Header header {};
            uint32_t link = 0;
            glm::vec3 minimum {0.0f}; // extents for XTOBB and OBB, bounds for AABB
            glm::vec3 maximum {0.0f};
            glm::mat4 transform {1.0f};
        };
        struct Sphere {
            SDK::Collision::Header header {};
            uint32_t link = 0;
            glm::vec3 center {0.0f};
            float radius = 0.0f;
        };
        struct Cylinder {
            SDK::Collision::Header header {};
            uint32_t link = 0;
            float x = 0.0f, z = 0.0f, radius = 0.0f;
        };

        std::vector<Record> records;
        std::vector<Face> faces;
        std::vector<Volume> volumes; // XTOBB, AABB and OBB, told apart by header.type
        std::vector<Sphere> spheres;
        std::vector<Cylinder> cylinders;
    };
    bool ParseKlz(std::span<const uint8_t> bytes, KlzFile &out);

    // scene2.bin objects that place a model: frame name and model file.
    struct ModelInstance {
        std::string frame;
        std::string model; // lower case, as the cache names it (".i3d")
    };
    bool ReadModelInstances(std::span<const uint8_t> bytes, std::vector<ModelInstance> &out);

    // The mission's own collision for one model, taken from one placed
    // instance and expressed in the model's space. Faces name the child
    // frame whose LOD 0 mesh their vertex indices point into.
    struct ModelCollision {
        struct Face {
            std::string child; // the child's name inside the model
            std::array<uint16_t, 3> vertices {};
            uint8_t material = 0;
            uint8_t flags    = 0;
        };
        struct Box {
            glm::mat4 transform {1.0f}; // model space, may carry scale
            glm::vec3 halfExtent {0.0f};
            bool plate       = false;
            uint8_t material = 0;
            uint8_t flags    = 0;
        };
        struct Ball {
            glm::vec3 center {0.0f};
            float radius     = 0.0f;
            uint8_t material = 0;
            uint8_t flags    = 0;
        };
        struct Post {
            glm::vec3 center {0.0f}; // at the instance's height
            float radius     = 0.0f;
            uint8_t material = 0;
            uint8_t flags    = 0;
        };
        std::string instance;
        std::vector<Face> faces;
        std::vector<Box> boxes;
        std::vector<Ball> balls;
        std::vector<Post> posts;

        bool Empty() const {
            return faces.empty() && boxes.empty() && balls.empty() && posts.empty();
        }
    };

    // A shot at glass or a crash frame breaks the frame the volume links;
    // script objects keep theirs (bulletproof glass, universal).
    uint8_t ScriptMaterial(uint8_t material);

    // Indexes a mission's klz frame records by the scene2.bin model
    // instances they belong to: the instance's own name for its volumes, and
    // "<instance>.<child>" for its children.
    class CollisionTemplates final {
      public:
        void Load(KlzFile file, std::vector<ModelInstance> instances);
        void Clear();
        bool Loaded() const {
            return _loaded;
        }

        // Instances of the model (lower case .i3d) that have collision in the
        // tree, in scene order.
        std::vector<std::string> Candidates(const std::string &model) const;
        // The instance's collision, expressed in its model space.
        // instanceWorld is the instance frame's world matrix.
        ModelCollision Localize(const std::string &instance, const glm::mat4 &instanceWorld) const;

      private:
        struct Owned {
            uint32_t record;
            std::string child; // empty for the instance frame itself
        };
        std::vector<Owned> RecordsOf(const std::string &instance) const;

        KlzFile _file;
        std::vector<ModelInstance> _instances;
        std::unordered_map<std::string, uint32_t> _exact;
        std::unordered_map<std::string, std::vector<std::pair<uint32_t, std::string>>> _children;
        std::vector<std::vector<uint32_t>> _facesByRecord;
        std::vector<std::vector<uint32_t>> _volumesByRecord;
        std::vector<std::vector<uint32_t>> _spheresByRecord;
        std::vector<std::vector<uint32_t>> _cylindersByRecord;
        bool _loaded = false;
    };

    // A model's LOD 0 mesh of one visual child, in that child's space.
    struct MeshPart {
        std::string child;
        glm::mat4 world {1.0f}; // the child's current world matrix
        std::vector<glm::vec3> vertices;
        std::vector<std::array<uint16_t, 3>> triangles;
    };

    // World-space collision of an object whose root has world matrix
    // rootWorld (glm, with scale). With a template, the template's faces and
    // volumes; without, every triangle of every part with material.
    // modelBounds is the model-space box of the parts, for tilted posts and
    // the posts' top.
    CollisionShape BuildShape(const ModelCollision *collision, const std::vector<MeshPart> &parts, const glm::mat4 &rootWorld, uint8_t material,
                              const glm::vec3 &modelMin, const glm::vec3 &modelMax);
} // namespace Mafia1Online::Features::MapObject
