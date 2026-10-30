#include "traffic_scripting.h"

#include "core/server.h"
#include "features/script/script_runtime.h"
#include "shared/features/traffic/traffic_jam.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <initializer_list>
#include <limits>
#include <utility>

namespace Mafia1Online::Scripting {
    namespace {
        void JS_GetJamSettings(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate        = info.GetIsolate();
            auto context         = isolate->GetCurrentContext();
            const auto &settings = GetServer().Traffic().GetJamSettings();
            auto object          = v8::Object::New(isolate);
            SetField(isolate, context, object, "enabled", v8::Boolean::New(isolate, settings.enabled));
            SetField(isolate, context, object, "minCars", v8::Uint32::New(isolate, settings.minCars));
            SetField(isolate, context, object, "radius", v8::Number::New(isolate, settings.radius));
            SetField(isolate, context, object, "maxSpeed", v8::Number::New(isolate, settings.maxSpeed));
            SetField(isolate, context, object, "stuckSeconds", v8::Number::New(isolate, settings.stuckSeconds));
            SetField(isolate, context, object, "cooldownSeconds", v8::Number::New(isolate, settings.cooldownSeconds));
            SetField(isolate, context, object, "playerClearance", v8::Number::New(isolate, settings.playerClearance));
            info.GetReturnValue().Set(object);
        }

        // setJamSettings({...}): the fields given replace the current ones.
        void JS_SetJamSettings(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate                = info.GetIsolate();
            constexpr const char *kUsage = "Traffic.setJamSettings(settings) expects an object of enabled (boolean), minCars (whole number) and radius, maxSpeed, "
                                           "stuckSeconds, cooldownSeconds, playerClearance (numbers)";
            if (info.Length() != 1 || !info[0]->IsObject()) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            auto context     = isolate->GetCurrentContext();
            auto object      = info[0].As<v8::Object>();
            auto settings    = GetServer().Traffic().GetJamSettings();
            const auto field = [&](const char *name, v8::Local<v8::Value> &value) {
                return object->Get(context, v8pp::to_v8(isolate, name)).ToLocal(&value) && !value->IsUndefined();
            };
            bool good = true;
            v8::Local<v8::Value> value;
            if (field("enabled", value)) {
                good             = good && value->IsBoolean();
                settings.enabled = value->IsBoolean() && value.As<v8::Boolean>()->Value();
            }
            if (field("minCars", value)) {
                uint32_t minCars = 0;
                good             = good && ScriptArgs::ReadUInt(value, std::numeric_limits<uint32_t>::max(), minCars);
                settings.minCars = minCars;
            }
            for (const auto &[name, target] :
                {std::pair<const char *, float *> {"radius", &settings.radius}, {"maxSpeed", &settings.maxSpeed}, {"stuckSeconds", &settings.stuckSeconds}, {"cooldownSeconds", &settings.cooldownSeconds}, {"playerClearance", &settings.playerClearance}}) {
                if (field(name, value)) {
                    good = good && ScriptArgs::ReadFloat(value, *target);
                }
            }
            if (!good) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            info.GetReturnValue().Set(GetServer().Traffic().SetJamSettings(settings));
        }

        void JS_ResetArea(const v8::FunctionCallbackInfo<v8::Value> &info) {
            glm::vec3 center;
            float radius = 0.0f;
            int next     = 0;
            if (!ScriptArgs::ReadPosition(info, 0, center, next) || info.Length() != next + 1 || !ScriptArgs::ReadFloat(info[next], radius) || radius < 0.0f || radius > Shared::Traffic::kMaxJamRadius * 4.0f) {
                ScriptArgs::Throw(info.GetIsolate(), "Traffic.resetArea(position, radius) expects a finite position and a radius from 0 to 800");
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(GetServer().Traffic().ResetArea(center, radius)));
        }

        void JS_RestartHost(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() > 1 || (info.Length() == 1 && !info[0]->IsBoolean())) {
                ScriptArgs::Throw(info.GetIsolate(), "Traffic.restartHost(handover?) expects an optional boolean");
                return;
            }
            const bool handover = info.Length() == 1 && info[0].As<v8::Boolean>()->Value();
            info.GetReturnValue().Set(GetServer().RestartTrafficHost(handover));
        }

        void JS_GetHost(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate    = info.GetIsolate();
            auto context     = isolate->GetCurrentContext();
            auto &simulation = GetServer().Simulation();
            auto object      = v8::Object::New(isolate);
            SetField(isolate, context, object, "attached", v8::Boolean::New(isolate, simulation.Host().has_value()));
            SetField(isolate, context, object, "standby", v8::Uint32::New(isolate, static_cast<uint32_t>(simulation.StandbyCount())));
            info.GetReturnValue().Set(object);
        }
    } // namespace

    void Traffic::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        v8pp::module traffic(isolate, ServerCatalog(), "Traffic",
            "The city's car traffic, which the simulation host drives around every player. The server watches it for jams (trafficJamReset) and asks the host to take "
            "jammed cars off the road; the host refills the road out of every player's sight. A car a player or scripted NPC sits in, a car a player stands at and a "
            "car a player took always stay. resetArea and restartHost also take parked cars and the police and mafia cars with their crews, each once no police "
            "case, road block or mafia pursuit runs through it.");
        traffic.function("getJamSettings", &JS_GetJamSettings, docs("TrafficJamSettings", {}, "Returns the jam watch's settings.", "The current settings."));
        traffic.function("setJamSettings", &JS_SetJamSettings,
            docs("boolean", {param("settings", "Partial<TrafficJamSettings>", false, "The fields to change; the others stay.")}, "Changes the jam watch for the whole server. server.json's mod.trafficJam sets the same fields at start.",
                "False when a value is out of range (minCars 2-64, radius 1-200, maxSpeed 0-20, stuckSeconds 1-3600, cooldownSeconds 0-3600, playerClearance 0-200); "
                "nothing changes then."));
        traffic.function("resetArea", &JS_ResetArea,
            docs("number", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Center; three numbers are accepted too."), param("radius", "number", false, "Distance in units, up to 800.")},
                "Asks the simulation host to take its traffic cars within radius off the road: parked ones, and police and mafia cars with their crews (npcRetire), "
                "which go with the next police retirement pass once no police case, road block or mafia pursuit runs through them; the others go at once, as "
                "the host's own despawn does.", "How many cars the host was asked to remove; 0 without a host."));
        traffic.function("restartHost", &JS_RestartHost,
            docs("boolean", {param("handover", "boolean", true, "Hand over to a standby simulation host instead; defaults to false.")},
                "Restarts the traffic: players aboard a tram or metro train step off where they boarded (playerRailwayEject), then the host takes its whole traffic "
                "pool off the road and refills it (trafficHostChange, reason restart). With handover the current host is kicked and the first standby host takes "
                "over one tick later; riders step off then (reason handover). The kicked host's process has to be started again to join as a standby.",
                "False without a host, or with handover and no standby host."));
        traffic.function("getHost", &JS_GetHost, docs("{ attached: boolean; standby: number }", {}, "Whether a simulation host runs the traffic, and how many standby hosts wait to take over.", "The host's state."));
        traffic.publish(global);
    }
} // namespace Mafia1Online::Scripting
