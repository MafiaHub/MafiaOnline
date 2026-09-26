#include <utils/safe_win32.h>

#include "visual_scripting.h"

#include "script_runtime.h"
#include "features/world/world_service.h"
#include "shared/scripting_catalog.h"

#include <mafia1/sdk/collision/native_collision.h>
#include <mafia1/sdk/car/native_car_catalog.h>
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
#include <chrono>
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
        constexpr size_t kMaxPreviews = 8;
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
            bool animated                   = false;
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

        struct Preview {
            SDK::Scene::NativeScene *scene = nullptr;
            NativeFrame *camera = nullptr;
            NativeFrame *model = nullptr;
            NativeFrame *ambient = nullptr;
            NativeFrame *keyLight = nullptr;
            SDK::Player::Vector3 center {};
            SDK::Player::Vector3 boundsMin {};
            SDK::Player::Vector3 boundsMax {};
            float radius = 3.0f;
            bool hasBounds = false;
            float pitch = 0.0f;
            float roll = 0.0f;
            uint64_t missionGeneration = 0;
            std::string owner;
        };
        struct PreviewCommand {
            uint32_t id = 0;
            float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
            float yaw = 0.0f, zoom = 1.0f;
            std::string owner;
        };
        std::unordered_map<uint32_t, Preview> gPreviews;
        std::vector<PreviewCommand> gPreviewCommands;
        uint32_t gNextPreviewId = 1;

        void ReleasePreview(Preview &preview) {
            if (preview.scene) preview.scene->SetActiveCamera(nullptr);
            for (auto *frame : {preview.model, preview.camera, preview.ambient, preview.keyLight}) {
                if (frame) {
                    frame->LinkTo(nullptr);
                    frame->Release();
                }
            }
            if (preview.scene) reinterpret_cast<NativeFrame *>(preview.scene)->Release();
            preview = {};
        }

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
            std::erase_if(gPreviewCommands, [&owner](const PreviewCommand &command) { return command.owner == owner; });
            for (auto it = gPreviews.begin(); it != gPreviews.end();) {
                if (it->second.owner != owner) { ++it; continue; }
                ReleasePreview(it->second);
                it = gPreviews.erase(it);
            }
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
        SDK::Scene::NativeScene *Scene();
        Preview *ResolvePreview(uint32_t id, const std::string &owner) {
            auto it = gPreviews.find(id);
            return it != gPreviews.end() && it->second.owner == owner &&
                it->second.missionGeneration == GetWorld().LoadedMissionGeneration() ? &it->second : nullptr;
        }
        std::string NativeNameUtf8(const char *name) {
            if (!name) return {};
            size_t length = 0;
            while (length < 32 && name[length]) ++length;
            if (!length) return {};
            const int wideCount = MultiByteToWideChar(CP_ACP, 0, name, static_cast<int>(length), nullptr, 0);
            if (wideCount <= 0) return {};
            std::wstring wide(static_cast<size_t>(wideCount), L'\0');
            MultiByteToWideChar(CP_ACP, 0, name, static_cast<int>(length), wide.data(), wideCount);
            const int utf8Count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideCount, nullptr, 0, nullptr, nullptr);
            if (utf8Count <= 0) return {};
            std::string result(static_cast<size_t>(utf8Count), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideCount, result.data(), utf8Count, nullptr, nullptr);
            return result;
        }
        void MeasurePreview(Preview &preview) {
            // reM I3D_frame::UpdateHRBoundVol at LS3DF+0x1cc50 rebuilds the
            // loaded model's hierarchy bounds. The box is at +0xb0 and sphere
            // center/radius are at +0xc8/+0xd4.
            using UpdateBounds = void(__stdcall *)(NativeFrame *);
            reinterpret_cast<UpdateBounds>(SDK::Graphics::Ls3dfBase() + 0x1cc50)(preview.model);
            preview.center = {};
            preview.radius = 3.0f;
            preview.hasBounds = false;
            const auto *box = reinterpret_cast<const float *>(reinterpret_cast<const std::byte *>(preview.model) + 0xb0);
            const auto *center = reinterpret_cast<const float *>(reinterpret_cast<const std::byte *>(preview.model) + 0xc8);
            const float radius = *reinterpret_cast<const float *>(reinterpret_cast<const std::byte *>(preview.model) + 0xd4);
            if (std::isfinite(radius) && radius >= 0.25f && radius <= 60.0f &&
                std::isfinite(center[0]) && std::isfinite(center[1]) && std::isfinite(center[2])) {
                preview.center = {center[0], center[1], center[2]};
                preview.radius = radius;
            }
            if (std::isfinite(box[0]) && std::isfinite(box[1]) && std::isfinite(box[2]) &&
                std::isfinite(box[3]) && std::isfinite(box[4]) && std::isfinite(box[5]) &&
                box[0] <= box[3] && box[1] <= box[4] && box[2] <= box[5] &&
                box[3] - box[0] <= 120.0f && box[4] - box[1] <= 120.0f && box[5] - box[2] <= 120.0f) {
                preview.boundsMin = {box[0], box[1], box[2]};
                preview.boundsMax = {box[3], box[4], box[5]};
                preview.center = {(box[0] + box[3]) * 0.5f, (box[1] + box[4]) * 0.5f, (box[2] + box[5]) * 0.5f};
                preview.hasBounds = true;
            }
        }
        bool LoadPreviewModel(Preview &preview, const std::string &name) {
            auto *model = SDK::Scene::GetDriver()->CreateModel();
            if (!model) return false;
            if (!SDK::Scene::GetModelCache()->OpenModel(model, name.c_str())) {
                model->Release();
                return false;
            }
            if (!model->LinkTo(preview.scene->PrimarySector())) {
                model->Release();
                return false;
            }
            model->Update();
            auto *old = preview.model;
            preview.model = model;
            MeasurePreview(preview);
            if (old) {
                old->LinkTo(nullptr);
                old->Release();
            }
            return true;
        }
        uint32_t CreatePreview(const std::string &model, const std::string &owner) {
            if (!Scene() || owner.empty() || gPreviews.size() >= kMaxPreviews) return 0;
            Preview preview;
            preview.scene = SDK::Scene::GetDriver()->CreateScene();
            if (!preview.scene) return 0;
            preview.camera = SDK::Scene::GetDriver()->CreateCamera();
            preview.ambient = SDK::Scene::GetDriver()->CreateLight();
            preview.keyLight = SDK::Scene::GetDriver()->CreateLight();
            if (!preview.camera || !preview.ambient || !preview.keyLight ||
                !preview.camera->LinkTo(preview.scene->PrimarySector()) ||
                !preview.ambient->LinkTo(preview.scene->PrimarySector()) ||
                !preview.keyLight->LinkTo(preview.scene->PrimarySector()) ||
                !LoadPreviewModel(preview, model)) {
                ReleasePreview(preview);
                return 0;
            }
            // A neutral showroom, independent of mission weather and lighting.
            preview.scene->SetClearColor({0.133f, 0.165f, 0.188f});
            SDK::Scene::LightSetType(preview.ambient, 4); // I3DLIGHT_AMBIENT
            SDK::Scene::LightSetColor(preview.ambient, 0.48f, 0.49f, 0.52f);
            SDK::Scene::LightSetPower(preview.ambient, 1.0f);
            SDK::Scene::LightSetType(preview.keyLight, 3); // I3DLIGHT_DIRECTIONAL
            SDK::Scene::LightSetColor(preview.keyLight, 0.96f, 0.91f, 0.82f);
            SDK::Scene::LightSetPower(preview.keyLight, 0.85f);
            preview.keyLight->SetDirection({-0.5f, -0.7f, 0.5f}, 0.0f);
            preview.keyLight->Update();
            SDK::Scene::SectorAddLight(preview.scene->PrimarySector(), preview.ambient);
            SDK::Scene::SectorAddLight(preview.scene->PrimarySector(), preview.keyLight);
            SDK::Scene::CameraSetFov(preview.camera, std::numbers::pi_v<float> / 3.0f);
            SDK::Scene::CameraSetRange(preview.camera, 0.1f, 250.0f);
            preview.scene->SetActiveCamera(preview.camera);
            preview.owner = owner;
            preview.missionGeneration = GetWorld().LoadedMissionGeneration();
            const uint32_t id = gNextPreviewId++;
            gPreviews.emplace(id, std::move(preview));
            return id;
        }
        SDK::Player::Vector3 RotatePreviewPoint(float w, float x, float y, float z, const SDK::Player::Vector3 &point) {
            const float tx = 2.0f * (y * point.z - z * point.y);
            const float ty = 2.0f * (z * point.x - x * point.z);
            const float tz = 2.0f * (x * point.y - y * point.x);
            return {point.x + w * tx + y * tz - z * ty,
                    point.y + w * ty + z * tx - x * tz,
                    point.z + w * tz + x * ty - y * tx};
        }
        void JS_CreatePreview(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsString()) {
                Args::Throw(info.GetIsolate(), "Scene.createPreview(model) expects a bare stock .i3d filename"); return;
            }
            const auto model = v8pp::from_v8<std::string>(info.GetIsolate(), info[0]);
            if (!ModelNameValid(model)) {
                Args::Throw(info.GetIsolate(), "Preview model must be a bare stock .i3d filename"); return;
            }
            const uint32_t id = CreatePreview(model, Owner(info.GetIsolate()));
            info.GetReturnValue().Set(id ? v8::Integer::NewFromUnsigned(info.GetIsolate(), id).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }
        void JS_SetPreviewModel(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            if (info.Length() != 2 || !ReadId(info[0], id) || !info[1]->IsString()) {
                Args::Throw(info.GetIsolate(), "Scene.setPreviewModel(handle, model) expects a preview handle and stock .i3d filename"); return;
            }
            const auto model = v8pp::from_v8<std::string>(info.GetIsolate(), info[1]);
            if (!ModelNameValid(model)) {
                Args::Throw(info.GetIsolate(), "Preview model must be a bare stock .i3d filename"); return;
            }
            auto *preview = ResolvePreview(id, Owner(info.GetIsolate()));
            info.GetReturnValue().Set(preview && LoadPreviewModel(*preview, model));
        }
        void JS_DestroyPreview(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            if (info.Length() != 1 || !ReadId(info[0], id)) {
                Args::Throw(info.GetIsolate(), "Scene.destroyPreview(handle) expects a preview handle"); return;
            }
            auto it = gPreviews.find(id);
            if (it == gPreviews.end() || it->second.owner != Owner(info.GetIsolate())) {
                info.GetReturnValue().Set(false); return;
            }
            ReleasePreview(it->second);
            gPreviews.erase(it);
            info.GetReturnValue().Set(true);
        }
        void JS_SetPreviewTilt(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            float pitch, roll;
            if (info.Length() != 3 || !ReadId(info[0], id) ||
                !ReadFloat(info[1], pitch, -std::numbers::pi_v<float>, std::numbers::pi_v<float>) ||
                !ReadFloat(info[2], roll, -std::numbers::pi_v<float>, std::numbers::pi_v<float>)) {
                Args::Throw(info.GetIsolate(), "Scene.setPreviewTilt(handle, pitch, roll) expects angles in radians"); return;
            }
            auto *preview = ResolvePreview(id, Owner(info.GetIsolate()));
            if (preview) { preview->pitch = pitch; preview->roll = roll; }
            info.GetReturnValue().Set(preview != nullptr);
        }
        NativeFrame *FindPreviewFrame(Preview *preview, const std::string &name) {
            if (!preview || name.empty() || name.size() > 64) return nullptr;
            for (unsigned char ch : name) {
                if (ch < 0x20 || ch > 0x7e || ch == '*' || ch == '?' || ch == '\\' || ch == '/') return nullptr;
            }
            return preview->model->FindChildFrame(name.c_str());
        }
        void JS_GetPreviewFrame(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            if (info.Length() != 2 || !ReadId(info[0], id) || !info[1]->IsString()) {
                Args::Throw(info.GetIsolate(), "Scene.getPreviewFrame(handle, name) expects a preview handle and child frame name"); return;
            }
            const std::string name = v8pp::from_v8<std::string>(info.GetIsolate(), info[1]);
            auto *frame = FindPreviewFrame(ResolvePreview(id, Owner(info.GetIsolate())), name);
            if (!frame) { info.GetReturnValue().SetNull(); return; }
            auto *isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto object = v8::Object::New(isolate);
            const auto point = frame->WorldPosition();
            const auto scale = frame->LocalScale();
            const auto rotation = frame->LocalRotation();
            auto quaternion = v8::Object::New(isolate);
            for (size_t i = 0; i < rotation.size(); ++i) {
                constexpr const char *keys[] {"w", "x", "y", "z"};
                quaternion->Set(context, v8pp::to_v8(isolate, keys[i]), v8::Number::New(isolate, rotation[i])).Check();
            }
            object->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, name)).Check();
            object->Set(context, v8pp::to_v8(isolate, "worldPosition"), Args::Position(isolate, {point.x, point.y, point.z})).Check();
            object->Set(context, v8pp::to_v8(isolate, "rotation"), quaternion).Check();
            object->Set(context, v8pp::to_v8(isolate, "scale"), Args::Position(isolate, {scale.x, scale.y, scale.z})).Check();
            object->Set(context, v8pp::to_v8(isolate, "visible"), v8::Boolean::New(isolate, frame->IsOn())).Check();
            info.GetReturnValue().Set(object);
        }
        void JS_SetPreviewFrame(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id;
            if (info.Length() != 3 || !ReadId(info[0], id) || !info[1]->IsString() || !info[2]->IsObject()) {
                Args::Throw(info.GetIsolate(), "Scene.setPreviewFrame(handle, name, changes) expects a preview, child name and transform object"); return;
            }
            auto *isolate = info.GetIsolate();
            const auto name = v8pp::from_v8<std::string>(isolate, info[1]);
            auto *frame = FindPreviewFrame(ResolvePreview(id, Owner(isolate)), name);
            if (!frame) { info.GetReturnValue().Set(false); return; }
            auto changes = info[2].As<v8::Object>();
            auto context = isolate->GetCurrentContext();
            const auto read = [&](const char *key, v8::Local<v8::Value> &value) {
                return changes->Get(context, v8pp::to_v8(isolate, key)).ToLocal(&value);
            };
            v8::Local<v8::Value> positionValue, rotationValue, scaleValue, visibleValue;
            if (!read("worldPosition", positionValue) || !read("rotation", rotationValue) ||
                !read("scale", scaleValue) || !read("visible", visibleValue)) return;
            const bool hasPosition = !positionValue->IsUndefined();
            const bool hasRotation = !rotationValue->IsUndefined();
            const bool hasScale = !scaleValue->IsUndefined();
            const bool hasVisible = !visibleValue->IsUndefined();
            glm::vec3 position, scale;
            std::array<float, 4> rotation {};
            if ((hasPosition && !ReadVector(isolate, positionValue, position)) ||
                (hasRotation && !ReadRotation(isolate, rotationValue, rotation)) ||
                (hasScale && !scaleValue->IsNumber() && !Args::ReadPosition(isolate, scaleValue, scale)) ||
                (hasVisible && !visibleValue->IsBoolean())) {
                Args::Throw(isolate, "Preview frame changes require finite worldPosition, quaternion, positive scale and boolean visible"); return;
            }
            if (hasScale) {
                if (scaleValue->IsNumber()) {
                    float uniform;
                    if (!ReadFloat(scaleValue, uniform, 0.001f, kMaxScale)) {
                        Args::Throw(isolate, "Preview frame scale must be between 0.001 and 100"); return;
                    }
                    scale = glm::vec3(uniform);
                }
                if (scale.x < 0.001f || scale.y < 0.001f || scale.z < 0.001f ||
                    scale.x > kMaxScale || scale.y > kMaxScale || scale.z > kMaxScale) {
                    Args::Throw(isolate, "Preview frame scale axes must each be between 0.001 and 100"); return;
                }
            }
            if (hasPosition) frame->SetWorldPosition({position.x, position.y, position.z});
            if (hasRotation) frame->SetLocalRotation(rotation[0], rotation[1], rotation[2], rotation[3]);
            if (hasScale) frame->SetLocalScale({scale.x, scale.y, scale.z});
            if (hasVisible) frame->SetOn(visibleValue.As<v8::Boolean>()->Value());
            frame->Update();
            info.GetReturnValue().Set(hasPosition || hasRotation || hasScale || hasVisible);
        }
        void JS_GetCarCatalog(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 0) {
                Args::Throw(info.GetIsolate(), "Scene.getCarCatalog() takes no arguments"); return;
            }
            auto *isolate = info.GetIsolate();
            auto *catalog = SDK::Car::GetCarCatalog();
            const uint32_t count = catalog->records && catalog->count <= 256 ? catalog->count : 0;
            auto list = v8::Array::New(isolate);
            uint32_t output = 0;
            // ID 0 is the retail '-' sentinel, not a spawnable vehicle.
            for (uint32_t id = 1; id < count; ++id) {
                char model[64] {};
                if (!catalog->Model(id, model)) continue;
                const std::string filename(model);
                const std::string name = NativeNameUtf8(catalog->Name(id));
                if (!ModelNameValid(filename) || filename.starts_with("none") || name.empty()) continue;
                auto item = v8::Object::New(isolate);
                auto context = isolate->GetCurrentContext();
                item->Set(context, v8pp::to_v8(isolate, "id"), v8::Integer::NewFromUnsigned(isolate, id)).Check();
                item->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, name)).Check();
                item->Set(context, v8pp::to_v8(isolate, "model"), v8pp::to_v8(isolate, filename)).Check();
                list->Set(context, output++, item).Check();
            }
            info.GetReturnValue().Set(list);
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
        void JS_PlayModelAnimation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id = 0;
            if (info.Length() < 1 || info.Length() > 3 || !ReadId(info[0], id) || (info.Length() > 1 && !info[1]->IsNullOrUndefined() && !info[1]->IsString()) || (info.Length() > 2 && !info[2]->IsBoolean())) {
                Args::Throw(info.GetIsolate(), "Scene.playModelAnimation(handle, filename?, loop?) expects a model, optional animation filename and boolean");
                return;
            }
            const auto filename = info.Length() > 1 && info[1]->IsString() ? v8pp::from_v8<std::string>(info.GetIsolate(), info[1]) : std::string();
            if (!filename.empty() && !AnimationNameValid(filename)) {
                Args::Throw(info.GetIsolate(), "Animation must be a bare .i3d filename of at most 59 bytes");
                return;
            }
            auto *entry       = Resolve(id, Owner(info.GetIsolate()));
            const bool played = entry && !entry->actor && entry->frame->FrameType() == 9 && SDK::Scene::ModelPlayAnimation(entry->frame, filename.empty() ? nullptr : filename.c_str(), info.Length() > 2 && info[2]->BooleanValue(info.GetIsolate()));
            if (played)
                entry->animated = true;
            info.GetReturnValue().Set(played);
        }
        void JS_StopModelAnimation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t id = 0;
            if (info.Length() != 1 || !ReadId(info[0], id)) {
                Args::Throw(info.GetIsolate(), "Scene.stopModelAnimation(handle) expects a model handle");
                return;
            }
            auto *entry      = Resolve(id, Owner(info.GetIsolate()));
            const bool valid = entry && !entry->actor && entry->frame->FrameType() == 9;
            if (valid) {
                SDK::Scene::ModelStopAnimation(entry->frame);
                entry->animated = false;
            }
            info.GetReturnValue().Set(valid);
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
        void JS_DrawPreview(const v8::FunctionCallbackInfo<v8::Value> &info) {
            PreviewCommand command;
            if (info.Length() < 5 || info.Length() > 7 || !ReadId(info[0], command.id) ||
                !ReadFloat(info[1], command.x, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[2], command.y, -kMaxCoordinate, kMaxCoordinate) ||
                !ReadFloat(info[3], command.width, 16.0f, kMaxCoordinate) ||
                !ReadFloat(info[4], command.height, 16.0f, kMaxCoordinate) ||
                (info.Length() >= 6 && !info[5]->IsUndefined() && !ReadFloat(info[5], command.yaw, -100000.0f, 100000.0f)) ||
                (info.Length() == 7 && !info[6]->IsUndefined() && !ReadFloat(info[6], command.zoom, 0.5f, 2.5f))) {
                Args::Throw(info.GetIsolate(), "Draw.preview(handle, x, y, width, height, yaw?, zoom?) expects a pixel rectangle, radians and zoom 0.5..2.5"); return;
            }
            command.owner = Owner(info.GetIsolate());
            if (!GetWorld().IsReady() || !ResolvePreview(command.id, command.owner) || gPreviewCommands.size() >= kMaxPreviews) {
                info.GetReturnValue().Set(false); return;
            }
            gPreviewCommands.push_back(std::move(command));
            info.GetReturnValue().Set(true);
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

    void BeginDrawFrame() {
        gDrawCommands.clear();
        gPreviewCommands.clear();
        const auto now       = std::chrono::steady_clock::now();
        static auto previous = now;
        const int elapsed    = static_cast<int>(std::clamp<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - previous).count(), 0, 100));
        previous             = now;
        for (auto &[id, entry] : gFrames) {
            if (entry.animated)
                entry.frame->Tick(elapsed);
        }
    }

    void RenderPreviewCommands() {
        if (!GetWorld().IsReady() || gPreviewCommands.empty()) return;
        auto *graph = SDK::Graphics::GetGraph();
        const int screenWidth = graph->ScreenWidth();
        const int screenHeight = graph->ScreenHeight();
        if (screenWidth <= 0 || screenHeight <= 0) return;
        const auto savedViewport = graph->Viewport();
        for (const auto &command : gPreviewCommands) {
            auto *preview = ResolvePreview(command.id, command.owner);
            if (!preview) continue;
            const int left = std::clamp(static_cast<int>(std::lround(command.x)), 0, screenWidth);
            const int top = std::clamp(static_cast<int>(std::lround(command.y)), 0, screenHeight);
            const int right = std::clamp(static_cast<int>(std::lround(command.x + command.width)), 0, screenWidth);
            const int bottom = std::clamp(static_cast<int>(std::lround(command.y + command.height)), 0, screenHeight);
            if (right - left < 16 || bottom - top < 16) continue;
            const uint32_t width = static_cast<uint32_t>(right - left);
            const uint32_t height = static_cast<uint32_t>(bottom - top);
            const float yaw = std::remainder(command.yaw, 2.0f * std::numbers::pi_v<float>);
            const float cy = std::cos(yaw * 0.5f), sy = std::sin(yaw * 0.5f);
            const float cp = std::cos(preview->pitch * 0.5f), sp = std::sin(preview->pitch * 0.5f);
            const float cr = std::cos(preview->roll * 0.5f), sr = std::sin(preview->roll * 0.5f);
            const float qw = cy * cp * cr + sy * sp * sr;
            const float qx = cy * sp * cr + sy * cp * sr;
            const float qy = sy * cp * cr - cy * sp * sr;
            const float qz = cy * cp * sr - sy * sp * cr;
            preview->model->SetLocalRotation(qw, qx, qy, qz);
            preview->model->Update();
            const auto target = RotatePreviewPoint(qw, qx, qy, qz, preview->center);
            float distance = std::max(2.5f, preview->radius * 2.15f);
            if (preview->hasBounds) {
                // Fit all eight rotated box corners to the actual viewport.
                // The perspective bound uses each corner's own depth, so a
                // long car need not inherit the empty margins of a sphere.
                constexpr float tanHalfHorizontalFov = 0.57735026919f; // 60 degrees
                const float tanHalfVerticalFov = tanHalfHorizontalFov * static_cast<float>(height) / static_cast<float>(width);
                distance = 2.5f;
                for (int corner = 0; corner < 8; ++corner) {
                    const SDK::Player::Vector3 point {
                        corner & 1 ? preview->boundsMax.x : preview->boundsMin.x,
                        corner & 2 ? preview->boundsMax.y : preview->boundsMin.y,
                        corner & 4 ? preview->boundsMax.z : preview->boundsMin.z};
                    const auto rotated = RotatePreviewPoint(qw, qx, qy, qz, point);
                    const float dx = rotated.x - target.x;
                    const float dy = rotated.y - target.y;
                    const float dz = rotated.z - target.z;
                    distance = std::max(distance, -dz + std::fabs(dx) / (tanHalfHorizontalFov * 0.76f));
                    distance = std::max(distance, -dz + std::fabs(dy) / (tanHalfVerticalFov * 0.76f));
                }
            }
            distance = std::max(2.5f, distance / command.zoom);
            const float eyeHeight = preview->radius * 0.18f;
            preview->camera->SetWorldPosition({target.x, target.y + eyeHeight, target.z - distance});
            preview->camera->SetDirection({0.0f, -eyeHeight / distance, 1.0f}, 0.0f);
            preview->camera->Update();
            SDK::Scene::CameraSetAspectRatio(preview->camera, static_cast<float>(width) / static_cast<float>(height));
            if (!preview->scene->SetViewport(static_cast<uint32_t>(left), static_cast<uint32_t>(top), width, height)) continue;
            SDK::Graphics::NativeViewport viewport {static_cast<uint32_t>(left), static_cast<uint32_t>(top), width, height, 0.0f, 1.0f};
            if (!graph->SetViewport(viewport)) continue;
            // Clear just this viewport so world geometry never shows through
            // the transparent CEF preview stage.
            graph->Clear(0xff222a30u, 1.0f, 3u);
            preview->scene->Render();
        }
        graph->SetViewport(savedViewport);
    }

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
        gPreviewCommands.clear();
        for (auto &[id, preview] : gPreviews) ReleasePreview(preview);
        gPreviews.clear();
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
        auto &carCatalogEntry = ClientCatalog().data_type("CarCatalogEntry", "One spawnable stock car from the loaded game's highest-priority carindex.def table.");
        carCatalogEntry.add_property("id", "number", "Native base car ID accepted by the server's stock car catalog.");
        carCatalogEntry.add_property("name", "string", "Retail display name decoded to UTF-8 from the game's native code page.");
        carCatalogEntry.add_property("model", "string", "Stock color-zero .i3d filename for preview and vehicle spawning.");
        auto &previewFrame = ClientCatalog().data_type("PreviewFrame", "Snapshot of a named child in a private model preview. Look it up again after replacing the model.");
        previewFrame.add_property("name", "string", "Native child frame name.");
        previewFrame.add_property("worldPosition", "Vector3", "Current world position inside the private showroom scene.");
        previewFrame.add_property("rotation", "{ w: number; x: number; y: number; z: number }", "Current local quaternion.");
        previewFrame.add_property("scale", "Vector3", "Current local scale.");
        previewFrame.add_property("visible", "boolean", "Native frame visibility flag.");
        scene.function("getCarCatalog", &JS_GetCarCatalog, docs("CarCatalogEntry[]", {}, "Returns valid stock car IDs, display names and color-zero model filenames from the live native car database. Empty before initialization."));
        scene.function("createPreview", &JS_CreatePreview, docs("number | null", {param("model", "string")}, "Loads a stock .i3d into a private client-only showroom scene for 2D rendering. Returns null if unavailable or eight previews already exist."));
        scene.function("setPreviewModel", &JS_SetPreviewModel, docs("boolean", {param("handle", "number"), param("model", "string")}, "Changes a preview's stock model; the old model stays if loading fails."));
        scene.function("setPreviewTilt", &JS_SetPreviewTilt, docs("boolean", {param("handle", "number"), param("pitch", "number"), param("roll", "number")}, "Sets preview pitch and roll in radians. Draw.preview supplies yaw and zoom per frame."));
        scene.function("getPreviewFrame", &JS_GetPreviewFrame, docs("PreviewFrame | null", {param("handle", "number"), param("name", "string")}, "Returns a snapshot of a named child frame in the preview model, or null if unavailable."));
        scene.function("setPreviewFrame", &JS_SetPreviewFrame, docs("boolean", {param("handle", "number"), param("name", "string"), param("changes", "{ worldPosition?: Vector3 | { x: number; y: number; z: number }; rotation?: { w: number; x: number; y: number; z: number }; scale?: number | Vector3 | { x: number; y: number; z: number }; visible?: boolean }")}, "Changes a named child frame in the private preview scene. It never edits mission or replicated frames; invalid names or expired previews return false."));
        scene.function("destroyPreview", &JS_DestroyPreview, docs("boolean", {param("handle", "number")}, "Releases a private showroom scene and its native frames."));
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
        scene.function("playModelAnimation", &JS_PlayModelAnimation,
            docs("boolean", {param("handle", "number"), param("filename", "string | null", true), param("loop", "boolean", true)},
                "Plays a local model animation. Omit filename or pass null to restart its embedded animation (including sipka.i3d). Loop defaults to false; owned models only."));
        scene.function("stopModelAnimation", &JS_StopModelAnimation, docs("boolean", {param("handle", "number")}, "Pauses the owned model's animation at its current pose."));
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
        draw.function("preview", &JS_DrawPreview, docs("boolean", {param("handle", "number"), param("x", "number"), param("y", "number"), param("width", "number"), param("height", "number"), param("yaw", "number", true, "Radians; 0 faces the model's native forward orientation."), param("zoom", "number", true, "0.5 to 2.5; 1 is stock framing, larger is closer.")}, "Queues a stock 3D model for a screen-pixel rectangle. Draws after the game scene and before CEF, clearing the viewport to a dark background. Call each Events.render tick."));
        draw.publish(global);
    }

    void RegisterVisualResourceCleanup(Framework::Scripting::ResourceManager &manager) {
        manager.AddOnResourceStopped(&CleanupResource);
    }
} // namespace Mafia1Online::Scripting
