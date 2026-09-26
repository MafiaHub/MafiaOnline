#include "script_runtime.h"

#include "core/server.h"
#include "features/car/vehicle_scripting.h"
#include "features/pickup/pickup_scripting.h"
#include "features/player/player_scripting.h"

#include <integrations/server/scripting/module.h>
#include <scripting/node_engine.h>

#include <v8pp/convert.hpp>

#include <cmath>
#include <limits>

namespace Mafia1Online::Scripting {
    namespace {
        Core::Server *gServer = nullptr;

        bool ReadComponent(v8::Isolate *isolate, v8::Local<v8::Object> object, const char *name, float &out) {
            v8::Local<v8::Value> value;
            return object->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, name)).ToLocal(&value) && ScriptArgs::ReadFloat(value, out);
        }
    } // namespace

    void SetServer(Core::Server *server) {
        gServer = server;
    }

    Core::Server &GetServer() {
        return *gServer;
    }

    uint64_t ScriptArgs::ReadEntityId(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (value.IsEmpty()) {
            return 0;
        }
        if (value->IsBigInt()) {
            bool exact        = false;
            const uint64_t id = value.As<v8::BigInt>()->Uint64Value(&exact);
            return exact ? id : 0;
        }
        if (value->IsNumber()) {
            const double number = value.As<v8::Number>()->Value();
            return std::isfinite(number) && number >= 1.0 && number <= 9007199254740991.0 && number == std::floor(number) ? static_cast<uint64_t>(number) : 0;
        }
        if (value->IsObject()) {
            v8::Local<v8::Value> id;
            if (value.As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "id")).ToLocal(&id) && (id->IsNumber() || id->IsBigInt())) {
                return ReadEntityId(isolate, id);
            }
        }
        return 0;
    }

    bool ScriptArgs::ReadFloat(v8::Local<v8::Value> value, float &out) {
        if (value.IsEmpty() || !value->IsNumber()) {
            return false;
        }
        const double number = value.As<v8::Number>()->Value();
        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) {
            return false;
        }
        out = static_cast<float>(number);
        return true;
    }

    bool ScriptArgs::ReadUInt(v8::Local<v8::Value> value, uint32_t max, uint32_t &out) {
        if (value.IsEmpty() || !value->IsNumber()) {
            return false;
        }
        const double number = value.As<v8::Number>()->Value();
        if (!std::isfinite(number) || number < 0.0 || number > static_cast<double>(max) || number != std::floor(number)) {
            return false;
        }
        out = static_cast<uint32_t>(number);
        return true;
    }

    bool ScriptArgs::ReadVector(v8::Isolate *isolate, v8::Local<v8::Value> value, glm::vec3 &out) {
        if (value.IsEmpty() || !value->IsObject()) {
            return false;
        }
        auto object = value.As<v8::Object>();
        glm::vec3 result;
        if (!ReadComponent(isolate, object, "x", result.x) || !ReadComponent(isolate, object, "y", result.y) || !ReadComponent(isolate, object, "z", result.z)) {
            return false;
        }
        out = result;
        return true;
    }

    bool ScriptArgs::ReadPosition(const v8::FunctionCallbackInfo<v8::Value> &info, int index, glm::vec3 &out, int &next) {
        if (index < info.Length() && info[index]->IsObject()) {
            next = index + 1;
            return ReadVector(info.GetIsolate(), info[index], out);
        }
        glm::vec3 result;
        if (index + 2 >= info.Length() || !ReadFloat(info[index], result.x) || !ReadFloat(info[index + 1], result.y) || !ReadFloat(info[index + 2], result.z)) {
            return false;
        }
        out  = result;
        next = index + 3;
        return true;
    }

    bool ScriptArgs::ReadColor(v8::Local<v8::Value> value, uint32_t &out) {
        uint32_t rgb = 0;
        if (!ReadUInt(value, 0xffffff, rgb)) {
            return false;
        }
        out = (rgb << 8) | 0xff;
        return true;
    }

    void ScriptArgs::Throw(v8::Isolate *isolate, const char *message) {
        isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, message)));
    }

    v8::Local<v8::Value> WrapPlayer(v8::Isolate *isolate, uint64_t networkId) {
        if (networkId == 0 || !GetServer().Players().FindByNetworkId(networkId)) {
            return v8::Null(isolate);
        }
        Player::GetClass(isolate);
        return v8pp::class_<Player>::create_object(isolate, networkId);
    }

    v8::Local<v8::Value> WrapVehicle(v8::Isolate *isolate, uint64_t networkId) {
        if (networkId == 0 || !GetServer().Cars().Find(networkId)) {
            return v8::Null(isolate);
        }
        Vehicle::GetClass(isolate);
        return v8pp::class_<Vehicle>::create_object(isolate, networkId);
    }

    v8::Local<v8::Value> WrapPickup(v8::Isolate *isolate, uint64_t networkId) {
        if (networkId == 0 || !GetServer().Pickups().Find(networkId)) {
            return v8::Null(isolate);
        }
        Pickup::GetClass(isolate);
        return v8pp::class_<Pickup>::create_object(isolate, networkId);
    }

    v8::Local<v8::Object> PlainVector(v8::Isolate *isolate, v8::Local<v8::Context> context, const glm::vec3 &value) {
        auto object = v8::Object::New(isolate);
        SetField(isolate, context, object, "x", v8::Number::New(isolate, value.x));
        SetField(isolate, context, object, "y", v8::Number::New(isolate, value.y));
        SetField(isolate, context, object, "z", v8::Number::New(isolate, value.z));
        return object;
    }

    void SetField(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char *key, v8::Local<v8::Value> value) {
        object->Set(context, v8pp::to_v8(isolate, key), value).Check();
    }

    void EmitEvent(const char *name, const std::function<void(v8::Isolate *, v8::Local<v8::Context>, EventArguments &)> &build) {
        auto *module = GetServer().GetScriptingModule();
        if (!module || !module->GetEngine() || !module->GetResourceManager() || !module->GetEngine()->IsInitialized()) {
            return;
        }
        auto *engine  = module->GetEngine();
        auto *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        auto context = engine->GetContext();
        v8::Context::Scope contextScope(context);
        EventArguments arguments;
        build(isolate, context, arguments);
        module->GetResourceManager()->GetEvents().EmitReserved(isolate, context, name, arguments);
    }
} // namespace Mafia1Online::Scripting
