#include "bridge_scripting.h"

#include "core/server.h"
#include "features/script/script_runtime.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

namespace Mafia1Online::Scripting {
    namespace {
        // E_bridge_phase (reM C_bridge.h).
        const char *PhaseName(uint8_t phase) {
            switch (phase) {
            case 0: return "closed";
            case 1: return "signaling";
            case 2: return "preparing";
            case 3: return "opening";
            case 4: return "open";
            case 5: return "closing";
            default: return "unknown";
            }
        }

        v8::Local<v8::Object> BridgeInfo(v8::Isolate *isolate, v8::Local<v8::Context> context, const Shared::Bridge::State &state) {
            auto object = v8::Object::New(isolate);
            SetField(isolate, context, object, "index", v8::Uint32::New(isolate, state.index));
            SetField(isolate, context, object, "position", PlainVector(isolate, context, {state.position.x, state.position.y, state.position.z}));
            SetField(isolate, context, object, "phase", v8pp::to_v8(isolate, PhaseName(state.phase)));
            SetField(isolate, context, object, "angle", v8::Number::New(isolate, state.angle));
            // The leaves are up or moving, and the river gap is open.
            SetField(isolate, context, object, "raised", v8::Boolean::New(isolate, state.phase >= 3 && state.phase <= 5));
            return object;
        }

        void JS_GetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto list = v8::Array::New(isolate);
            uint32_t index = 0;
            GetServer().Bridges().ForEach([&](const Shared::Bridge::State &state) { list->Set(context, index++, BridgeInfo(isolate, context, state)).Check(); });
            info.GetReturnValue().Set(list);
        }

        void JS_Get(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t index = 0;
            if (info.Length() != 1 || !ScriptArgs::ReadUInt(info[0], Shared::Bridge::kMaxBridges - 1, index)) {
                ScriptArgs::Throw(info.GetIsolate(), "Bridge.get(index) expects an index from 0 to 7");
                return;
            }
            auto *isolate = info.GetIsolate();
            const auto *state = GetServer().Bridges().Find(index);
            info.GetReturnValue().Set(state ? BridgeInfo(isolate, isolate->GetCurrentContext(), *state).As<v8::Value>() : v8::Null(isolate).As<v8::Value>());
        }
    } // namespace

    void Bridge::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        v8pp::module bridge(isolate, ServerCatalog(), "Bridge",
            "The mission's drawbridges. The simulation host runs them with the retail AI, which raises one when a steamer comes; every player sees them, their steamers "
            "and their collision where the host has them, a few hundred milliseconds later. Read only.");
        bridge.function("getAll", &JS_GetAll, docs("BridgeInfo[]", {}, "Lists every bridge the simulation host has reported this mission.", "The bridges, by load index."));
        bridge.function("get", &JS_Get,
            docs("BridgeInfo | null", {param("index", "number", false, "Load index from 0.")}, "Returns one bridge as the host last reported it.",
                "The bridge, or null before the host reported it."));
        bridge.publish(global);
    }
} // namespace Mafia1Online::Scripting
