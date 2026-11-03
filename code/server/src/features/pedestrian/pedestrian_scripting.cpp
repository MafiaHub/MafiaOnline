#include "pedestrian_scripting.h"

#include "core/server.h"
#include "features/script/script_runtime.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>

#include <cmath>
#include <sstream>

namespace Mafia1Online::Scripting {
    std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Pedestrian>>> Pedestrian::_classes;

    namespace {
        using Settings = Features::Pedestrian::PedestrianService::Settings;

        void List(const v8::FunctionCallbackInfo<v8::Value> &info, const std::function<bool(const Shared::Entities::PedestrianEntity &)> &include) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            GetServer().Pedestrians().ForEach([&](Shared::Entities::PedestrianEntity &walker) {
                if (include(walker)) {
                    list->Set(context, index++, WrapPedestrian(isolate, walker.GetNetworkID())).Check();
                }
            });
            info.GetReturnValue().Set(list);
        }

        void JS_PedestrianGetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            List(info, [](const Shared::Entities::PedestrianEntity &) { return true; });
        }

        // Pedestrian.getInRange(position, radius)
        void JS_PedestrianGetInRange(const v8::FunctionCallbackInfo<v8::Value> &info) {
            glm::vec3 center;
            float radius = 0.0f;
            int next = 0;
            if (!ScriptArgs::ReadPosition(info, 0, center, next) || info.Length() != next + 1 || !ScriptArgs::ReadFloat(info[next], radius) || radius < 0.0f) {
                ScriptArgs::Throw(info.GetIsolate(), "Pedestrian.getInRange(position, radius) expects a finite position and a radius of 0 or more");
                return;
            }
            const float radiusSq = radius * radius;
            List(info, [&](const Shared::Entities::PedestrianEntity &walker) {
                const auto offset = walker.position - center;
                return offset.x * offset.x + offset.y * offset.y + offset.z * offset.z <= radiusSq;
            });
        }

        void JS_PedestrianGetCount(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<uint32_t>(GetServer().Pedestrians().Count()));
        }

        void JS_PedestrianSetEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsBoolean()) {
                ScriptArgs::Throw(info.GetIsolate(), "Pedestrian.setEnabled(enabled) expects a boolean");
                return;
            }
            auto settings = GetServer().Pedestrians().GetSettings();
            settings.enabled = info[0]->BooleanValue(info.GetIsolate());
            info.GetReturnValue().Set(GetServer().Pedestrians().SetSettings(settings));
        }

        void JS_PedestrianIsEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(GetServer().Pedestrians().GetSettings().enabled);
        }

        void JS_PedestrianSetDensity(const v8::FunctionCallbackInfo<v8::Value> &info) {
            float density = 0.0f;
            if (info.Length() != 1 || !ScriptArgs::ReadFloat(info[0], density) || density < 0.0f || density > 1.0f) {
                ScriptArgs::Throw(info.GetIsolate(), "Pedestrian.setDensity(density) expects a number from 0 to 1");
                return;
            }
            auto settings = GetServer().Pedestrians().GetSettings();
            settings.density = density;
            info.GetReturnValue().Set(GetServer().Pedestrians().SetSettings(settings));
        }

        void JS_PedestrianGetDensity(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<double>(GetServer().Pedestrians().GetSettings().density));
        }

        void JS_PedestrianSetCorpseLifetime(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t lifetime = 0;
            if (info.Length() != 1 || !ScriptArgs::ReadUInt(info[0], Features::Pedestrian::PedestrianService::kMaxCorpseLifetimeMs, lifetime)) {
                ScriptArgs::Throw(info.GetIsolate(), "Pedestrian.setCorpseLifetime(milliseconds) expects 0 to 3600000");
                return;
            }
            auto settings = GetServer().Pedestrians().GetSettings();
            settings.corpseLifetimeMs = lifetime;
            info.GetReturnValue().Set(GetServer().Pedestrians().SetSettings(settings));
        }

        void JS_PedestrianGetCorpseLifetime(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(GetServer().Pedestrians().GetSettings().corpseLifetimeMs);
        }
    } // namespace

    Shared::Entities::PedestrianEntity *Pedestrian::ResolvePedestrian() const {
        return GetServer().Pedestrians().Find(_id);
    }

    std::string Pedestrian::GetModel() const {
        const auto *walker = ResolvePedestrian();
        return walker ? walker->model : std::string {};
    }

    Framework::Scripting::Builtins::Vector3 Pedestrian::GetPedestrianPosition() const {
        const auto *walker = ResolvePedestrian();
        return walker ? Framework::Scripting::Builtins::Vector3(walker->position) : Framework::Scripting::Builtins::Vector3();
    }

    double Pedestrian::GetHeading() const {
        const auto *walker = ResolvePedestrian();
        // The host reports a rotation about the Y axis only.
        return walker ? 2.0 * std::atan2(walker->rotation.y, walker->rotation.w) : 0.0;
    }

    bool Pedestrian::GetAlive() const {
        const auto *walker = ResolvePedestrian();
        return walker && walker->alive;
    }

    bool Pedestrian::GetUmbrella() const {
        const auto *walker = ResolvePedestrian();
        return walker && walker->umbrella;
    }

    int32_t Pedestrian::GetAnimation() const {
        const auto *walker = ResolvePedestrian();
        return walker ? walker->animation : 0;
    }

    uint32_t Pedestrian::GetWalkStyle() const {
        const auto *walker = ResolvePedestrian();
        return walker ? walker->walkStyle : 0;
    }

    bool Pedestrian::Remove() {
        return GetServer().Pedestrians().Remove(_id);
    }

    std::string Pedestrian::ToString() const {
        std::ostringstream stream;
        stream << "Pedestrian{ id: " << _id << ", model: " << GetModel() << " }";
        return stream.str();
    }

    void Pedestrian::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    v8pp::class_<Pedestrian> &Pedestrian::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (_class) {
            return *_class;
        }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Pedestrian>>(isolate, ServerCatalog(), "Pedestrian",
            "A walker of the city's pedestrian traffic. The simulation host runs it with the retail AI around every player; it is a new pedestrian each time it walks again.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing pedestrian."));
        cls.function("toString", &Pedestrian::ToString, docs("string", {}, "Formats this pedestrian for logging.", "The pedestrian ID and model."));
        cls.property("model", &Pedestrian::GetModel, property_docs("string", "Retail .i3d model filename."));
        cls.property("position", &Pedestrian::GetPedestrianPosition, property_docs("Vector3", "Position the simulation host last reported."));
        cls.property("heading", &Pedestrian::GetHeading, property_docs("number", "Facing in radians."));
        cls.property("alive", &Pedestrian::GetAlive, property_docs("boolean", "False once it died; the corpse stays until it retires."));
        cls.property("umbrella", &Pedestrian::GetUmbrella, property_docs("boolean", "Whether it holds a rain umbrella; a hit drops it."));
        cls.property("animation", &Pedestrian::GetAnimation, property_docs("number", "Retail traffic animation id; 6 is its walk."));
        cls.property("walkStyle", &Pedestrian::GetWalkStyle, property_docs("number", "Retail walk style, 0 to 4."));
        cls.function("remove", &Pedestrian::Remove, docs("boolean", {}, "Asks the simulation host to retire it now; pedestrianRetire follows.", "False when it no longer exists or no host is attached."));
        return cls;
    }

    void Pedestrian::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        auto &cls = GetClass(isolate);
        cls.static_function("getAll", &JS_PedestrianGetAll, docs("Pedestrian[]", {}, "Lists every pedestrian.", "The current pedestrians."));
        cls.static_function("getInRange", &JS_PedestrianGetInRange,
            docs("Pedestrian[]", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Center; three numbers are accepted too."), param("radius", "number", false, "Distance in units.")},
                "Lists the pedestrians within radius of position.", "The pedestrians in range."));
        cls.static_function("getCount", &JS_PedestrianGetCount, docs("number", {}, "Counts the pedestrians.", "The number of pedestrians."));
        cls.static_function("setEnabled", &JS_PedestrianSetEnabled,
            docs("boolean", {param("enabled", "boolean")}, "Turns pedestrian traffic on or off; off retires every pedestrian at once.", "False when the value was rejected."));
        cls.static_function("isEnabled", &JS_PedestrianIsEnabled, docs("boolean", {}, "Whether pedestrian traffic runs.", "The current setting."));
        cls.static_function("setDensity", &JS_PedestrianSetDensity,
            docs("boolean", {param("density", "number", false, "0 to 1 of the retail count around each group of players.")}, "Scales how many pedestrians walk around players; existing ones retire naturally.", "False when the value was rejected."));
        cls.static_function("getDensity", &JS_PedestrianGetDensity, docs("number", {}, "The pedestrian density.", "0 to 1."));
        cls.static_function("setCorpseLifetime", &JS_PedestrianSetCorpseLifetime,
            docs("boolean", {param("milliseconds", "number", false, "0 to 3600000; 0 keeps a corpse until every player is far from it, as in single player.")},
                "Sets how long a dead pedestrian stays. Defaults to 30000.", "False when the value was rejected."));
        cls.static_function("getCorpseLifetime", &JS_PedestrianGetCorpseLifetime, docs("number", {}, "How long a dead pedestrian stays, in milliseconds.", "The current setting."));
        auto context = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "Pedestrian"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }
} // namespace Mafia1Online::Scripting
