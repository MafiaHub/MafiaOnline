#include "railway_scripting.h"

#include "core/server.h"
#include "features/script/script_runtime.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <functional>

namespace Mafia1Online::Scripting {
    namespace {
        // E_railway_state (reM C_railway.h).
        const char *StateName(uint8_t state) {
            switch (state) {
            case 0: return "moving";
            case 1: return "stationArrival";
            case 2: return "passengersLeaving";
            case 3: return "passengersEntering";
            case 4: return "stationWait";
            case 5: return "scheduleWait";
            case 6: return "departing";
            case 7: return "collisionBraking";
            case 9: return "stopPointBraking";
            case 10: return "semaphoreBraking";
            case 11: return "driverHit";
            case 12: return "followingRailway";
            default: return "unknown";
            }
        }

        glm::vec3 Vec(const Shared::Railway::Vector &vector) {
            return {vector.x, vector.y, vector.z};
        }

        void List(const v8::FunctionCallbackInfo<v8::Value> &info, const std::function<bool(const Shared::Railway::State &)> &include) {
            auto *isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto list = v8::Array::New(isolate);
            uint32_t index = 0;
            GetServer().Railways().ForEach([&](const Shared::Railway::State &state) {
                if (include(state)) {
                    list->Set(context, index++, RailwayInfo(isolate, context, state)).Check();
                }
            });
            info.GetReturnValue().Set(list);
        }

        void JS_GetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            List(info, [](const Shared::Railway::State &) { return true; });
        }

        void JS_GetInRange(const v8::FunctionCallbackInfo<v8::Value> &info) {
            glm::vec3 center;
            float radius = 0.0f;
            int next = 0;
            if (!ScriptArgs::ReadPosition(info, 0, center, next) || info.Length() != next + 1 || !ScriptArgs::ReadFloat(info[next], radius) || radius < 0.0f) {
                ScriptArgs::Throw(info.GetIsolate(), "Railway.getInRange(position, radius) expects a finite position and a radius of 0 or more");
                return;
            }
            const float radiusSq = radius * radius;
            List(info, [&](const Shared::Railway::State &state) {
                const glm::vec3 offset = Vec(state.position) - center;
                return offset.x * offset.x + offset.y * offset.y + offset.z * offset.z <= radiusSq;
            });
        }

        void JS_Get(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t index = 0;
            if (info.Length() != 1 || !ScriptArgs::ReadUInt(info[0], Shared::Railway::kMaxRailways - 1, index)) {
                ScriptArgs::Throw(info.GetIsolate(), "Railway.get(index) expects an index from 0 to 63");
                return;
            }
            auto *isolate = info.GetIsolate();
            const auto *state = GetServer().Railways().Find(index);
            info.GetReturnValue().Set(state ? RailwayInfo(isolate, isolate->GetCurrentContext(), *state).As<v8::Value>() : v8::Null(isolate).As<v8::Value>());
        }

        void JS_GetCount(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t count = 0;
            GetServer().Railways().ForEach([&](const Shared::Railway::State &) { ++count; });
            info.GetReturnValue().Set(count);
        }
    } // namespace

    v8::Local<v8::Object> RailwayInfo(v8::Isolate *isolate, v8::Local<v8::Context> context, const Shared::Railway::State &state) {
        auto object = v8::Object::New(isolate);
        auto wagons = v8::Array::New(isolate, state.wagonCount);
        for (uint8_t i = 0; i < state.wagonCount; ++i) {
            auto wagon = v8::Object::New(isolate);
            SetField(isolate, context, wagon, "position", PlainVector(isolate, context, Vec(state.wagonPositions[i])));
            SetField(isolate, context, wagon, "direction", PlainVector(isolate, context, Vec(state.wagonDirections[i])));
            wagons->Set(context, i, wagon).Check();
        }
        SetField(isolate, context, object, "index", v8::Uint32::New(isolate, state.index));
        SetField(isolate, context, object, "position", PlainVector(isolate, context, Vec(state.position)));
        SetField(isolate, context, object, "direction", PlainVector(isolate, context, Vec(state.direction)));
        SetField(isolate, context, object, "speed", v8::Number::New(isolate, state.speed));
        SetField(isolate, context, object, "state", v8pp::to_v8(isolate, StateName(state.state)));
        SetField(isolate, context, object, "wagons", wagons);
        return object;
    }

    void Railway::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        v8pp::module railway(isolate, ServerCatalog(), "Railway",
            "The mission's trams and metro trains. The simulation host runs them with the retail AI everywhere on the map and reports them ten times a second; every player "
            "sees them, with the host's passengers, where the host has them. Players ride them through the doors at stations.");
        railway.function("getAll", &JS_GetAll, docs("RailwayInfo[]", {}, "Lists every railway the simulation host has reported this mission.", "The railways, by load index."));
        railway.function("get", &JS_Get,
            docs("RailwayInfo | null", {param("index", "number", false, "Load index from 0.")}, "Returns one railway as the host last reported it.",
                "The railway, or null before the host reported it."));
        railway.function("getInRange", &JS_GetInRange,
            docs("RailwayInfo[]", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Center; three numbers are accepted too."), param("radius", "number", false, "Distance in units.")},
                "Lists the railways whose body is within radius of position.", "The railways in range."));
        railway.function("getCount", &JS_GetCount, docs("number", {}, "Counts the reported railways.", "The number of railways; freeride has 38."));
        railway.publish(global);
    }
} // namespace Mafia1Online::Scripting
