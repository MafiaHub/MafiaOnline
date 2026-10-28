#include "pumper_scripting.h"

#include "core/server.h"
#include "features/script/script_runtime.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cmath>

namespace Mafia1Online::Scripting {
    namespace {
        v8::Local<v8::Object> PumperInfo(v8::Isolate *isolate, v8::Local<v8::Context> context, const Shared::Pumper::State &state) {
            auto &pumpers = GetServer().Pumpers();
            const uint64_t carId = pumpers.ServiceCar(state.index);
            auto object = v8::Object::New(isolate);
            SetField(isolate, context, object, "index", v8::Uint32::New(isolate, state.index));
            SetField(isolate, context, object, "position", PlainVector(isolate, context, {state.position.x, state.position.y, state.position.z}));
            SetField(isolate, context, object, "state", v8pp::to_v8(isolate, Shared::Pumper::StateName(state.state)));
            SetField(isolate, context, object, "busy", v8::Boolean::New(isolate, carId != 0 || Shared::Pumper::Pumping(state.state)));
            SetField(isolate, context, object, "vehicle", carId != 0 ? WrapVehicle(isolate, carId) : v8::Null(isolate).As<v8::Value>());
            return object;
        }

        void JS_GetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto list = v8::Array::New(isolate);
            uint32_t index = 0;
            GetServer().Pumpers().ForEach([&](const Shared::Pumper::State &state) { list->Set(context, index++, PumperInfo(isolate, context, state)).Check(); });
            info.GetReturnValue().Set(list);
        }

        void JS_Get(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t index = 0;
            if (info.Length() != 1 || !ScriptArgs::ReadUInt(info[0], Shared::Pumper::kMaxPumpers - 1, index)) {
                ScriptArgs::Throw(info.GetIsolate(), "Pumper.get(index) expects an index from 0 to 7");
                return;
            }
            auto *isolate = info.GetIsolate();
            const auto *state = GetServer().Pumpers().Find(index);
            info.GetReturnValue().Set(state ? PumperInfo(isolate, isolate->GetCurrentContext(), *state).As<v8::Value>() : v8::Null(isolate).As<v8::Value>());
        }

        void JS_SetEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsBoolean()) {
                ScriptArgs::Throw(info.GetIsolate(), "Pumper.setEnabled(enabled) expects a boolean");
                return;
            }
            GetServer().Pumpers().SetEnabled(info[0]->BooleanValue(info.GetIsolate()));
        }

        void JS_IsEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(GetServer().Pumpers().IsEnabled());
        }

        void JS_SetPrice(const v8::FunctionCallbackInfo<v8::Value> &info) {
            constexpr double kMax = Features::Pumper::PumperService::kMaxPricePerLitre;
            if (info.Length() != 1 || !info[0]->IsNumber()) {
                ScriptArgs::Throw(info.GetIsolate(), "Pumper.setPrice(pricePerLitre) expects a number from 0 to 10000");
                return;
            }
            const double price = info[0].As<v8::Number>()->Value();
            if (!std::isfinite(price) || price < 0.0 || price > kMax) {
                ScriptArgs::Throw(info.GetIsolate(), "Pumper.setPrice(pricePerLitre) expects a number from 0 to 10000");
                return;
            }
            GetServer().Pumpers().SetPricePerLitre(price);
        }

        void JS_GetPrice(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(GetServer().Pumpers().PricePerLitre());
        }
    } // namespace

    void EmitVehicleRefuel(uint64_t carId, uint64_t playerId, uint8_t index, float litres, uint32_t cost) {
        EmitEvent("vehicleRefuel", [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
            auto object = v8::Object::New(isolate);
            SetField(isolate, context, object, "pumper", v8::Uint32::New(isolate, index));
            SetField(isolate, context, object, "litres", v8::Number::New(isolate, litres));
            SetField(isolate, context, object, "cost", v8::Uint32::New(isolate, cost));
            arguments.push_back(WrapVehicle(isolate, carId));
            arguments.push_back(WrapPlayer(isolate, playerId));
            arguments.push_back(object);
        });
    }

    void EmitPumperRefused(uint64_t playerId, uint64_t carId, uint8_t index, const char *reason, uint32_t cost) {
        EmitEvent("pumperRefused", [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
            auto object = v8::Object::New(isolate);
            SetField(isolate, context, object, "pumper", v8::Uint32::New(isolate, index));
            SetField(isolate, context, object, "reason", v8pp::to_v8(isolate, reason));
            SetField(isolate, context, object, "cost", v8::Uint32::New(isolate, cost));
            arguments.push_back(WrapPlayer(isolate, playerId));
            arguments.push_back(WrapVehicle(isolate, carId));
            arguments.push_back(object);
        });
    }

    void Pumper::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        v8pp::module pumper(isolate, ServerCatalog(), "Pumper",
            "The mission's gas station attendants (the Free Ride scenes have three, one per pump). The simulation host runs them with the retail AI and every player sees "
            "them the same: idling, smoking, and serving a car. A driver who stops at a pump with the engine off and takes the retail \"use the pump\" choice gets the "
            "retail service: the attendant greets, walks to the car, fuels it with its sound, says he is done and walks back, while the car stands locked. The server "
            "fills the tank, charges the price and dispatches vehicleRefuel.");
        pumper.function("getAll", &JS_GetAll, docs("PumperInfo[]", {}, "Lists every attendant the simulation host has reported this mission.", "The attendants, by load index."));
        pumper.function("get", &JS_Get,
            docs("PumperInfo | null", {param("index", "number", false, "Load index from 0.")}, "Returns one attendant as the host last reported it.",
                "The attendant, or null before the host reported it."));
        pumper.function("setEnabled", &JS_SetEnabled,
            docs("void", {param("enabled", "boolean", false, "Whether attendants take new cars.")},
                "Turns the service on or off for everyone. While off, a request is refused (pumperRefused, reason disabled); a service under way finishes."));
        pumper.function("isEnabled", &JS_IsEnabled, docs("boolean", {}, "Whether attendants take new cars.", "True unless Pumper.setEnabled(false)."));
        pumper.function("setPrice", &JS_SetPrice,
            docs("void", {param("pricePerLitre", "number", false, "Money per litre, 0 to 10000; 0 (the default) is free.")},
                "Sets what a service costs. The fill takes every litre the tank lacks, or as many as the player's money buys; a player who cannot pay for one litre "
                "is refused (reason money)."));
        pumper.function("getPrice", &JS_GetPrice, docs("number", {}, "Money per litre.", "The price Pumper.setPrice set, 0 by default."));
        pumper.publish(global);
    }
} // namespace Mafia1Online::Scripting
