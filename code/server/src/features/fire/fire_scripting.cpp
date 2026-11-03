#include "fire_scripting.h"

#include "core/server.h"
#include "features/script/script_runtime.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/scripting_catalog.h"

#include <scripting/builtins/vector3.h>
#include <scripting/engine.h>

#include <v8pp/convert.hpp>

#include <glm/geometric.hpp>

#include <cmath>
#include <optional>
#include <sstream>

namespace Mafia1Online::Scripting {
    std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Fire>>> Fire::_classes;
    std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<WaterJet>>> WaterJet::_classes;

    namespace {
        using Shared::Entities::FireEntity;
        using Shared::Entities::WaterJetEntity;
        using Service = Features::Fire::FireService;

        Service &Fires() {
            return GetServer().Fires();
        }

        Fire *SelfFire(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Fire>::unwrap_object(info.GetIsolate(), info.This());
        }

        WaterJet *SelfJet(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<WaterJet>::unwrap_object(info.GetIsolate(), info.This());
        }

        std::string Owner(v8::Isolate *isolate) {
            auto *module = GetServer().GetScriptingModule();
            return module && module->GetEngine() ? module->GetEngine()->GetResourceForIsolate(isolate) : std::string();
        }

        v8::Local<v8::Value> Vector(v8::Isolate *isolate, const glm::vec3 &value) {
            auto &cls = Framework::Scripting::Builtins::Vector3::GetClass(isolate);
            return cls.import_external(isolate, new Framework::Scripting::Builtins::Vector3(value));
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

        bool Get(v8::Isolate *isolate, v8::Local<v8::Object> object, const char *key, v8::Local<v8::Value> &out) {
            return object->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, key)).ToLocal(&out);
        }

        // An absent key keeps out; a present one must be a finite number.
        bool OptionalNumber(v8::Isolate *isolate, v8::Local<v8::Object> object, const char *key, float &out) {
            v8::Local<v8::Value> value;
            if (!Get(isolate, object, key, value)) {
                return false;
            }
            return value->IsUndefined() || ScriptArgs::ReadFloat(value, out);
        }

        bool ReadFireOptions(v8::Isolate *isolate, v8::Local<v8::Value> value, Service::Options &options) {
            if (value->IsUndefined() || value->IsNull()) {
                return true;
            }
            if (!value->IsObject()) {
                return false;
            }
            auto object    = value.As<v8::Object>();
            float lifetime = 0.0f;
            if (!OptionalNumber(isolate, object, "radius", options.radius) || !OptionalNumber(isolate, object, "height", options.height) ||
                !OptionalNumber(isolate, object, "intensity", options.intensity) || !OptionalNumber(isolate, object, "damage", options.damage) ||
                !OptionalNumber(isolate, object, "lifetime", lifetime) || !OptionalNumber(isolate, object, "growth", options.growth) ||
                !OptionalNumber(isolate, object, "toughness", options.toughness)) {
                return false;
            }
            if (options.radius < FireEntity::kMinRadius || options.radius > FireEntity::kMaxRadius || options.height < 0.0f || options.height > FireEntity::kMaxHeight ||
                options.intensity <= 0.0f || options.intensity > 1.0f || options.damage < 0.0f || options.damage > 200.0f || lifetime < 0.0f || lifetime > 3600000.0f ||
                options.growth < 0.0f || options.growth > 1.0f || options.toughness < 0.1f || options.toughness > 20.0f) {
                return false;
            }
            options.lifetime = std::chrono::milliseconds(static_cast<int64_t>(lifetime));
            v8::Local<v8::Value> attach, world;
            if (!Get(isolate, object, "attachTo", attach) || !Get(isolate, object, "virtualWorld", world)) {
                return false;
            }
            if (!attach->IsNullOrUndefined()) {
                options.attachedId = ScriptArgs::ReadEntityId(isolate, attach);
                if (!options.attachedId || !GetServer().Cars().Find(options.attachedId)) {
                    return false;
                }
            }
            return world->IsUndefined() || ReadWorld(world, options.virtualWorld);
        }

        // Fire.create(position, options?)
        void JS_FireCreate(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            glm::vec3 position;
            Service::Options options;
            int next = 0;
            if (!ScriptArgs::ReadPosition(info, 0, position, next) || info.Length() > next + 1 || (info.Length() == next + 1 && !ReadFireOptions(isolate, info[next], options))) {
                ScriptArgs::Throw(isolate, "Fire.create(position, options?) expects a finite position and options { radius? 0.5-15, height? 0-12, intensity? 0-1, damage? 0-200, "
                                           "lifetime? ms, growth? 0-1, toughness? 0.1-20, attachTo?: Vehicle, virtualWorld? }");
                return;
            }
            auto *fire = Fires().Create(position, options, GetServer().MissionGeneration(), Owner(isolate));
            info.GetReturnValue().Set(WrapFire(isolate, fire ? fire->GetNetworkID() : 0));
        }

        void JS_FireGetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            for (uint64_t id : Fires().List()) {
                list->Set(context, index++, WrapFire(isolate, id)).Check();
            }
            info.GetReturnValue().Set(list);
        }

        void JS_FireGetInRange(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            glm::vec3 center;
            float radius = 0.0f;
            int next     = 0;
            if (!ScriptArgs::ReadPosition(info, 0, center, next) || info.Length() != next + 1 || !ScriptArgs::ReadFloat(info[next], radius) || radius < 0.0f) {
                ScriptArgs::Throw(isolate, "Fire.getInRange(position, radius) expects a finite position and a radius");
                return;
            }
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            for (uint64_t id : Fires().List()) {
                const auto *fire = Fires().Find(id);
                if (fire && glm::distance(fire->position, center) <= radius) {
                    list->Set(context, index++, WrapFire(isolate, id)).Check();
                }
            }
            info.GetReturnValue().Set(list);
        }

        void JS_FireGetPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfFire(info);
            auto *fire = self ? self->ResolveFire() : nullptr;
            info.GetReturnValue().Set(Vector(info.GetIsolate(), fire ? fire->position : glm::vec3(0.0f)));
        }

        void JS_FireSetPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfFire(info);
            glm::vec3 position;
            if (!self || info.Length() < 1 || !ScriptArgs::ReadVector(info.GetIsolate(), info[0], position)) {
                ScriptArgs::Throw(info.GetIsolate(), "Fire.position must be a finite Vector3 or { x, y, z }");
                return;
            }
            Fires().SetPosition(self->GetId(), position);
        }

        void JS_FireGetIntensity(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfFire(info);
            auto *fire = self ? self->ResolveFire() : nullptr;
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), fire ? fire->intensity : 0.0));
        }

        void JS_FireSetIntensity(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self      = SelfFire(info);
            float intensity = 0.0f;
            if (!self || info.Length() < 1 || !ScriptArgs::ReadFloat(info[0], intensity) || intensity < 0.0f || intensity > 1.0f) {
                ScriptArgs::Throw(info.GetIsolate(), "Fire.intensity must be a number from 0 to 1");
                return;
            }
            Fires().SetIntensity(self->GetId(), intensity);
        }

        void JS_FireGetRadius(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfFire(info);
            auto *fire = self ? self->ResolveFire() : nullptr;
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), fire ? fire->radius : 0.0));
        }

        void JS_FireSetRadius(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self   = SelfFire(info);
            float radius = 0.0f;
            if (!self || info.Length() < 1 || !ScriptArgs::ReadFloat(info[0], radius) || radius < FireEntity::kMinRadius || radius > FireEntity::kMaxRadius) {
                ScriptArgs::Throw(info.GetIsolate(), "Fire.radius must be a number from 0.5 to 15");
                return;
            }
            Fires().SetRadius(self->GetId(), radius);
        }

        void JS_FireGetVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfFire(info);
            auto *fire = self ? self->ResolveFire() : nullptr;
            info.GetReturnValue().Set(fire && fire->attachedId ? WrapVehicle(info.GetIsolate(), fire->attachedId) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        // fire.douse(amount, player?)
        void JS_FireDouse(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = SelfFire(info);
            float amount  = 0.0f;
            uint64_t playerId = 0;
            if (!self || info.Length() < 1 || info.Length() > 2 || !ScriptArgs::ReadFloat(info[0], amount) || amount < 0.0f ||
                (info.Length() == 2 && !info[1]->IsNullOrUndefined() && !(playerId = ScriptArgs::ReadEntityId(isolate, info[1])))) {
                ScriptArgs::Throw(isolate, "Fire.douse(amount, player?) expects an intensity amount of 0 or more and an optional Player");
                return;
            }
            if (playerId && !GetServer().Players().FindByNetworkId(playerId)) {
                playerId = 0;
            }
            info.GetReturnValue().Set(v8::Number::New(isolate, Fires().Douse(self->GetId(), amount, playerId)));
        }

        // fire.attachTo(vehicle | null)
        void JS_FireAttachTo(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = SelfFire(info);
            if (!self || info.Length() != 1) {
                ScriptArgs::Throw(isolate, "Fire.attachTo(vehicle) expects a Vehicle or null");
                return;
            }
            const uint64_t vehicleId = info[0]->IsNull() ? 0 : ScriptArgs::ReadEntityId(isolate, info[0]);
            if (!info[0]->IsNull() && !vehicleId) {
                ScriptArgs::Throw(isolate, "Fire.attachTo(vehicle) expects a Vehicle or null");
                return;
            }
            info.GetReturnValue().Set(Fires().AttachTo(self->GetId(), vehicleId));
        }

        v8::Local<v8::Array> ContributorList(v8::Isolate *isolate, v8::Local<v8::Context> context, const std::vector<Service::Contribution> &contributors) {
            float total = 0.0f;
            for (const auto &entry : contributors) {
                total += entry.amount;
            }
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            for (const auto &entry : contributors) {
                auto item = v8::Object::New(isolate);
                SetField(isolate, context, item, "player", WrapPlayer(isolate, entry.playerId));
                SetField(isolate, context, item, "amount", v8::Number::New(isolate, entry.amount));
                SetField(isolate, context, item, "share", v8::Number::New(isolate, total > 0.0f ? entry.amount / total : 0.0));
                list->Set(context, index++, item).Check();
            }
            return list;
        }

        void JS_FireGetContributors(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfFire(info);
            auto *isolate = info.GetIsolate();
            info.GetReturnValue().Set(ContributorList(isolate, isolate->GetCurrentContext(), self ? Fires().Contributors(self->GetId()) : std::vector<Service::Contribution> {}));
        }

        // WaterJet.create(source, options?)
        void JS_JetCreate(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            const uint64_t sourceId = info.Length() >= 1 ? ScriptArgs::ReadEntityId(isolate, info[0]) : 0;
            const bool vehicle      = sourceId && GetServer().Cars().Find(sourceId);
            const bool player       = sourceId && GetServer().Players().FindByNetworkId(sourceId);
            Service::JetOptions options;
            bool valid = (vehicle || player) && info.Length() <= 2;
            if (valid && info.Length() == 2 && !info[1]->IsNullOrUndefined()) {
                valid = info[1]->IsObject();
                if (valid) {
                    auto object = info[1].As<v8::Object>();
                    v8::Local<v8::Value> credit, world;
                    valid = OptionalNumber(isolate, object, "pressure", options.pressure) && Get(isolate, object, "player", credit) && Get(isolate, object, "virtualWorld", world) &&
                            options.pressure >= WaterJetEntity::kMinPressure && options.pressure <= WaterJetEntity::kMaxPressure &&
                            (world->IsUndefined() || ReadWorld(world, options.virtualWorld));
                    if (valid && !credit->IsNullOrUndefined()) {
                        options.creditId = ScriptArgs::ReadEntityId(isolate, credit);
                        valid            = options.creditId && GetServer().Players().FindByNetworkId(options.creditId);
                    }
                }
            }
            if (!valid) {
                ScriptArgs::Throw(isolate, "WaterJet.create(source, options?) expects a Player or Vehicle and options { pressure? 8-30, player?: Player, virtualWorld? }");
                return;
            }
            auto *jet = Fires().CreateJet(sourceId, vehicle, options, GetServer().MissionGeneration(), Owner(isolate));
            info.GetReturnValue().Set(WrapWaterJet(isolate, jet ? jet->GetNetworkID() : 0));
        }

        void JS_JetGetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            for (uint64_t id : Fires().ListJets()) {
                list->Set(context, index++, WrapWaterJet(isolate, id)).Check();
            }
            info.GetReturnValue().Set(list);
        }

        // jet.aimAt(position)
        void JS_JetAimAt(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfJet(info);
            glm::vec3 target;
            int next = 0;
            if (!self || !ScriptArgs::ReadPosition(info, 0, target, next) || info.Length() != next) {
                ScriptArgs::Throw(info.GetIsolate(), "WaterJet.aimAt(position) expects a finite position");
                return;
            }
            info.GetReturnValue().Set(Fires().AimJet(self->GetId(), target));
        }

        void JS_JetGetTarget(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfJet(info);
            auto *jet  = self ? self->ResolveJet() : nullptr;
            info.GetReturnValue().Set(jet && jet->active ? Vector(info.GetIsolate(), jet->target) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_JetGetNozzle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfJet(info);
            auto *jet  = self ? self->ResolveJet() : nullptr;
            const auto nozzle = jet ? Fires().Nozzle(*jet) : std::nullopt;
            info.GetReturnValue().Set(nozzle ? Vector(info.GetIsolate(), *nozzle) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_JetGetSource(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfJet(info);
            auto *jet  = self ? self->ResolveJet() : nullptr;
            if (!jet) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(jet->sourceIsVehicle ? WrapVehicle(info.GetIsolate(), jet->sourceId) : WrapPlayer(info.GetIsolate(), jet->sourceId));
        }

        void JS_JetGetPressure(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfJet(info);
            auto *jet  = self ? self->ResolveJet() : nullptr;
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), jet ? jet->pressure : 0.0));
        }

        void JS_JetSetPressure(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self     = SelfJet(info);
            float pressure = 0.0f;
            if (!self || info.Length() < 1 || !ScriptArgs::ReadFloat(info[0], pressure) || pressure < WaterJetEntity::kMinPressure || pressure > WaterJetEntity::kMaxPressure) {
                ScriptArgs::Throw(info.GetIsolate(), "WaterJet.pressure must be a number from 8 to 30");
                return;
            }
            Fires().SetJetPressure(self->GetId(), pressure);
        }

        const char *ReasonName(Service::Reason reason) {
            switch (reason) {
            case Service::Reason::Doused: return "doused";
            case Service::Reason::Script: return "script";
            case Service::Reason::BurnedOut: return "burnedOut";
            }
            return "script";
        }
    } // namespace

    v8::Local<v8::Value> WrapFire(v8::Isolate *isolate, uint64_t id) {
        if (!id || !GetServer().Fires().Find(id)) {
            return v8::Null(isolate);
        }
        Fire::GetClass(isolate);
        return v8pp::class_<Fire>::create_object(isolate, id);
    }

    v8::Local<v8::Value> WrapWaterJet(v8::Isolate *isolate, uint64_t id) {
        if (!id || !GetServer().Fires().FindJet(id)) {
            return v8::Null(isolate);
        }
        WaterJet::GetClass(isolate);
        return v8pp::class_<WaterJet>::create_object(isolate, id);
    }

    void InstallFireEvents(Core::Server &server) {
        server.Fires().SetExtinguishedCallback([](uint64_t fireId, Service::Reason reason, const std::vector<Service::Contribution> &contributors) {
            EmitEvent("fireExtinguished", [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
                auto info = v8::Object::New(isolate);
                SetField(isolate, context, info, "reason", v8pp::to_v8(isolate, ReasonName(reason)));
                SetField(isolate, context, info, "contributors", ContributorList(isolate, context, contributors));
                arguments.push_back(WrapFire(isolate, fireId));
                arguments.push_back(info);
            });
        });
    }

    FireEntity *Fire::ResolveFire() const {
        return Fires().Find(_id);
    }

    double Fire::GetHeight() const {
        const auto *fire = ResolveFire();
        return fire ? fire->height : 0.0;
    }

    bool Fire::GetBurning() const {
        const auto *fire = ResolveFire();
        return fire && fire->intensity > 0.0f;
    }

    bool Fire::GetDousing() const {
        const auto *fire = ResolveFire();
        return fire && fire->dousing;
    }

    double Fire::GetMissionGeneration() const {
        const auto *fire = ResolveFire();
        return fire ? static_cast<double>(fire->missionGeneration) : 0.0;
    }

    bool Fire::Extinguish() {
        return Fires().Extinguish(_id);
    }

    bool Fire::Destroy() {
        return Fires().Destroy(_id);
    }

    std::string Fire::ToString() const {
        std::ostringstream stream;
        const auto *fire = ResolveFire();
        stream << "Fire{ id: " << _id << ", intensity: " << (fire ? fire->intensity : 0.0f) << " }";
        return stream.str();
    }

    void Fire::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    v8pp::class_<Fire> &Fire::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (_class) {
            return *_class;
        }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Fire>>(isolate, ServerCatalog(), "Fire",
            "A fire that burns until water or a script puts it out. Every client draws it from the retail fire effects, light and sound; the fewer flames burn the lower "
            "its intensity. Water jets aimed into its area lower the intensity, and players standing in it burn. It lasts until destroyed, the mission changes or the "
            "resource that created it stops.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing fire; use Fire.create to start one."));
        cls.function("toString", &Fire::ToString, docs("string", {}, "Formats this fire for logging.", "The fire ID and intensity."));
        cls.property("height", &Fire::GetHeight, property_docs("number", "How far above its position the flames reach, 0 to 12."));
        cls.property("burning", &Fire::GetBurning, property_docs("boolean", "Whether its intensity is above 0."));
        cls.property("dousing", &Fire::GetDousing, property_docs("boolean", "Whether a water jet hit it in the last 1.5 seconds."));
        cls.property("missionGeneration", &Fire::GetMissionGeneration, property_docs("number", "Mission generation the fire belongs to."));
        cls.accessor_property("position", v8::FunctionTemplate::New(isolate, &JS_FireGetPosition), v8::FunctionTemplate::New(isolate, &JS_FireSetPosition),
            property_docs("Vector3", "Where it burns. Assigning moves it and detaches it from a vehicle."));
        cls.accessor_property("intensity", v8::FunctionTemplate::New(isolate, &JS_FireGetIntensity), v8::FunctionTemplate::New(isolate, &JS_FireSetIntensity),
            property_docs("number", "0 (out, smoke only) to 1 (fully ablaze). Assigning 0 fires fireExtinguished with reason \"script\"; above 0 sets it alight again."));
        cls.accessor_property("radius", v8::FunctionTemplate::New(isolate, &JS_FireGetRadius), v8::FunctionTemplate::New(isolate, &JS_FireSetRadius),
            property_docs("number", "Radius of its area in units, 0.5 to 15."));
        cls.accessor_property("vehicle", v8::FunctionTemplate::New(isolate, &JS_FireGetVehicle), v8::Local<v8::FunctionTemplate>(),
            property_docs("Vehicle | null", "The vehicle it burns on, or null."));
        cls.prototype_function("douse", &JS_FireDouse,
            docs("number", {param("amount", "number", false, "Intensity to take off, 0 or more."), param("player", "Player", true, "Who gets the credit in getContributors and fireExtinguished.")},
                "Lowers the intensity, as an extinguisher or a bucket would. Fires fireExtinguished with reason \"doused\" when it reaches 0.",
                "The remaining intensity, or -1 when the fire no longer exists."));
        cls.prototype_function("attachTo", &JS_FireAttachTo,
            docs("boolean", {param("vehicle", "Vehicle | null", false, "A vehicle to burn on, or null to stay where it is.")}, "Makes the fire follow a vehicle.",
                "False when the fire or vehicle no longer exists."));
        cls.prototype_function("getContributors", &JS_FireGetContributors,
            docs("{ player: Player | null; amount: number; share: number }[]", {}, "Lists who put out how much of this fire, most first. share sums to 1.", "The contributions."));
        cls.function("extinguish", &Fire::Extinguish, docs("boolean", {}, "Puts the fire out at once. Fires fireExtinguished with reason \"script\" unless it was out.", "False when it no longer exists."));
        cls.function("destroy", &Fire::Destroy, docs("boolean", {}, "Removes the fire, smoke and all, from every client without an event.", "False when it no longer exists."));
        return cls;
    }

    void Fire::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        auto &cls = GetClass(isolate);
        cls.static_function("create", &JS_FireCreate,
            docs("Fire | null",
                {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Where it burns (its base); three numbers are accepted too."),
                    param("options",
                        "{ radius?: number; height?: number; intensity?: number; damage?: number; lifetime?: number; growth?: number; toughness?: number; attachTo?: Vehicle; virtualWorld?: number }",
                        true,
                        "radius 0.5-15 (default 3); height 0-12, how far up a facade the flames climb (default 0); intensity 0-1 (default 1); damage per second to a player in it at full "
                        "intensity, 0-200 (default 20); lifetime in ms before it burns out by itself, 0 for never (default); growth, intensity regained per second three seconds after "
                        "the last water (default 0); toughness, how much harder than a 3-unit fire it is to put out (default 1); attachTo, a vehicle it burns on; virtualWorld "
                        "(default every world, -1).")},
                "Starts a fire every client sees, including late joiners.", "The fire, or null at the limit of 64 fires."));
        cls.static_function("getAll", &JS_FireGetAll, docs("Fire[]", {}, "Lists every fire, burning or out.", "The current fires."));
        cls.static_function("getInRange", &JS_FireGetInRange,
            docs("Fire[]", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Center."), param("radius", "number", false, "Distance in units.")},
                "Lists the fires within radius of position.", "The fires in range."));
        auto context = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "Fire"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }

    WaterJetEntity *WaterJet::ResolveJet() const {
        return Fires().FindJet(_id);
    }

    bool WaterJet::GetActive() const {
        const auto *jet = ResolveJet();
        return jet && jet->active;
    }

    bool WaterJet::Stop() {
        return Fires().AimJet(_id, std::nullopt);
    }

    bool WaterJet::Destroy() {
        return Fires().DestroyJet(_id);
    }

    std::string WaterJet::ToString() const {
        std::ostringstream stream;
        stream << "WaterJet{ id: " << _id << ", active: " << (GetActive() ? "true" : "false") << " }";
        return stream.str();
    }

    void WaterJet::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    v8pp::class_<WaterJet> &WaterJet::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (_class) {
            return *_class;
        }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<WaterJet>>(isolate, ServerCatalog(), "WaterJet",
            "A fire hose held by a player or a water cannon on a vehicle. While aimed, every client draws the retail hose spray arcing from the nozzle to the target with the "
            "hydrant's water sound, and the server lowers the intensity of every fire whose area holds the target. It lasts until destroyed, the mission changes or the "
            "resource that created it stops.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing water jet; use WaterJet.create."));
        cls.function("toString", &WaterJet::ToString, docs("string", {}, "Formats this jet for logging.", "The jet ID and whether it sprays."));
        cls.property("active", &WaterJet::GetActive, property_docs("boolean", "Whether it sprays."));
        cls.accessor_property("target", v8::FunctionTemplate::New(isolate, &JS_JetGetTarget), v8::Local<v8::FunctionTemplate>(),
            property_docs("Vector3 | null", "Where the water lands while it sprays, else null."));
        cls.accessor_property("source", v8::FunctionTemplate::New(isolate, &JS_JetGetSource), v8::Local<v8::FunctionTemplate>(),
            property_docs("Player | Vehicle | null", "Who holds the nozzle."));
        cls.accessor_property("pressure", v8::FunctionTemplate::New(isolate, &JS_JetGetPressure), v8::FunctionTemplate::New(isolate, &JS_JetSetPressure),
            property_docs("number", "Muzzle speed in m/s, 8 to 30 (default 18). The arc reaches about pressure squared over 9.8, and water puts fires out faster at higher pressure."));
        cls.prototype_function("aimAt", &JS_JetAimAt,
            docs("boolean", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Where the water should land; three numbers are accepted too.")},
                "Opens the nozzle towards the target, or turns it. Fires within 45 units of the nozzle whose area holds the target are doused.", "False when the jet no longer exists."));
        cls.prototype_function("getNozzle", &JS_JetGetNozzle,
            docs("Vector3 | null", {}, "Where the water leaves: a player's hands or a vehicle's roof.", "The nozzle, or null while its holder is dead, unspawned or gone."));
        cls.function("stop", &WaterJet::Stop, docs("boolean", {}, "Closes the nozzle.", "False when the jet no longer exists."));
        cls.function("destroy", &WaterJet::Destroy, docs("boolean", {}, "Removes the jet from every client.", "False when it no longer exists."));
        return cls;
    }

    void WaterJet::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        auto &cls = GetClass(isolate);
        cls.static_function("create", &JS_JetCreate,
            docs("WaterJet | null",
                {param("source", "Player | Vehicle", false, "Who holds the nozzle: a player's hands, or the roof of a vehicle."),
                    param("options", "{ pressure?: number; player?: Player; virtualWorld?: number }", true,
                        "pressure 8-30 m/s (default 18); player, who gets the credit for a vehicle's jet; virtualWorld (default every world, -1).")},
                "Creates a closed hose; aimAt opens it.", "The jet, or null at the limit of 32 jets."));
        cls.static_function("getAll", &JS_JetGetAll, docs("WaterJet[]", {}, "Lists every water jet.", "The current jets."));
        auto context = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "WaterJet"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }
} // namespace Mafia1Online::Scripting
