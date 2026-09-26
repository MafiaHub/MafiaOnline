#include "script_runtime.h"

#include "hud_scripting.h"
#include "door_scripting.h"
#include "pickup_scripting.h"
#include "player_scripting.h"
#include "world_scripting.h"
#include "visual_scripting.h"

#include "core/application.h"
#include "shared/scripting_catalog.h"

#include <core_modules.h>
#include <integrations/client/scripting/module.h>
#include <logging/logger.h>
#include <scripting/builtins/entity.h>
#include <scripting/builtins/vector3.h>
#include <scripting/engine.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/scripting_catalog.h>

#include <v8pp/convert.hpp>
#include <v8pp/metadata.hpp>

#include <cmath>

namespace Mafia1Online::Scripting {
    void EmitEvent(const char *name, const std::function<void(v8::Isolate *, v8::Local<v8::Context>, EventArguments &)> &build) {
        auto *module = static_cast<Framework::Integrations::Client::Scripting::ClientScriptingModule *>(Framework::CoreModules::GetScriptingModule());
        if (!module) {
            return;
        }
        auto *engine          = module->GetEngine();
        auto *resourceManager = module->GetResourceManager();
        if (!engine || !resourceManager || !engine->IsInitialized()) {
            return;
        }
        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);
        EventArguments args;
        if (build) {
            build(isolate, context, args);
        }
        resourceManager->GetEvents().EmitReserved(isolate, context, name, args);
    }

    namespace Args {
        bool ReadFloat(v8::Local<v8::Value> value, float &out) {
            if (!value->IsNumber()) {
                return false;
            }
            const double read = value.As<v8::Number>()->Value();
            if (!std::isfinite(read)) {
                return false;
            }
            out = static_cast<float>(read);
            return true;
        }

        bool ReadPosition(v8::Isolate *isolate, v8::Local<v8::Value> value, glm::vec3 &out) {
            if (auto *vector = v8pp::class_<Framework::Scripting::Builtins::Vector3>::unwrap_object(isolate, value)) {
                out = {vector->getX(), vector->getY(), vector->getZ()};
                return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
            }
            if (!value->IsObject()) {
                return false;
            }
            auto context = isolate->GetCurrentContext();
            auto object  = value.As<v8::Object>();
            float *axes[] {&out.x, &out.y, &out.z};
            const char *names[] {"x", "y", "z"};
            for (int i = 0; i < 3; ++i) {
                v8::Local<v8::Value> axis;
                if (!object->Get(context, v8pp::to_v8(isolate, names[i])).ToLocal(&axis) || !ReadFloat(axis, *axes[i])) {
                    return false;
                }
            }
            return true;
        }

        bool OptionalFloat(v8::Isolate *isolate, v8::Local<v8::Value> options, const char *name, float minimum, float maximum, float &out) {
            if (options.IsEmpty() || options->IsUndefined() || options->IsNull()) {
                return true;
            }
            if (!options->IsObject()) {
                return false;
            }
            v8::Local<v8::Value> value;
            if (!options.As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, name)).ToLocal(&value) || value->IsUndefined()) {
                return true;
            }
            float read = 0.0f;
            if (!ReadFloat(value, read)) {
                return false;
            }
            out = std::fmin(std::fmax(read, minimum), maximum);
            return true;
        }

        void Throw(v8::Isolate *isolate, const char *message) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, message)));
        }

        v8::Local<v8::Value> Position(v8::Isolate *isolate, const glm::vec3 &position) {
            auto &cls = Framework::Scripting::Builtins::Vector3::GetClass(isolate);
            return cls.import_external(isolate, new Framework::Scripting::Builtins::Vector3(position.x, position.y, position.z));
        }
    } // namespace Args

    void RegisterClientScripting(Framework::Scripting::Engine *engine) {
        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);
        auto global = context->Global();

        RegisterClientEventMetadata();
        Framework::Scripting::Builtins::Entity::Register(isolate, global);
        Player::Register(isolate, global);
        Vehicle::Register(isolate, global);
        Pickup::Register(isolate, global);
        Door::Register(isolate, global);
        RegisterLocalPlayer(isolate, global);
        ClientWorld::Register(isolate, global);
        ClientHud::Register(isolate, global);
        RegisterVisualScripting(isolate, global);
#ifndef NDEBUG
        auto &catalog = ClientCatalog();
        Framework::Scripting::MergeScriptingCatalog(catalog, v8pp::metadata::catalog("framework-client"));
        const auto output = MetadataOutputPath("client");
        if (!output.empty() && !v8pp::metadata::write_json_file(catalog, output.string())) {
            Framework::Logging::GetLogger("Scripting")->error("Failed to export Mafia1Online client scripting API metadata");
        }
#endif
    }

    Features::World::WorldService &GetWorld() {
        return static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance())->World();
    }
} // namespace Mafia1Online::Scripting
