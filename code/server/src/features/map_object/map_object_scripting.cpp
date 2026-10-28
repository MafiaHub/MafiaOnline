#include "map_object_scripting.h"

#include "core/server.h"
#include "features/script/script_runtime.h"
#include "shared/scripting_catalog.h"

#include <scripting/builtins/quaternion.h>
#include <scripting/builtins/vector3.h>
#include <scripting/engine.h>

#include <v8pp/convert.hpp>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <optional>
#include <sstream>

namespace Mafia1Online::Scripting {
    std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<MapObject>>> MapObject::_classes;

    namespace {
        using Shared::Entities::MapObjectEntity;
        using Service = Features::MapObject::MapObjectService;

        Service &Objects() {
            return GetServer().MapObjects();
        }

        MapObject *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<MapObject>::unwrap_object(info.GetIsolate(), info.This());
        }

        bool ReadNumber(v8::Isolate *isolate, v8::Local<v8::Object> object, const char *key, float &out) {
            v8::Local<v8::Value> value;
            return object->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, key)).ToLocal(&value) && ScriptArgs::ReadFloat(value, out);
        }

        // A Quaternion (any object with finite w, x, y, z) or Euler angles
        // in radians as { x, y, z }, applied as glm::quat(euler).
        bool ReadRotation(v8::Isolate *isolate, v8::Local<v8::Value> value, glm::quat &out) {
            if (value.IsEmpty() || !value->IsObject()) {
                return false;
            }
            auto object = value.As<v8::Object>();
            glm::vec3 axes;
            if (!ScriptArgs::ReadVector(isolate, value, axes)) {
                return false;
            }
            v8::Local<v8::Value> w;
            if (!object->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "w")).ToLocal(&w)) {
                return false;
            }
            if (w->IsUndefined()) {
                out = glm::quat(axes);
                return true;
            }
            float scalar = 0.0f;
            if (!ScriptArgs::ReadFloat(w, scalar)) {
                return false;
            }
            const glm::quat rotation(scalar, axes.x, axes.y, axes.z);
            const float length = glm::length(rotation);
            if (!std::isfinite(length) || length <= 0.0001f) {
                return false;
            }
            out = rotation / length;
            return true;
        }

        // -1 is an alias of the all-worlds id, 4294967295.
        bool ReadWorld(v8::Local<v8::Value> value, uint32_t &out) {
            if (!value->IsNumber()) {
                return false;
            }
            const double number = value.As<v8::Number>()->Value();
            if (number == -1.0) {
                out = 0xFFFFFFFFu;
                return true;
            }
            if (!(number >= 0.0) || number > 4294967295.0 || std::floor(number) != number) {
                return false;
            }
            out = static_cast<uint32_t>(number);
            return true;
        }

        bool ReadOptions(v8::Isolate *isolate, v8::Local<v8::Value> value, Service::Options &options) {
            if (value->IsUndefined() || value->IsNull()) {
                return true;
            }
            if (!value->IsObject()) {
                return false;
            }
            auto object  = value.As<v8::Object>();
            auto context = isolate->GetCurrentContext();
            const auto get = [&](const char *key, v8::Local<v8::Value> &out) {
                return object->Get(context, v8pp::to_v8(isolate, key)).ToLocal(&out);
            };
            v8::Local<v8::Value> world, collision, scale, material;
            if (!get("virtualWorld", world) || !get("collision", collision) || !get("scale", scale) || !get("material", material)) {
                return false;
            }
            if (!world->IsUndefined() && !ReadWorld(world, options.virtualWorld)) {
                return false;
            }
            if (!collision->IsUndefined()) {
                if (!collision->IsBoolean()) {
                    return false;
                }
                options.collision = collision->BooleanValue(isolate);
            }
            if (!scale->IsUndefined()) {
                float uniform = 0.0f;
                if (ScriptArgs::ReadFloat(scale, uniform)) {
                    options.scale = glm::vec3(uniform);
                }
                else if (!ScriptArgs::ReadVector(isolate, scale, options.scale)) {
                    return false;
                }
            }
            if (!material->IsUndefined()) {
                uint32_t id = 0;
                if (!ScriptArgs::ReadUInt(material, 255, id) || !Shared::MapObject::ValidMaterial(id)) {
                    return false;
                }
                options.material = static_cast<uint8_t>(id);
            }
            return true;
        }

        std::string Owner(v8::Isolate *isolate) {
            auto *module = GetServer().GetScriptingModule();
            return module && module->GetEngine() ? module->GetEngine()->GetResourceForIsolate(isolate) : std::string();
        }

        // MapObject.create(model, position, rotation?, options?)
        void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            glm::vec3 position;
            glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            Service::Options options;
            int next = 0;
            if (info.Length() < 2 || !info[0]->IsString() || !ScriptArgs::ReadPosition(info, 1, position, next) || info.Length() > next + 2 ||
                (info.Length() > next && !info[next]->IsNullOrUndefined() && !ReadRotation(isolate, info[next], rotation)) ||
                (info.Length() > next + 1 && !ReadOptions(isolate, info[next + 1], options))) {
                ScriptArgs::Throw(isolate, "MapObject.create(model, position, rotation?, options?) expects a model file, a finite position, a quaternion or Euler radians { x, y, z }, "
                                           "and options { virtualWorld?, collision?, scale?, material? }");
                return;
            }
            const auto model = v8pp::from_v8<std::string>(isolate, info[0]);
            if (!Shared::MapObject::ValidModelName(model)) {
                ScriptArgs::Throw(isolate, "MapObject.create: the model must be a bare .4ds or .i3d file name under models\\ of at most 63 characters");
                return;
            }
            auto &server = GetServer();
            auto *object = Objects().Create(model, position, rotation, options, server.MissionGeneration(), Owner(isolate));
            info.GetReturnValue().Set(WrapMapObject(isolate, object ? object->GetNetworkID() : 0));
        }

        void JS_GetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            for (uint64_t id : Objects().List()) {
                list->Set(context, index++, WrapMapObject(isolate, id)).Check();
            }
            info.GetReturnValue().Set(list);
        }

        void JS_GetPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self   = Self(info);
            auto *object = self ? self->ResolveObject() : nullptr;
            const glm::vec3 position = object ? object->position : glm::vec3(0.0f);
            auto &cls = Framework::Scripting::Builtins::Vector3::GetClass(info.GetIsolate());
            info.GetReturnValue().Set(cls.import_external(info.GetIsolate(), new Framework::Scripting::Builtins::Vector3(position)));
        }

        void JS_SetPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            glm::vec3 position;
            if (!self || info.Length() < 1 || !ScriptArgs::ReadVector(info.GetIsolate(), info[0], position)) {
                ScriptArgs::Throw(info.GetIsolate(), "MapObject.position must be a finite Vector3 or { x, y, z }");
                return;
            }
            Objects().SetPosition(self->GetId(), position);
        }

        void JS_GetRotation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self   = Self(info);
            auto *object = self ? self->ResolveObject() : nullptr;
            const glm::quat rotation = object ? object->rotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            auto &cls = Framework::Scripting::Builtins::Quaternion::GetClass(info.GetIsolate());
            info.GetReturnValue().Set(cls.import_external(info.GetIsolate(), new Framework::Scripting::Builtins::Quaternion(rotation)));
        }

        void JS_SetRotation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            glm::quat rotation;
            if (!self || info.Length() < 1 || !ReadRotation(info.GetIsolate(), info[0], rotation)) {
                ScriptArgs::Throw(info.GetIsolate(), "MapObject.rotation must be a Quaternion or Euler radians { x, y, z }");
                return;
            }
            Objects().SetRotation(self->GetId(), rotation);
        }

        void JS_GetWorld(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self   = Self(info);
            auto *object = self ? self->ResolveObject() : nullptr;
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), object ? static_cast<double>(object->GetVirtualWorld()) : 0.0));
        }

        void JS_SetWorld(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            uint32_t world = 0;
            if (!self || info.Length() < 1 || !ReadWorld(info[0], world)) {
                ScriptArgs::Throw(info.GetIsolate(), "MapObject.virtualWorld must be an integer from 0 to 4294967295, or -1 for every world");
                return;
            }
            Objects().SetVirtualWorld(self->GetId(), world);
        }

        // moveTo(position, speed, rotation?)
        void JS_MoveTo(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = Self(info);
            glm::vec3 position;
            float speed = 0.0f;
            std::optional<glm::quat> rotation;
            int next = 0;
            if (!self || !ScriptArgs::ReadPosition(info, 0, position, next) || info.Length() < next + 1 || info.Length() > next + 2 || !ScriptArgs::ReadFloat(info[next], speed) ||
                (info.Length() == next + 2 && !info[next + 1]->IsNullOrUndefined() && !ReadRotation(isolate, info[next + 1], rotation.emplace()))) {
                ScriptArgs::Throw(isolate, "MapObject.moveTo(position, speed, rotation?) expects a finite position, a speed in m/s and an optional quaternion or Euler radians");
                return;
            }
            info.GetReturnValue().Set(Objects().MoveTo(self->GetId(), position, speed, rotation));
        }
    } // namespace

    v8::Local<v8::Value> WrapMapObject(v8::Isolate *isolate, uint64_t id) {
        if (!id || !GetServer().MapObjects().Find(id)) {
            return v8::Null(isolate);
        }
        MapObject::GetClass(isolate);
        return v8pp::class_<MapObject>::create_object(isolate, id);
    }

    MapObjectEntity *MapObject::ResolveObject() const {
        return Objects().Find(_id);
    }

    std::string MapObject::GetModel() const {
        const auto *object = ResolveObject();
        return object ? object->model : std::string();
    }

    bool MapObject::GetCollision() const {
        const auto *object = ResolveObject();
        return object && object->collision;
    }

    bool MapObject::GetMoving() const {
        return Objects().IsMoving(_id);
    }

    double MapObject::GetMissionGeneration() const {
        const auto *object = ResolveObject();
        return object ? static_cast<double>(object->missionGeneration) : 0.0;
    }

    bool MapObject::Stop() {
        return Objects().Stop(_id);
    }

    bool MapObject::Destroy() {
        return Objects().Destroy(_id);
    }

    std::string MapObject::ToString() const {
        std::ostringstream stream;
        stream << "MapObject{ id: " << _id << ", model: " << GetModel() << " }";
        return stream.str();
    }

    void MapObject::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    v8pp::class_<MapObject> &MapObject::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (_class) {
            return *_class;
        }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<MapObject>>(isolate, ServerCatalog(), "MapObject",
            "A script-placed world object built from a stock model, like SA-MP's CreateObject. Every client in its virtual world sees it and collides with it on foot, "
            "in cars and with bullets. It lasts until destroyed, until the mission changes or until the resource that created it stops.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing map object; use MapObject.create to place one."));
        cls.function("toString", &MapObject::ToString, docs("string", {}, "Formats this object for logging.", "The object ID and model."));
        cls.property("model", &MapObject::GetModel, property_docs("string", "Model file the object was created from."));
        cls.property("collision", &MapObject::GetCollision, property_docs("boolean", "Whether players, cars and bullets collide with the object."));
        cls.property("moving", &MapObject::GetMoving, property_docs("boolean", "Whether a moveTo is running."));
        cls.property("missionGeneration", &MapObject::GetMissionGeneration, property_docs("number", "Mission generation the object belongs to."));
        cls.accessor_property("position", v8::FunctionTemplate::New(isolate, &JS_GetPosition), v8::FunctionTemplate::New(isolate, &JS_SetPosition),
            property_docs("Vector3", "Current position, also while moving. Assigning teleports the object and stops a running move without mapObjectMoved."));
        cls.accessor_property("rotation", v8::FunctionTemplate::New(isolate, &JS_GetRotation), v8::FunctionTemplate::New(isolate, &JS_SetRotation),
            property_docs("Quaternion", "Current rotation. Assigning a quaternion or Euler radians { x, y, z } turns the object at once and stops a running move."));
        cls.accessor_property("virtualWorld", v8::FunctionTemplate::New(isolate, &JS_GetWorld), v8::FunctionTemplate::New(isolate, &JS_SetWorld),
            property_docs("number", "Virtual world; only players in it see and collide with the object. 4294967295 (assign -1 for it) is every world, the default."));
        cls.prototype_function("moveTo", &JS_MoveTo,
            docs("boolean", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Where to go; three numbers are accepted too."),
                                param("speed", "number", false, "Meters per second, up to 1000. A move that only turns uses radians per second."),
                                param("rotation", "Quaternion | { x: number; y: number; z: number }", true, "Rotation to reach on arrival, turned evenly on the way; keeps the current one when omitted.")},
                "Moves the object smoothly from where it is. Fires mapObjectMoved on arrival. Replaces a running move.", "False for an invalid speed or a destroyed object."));
        cls.function("stop", &MapObject::Stop, docs("boolean", {}, "Stops a running move where the object is now. Does not fire mapObjectMoved.", "False when the object no longer exists."));
        cls.function("destroy", &MapObject::Destroy, docs("boolean", {}, "Removes the object and its collision from every client.", "False when it no longer exists."));
        return cls;
    }

    void MapObject::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        auto &cls = GetClass(isolate);
        cls.static_function("create", &JS_Create,
            docs("MapObject | null",
                {param("model", "string", false, "Stock model file under models\\, such as \"bar_fence.4ds\"; .i3d is accepted too."),
                    param("position", "Vector3 | { x: number; y: number; z: number }", false, "World position; three numbers are accepted too."),
                    param("rotation", "Quaternion | { x: number; y: number; z: number }", true, "A quaternion, or Euler angles in radians about x, y (up) and z. Defaults to none."),
                    param("options", "{ virtualWorld?: number; collision?: boolean; scale?: number | Vector3 | { x: number; y: number; z: number }; material?: number }", true,
                        "virtualWorld defaults to every world (-1). collision defaults to true. scale is 0.01 to 100 per axis. material is a tables\\materials.def id "
                        "(0 to 46 except glass 24 and crash frames 41) for collision built from the model's mesh; by default the mission's own collision for the model is used where it "
                        "has some, and concrete (4) otherwise.")},
                "Places an object in the current mission for every client, including late joiners.", "The object, or null at the limit of 4096 objects."));
        cls.static_function("getAll", &JS_GetAll, docs("MapObject[]", {}, "Lists every map object.", "The current map objects."));
        auto context = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "MapObject"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }
} // namespace Mafia1Online::Scripting
