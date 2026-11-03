#include <utils/safe_win32.h>

#include "map_object_service.h"

#include "features/world/world_service.h"
#include "shared/features/map_object/map_object_entity.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/collision/native_collision_tree.h>
#include <mafia1/sdk/core/data_file.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/core/system.h>
#include <mafia1/sdk/scene/native_model_geometry.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <limits>

namespace Mafia1Online::Features::MapObject {
    namespace {
        using Entity = Shared::Entities::MapObjectEntity;
        namespace Motion = Shared::MapObject;

        using DeleteCollsLevel = void(__thiscall *)(void *);
        DeleteCollsLevel gDeleteOriginal = nullptr;
        MapObjectService *gService       = nullptr;

        void __fastcall DeleteCollsLevelHook(void *collision, void *) {
            if (gService) {
                gService->OnCollisionDeleting();
            }
            gDeleteOriginal(collision);
        }

        auto Log() {
            return Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT);
        }

        // An LS3D matrix (row vectors, v * M) has the memory layout of the
        // glm column-vector matrix of the same transform.
        glm::mat4 FromNative(const float *matrix) {
            return glm::make_mat4(matrix);
        }

        SDK::Collision::TreeLevel *LoadedTree() {
            return SDK::Collision::LoadedTree(SDK::Core::System::kCollision);
        }

        // LS3D rotates by the conjugate of the glm quaternion with the same
        // components (S_matrix::SetRot3 builds the transposed matrix), so a
        // heading h of glm::angleAxis(h, up) faces (sin h, 0, cos h) as
        // retail's SetDirection does.
        void SetNativeRotation(SDK::Scene::NativeFrame &frame, const glm::quat &rotation) {
            frame.SetLocalRotation(rotation.w, -rotation.x, -rotation.y, -rotation.z);
        }
    } // namespace

    MapObjectService::MapObjectService()  = default;
    MapObjectService::~MapObjectService() = default;

    bool MapObjectService::Install() {
        gService = this;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Collision::kDeleteCollsLevel), reinterpret_cast<void *>(&DeleteCollsLevelHook), reinterpret_cast<void **>(&gDeleteOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(SDK::Collision::kDeleteCollsLevel)) != MH_OK) {
            gService = nullptr;
            return false;
        }
        _installed = true;
        return true;
    }

    void MapObjectService::Uninstall() {
        Reset();
        _collision.Clear();
        if (_installed) {
            MH_DisableHook(reinterpret_cast<void *>(SDK::Collision::kDeleteCollsLevel));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Collision::kDeleteCollsLevel));
            _installed = false;
        }
        gService = nullptr;
    }

    void MapObjectService::OnCollisionDeleting() {
        // Normally empty by now (OnMissionClosing ran first); a mission script
        // reloading the tree keeps its bodies, which link into the new tree.
        _collision.Detach();
    }

    void MapObjectService::AttachTree() {
        auto *tree = LoadedTree();
        if (tree == _collision.Tree()) {
            return;
        }
        _collision.Attach(tree);
        if (tree) {
            const auto check = CheckTree(*tree);
            Log()->info("Map objects: collision tree {}x{} attached; {} faces, {} with unsorted vertices, {} winding bits disagreeing, {} cell faces out of order, "
                        "{} volumes after faces",
                        tree->width, tree->length, check.faces, check.unsortedVertices, check.windingMismatches, check.facesOutOfOrder, check.volumesAfterFaces);
        }
    }

    const ModelCollision *MapObjectService::Template(World::WorldService &world, SDK::Scene::NativeScene &scene, const std::string &model) {
        if (!_templatesTried) {
            _templatesTried = true;
            if (world.SelectedMissionGeneration() == world.LoadedMissionGeneration() && !world.SelectedMission().empty()) {
                const std::string root = "missions\\" + world.SelectedMission() + "\\";
                std::vector<uint8_t> klzBytes, sceneBytes;
                KlzFile file;
                std::vector<ModelInstance> instances;
                if (SDK::Core::DataFile::ReadAll((root + "tree.klz").c_str(), klzBytes) && ParseKlz(klzBytes, file) &&
                    SDK::Core::DataFile::ReadAll((root + "scene2.bin").c_str(), sceneBytes) && ReadModelInstances(sceneBytes, instances)) {
                    Log()->info("Map objects: {} collision records and {} model instances indexed in '{}'", file.records.size(), instances.size(), world.SelectedMission());
                    _templates.Load(std::move(file), std::move(instances));
                }
                else {
                    Log()->warn("Map objects: could not index the collision of '{}'; objects collide by their meshes", world.SelectedMission());
                }
            }
        }
        if (const auto it = _templateCache.find(model); it != _templateCache.end()) {
            return it->second.get();
        }
        std::unique_ptr<ModelCollision> found;
        if (_templates.Loaded()) {
            for (const auto &instance : _templates.Candidates(model)) {
                auto *frame = scene.FindFrame(instance.c_str());
                if (!frame) {
                    continue;
                }
                auto collision = std::make_unique<ModelCollision>(_templates.Localize(instance, FromNative(frame->UpdatedWorldMatrix())));
                if (!collision->Empty()) {
                    Log()->info("Map objects: '{}' takes the mission's collision of '{}': {} faces, {} boxes, {} spheres, {} cylinders", model, instance,
                                collision->faces.size(), collision->boxes.size(), collision->balls.size(), collision->posts.size());
                    found = std::move(collision);
                    break;
                }
            }
        }
        return (_templateCache[model] = std::move(found)).get();
    }

    void MapObjectService::Create(World::WorldService &world, Entity &entity) {
        auto *mission = SDK::Core::Mission::Get();
        auto *scene   = mission ? mission->GetScene() : nullptr;
        const uint64_t id = entity.GetNetworkID();
        if (!scene) {
            return;
        }
        const std::string model = Motion::CacheModelName(entity.model);
        auto *frame             = SDK::Scene::GetDriver()->CreateModel();
        if (!frame) {
            _failed.insert(id);
            return;
        }
        const std::string name = "mp_object_" + std::to_string(id);
        if (!frame->SetName(name.c_str()) || !SDK::Scene::GetModelCache()->OpenModel(frame, model.c_str()) || !frame->LinkTo(scene->PrimarySector())) {
            Log()->warn("Map objects: model '{}' could not load", entity.model);
            frame->Release();
            _failed.insert(id);
            return;
        }
        scene->AddFrame(frame);
        Object object;
        object.frame    = frame;
        object.scene    = scene;
        object.scale    = entity.scale;
        object.collides = entity.collision;
        object.material = entity.material == Entity::kAutomaticMaterial ? static_cast<uint8_t>(Motion::kDefaultMaterial) : entity.material;
        Sync(object, entity);
        const auto pose = Motion::SampleMotion(object.motion, std::chrono::duration<float>(std::chrono::steady_clock::now() - object.startedAt).count());
        Place(object, pose.position, pose.rotation);

        if (object.collides) {
            // The model's visual parts, as LoadCollision resolves them: LOD 0
            // of every visible I3D_object below the model.
            std::vector<SDK::Scene::NativeFrame *> pending {frame};
            std::vector<SDK::Player::Vector3> positions;
            const glm::mat4 toModel = glm::inverse(FromNative(frame->UpdatedWorldMatrix()));
            object.modelMin         = glm::vec3(std::numeric_limits<float>::max());
            object.modelMax         = glm::vec3(-std::numeric_limits<float>::max());
            while (!pending.empty() && object.parts.size() < 1024) {
                auto *current = pending.back();
                pending.pop_back();
                if (!current->IsOn()) {
                    continue;
                }
                for (auto *child = SDK::Scene::FirstChild(current); child; child = SDK::Scene::NextSibling(child)) {
                    pending.push_back(child);
                }
                auto *mesh = SDK::Scene::ObjectMesh(current);
                if (!mesh || mesh->lodCount == 0) {
                    continue;
                }
                auto *level = mesh->GetLOD(0);
                MeshPart part;
                if (!level || !SDK::Scene::ReadMeshLevel(level, positions, part.triangles) || part.triangles.empty()) {
                    continue;
                }
                part.child = SDK::Scene::ModelLocalName(current);
                part.world = FromNative(current->UpdatedWorldMatrix());
                part.vertices.reserve(positions.size());
                const glm::mat4 partToModel = toModel * part.world;
                for (const auto &position : positions) {
                    part.vertices.emplace_back(position.x, position.y, position.z);
                    const glm::vec3 inModel(partToModel * glm::vec4(position.x, position.y, position.z, 1.0f));
                    object.modelMin = glm::min(object.modelMin, inModel);
                    object.modelMax = glm::max(object.modelMax, inModel);
                }
                object.partFrames.push_back(current);
                object.parts.push_back(std::move(part));
            }
            if (object.parts.empty()) {
                object.modelMin = object.modelMax = glm::vec3(0.0f);
            }
            if (entity.material == Entity::kAutomaticMaterial) {
                object.collision = Template(world, *scene, model);
            }
            AttachTree();
            const auto shape = BuildShape(object.collision, object.parts, FromNative(frame->UpdatedWorldMatrix()), object.material, object.modelMin, object.modelMax);
            object.body      = _collision.Add(shape, scene->PrimarySector());
            if (object.body && _collision.Tree() && _collision.LinkedPrimitives(object.body) == 0) {
                Log()->warn("Map objects: '{}' lies outside the mission's collision grid and has no collision", entity.model);
            }
            if (!object.body) {
                Log()->warn("Map objects: '{}' has no collision geometry", entity.model);
            }
        }
        _objects.emplace(id, std::move(object));
    }

    void MapObjectService::Sync(Object &object, const Entity &entity) {
        object.revision  = entity.motionRevision;
        object.motion    = entity.motion;
        // When this client received the segment, so playback stays in step
        // however late the object is built or noticed.
        object.startedAt = entity.motionStartedAt;
        object.settled   = false;
    }

    void MapObjectService::Place(Object &object, const glm::vec3 &position, const glm::quat &rotation) {
        auto *frame = object.frame;
        const SDK::Player::Vector3 native {position.x, position.y, position.z};
        frame->SetWorldPosition(native);
        SetNativeRotation(*frame, rotation);
        frame->SetLocalScale({object.scale.x, object.scale.y, object.scale.z});
        // Files the frame into the sector it now stands in.
        object.scene->SetFrameSectorPos(frame, native);
        frame->Update();
    }

    void MapObjectService::Animate(Object &object) {
        if (object.settled) {
            return;
        }
        const auto pose = Motion::SampleMotion(object.motion, std::chrono::duration<float>(std::chrono::steady_clock::now() - object.startedAt).count());
        Place(object, pose.position, pose.rotation);
        object.settled = pose.done;
        if (object.body) {
            for (size_t i = 0; i < object.parts.size(); ++i) {
                object.parts[i].world = FromNative(object.partFrames[i]->UpdatedWorldMatrix());
            }
            _collision.Update(object.body, BuildShape(object.collision, object.parts, FromNative(object.frame->UpdatedWorldMatrix()), object.material, object.modelMin,
                                                      object.modelMax));
        }
    }

    void MapObjectService::Remove(Object &object) {
        if (object.body) {
            _collision.Remove(object.body);
            object.body = 0;
        }
        if (object.frame) {
            object.frame->LinkTo(nullptr);
            object.scene->DeleteFrame(object.frame);
            object.frame->Release();
            object.frame = nullptr;
        }
        object.partFrames.clear();
    }

    void MapObjectService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || !world.IsReady()) {
            return;
        }
        if (_missionGeneration != world.LoadedMissionGeneration()) {
            OnMissionClosing();
            _missionGeneration = world.LoadedMissionGeneration();
        }
        AttachTree();
        std::unordered_set<uint64_t> seen;
        replication->ForEach<Entity>([&](Entity *entity) {
            if (entity->missionGeneration != _missionGeneration) {
                return;
            }
            const uint64_t id = entity->GetNetworkID();
            seen.insert(id);
            auto it = _objects.find(id);
            if (it == _objects.end()) {
                if (!_failed.contains(id)) {
                    Create(world, *entity);
                }
                return;
            }
            auto &object = it->second;
            // A new revision restarts playback; the server finishing the
            // same move lands the object on the rest pose it sends.
            if (entity->motionRevision != object.revision || entity->motion.moving != object.motion.moving) {
                Sync(object, *entity);
            }
            Animate(object);
        });
        for (auto it = _objects.begin(); it != _objects.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }
            Remove(it->second);
            it = _objects.erase(it);
        }
        std::erase_if(_failed, [&](uint64_t id) { return !seen.contains(id); });
    }

    void MapObjectService::OnMissionClosing() {
        for (auto &[id, object] : _objects) {
            Remove(object);
        }
        _objects.clear();
        _failed.clear();
        _templateCache.clear();
        _templates.Clear();
        _templatesTried = false;
        _missionGeneration = 0;
    }

    void MapObjectService::Reset() {
        OnMissionClosing();
    }
} // namespace Mafia1Online::Features::MapObject
