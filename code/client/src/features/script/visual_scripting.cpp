#include <utils/safe_win32.h>

#include "visual_scripting.h"

#include "script_runtime.h"
#include "features/world/world_service.h"
#include "shared/scripting_catalog.h"

#include <mafia1/sdk/collision/native_collision.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/player/native_human_factory.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/ui/native_hud.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <core_modules.h>
#include <integrations/client/scripting/module.h>
#include <scripting/resource/resource_manager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::Scripting {
    namespace {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using SDK::Scene::NativeFrame;

        constexpr size_t kMaxFrames = 128;
        constexpr size_t kMaxHumanActors = 24;
        constexpr size_t kMaxAnimationFilename = 59; // Retail track path uses a 64-byte buffer plus .tck and NUL.
        constexpr size_t kMaxDrawCommands = 512;
        constexpr size_t kMaxTextBytes = 200;
        constexpr size_t kMaxWorldFrameNameLength = 64;
        constexpr float kMaxCoordinate = 16384.0f;
        constexpr float kMaxScale = 100.0f;
        constexpr uint32_t kDefaultColor = 0xffffffffu;
        constexpr uint32_t kFirstFrameId = 1;

        struct LocalFrame {
            NativeFrame *frame = nullptr;
            SDK::Scene::NativeScene *scene = nullptr;
            uint64_t missionGeneration = 0;
            std::string owner;
            SDK::Player::NativeActor *actor = nullptr;
            bool solid = false;
        };
        std::unordered_map<uint32_t, LocalFrame> gFrames;
        // RemoveTemporaryActor defers destruction until the native game tick.
        // Keep the model frame until the actor destructor hook can release it.
        std::unordered_map<SDK::Player::NativeActor *, NativeFrame *> gPendingHumanFrames;
        uint32_t gNextFrameId = kFirstFrameId;

        enum class DrawKind { Rectangle, Line, Circle, Text };
        struct DrawCommand {
            DrawKind kind = DrawKind::Rectangle;
            float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
            float thickness = 1.0f;
            uint32_t color = kDefaultColor;
            uint32_t font = SDK::UI::kHudFont;
            std::string text;
            std::string owner;
        };
        std::vector<DrawCommand> gDrawCommands;

        Framework::Scripting::ResourceManager *Manager() {
            auto *module = static_cast<Framework::Integrations::Client::Scripting::ClientScriptingModule *>(Framework::CoreModules::GetScriptingModule());
            return module ? module->GetResourceManager() : nullptr;
        }
        std::string Owner(v8::Isolate *isolate) {
            auto *manager = Manager();
            if (!manager) return {};
            auto owner = manager->GetCurrentResourceContext();
            return owner.empty() ? manager->GetResourceContextFromStack(isolate) : owner;
        }
        void ReleaseFrame(LocalFrame &entry) {
            if (entry.actor) {
                gPendingHumanFrames.emplace(entry.actor, entry.frame);
                if (auto *mission = SDK::Core::Mission::Get(); mission && entry.missionGeneration == GetWorld().LoadedMissionGeneration()) {
                    mission->Game()->RemoveTemporaryActor(entry.actor);
                }
                return;
            }
            entry.frame->LinkTo(nullptr);
            entry.scene->DeleteFrame(entry.frame);
            entry.frame->Release();
        }
        void CleanupResource(const std::string &owner) {
            std::erase_if(gDrawCommands, [&owner](const DrawCommand &command) { return command.owner == owner; });
            for (auto it = gFrames.begin(); it != gFrames.end();) {
                if (it->second.owner != owner) { ++it; continue; }
                ReleaseFrame(it->second);
                it = gFrames.erase(it);
            }
        }

        bool ReadFloat(v8::Local<v8::Value> value, float &out, float minimum, float maximum) {
            return Args::ReadFloat(value, out) && out >= minimum && out <= maximum;
        }
        bool ReadId(v8::Local<v8::Value> value, uint32_t &out) {
            if (!value->IsUint32()) return false;
            out = value.As<v8::Uint32>()->Value();
            return out != 0;
        }
        bool ReadColor(v8::Local<v8::Value> value, uint32_t &out) {
            if (!value->IsUint32()) return false;
            out = value.As<v8::Uint32>()->Value();
            return true;
        }
        bool ReadFont(v8::Local<v8::Value> value, uint32_t &out) {
            if (!value->IsUint32()) return false;
            out = value.As<v8::Uint32>()->Value();
            // NativeIndicators contains four stock font definitions.
            return out < 4;
        }
        bool ReadVector(v8::Isolate *isolate, v8::Local<v8::Value> value, glm::vec3 &out) {
            return Args::ReadPosition(isolate, value, out) &&
                std::fabs(out.x) <= kMaxCoordinate && std::fabs(out.y) <= kMaxCoordinate && std::fabs(out.z) <= kMaxCoordinate;
        }
        bool ReadRotation(v8::Isolate *isolate, v8::Local<v8::Value> value, std::array<float, 4> &out) {
            if (!value->IsObject()) return false;
            auto object = value.As<v8::Object>();
            auto context = isolate->GetCurrentContext();
            const char *keys[] {"w", "x", "y", "z"};
            for (size_t i = 0; i < out.size(); ++i) {
                v8::Local<v8::Value> part;
                if (!object->Get(context, v8pp::to_v8(isolate, keys[i])).ToLocal(&part) ||
                    !ReadFloat(part, out[i], -1.0f, 1.0f)) return false;
            }
            const float lengthSquared = out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3];
            return lengthSquared > 0.000001f;
        }
        LocalFrame *Resolve(uint32_t id, const std::string &owner) {
            auto it = gFrames.find(id);
            return it != gFrames.end() && it->second.owner == owner && it->second.missionGeneration == GetWorld().LoadedMissionGeneration() ? &it->second : nullptr;
        }
        SDK::Scene::NativeScene *Scene() {
            auto *mission = SDK::Core::Mission::Get();
            return GetWorld().IsReady() && mission ? mission->GetScene() : nullptr;
        }
        bool HasI3DExtension(const std::string &name) {
            if (name.size() < 5) return false;
            const auto tail = name.substr(name.size() - 4);
            return tail == ".i3d" || tail == ".I3D";
        }
        bool ModelNameValid(const std::string &name) {
            if (name.size() > 64 || !HasI3DExtension(name)) return false;
            for (char ch : name) {
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return false;
            }
            return true;
        }
        bool AnimationNameValid(const std::string &name) {
            if (name.size() > kMaxAnimationFilename || !HasI3DExtension(name)) return false;
            for (char ch : name) {
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.' || ch == ' ')) return false;
            }
            return true;
        }
        SDK::Player::NativeHuman *Human(LocalFrame *entry) {
            return entry && entry->actor ? static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(entry->actor)) : nullptr;
        }
        void StoreActorTransform(LocalFrame &entry) {
            entry.actor->SetStoredTransform(entry.frame->WorldPosition(), entry.frame->WorldDirection());
        }
        uint32_t CreateFrame(const char *model, const std::string &owner) {
            auto *scene = Scene();
            if (!scene || owner.empty() || gFrames.size() >= kMaxFrames) return 0;
            auto *frame = model ? SDK::Scene::GetDriver()->CreateModel() : SDK::Scene::GetDriver()->CreateDummy();
            if (!frame) return 0;
            if (model && !SDK::Scene::GetModelCache()->OpenModel(frame, model)) {
                frame->Release();
                return 0;
            }
            // The scene takes its own reference in AddFrame; the script keeps
            // the CreateFrame reference until destroy or mission close.
            const uint32_t id = gNextFrameId++;
            const std::string name = std::string(model ? "mp_local_model_" : "mp_local_dummy_") + std::to_string(id);
            if (!frame->SetName(name.c_str()) || !frame->LinkTo(scene->PrimarySector())) {
                frame->Release();
                return 0;
            }
            frame->Update();
            scene->AddFrame(frame);
            gFrames.emplace(id, LocalFrame {frame, scene, GetWorld().LoadedMissionGeneration(), owner});
            return id;
        }
        void JS_CreateModel(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsString()) {
                Args::Throw(info.GetIsolate(), "Scene.createModel(filename) expects a stock .i3d filename");
                return;
            }
            const auto name = v8pp::from_v8<std::string>(info.GetIsolate(), info[0]);
            if (!ModelNameValid(name)) {
                Args::Throw(info.GetIsolate(), "Model filename must be a bare .i3d filename");
                return;
            }
            const uint32_t id = CreateFrame(name.c_str(), Owner(info.GetIsolate()));
            info.GetReturnValue().Set(id ? v8::Integer::NewFromUnsigned(info.GetIsolate(), id).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }
        void JS_CanUseHumanModel(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsString()) {
                Args::Throw(info.GetIsolate(), "Scene.canUseHumanModel(filename) expects a stock .i3d filename");
                return;
            }
            const auto name = v8pp::from_v8<std::string>(info.GetIsolate(), info[0]);
            info.GetReturnValue().Set(ModelNameValid(name) && Scene() && SDK::Player::NativeHumanFactory::CanLoadHumanModel(name));
        }
        void JS_CreateDummy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 0) {
                Args::Throw(info.GetIsolate(), "Scene.createDummy() takes no arguments");
                return;
            }
            const uint32_t id = CreateFrame(nullptr, Owner(info.GetIsolate()));
            info.GetReturnValue().Set(id ? v8::Integer::NewFromUnsigned(info.GetIsolate(), id).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }
        uint32_t CreateHumanFrame(const std::string &model, const glm::vec3 &position, glm::vec3 direction, bool solid, const std::string &owner) {
            direction.y = 0.0f;
            const float length = std::hypot(direction.x, direction.z);
            auto *mission = SDK::Core::Mission::Get();
            const size_t humanCount = gPendingHumanFrames.size() +
                std::count_if(gFrames.begin(), gFrames.end(), [](const auto &item) { return item.second.actor != nullptr; });
            if (!Scene() || !mission || owner.empty() || gFrames.size() >= kMaxFrames || humanCount >= kMaxHumanActors ||
                !SDK::Player::NativeHumanFactory::CanLoadHumanModel(model)) {
                return 0;
            }
            const uint32_t id = gNextFrameId++;
            const std::string name = "mp_local_human_" + std::to_string(id);
            direction /= length;
            auto *actor = SDK::Player::NativeHumanFactory::CreateTemporary(*mission, SDK::Player::NativeActor::Type::Entity, name.c_str(), model.c_str(),
                {position.x, position.y, position.z}, {direction.x, direction.y, direction.z});
            if (!actor) return 0;
            auto *human = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
            human->HoldEntityForCutscene();
            // GameInit has already inserted both native human collision spheres.
            // Preview humans remain visual-only; stationary shopkeepers can opt
            // into the same collision response as a stock pedestrian.
            if (!solid) human->DisableCollisions();
            gFrames.emplace(id, LocalFrame {actor->Frame(), mission->GetScene(), GetWorld().LoadedMissionGeneration(), owner, actor, solid});
            return id;
        }
        bool ReadHumanCreate(const v8::FunctionCallbackInfo<v8::Value> &info, std::string &model, glm::vec3 &position, glm::vec3 &direction, bool &solid) {
            direction = {0.0f, 0.0f, 1.0f};
            solid = false;
            if (info.Length() < 2 || info.Length() > 4 || !info[0]->IsString() || !ReadVector(info.GetIsolate(), info[1], position) ||
                (info.Length() >= 3 && !info[2]->IsUndefined() && !ReadVector(info.GetIsolate(), info[2], direction)) ||
                (info.Length() == 4 && !info[3]->IsBoolean())) {
                Args::Throw(info.GetIsolate(), "Scene.createHuman(model, position, direction?, solid?) expects a human model, world vectors and optional collision flag"); return false;
            }
            if (info.Length() == 4) solid = info[3].As<v8::Boolean>()->Value();
            model = v8pp::from_v8<std::string>(info.GetIsolate(), info[0]);
            if (!ModelNameValid(model) || std::hypot(direction.x, direction.z) < 0.001f) {
                Args::Throw(info.GetIsolate(), "Human model must be a bare .i3d filename and direction must have a horizontal component"); return false;
            }
            return true;
        }
        void JS_CreateHuman(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::string model;
            glm::vec3 position, direction;
            bool solid;
            if (!ReadHumanCreate(info, model, position, direction, solid)) return;
            const uint32_t id = CreateHumanFrame(model, position, direction, solid, Owner(info.GetIsolate()));
            info.GetReturnValue().Set(id ? v8::Integer::NewFromUnsigned(info.GetIsolate(), id).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }
        void JS_PlayHumanAnimation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            if (info.Length() < 2 || info.Length() > 3 || !ReadId(info[0], id) || !info[1]->IsString() ||
                (info.Length() == 3 && !info[2]->IsUndefined() && !info[2]->IsBoolean())) {
                Args::Throw(info.GetIsolate(), "Scene.playHumanAnimation(handle, filename, loop?) expects a human and .i3d animation"); return;
            }
            const auto filename = v8pp::from_v8<std::string>(info.GetIsolate(), info[1]);
            if (!AnimationNameValid(filename)) {
                Args::Throw(info.GetIsolate(), "Animation must be a bare .i3d filename of at most 59 bytes"); return;
            }
            auto *human = Human(Resolve(id, Owner(info.GetIsolate())));
            if (!human || !human->Actor().IsAlive()) { info.GetReturnValue().Set(false); return; }
            const bool loop = info.Length() == 3 && info[2]->IsBoolean() && info[2].As<v8::Boolean>()->Value();
            const bool played = human->PlayAnimation(filename.c_str(), loop);
            if (!played) human->ReturnToIdle();
            info.GetReturnValue().Set(played);
        }
        void JS_SetHumanIdle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            if (info.Length() != 1 || !ReadId(info[0], id)) {
                Args::Throw(info.GetIsolate(), "Scene.setHumanIdle(handle) expects a human handle"); return;
            }
            auto *human = Human(Resolve(id, Owner(info.GetIsolate())));
            if (!human || !human->Actor().IsAlive()) { info.GetReturnValue().Set(false); return; }
            human->ReturnToIdle();
            info.GetReturnValue().Set(true);
        }
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            if (info.Length() != 1 || !ReadId(info[0], id)) {
                Args::Throw(info.GetIsolate(), "Scene.destroy(handle) expects a local frame handle");
                return;
            }
            auto it = gFrames.find(id);
            if (it == gFrames.end() || it->second.owner != Owner(info.GetIsolate())) {
                info.GetReturnValue().Set(false);
                return;
            }
            ReleaseFrame(it->second);
            gFrames.erase(it);
            info.GetReturnValue().Set(true);
        }
        void JS_SetPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            glm::vec3 position;
            if (info.Length() != 2 || !ReadId(info[0], id) || !ReadVector(info.GetIsolate(), info[1], position)) {
                Args::Throw(info.GetIsolate(), "Scene.setPosition(handle, position) expects a handle and finite x/y/z");
                return;
            }
            auto *entry = Resolve(id, Owner(info.GetIsolate()));
            auto *scene = Scene();
            if (!entry || !scene) { info.GetReturnValue().Set(false); return; }
            const SDK::Player::Vector3 native {position.x, position.y, position.z};
            entry->frame->SetWorldPosition(native);
            scene->SetFrameSectorPos(entry->frame, native);
            entry->frame->Update();
            if (entry->actor) StoreActorTransform(*entry);
            info.GetReturnValue().Set(true);
        }
        void JS_SetRotation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            std::array<float, 4> rotation {};
            if (info.Length() != 2 || !ReadId(info[0], id) || !ReadRotation(info.GetIsolate(), info[1], rotation)) {
                Args::Throw(info.GetIsolate(), "Scene.setRotation(handle, quaternion) expects {w,x,y,z}");
                return;
            }
            auto *entry = Resolve(id, Owner(info.GetIsolate()));
            if (!entry) { info.GetReturnValue().Set(false); return; }
            entry->frame->SetLocalRotation(rotation[0], rotation[1], rotation[2], rotation[3]);
            entry->frame->Update();
            if (entry->actor) StoreActorTransform(*entry);
            info.GetReturnValue().Set(true);
        }
        void JS_SetDirection(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            glm::vec3 direction;
            if (info.Length() != 2 || !ReadId(info[0], id) || !ReadVector(info.GetIsolate(), info[1], direction)) {
                Args::Throw(info.GetIsolate(), "Scene.setDirection(handle, direction) expects a handle and finite x/y/z"); return;
            }
            auto *entry = Resolve(id, Owner(info.GetIsolate()));
            if (!entry) { info.GetReturnValue().Set(false); return; }
            if (entry->actor) direction.y = 0.0f;
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
            if (length < 0.001f) { Args::Throw(info.GetIsolate(), "Direction must have nonzero length"); return; }
            direction /= length;
            entry->frame->SetDirection({direction.x, direction.y, direction.z});
            entry->frame->Update();
            if (entry->actor) StoreActorTransform(*entry);
            info.GetReturnValue().Set(true);
        }
        void JS_SetScale(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            glm::vec3 scale;
            if (info.Length() != 2 || !ReadId(info[0], id)) {
                Args::Throw(info.GetIsolate(), "Scene.setScale(handle, scale) expects a handle and a positive number or vector");
                return;
            }
            if (info[1]->IsNumber()) {
                float uniform;
                if (!ReadFloat(info[1], uniform, 0.001f, kMaxScale)) {
                    Args::Throw(info.GetIsolate(), "Scale must be between 0.001 and 100"); return;
                }
                scale = glm::vec3(uniform);
            }
            else if (!Args::ReadPosition(info.GetIsolate(), info[1], scale) ||
                scale.x < 0.001f || scale.y < 0.001f || scale.z < 0.001f ||
                scale.x > kMaxScale || scale.y > kMaxScale || scale.z > kMaxScale) {
                Args::Throw(info.GetIsolate(), "Scale axes must each be between 0.001 and 100"); return;
            }
            auto *entry = Resolve(id, Owner(info.GetIsolate()));
            if (!entry) { info.GetReturnValue().Set(false); return; }
            if (entry->actor) { info.GetReturnValue().Set(false); return; }
            entry->frame->SetLocalScale({scale.x, scale.y, scale.z});
            entry->frame->Update();
            info.GetReturnValue().Set(true);
        }
        void JS_SetVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            if (info.Length() != 2 || !ReadId(info[0], id) || !info[1]->IsBoolean()) {
                Args::Throw(info.GetIsolate(), "Scene.setVisible(handle, visible) expects a handle and boolean"); return;
            }
            auto *entry = Resolve(id, Owner(info.GetIsolate()));
            if (!entry) { info.GetReturnValue().Set(false); return; }
            entry->frame->SetOn(info[1].As<v8::Boolean>()->Value());
            info.GetReturnValue().Set(true);
        }
        struct FrameRef {
            bool local = false;
            uint32_t id = 0;
            uint64_t generation = 0;
            std::string name;
            std::string owner;
        };
        FrameRef ReadFrameRef(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto data = info.Data().As<v8::Object>();
            auto value = [&](const char *key) { return data->Get(context, v8pp::to_v8(isolate, key)).ToLocalChecked(); };
            return {value("local")->BooleanValue(isolate), value("id").As<v8::Uint32>()->Value(),
                static_cast<uint64_t>(value("generation").As<v8::Number>()->Value()),
                v8pp::from_v8<std::string>(isolate, value("name")), v8pp::from_v8<std::string>(isolate, value("owner"))};
        }
        NativeFrame *ResolveFrame(const FrameRef &ref, LocalFrame **local = nullptr) {
            if (local) *local = nullptr;
            if (ref.owner.empty() || ref.owner != Owner(v8::Isolate::GetCurrent()) ||
                ref.generation != GetWorld().LoadedMissionGeneration()) return nullptr;
            auto *scene = Scene();
            if (!scene) return nullptr;
            if (!ref.local) return scene->FindFrame(ref.name.c_str());
            auto *entry = Resolve(ref.id, ref.owner);
            if (local) *local = entry;
            return entry ? entry->frame : nullptr;
        }
        void FrameIsValid(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(ResolveFrame(ReadFrameRef(info)) != nullptr);
        }
        void FrameGetWorldPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *frame = ResolveFrame(ReadFrameRef(info));
            if (!frame) { info.GetReturnValue().SetNull(); return; }
            const auto point = frame->WorldPosition();
            info.GetReturnValue().Set(Args::Position(info.GetIsolate(), {point.x, point.y, point.z}));
        }
        void FrameGetWorldDirection(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *frame = ResolveFrame(ReadFrameRef(info));
            if (!frame) { info.GetReturnValue().SetNull(); return; }
            const auto direction = frame->WorldDirection();
            info.GetReturnValue().Set(Args::Position(info.GetIsolate(), {direction.x, direction.y, direction.z}));
        }
        void FrameGetRotation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *frame = ResolveFrame(ReadFrameRef(info));
            if (!frame) { info.GetReturnValue().SetNull(); return; }
            const auto rotation = frame->LocalRotation();
            auto *isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto object = v8::Object::New(isolate);
            constexpr std::array<const char *, 4> keys {"w", "x", "y", "z"};
            for (size_t i = 0; i < keys.size(); ++i) object->Set(context, v8pp::to_v8(isolate, keys[i]), v8::Number::New(isolate, rotation[i])).Check();
            info.GetReturnValue().Set(object);
        }
        void FrameGetScale(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *frame = ResolveFrame(ReadFrameRef(info));
            if (!frame) { info.GetReturnValue().SetNull(); return; }
            const auto scale = frame->LocalScale();
            info.GetReturnValue().Set(Args::Position(info.GetIsolate(), {scale.x, scale.y, scale.z}));
        }
        void FrameSetWorldPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto ref = ReadFrameRef(info);
            LocalFrame *entry;
            if (!ref.local || !ResolveFrame(ref, &entry)) { info.GetReturnValue().Set(false); return; }
            glm::vec3 position;
            if (info.Length() != 1 || !ReadVector(info.GetIsolate(), info[0], position)) {
                Args::Throw(info.GetIsolate(), "Frame.setWorldPosition(position) expects finite x/y/z"); return;
            }
            const SDK::Player::Vector3 native {position.x, position.y, position.z};
            entry->frame->SetWorldPosition(native);
            entry->scene->SetFrameSectorPos(entry->frame, native);
            entry->frame->Update();
            if (entry->actor) StoreActorTransform(*entry);
            info.GetReturnValue().Set(true);
        }
        void FrameSetRotation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto ref = ReadFrameRef(info);
            LocalFrame *entry;
            if (!ref.local || !ResolveFrame(ref, &entry)) { info.GetReturnValue().Set(false); return; }
            std::array<float, 4> rotation {};
            if (info.Length() != 1 || !ReadRotation(info.GetIsolate(), info[0], rotation)) {
                Args::Throw(info.GetIsolate(), "Frame.setRotation(rotation) expects {w,x,y,z}"); return;
            }
            entry->frame->SetLocalRotation(rotation[0], rotation[1], rotation[2], rotation[3]);
            entry->frame->Update();
            if (entry->actor) StoreActorTransform(*entry);
            info.GetReturnValue().Set(true);
        }
        void FrameSetDirection(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto ref = ReadFrameRef(info);
            LocalFrame *entry;
            if (!ref.local || !ResolveFrame(ref, &entry)) { info.GetReturnValue().Set(false); return; }
            glm::vec3 direction;
            if (info.Length() != 1 || !ReadVector(info.GetIsolate(), info[0], direction)) {
                Args::Throw(info.GetIsolate(), "Frame.setDirection(direction) expects finite x/y/z"); return;
            }
            if (entry->actor) direction.y = 0.0f;
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
            if (length < 0.001f) { Args::Throw(info.GetIsolate(), "Direction must have nonzero length"); return; }
            direction /= length;
            entry->frame->SetDirection({direction.x, direction.y, direction.z});
            entry->frame->Update();
            if (entry->actor) StoreActorTransform(*entry);
            info.GetReturnValue().Set(true);
        }
        void FrameSetScale(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto ref = ReadFrameRef(info);
            LocalFrame *entry;
            if (!ref.local || !ResolveFrame(ref, &entry) || entry->actor) { info.GetReturnValue().Set(false); return; }
            glm::vec3 scale;
            if (info.Length() != 1) { Args::Throw(info.GetIsolate(), "Frame.setScale(scale) expects a positive number or vector"); return; }
            if (info[0]->IsNumber()) {
                float uniform;
                if (!ReadFloat(info[0], uniform, 0.001f, kMaxScale)) { Args::Throw(info.GetIsolate(), "Scale must be between 0.001 and 100"); return; }
                scale = glm::vec3(uniform);
            }
            else if (!Args::ReadPosition(info.GetIsolate(), info[0], scale) ||
                scale.x < 0.001f || scale.y < 0.001f || scale.z < 0.001f ||
                scale.x > kMaxScale || scale.y > kMaxScale || scale.z > kMaxScale) {
                Args::Throw(info.GetIsolate(), "Scale axes must each be between 0.001 and 100"); return;
            }
            entry->frame->SetLocalScale({scale.x, scale.y, scale.z});
            entry->frame->Update();
            info.GetReturnValue().Set(true);
        }
        void FrameSetVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto ref = ReadFrameRef(info);
            LocalFrame *entry;
            if (!ref.local || !ResolveFrame(ref, &entry)) { info.GetReturnValue().Set(false); return; }
            if (info.Length() != 1 || !info[0]->IsBoolean()) { Args::Throw(info.GetIsolate(), "Frame.setVisible(visible) expects boolean"); return; }
            entry->frame->SetOn(info[0].As<v8::Boolean>()->Value());
            info.GetReturnValue().Set(true);
        }
        void FrameDestroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto ref = ReadFrameRef(info);
            LocalFrame *entry;
            if (!ref.local || !ResolveFrame(ref, &entry)) { info.GetReturnValue().Set(false); return; }
            ReleaseFrame(*entry);
            gFrames.erase(ref.id);
            info.GetReturnValue().Set(true);
        }
        v8::Local<v8::Object> WrapFrame(v8::Isolate *isolate, const FrameRef &ref) {
            auto context = isolate->GetCurrentContext();
            auto data = v8::Object::New(isolate);
            auto setData = [&](const char *key, v8::Local<v8::Value> value) { data->Set(context, v8pp::to_v8(isolate, key), value).Check(); };
            setData("local", v8::Boolean::New(isolate, ref.local));
            setData("id", v8::Integer::NewFromUnsigned(isolate, ref.id));
            setData("generation", v8::Number::New(isolate, static_cast<double>(ref.generation)));
            setData("name", v8pp::to_v8(isolate, ref.name));
            setData("owner", v8pp::to_v8(isolate, ref.owner));
            auto result = v8::Object::New(isolate);
            auto method = [&](const char *name, v8::FunctionCallback callback) {
                result->Set(context, v8pp::to_v8(isolate, name), v8::Function::New(context, callback, data).ToLocalChecked()).Check();
            };
            constexpr auto attributes = static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);
            result->DefineOwnProperty(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, ref.name), attributes).Check();
            result->DefineOwnProperty(context, v8pp::to_v8(isolate, "id"),
                ref.local ? v8::Integer::NewFromUnsigned(isolate, ref.id).As<v8::Value>() : v8::Null(isolate).As<v8::Value>(), attributes).Check();
            result->DefineOwnProperty(context, v8pp::to_v8(isolate, "readOnly"), v8::Boolean::New(isolate, !ref.local), attributes).Check();
            method("isValid", &FrameIsValid);
            method("getWorldPosition", &FrameGetWorldPosition);
            method("getWorldDirection", &FrameGetWorldDirection);
            method("getRotation", &FrameGetRotation);
            method("getScale", &FrameGetScale);
            method("setWorldPosition", &FrameSetWorldPosition);
            method("setRotation", &FrameSetRotation);
            method("setDirection", &FrameSetDirection);
            method("setScale", &FrameSetScale);
            method("setVisible", &FrameSetVisible);
            method("destroy", &FrameDestroy);
            return result;
        }
        v8::Local<v8::Value> WrapLocalFrame(v8::Isolate *isolate, uint32_t id) {
            const auto owner = Owner(isolate);
            auto *entry = Resolve(id, owner);
            if (!entry) return v8::Null(isolate);
            return WrapFrame(isolate, {true, id, entry->missionGeneration, entry->frame->Name(), owner});
        }
        void JS_GetFrame(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            if (info.Length() != 1 || !ReadId(info[0], id)) { Args::Throw(info.GetIsolate(), "Scene.getFrame(handle) expects a local frame handle"); return; }
            info.GetReturnValue().Set(WrapLocalFrame(info.GetIsolate(), id));
        }
        void JS_CreateModelFrame(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsString()) { Args::Throw(info.GetIsolate(), "Scene.createModelFrame(filename) expects a stock .i3d filename"); return; }
            const auto name = v8pp::from_v8<std::string>(info.GetIsolate(), info[0]);
            if (!ModelNameValid(name)) { Args::Throw(info.GetIsolate(), "Model filename must be a bare .i3d filename"); return; }
            info.GetReturnValue().Set(WrapLocalFrame(info.GetIsolate(), CreateFrame(name.c_str(), Owner(info.GetIsolate()))));
        }
        void JS_CreateDummyFrame(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 0) { Args::Throw(info.GetIsolate(), "Scene.createDummyFrame() takes no arguments"); return; }
            info.GetReturnValue().Set(WrapLocalFrame(info.GetIsolate(), CreateFrame(nullptr, Owner(info.GetIsolate()))));
        }
        void JS_CreateHumanFrame(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::string model;
            glm::vec3 position, direction;
            bool solid;
            if (!ReadHumanCreate(info, model, position, direction, solid)) return;
            info.GetReturnValue().Set(WrapLocalFrame(info.GetIsolate(), CreateHumanFrame(model, position, direction, solid, Owner(info.GetIsolate()))));
        }
        struct Projection { float x = 0, y = 0; bool onScreen = false, occluded = false; };
        bool Project(const glm::vec3 &position, bool checkOcclusion, Projection &out) {
            auto *scene = Scene();
            if (!scene || !scene->ActiveCamera()) return false;
            const SDK::Player::Vector3 point {position.x, position.y, position.z};
            const bool inFront = scene->ProjectToScreen(point, out.x, out.y);
            auto *graph = SDK::Graphics::GetGraph();
            out.onScreen = inFront && graph && out.x >= 0 && out.y >= 0 && out.x < graph->ScreenWidth() && out.y < graph->ScreenHeight();
            if (out.onScreen && checkOcclusion) {
                const auto eye = scene->ActiveCamera()->ValidWorldPosition();
                out.occluded = SDK::Collision::StaticLineBlocked(eye, {point.x - eye.x, point.y - eye.y, point.z - eye.z});
            }
            return true;
        }
        void JS_ProjectWorld(const v8::FunctionCallbackInfo<v8::Value> &info) {
            glm::vec3 position;
            if (info.Length() < 1 || info.Length() > 2 || !ReadVector(info.GetIsolate(), info[0], position) ||
                (info.Length() == 2 && !info[1]->IsUndefined() && !info[1]->IsBoolean())) {
                Args::Throw(info.GetIsolate(), "Scene.projectWorld(position, checkOcclusion?) expects a position and optional boolean"); return;
            }
            Projection projection;
            if (!Project(position, info.Length() == 2 && info[1]->IsBoolean() && info[1].As<v8::Boolean>()->Value(), projection)) {
                info.GetReturnValue().SetNull(); return;
            }
            auto *isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto result = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "x"), projection.onScreen ? v8::Number::New(isolate, projection.x).As<v8::Value>() : v8::Null(isolate).As<v8::Value>()).Check();
            result->Set(context, v8pp::to_v8(isolate, "y"), projection.onScreen ? v8::Number::New(isolate, projection.y).As<v8::Value>() : v8::Null(isolate).As<v8::Value>()).Check();
            result->Set(context, v8pp::to_v8(isolate, "onScreen"), v8::Boolean::New(isolate, projection.onScreen)).Check();
            result->Set(context, v8pp::to_v8(isolate, "occluded"), v8::Boolean::New(isolate, projection.occluded)).Check();
            result->Set(context, v8pp::to_v8(isolate, "visible"), v8::Boolean::New(isolate, projection.onScreen && !projection.occluded)).Check();
            info.GetReturnValue().Set(result);
        }
        bool AddCommand(DrawCommand command, v8::Isolate *isolate) {
            if (!GetWorld().IsReady() || gDrawCommands.size() >= kMaxDrawCommands) return false;
            command.owner = Owner(isolate);
            if (command.owner.empty()) return false;
            gDrawCommands.push_back(std::move(command));
            return true;
        }
        void JS_Rect(const v8::FunctionCallbackInfo<v8::Value> &info) {
            DrawCommand command;
            command.kind = DrawKind::Rectangle;
            if (info.Length() != 5 || !ReadFloat(info[0], command.x, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[1], command.y, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[2], command.width, 0.0f, kMaxCoordinate) ||
                !ReadFloat(info[3], command.height, 0.0f, kMaxCoordinate) || !ReadColor(info[4], command.color)) {
                Args::Throw(info.GetIsolate(), "Draw.rect(x, y, width, height, argb) expects finite pixel values and 0xAARRGGBB"); return;
            }
            info.GetReturnValue().Set(AddCommand(std::move(command), info.GetIsolate()));
        }
        void JS_Line(const v8::FunctionCallbackInfo<v8::Value> &info) {
            DrawCommand command;
            command.kind = DrawKind::Line;
            if (info.Length() != 6 || !ReadFloat(info[0], command.x, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[1], command.y, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[2], command.width, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[3], command.height, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[4], command.thickness, 0.1f, 100.0f) || !ReadColor(info[5], command.color)) {
                Args::Throw(info.GetIsolate(), "Draw.line(x1, y1, x2, y2, thickness, argb) expects pixel values"); return;
            }
            info.GetReturnValue().Set(AddCommand(std::move(command), info.GetIsolate()));
        }
        void JS_Circle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            DrawCommand command;
            command.kind = DrawKind::Circle;
            if (info.Length() != 4 || !ReadFloat(info[0], command.x, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[1], command.y, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[2], command.width, 0.1f, 2048.0f) || !ReadColor(info[3], command.color)) {
                Args::Throw(info.GetIsolate(), "Draw.circle(x, y, radius, argb) expects pixel values"); return;
            }
            info.GetReturnValue().Set(AddCommand(std::move(command), info.GetIsolate()));
        }
        void JS_Text(const v8::FunctionCallbackInfo<v8::Value> &info) {
            DrawCommand command;
            command.kind = DrawKind::Text;
            if (info.Length() < 5 || info.Length() > 6 || !info[0]->IsString() ||
                !ReadFloat(info[1], command.x, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[2], command.y, -kMaxCoordinate, kMaxCoordinate)) {
                Args::Throw(info.GetIsolate(), "Draw.text(text, x, y, size, argb, font?) expects pixel values"); return;
            }
            command.text = v8pp::from_v8<std::string>(info.GetIsolate(), info[0]);
            if (command.text.size() > kMaxTextBytes || !ReadFloat(info[3], command.width, 6.0f, 128.0f) ||
                !ReadColor(info[4], command.color) || (info.Length() == 6 && !info[5]->IsUndefined() && !ReadFont(info[5], command.font))) {
                Args::Throw(info.GetIsolate(), "Draw.text requires up to 200 UTF-8 bytes, size 6..128, ARGB and stock font 0..3"); return;
            }
            info.GetReturnValue().Set(AddCommand(std::move(command), info.GetIsolate()));
        }
        void JS_WorldText(const v8::FunctionCallbackInfo<v8::Value> &info) {
            glm::vec3 position;
            if (info.Length() < 4 || info.Length() > 6 || !info[0]->IsString() || !ReadVector(info.GetIsolate(), info[1], position)) {
                Args::Throw(info.GetIsolate(), "Draw.worldText(text, position, size, argb, font?, checkOcclusion?) expects a world point"); return;
            }
            DrawCommand command;
            command.kind = DrawKind::Text;
            command.text = v8pp::from_v8<std::string>(info.GetIsolate(), info[0]);
            if (command.text.size() > kMaxTextBytes || !ReadFloat(info[2], command.width, 6.0f, 128.0f) ||
                !ReadColor(info[3], command.color) || (info.Length() >= 5 && !info[4]->IsUndefined() && !ReadFont(info[4], command.font)) ||
                (info.Length() == 6 && !info[5]->IsUndefined() && !info[5]->IsBoolean())) {
                Args::Throw(info.GetIsolate(), "Draw.worldText requires text, size, ARGB, font 0..3 and optional boolean"); return;
            }
            Projection projection;
            const bool checkOcclusion = info.Length() < 6 || info[5]->IsUndefined() || info[5].As<v8::Boolean>()->Value();
            if (!Project(position, checkOcclusion, projection) || !projection.onScreen || projection.occluded) {
                info.GetReturnValue().Set(false); return;
            }
            command.x = projection.x;
            command.y = projection.y;
            info.GetReturnValue().Set(AddCommand(std::move(command), info.GetIsolate()));
        }
        std::string NativeText(const std::string &utf8) {
            if (utf8.empty()) return {};
            const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            if (wideCount <= 0) return {};
            std::wstring wide(static_cast<size_t>(wideCount), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), wide.data(), wideCount);
            const int nativeCount = WideCharToMultiByte(CP_ACP, 0, wide.data(), wideCount, nullptr, 0, nullptr, nullptr);
            if (nativeCount <= 0) return {};
            std::string result(static_cast<size_t>(nativeCount), '\0');
            WideCharToMultiByte(CP_ACP, 0, wide.data(), wideCount, result.data(), nativeCount, nullptr, nullptr);
            std::replace_if(result.begin(), result.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x20; }, ' ');
            return result;
        }
        void DrawTriangles(const SDK::Graphics::NativeTLVertex *vertices, uint32_t count) {
            auto *graph = SDK::Graphics::GetGraph();
            graph->vtable->setState(graph, SDK::Graphics::kAlphaState, SDK::Graphics::kAlphaBlend);
            graph->vtable->setTexture(graph, nullptr);
            graph->vtable->drawPrimitiveList(graph, 3, count, const_cast<SDK::Graphics::NativeTLVertex *>(vertices), 1);
        }
        SDK::Graphics::NativeTLVertex Vertex(float x, float y, uint32_t color) {
            return {x, y, 0.0f, 1.0f, color, 0, 0.0f, 0.0f};
        }
        void DrawLine(const DrawCommand &command) {
            const float thickness = command.thickness;
            const float dx = command.width - command.x, dy = command.height - command.y;
            const float length = std::hypot(dx, dy);
            if (length < 0.001f) return;
            const float nx = -dy * thickness / (length * 2.0f), ny = dx * thickness / (length * 2.0f);
            SDK::Graphics::NativeTLVertex vertices[6] {
                Vertex(command.x + nx, command.y + ny, command.color), Vertex(command.width + nx, command.height + ny, command.color), Vertex(command.x - nx, command.y - ny, command.color),
                Vertex(command.x - nx, command.y - ny, command.color), Vertex(command.width + nx, command.height + ny, command.color), Vertex(command.width - nx, command.height - ny, command.color),
            };
            DrawTriangles(vertices, 2);
        }
        void DrawCircle(const DrawCommand &command) {
            constexpr uint32_t segments = 32;
            std::array<SDK::Graphics::NativeTLVertex, segments * 3> vertices;
            for (uint32_t index = 0; index < segments; ++index) {
                const float a = static_cast<float>(index) * 2.0f * std::numbers::pi_v<float> / segments;
                const float b = static_cast<float>(index + 1) * 2.0f * std::numbers::pi_v<float> / segments;
                vertices[index * 3] = Vertex(command.x, command.y, command.color);
                vertices[index * 3 + 1] = Vertex(command.x + std::cos(a) * command.width, command.y + std::sin(a) * command.width, command.color);
                vertices[index * 3 + 2] = Vertex(command.x + std::cos(b) * command.width, command.y + std::sin(b) * command.width, command.color);
            }
            DrawTriangles(vertices.data(), segments);
        }
    } // namespace

    void JS_FindWorldFrame(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        if (info.Length() != 1 || !info[0]->IsString()) {
            Args::Throw(isolate, "World.findWorldFrame(name) expects a frame name"); return;
        }
        const auto name = v8pp::from_v8<std::string>(isolate, info[0]);
        auto *scene = Scene();
        const auto owner = Owner(isolate);
        if (name.empty() || name.size() > kMaxWorldFrameNameLength || !scene || owner.empty() || !scene->FindFrame(name.c_str())) {
            info.GetReturnValue().SetNull(); return;
        }
        info.GetReturnValue().Set(WrapFrame(isolate, {false, 0, GetWorld().LoadedMissionGeneration(), name, owner}));
    }

    void BeginDrawFrame() { gDrawCommands.clear(); }

    void RenderDrawCommands() {
        if (!GetWorld().IsReady()) return;
        for (const auto &command : gDrawCommands) {
            switch (command.kind) {
            case DrawKind::Rectangle: SDK::Graphics::FillRect(command.x, command.y, command.width, command.height, command.color); break;
            case DrawKind::Line: DrawLine(command); break;
            case DrawKind::Circle: DrawCircle(command); break;
            case DrawKind::Text: {
                const auto native = NativeText(command.text);
                if (!native.empty()) {
                    const float width = SDK::UI::MeasureText(native.c_str(), command.width, command.font);
                    SDK::UI::DrawShadowedText(native.c_str(), command.x - width * 0.5f, command.y - command.width, width + 4.0f,
                                              command.width, command.color, 0, command.font);
                }
                break;
            }
            }
        }
    }

    void ResetLocalVisuals() {
        gDrawCommands.clear();
        for (auto &[id, entry] : gFrames) {
            ReleaseFrame(entry);
        }
        gFrames.clear();
    }

    bool IsSolidLocalHumanActor(const void *actor) {
        return std::any_of(gFrames.begin(), gFrames.end(), [actor](const auto &item) {
            return item.second.solid && item.second.actor == actor &&
                item.second.missionGeneration == GetWorld().LoadedMissionGeneration();
        });
    }

    SDK::Scene::NativeFrame *OnLocalHumanDestroyed(void *actor) {
        auto *native = static_cast<SDK::Player::NativeActor *>(actor);
        if (auto pending = gPendingHumanFrames.find(native); pending != gPendingHumanFrames.end()) {
            auto *frame = pending->second;
            gPendingHumanFrames.erase(pending);
            return frame;
        }
        for (auto it = gFrames.begin(); it != gFrames.end(); ++it) {
            if (it->second.actor == native) {
                auto *frame = it->second.frame;
                gFrames.erase(it);
                return frame;
            }
        }
        return nullptr;
    }

    void RegisterVisualScripting(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &frame = ClientCatalog().data_type("Frame", "Live handle to a native scene frame in the loaded mission. Named world frames are borrowed and read-only; local frames belong to their creating resource. Handles expire on mission unload or resource stop.");
        frame.add_property("name", "string", "Frame name at the time this handle was created.", true);
        frame.add_property("id", "number | null", "Legacy numeric ID for an owned local frame; null for a borrowed mission frame.", true);
        frame.add_property("readOnly", "boolean", "True for a named mission frame; its setters and destroy return false.", true);
        frame.add_function<void()>("isValid", docs("boolean", {}, "Checks whether the frame still resolves in this mission and resource.", "False after mission unload, resource stop, local destruction, or a removed named frame."));
        frame.add_function<void()>("getWorldPosition", docs("Vector3 | null", {}, "Reads the current world position through the native world matrix.", "Null after this handle expires."));
        frame.add_function<void()>("getWorldDirection", docs("Vector3 | null", {}, "Reads the current normalized world forward direction.", "Null after this handle expires."));
        frame.add_function<void()>("getRotation", docs("{ w: number; x: number; y: number; z: number } | null", {}, "Reads the frame's local quaternion rotation.", "Null after this handle expires."));
        frame.add_function<void()>("getScale", docs("Vector3 | null", {}, "Reads the frame's local scale.", "Null after this handle expires."));
        frame.add_function<void()>("setWorldPosition", docs("boolean", {param("position", "Vector3 | { x: number; y: number; z: number }")}, "Moves an owned local frame in world space; borrowed mission frames are read-only.", "False for a read-only or expired frame."));
        frame.add_function<void()>("setRotation", docs("boolean", {param("rotation", "{ w: number; x: number; y: number; z: number }")}, "Sets an owned local frame's local quaternion rotation.", "False for a read-only or expired frame."));
        frame.add_function<void()>("setDirection", docs("boolean", {param("direction", "Vector3 | { x: number; y: number; z: number }")}, "Points an owned local frame forward; human directions remain horizontal.", "False for a read-only or expired frame."));
        frame.add_function<void()>("setScale", docs("boolean", {param("scale", "number | Vector3 | { x: number; y: number; z: number }")}, "Sets an owned model or dummy frame's local scale. Native human actor scale is fixed.", "False for a read-only, human, or expired frame."));
        frame.add_function<void()>("setVisible", docs("boolean", {param("visible", "boolean")}, "Shows or hides an owned local frame.", "False for a read-only or expired frame."));
        frame.add_function<void()>("destroy", docs("boolean", {}, "Releases an owned local frame.", "False for a borrowed or expired frame."));

        v8pp::module scene(isolate, ClientCatalog(), "Scene", "Client-local mission frames for visual effects. Handles do not replicate and are invalid after mission unload.");
        scene.function("createModel", &JS_CreateModel, docs("number | null", {param("filename", "string")}, "Loads a stock .i3d as a non-interactive local model. Returns null if unavailable or the 128-frame cap is reached."));
        scene.function("canUseHumanModel", &JS_CanUseHumanModel, docs("boolean", {param("filename", "string", false, "Bare stock human .i3d filename.")},
            "Checks that the stock model loads and has the complete skeleton required by native human collision and animation.", "False for a missing or non-human model, or before mission load."));
        scene.function("createDummy", &JS_CreateDummy, docs("number | null", {}, "Creates a local dummy frame; it has no mesh or collision."));
        scene.function("createModelFrame", &JS_CreateModelFrame, docs("Frame | null", {param("filename", "string")}, "Loads a stock .i3d as a local model and returns a live Frame object."));
        scene.function("createDummyFrame", &JS_CreateDummyFrame, docs("Frame | null", {}, "Creates an invisible local dummy and returns a live Frame object."));
        scene.function("createHumanFrame", &JS_CreateHumanFrame, docs("Frame | null", {param("model", "string"), param("position", "Vector3 | { x: number; y: number; z: number }"), param("direction", "Vector3 | { x: number; y: number; z: number }", true), param("solid", "boolean", true, "Native collision on this client; defaults to false.")},
            "Creates a local human and returns its live Frame. Use frame.id with Scene.playHumanAnimation or Scene.setHumanIdle."));
        scene.function("getFrame", &JS_GetFrame, docs("Frame | null", {param("handle", "number")}, "Wraps an existing owned numeric local frame handle in the common Frame interface."));
        scene.function("createHuman", &JS_CreateHuman, docs("number | null", {param("model", "string"), param("position", "Vector3 | { x: number; y: number; z: number }"), param("direction", "Vector3 | { x: number; y: number; z: number }", true), param("solid", "boolean", true, "Native collision on this client; defaults to false.")},
            "Creates a stationary local C_entity with a validated stock human model. Native animation still ticks. Up to 24 local humans; no network replication or server-side interaction."));
        scene.function("playHumanAnimation", &JS_PlayHumanAnimation, docs("boolean", {param("handle", "number"), param("filename", "string"), param("loop", "boolean", true)},
            "Plays a stock .i3d clip on a local human; loop defaults to false. Returns false for a missing animation or expired handle."));
        scene.function("setHumanIdle", &JS_SetHumanIdle, docs("boolean", {param("handle", "number")}, "Stops a local human clip and restores the stock idle animation."));
        scene.function("destroy", &JS_Destroy, docs("boolean", {param("handle", "number")}, "Releases a local frame and invalidates its handle."));
        scene.function("setPosition", &JS_SetPosition, docs("boolean", {param("handle", "number"), param("position", "Vector3 | { x: number; y: number; z: number }")}, "Sets a frame's world position."));
        scene.function("setRotation", &JS_SetRotation, docs("boolean", {param("handle", "number"), param("rotation", "{ w: number; x: number; y: number; z: number }")}, "Sets a frame's full rotation as a normalized quaternion."));
        scene.function("setDirection", &JS_SetDirection, docs("boolean", {param("handle", "number"), param("direction", "Vector3 | { x: number; y: number; z: number }")}, "Points a frame forward; human directions stay horizontal."));
        scene.function("setScale", &JS_SetScale, docs("boolean", {param("handle", "number"), param("scale", "number | Vector3 | { x: number; y: number; z: number }")}, "Sets uniform or per-axis scale."));
        scene.function("setVisible", &JS_SetVisible, docs("boolean", {param("handle", "number"), param("visible", "boolean")}, "Toggles a local frame."));
        scene.function("projectWorld", &JS_ProjectWorld, docs("WorldProjection | null", {param("position", "Vector3 | { x: number; y: number; z: number }"), param("checkOcclusion", "boolean", true)}, "Projects a point into screen pixels. With occlusion enabled, static world geometry blocks visibility."));
        scene.publish(global);

        auto &projection = ClientCatalog().data_type("WorldProjection", "Projection of a world point into the current game viewport.");
        projection.add_property("x", "number | null", "Screen pixel X, or null when off screen or behind the camera.");
        projection.add_property("y", "number | null", "Screen pixel Y, or null when off screen or behind the camera.");
        projection.add_property("onScreen", "boolean", "Point is in front of the camera and inside the viewport.");
        projection.add_property("occluded", "boolean", "Static world geometry blocks the point when occlusion was requested.");
        projection.add_property("visible", "boolean", "onScreen and not occluded.");

        v8pp::module draw(isolate, ClientCatalog(), "Draw", "Per-frame native overlay primitives in screen pixels. Call from Events.on('render'). Colors are 0xAARRGGBB.");
        draw.function("rect", &JS_Rect, docs("boolean", {param("x", "number"), param("y", "number"), param("width", "number"), param("height", "number"), param("argb", "number")}, "Queues a filled rectangle."));
        draw.function("line", &JS_Line, docs("boolean", {param("x1", "number"), param("y1", "number"), param("x2", "number"), param("y2", "number"), param("thickness", "number"), param("argb", "number")}, "Queues a line."));
        draw.function("circle", &JS_Circle, docs("boolean", {param("x", "number"), param("y", "number"), param("radius", "number"), param("argb", "number")}, "Queues a filled circle."));
        draw.function("text", &JS_Text, docs("boolean", {param("text", "string"), param("x", "number"), param("y", "number"), param("size", "number"), param("argb", "number"), param("font", "number", true)}, "Queues centered native text. Font indices 0..3 are the game's built-in faces."));
        draw.function("worldText", &JS_WorldText, docs("boolean", {param("text", "string"), param("position", "Vector3 | { x: number; y: number; z: number }"), param("size", "number"), param("argb", "number"), param("font", "number", true), param("checkOcclusion", "boolean", true)}, "Projects and queues centered text only when on screen and not blocked by static world geometry."));
        draw.publish(global);
    }

    void RegisterVisualResourceCleanup(Framework::Scripting::ResourceManager &manager) {
        manager.AddOnResourceStopped(&CleanupResource);
    }
} // namespace Mafia1Online::Scripting
