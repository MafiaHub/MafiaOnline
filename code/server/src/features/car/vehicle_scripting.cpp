#include "vehicle_scripting.h"

#include "core/server.h"
#include "features/script/script_events.h"
#include "features/script/script_runtime.h"
#include "shared/features/car/seat_action.h"
#include "shared/features/player/player_entity.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <sstream>

namespace Mafia1Online::Scripting {
    std::unique_ptr<v8pp::class_<Vehicle>> Vehicle::_class;

    namespace {
        using CarEntity = Shared::Entities::CarEntity;

        Vehicle *SelfVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Vehicle>::unwrap_object(info.GetIsolate(), info.This());
        }

        CarEntity *SelfCar(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfVehicle(info);
            return self ? self->ResolveCar() : nullptr;
        }

        // A heading in radians, or any object with finite x, y, z and w.
        bool ReadRotation(v8::Isolate *isolate, v8::Local<v8::Value> value, glm::quat &out) {
            float heading = 0.0f;
            if (ScriptArgs::ReadFloat(value, heading)) {
                out = glm::angleAxis(heading, glm::vec3(0.0f, 1.0f, 0.0f));
                return true;
            }
            glm::vec3 axis;
            v8::Local<v8::Value> w;
            float scalar = 0.0f;
            if (!ScriptArgs::ReadVector(isolate, value, axis) || !value.As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "w")).ToLocal(&w) || !ScriptArgs::ReadFloat(w, scalar)) {
                return false;
            }
            const glm::quat rotation(scalar, axis.x, axis.y, axis.z);
            const float length = glm::length(rotation);
            if (!std::isfinite(length) || length <= 0.0001f) {
                return false;
            }
            out = rotation / length;
            return true;
        }

        // Vehicle.spawn(model, position, rotation?, controller?)
        void JS_VehicleSpawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            glm::vec3 position;
            glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            int next = 0;
            if (info.Length() < 2 || !info[0]->IsString() || !ScriptArgs::ReadPosition(info, 1, position, next) || info.Length() > next + 2 ||
                (info.Length() > next && !info[next]->IsNullOrUndefined() && !ReadRotation(isolate, info[next], rotation))) {
                ScriptArgs::Throw(isolate, "Vehicle.spawn(model, position, rotation?, controller?) expects a stock .i3d model, a finite position and a heading in radians or a quaternion");
                return;
            }
            auto &server       = GetServer();
            uint64_t ownerGuid = 0;
            if (info.Length() == next + 2) {
                const uint64_t playerId = ScriptArgs::ReadEntityId(isolate, info[next + 1]);
                if (!server.Players().FindByNetworkId(playerId) || !server.IsPlayerReady(server.Players().GuidForNetworkId(playerId))) {
                    ScriptArgs::Throw(isolate, "Vehicle.spawn controller must be a mission-ready Player");
                    return;
                }
                ownerGuid = static_cast<uint64_t>(server.Players().GuidForNetworkId(playerId));
            }
            if (!server.AllPlayersReady()) {
                info.GetReturnValue().SetNull();
                return;
            }
            if (ownerGuid == 0) {
                server.Players().ForEach([&](Shared::Entities::PlayerEntity *player) {
                    const auto guid = server.Players().GuidForNetworkId(player->GetNetworkID());
                    if (ownerGuid == 0 && server.IsPlayerReady(guid)) {
                        ownerGuid = static_cast<uint64_t>(guid);
                    }
                });
            }
            auto *car = server.Cars().Spawn(v8pp::from_v8<std::string>(isolate, info[0]), position, rotation, server.MissionGeneration(), ownerGuid);
            if (!car) {
                info.GetReturnValue().SetNull();
                return;
            }
            Features::Script::EmitCarEvent(server, "vehicleSpawn", *car);
            info.GetReturnValue().Set(WrapVehicle(isolate, car->GetNetworkID()));
        }

        // setTransform(position, rotation?, velocity?)
        void JS_SetTransform(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *car     = SelfCar(info);
            glm::vec3 position, velocity(0.0f);
            int next = 0;
            glm::quat rotation = car ? car->rotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            if (!ScriptArgs::ReadPosition(info, 0, position, next) || info.Length() > next + 2 ||
                (info.Length() > next && !info[next]->IsNullOrUndefined() && !ReadRotation(isolate, info[next], rotation)) ||
                (info.Length() > next + 1 && !ScriptArgs::ReadVector(isolate, info[next + 1], velocity))) {
                ScriptArgs::Throw(isolate, "Vehicle.setTransform(position, rotation?, velocity?) expects a finite position, a heading in radians or a quaternion, and a velocity");
                return;
            }
            info.GetReturnValue().Set(car && GetServer().Cars().SetTransform(car->GetNetworkID(), position, velocity, rotation));
        }

        void JS_SetRadarMarker(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto *car      = SelfCar(info);
            uint32_t color = 0xFFFFFF;
            if (info.Length() < 1 || info.Length() > 2 || !info[0]->IsBoolean() || (info.Length() == 2 && !ScriptArgs::ReadUInt(info[1], 0xFFFFFF, color))) {
                ScriptArgs::Throw(isolate, "Vehicle.setRadarMarker(visible, color?) expects a boolean and a 0xRRGGBB color");
                return;
            }
            const bool visible = info[0]->BooleanValue(isolate);
            info.GetReturnValue().Set(car && GetServer().Cars().SetRadarColor(car->GetNetworkID(), visible ? 0xFF000000u | color : 0u));
        }

        v8::Local<v8::Value> Occupant(v8::Isolate *isolate, const CarEntity &car, uint32_t seat) {
            if (seat >= car.seatCount || seat >= CarEntity::kMaxSeats) {
                return v8::Null(isolate);
            }
            auto *player = GetServer().Players().FindByNetworkId(car.occupantIds[seat]);
            return player && player->spawnGeneration == car.occupantGenerations[seat] ? WrapPlayer(isolate, player->GetNetworkID()) : v8::Null(isolate).As<v8::Value>();
        }

        void JS_GetDriver(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *car = SelfCar(info);
            info.GetReturnValue().Set(car ? Occupant(info.GetIsolate(), *car, 0) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_GetOccupant(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            uint32_t seat = 0;
            if (info.Length() != 1 || !ScriptArgs::ReadUInt(info[0], CarEntity::kMaxSeats - 1, seat)) {
                ScriptArgs::Throw(isolate, "Vehicle.getOccupant(seat) expects a seat from 0 to 7");
                return;
            }
            auto *car = SelfCar(info);
            info.GetReturnValue().Set(car ? Occupant(isolate, *car, seat) : v8::Null(isolate).As<v8::Value>());
        }

        void JS_GetOccupants(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *car     = SelfCar(info);
            auto context  = isolate->GetCurrentContext();
            auto list     = v8::Array::New(isolate, car ? car->seatCount : 0);
            for (uint32_t seat = 0; car && seat < car->seatCount && seat < CarEntity::kMaxSeats; ++seat) {
                list->Set(context, seat, Occupant(isolate, *car, seat)).Check();
            }
            info.GetReturnValue().Set(list);
        }

        void JS_GetController(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *car     = SelfCar(info);
            uint64_t controllerId = 0;
            if (car && car->simulationControllerGuid != 0) {
                if (auto *player = GetServer().Players().FindByGuid(static_cast<MafiaNet::PeerGuid>(car->simulationControllerGuid))) {
                    controllerId = player->GetNetworkID();
                }
            }
            info.GetReturnValue().Set(WrapPlayer(isolate, controllerId));
        }

        void JS_GetDoors(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *car     = SelfCar(info);
            auto context  = isolate->GetCurrentContext();
            auto doors    = v8::Array::New(isolate, car ? car->seatCount : 0);
            for (uint32_t seat = 0; car && seat < car->seatCount && seat < CarEntity::kMaxSeats; ++seat) {
                doors->Set(context, seat, v8::Number::New(isolate, car->doorTargets[seat] / 255.0)).Check();
            }
            info.GetReturnValue().Set(doors);
        }

        void JS_GetDamageState(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *car = SelfCar(info);
            if (!car || !car->nativeDamageValid) {
                info.GetReturnValue().SetNull();
                return;
            }
            auto *isolate = info.GetIsolate();
            info.GetReturnValue().Set(Vehicle::DamageObject(isolate, isolate->GetCurrentContext(), *car));
        }

        void JS_RecordSeatOutcome(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto *self     = SelfVehicle(info);
            uint32_t seat = 0, result = 0;
            const uint64_t playerId = info.Length() == 3 ? ScriptArgs::ReadEntityId(isolate, info[1]) : 0;
            if (!self || playerId == 0 || !ScriptArgs::ReadUInt(info[0], CarEntity::kMaxSeats - 1, seat) || !ScriptArgs::ReadUInt(info[2], 4, result) || result < 1) {
                ScriptArgs::Throw(isolate, "Vehicle.recordSeatOutcome(seat, player, result) expects a seat from 0 to 7, a Player and result 1 entered, 2 stolen, 3 exited or 4 exit blocked");
                return;
            }
            info.GetReturnValue().Set(Vehicle::RecordSeatOutcome(self->GetId(), static_cast<uint8_t>(seat), playerId, static_cast<CarEntity::SeatResult>(result)));
        }
    } // namespace

    CarEntity *Vehicle::ResolveCar() const {
        return GetServer().Cars().Find(_id);
    }

#define MAFIA1ONLINE_CAR_FIELD(type, name, expression, fallback) \
    type Vehicle::name() const {                                  \
        const auto *car = ResolveCar();                          \
        return car ? static_cast<type>(expression) : fallback;   \
    }
    MAFIA1ONLINE_CAR_FIELD(std::string, GetModel, car->model, std::string {})
    MAFIA1ONLINE_CAR_FIELD(double, GetSteeringInput, car->steeringInput, 0.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetFuel, car->fuel, 0.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetFuelTankCapacity, car->fuelTankCapacity, 0.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetEngineRotations, car->engineRotations, 0.0)
    MAFIA1ONLINE_CAR_FIELD(int32_t, GetGear, car->gear, 0)
    MAFIA1ONLINE_CAR_FIELD(int32_t, GetMaximumGear, car->maximumGear, 0)
    MAFIA1ONLINE_CAR_FIELD(uint32_t, GetLightState, car->lightState, 0u)
    MAFIA1ONLINE_CAR_FIELD(bool, GetHorn, car->hornOn, false)
    MAFIA1ONLINE_CAR_FIELD(bool, GetSiren, car->sirenOn, false)
    MAFIA1ONLINE_CAR_FIELD(bool, GetEngineOn, car->engineOn, false)
    MAFIA1ONLINE_CAR_FIELD(uint32_t, GetRadarColor, car->radarColor & 0xFFFFFF, 0u)
    MAFIA1ONLINE_CAR_FIELD(double, GetOpacity, car->opacity, 1.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetRepairRevision, car->repairRevision, 0.0)
    MAFIA1ONLINE_CAR_FIELD(bool, GetRadarVisible, car->radarColor != 0, false)
    MAFIA1ONLINE_CAR_FIELD(bool, GetSpeedLimited, car->speedLimited, false)
    MAFIA1ONLINE_CAR_FIELD(double, GetPowerInput, car->powerInput, 0.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetBrakeInput, car->brakeInput, 0.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetHandbrakeInput, car->handbrakeInput, 0.0)
    MAFIA1ONLINE_CAR_FIELD(bool, GetDynamicsValid, car->dynamicsValid, false)
    MAFIA1ONLINE_CAR_FIELD(double, GetHealth, car->health, 0.0)
    MAFIA1ONLINE_CAR_FIELD(uint32_t, GetDamageFlags, car->damageFlags, 0u)
    MAFIA1ONLINE_CAR_FIELD(uint32_t, GetDetachedParts, car->detachedParts, 0u)
    MAFIA1ONLINE_CAR_FIELD(bool, GetNativeDamageValid, car->nativeDamageValid, false)
    MAFIA1ONLINE_CAR_FIELD(double, GetNativeDamageRevision, car->nativeDamageRevision, 0.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetMeshRevision, car->meshRevision, 0.0)
    MAFIA1ONLINE_CAR_FIELD(uint32_t, GetSeatCount, car->seatCount, 0u)
    MAFIA1ONLINE_CAR_FIELD(uint32_t, GetTerminalState, car->terminalState, 0u)
    MAFIA1ONLINE_CAR_FIELD(double, GetMissionGeneration, car->missionGeneration, 0.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetEngineRevision, car->engineRevision, 0.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetDynamicsCommandRevision, car->dynamicsCommandRevision, 0.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetTerminalSequence, car->terminalSequence, 0.0)
    MAFIA1ONLINE_CAR_FIELD(double, GetSeatSequence, car->seatSequence, 0.0)
#undef MAFIA1ONLINE_CAR_FIELD

    Framework::Scripting::Builtins::Vector3 Vehicle::GetCarPosition() const {
        const auto *car = ResolveCar();
        return car ? Framework::Scripting::Builtins::Vector3(car->position) : Framework::Scripting::Builtins::Vector3();
    }

    Framework::Scripting::Builtins::Quaternion Vehicle::GetCarRotation() const {
        const auto *car = ResolveCar();
        return car ? Framework::Scripting::Builtins::Quaternion(car->rotation) : Framework::Scripting::Builtins::Quaternion();
    }

    Framework::Scripting::Builtins::Vector3 Vehicle::GetVelocity() const {
        const auto *car = ResolveCar();
        return car ? Framework::Scripting::Builtins::Vector3(car->velocity) : Framework::Scripting::Builtins::Vector3();
    }

    Framework::Scripting::Builtins::Vector3 Vehicle::GetAngularVelocity() const {
        const auto *car = ResolveCar();
        return car ? Framework::Scripting::Builtins::Vector3(car->angularVelocity) : Framework::Scripting::Builtins::Vector3();
    }

    bool Vehicle::Destroy() {
        auto &server = GetServer();
        auto *car    = ResolveCar();
        if (!car) {
            return false;
        }
        // The handle stops resolving once the replica is gone.
        Features::Script::EmitCarEvent(server, "vehicleDestroy", *car);
        return server.Cars().Despawn(_id);
    }

    bool Vehicle::SetEngine(bool on) {
        auto &server       = GetServer();
        auto *car          = ResolveCar();
        const bool oldOn   = car && car->engineOn;
        const bool changed = server.Cars().SetEngineOn(_id, on);
        car                = ResolveCar();
        if (changed && car && oldOn != car->engineOn) {
            Features::Script::EmitCarEvent(server, "vehicleEngineChange", *car);
        }
        return changed;
    }

    bool Vehicle::SetFuel(double fuel) {
        auto &server        = GetServer();
        auto *car           = ResolveCar();
        const float oldFuel = car ? car->fuel : 0.0f;
        const bool changed  = std::isfinite(fuel) && server.Cars().SetFuel(_id, static_cast<float>(fuel));
        if (changed && car && car->fuel != oldFuel) {
            Features::Script::EmitCarEvent(server, "vehicleFuelChange", *car);
        }
        return changed;
    }

    bool Vehicle::SetLights(uint32_t lightState) {
        auto &server             = GetServer();
        auto *car                = ResolveCar();
        const uint32_t oldLights = car ? car->lightState : 0;
        const bool changed       = server.Cars().SetLights(_id, lightState);
        if (changed && car && car->lightState != oldLights) {
            Features::Script::EmitCarEvent(server, "vehicleLightsChange", *car);
        }
        return changed;
    }

    bool Vehicle::SetHorn(bool on) {
        auto &server       = GetServer();
        auto *car          = ResolveCar();
        const bool oldHorn = car && car->hornOn;
        const bool changed = server.Cars().SetHorn(_id, on);
        if (changed && car && car->hornOn != oldHorn) {
            Features::Script::EmitCarEvent(server, "vehicleHornChange", *car);
        }
        return changed;
    }

    bool Vehicle::SetSiren(bool on) {
        auto &server        = GetServer();
        auto *car           = ResolveCar();
        const bool oldSiren = car && car->sirenOn;
        const bool changed  = server.Cars().SetSiren(_id, on);
        if (changed && car && car->sirenOn != oldSiren) {
            Features::Script::EmitCarEvent(server, "vehicleSirenChange", *car);
        }
        return changed;
    }

    bool Vehicle::SetSeatCount(uint32_t count) {
        return count >= 1 && count <= CarEntity::kMaxSeats && GetServer().Cars().SetSeatCount(_id, static_cast<uint8_t>(count));
    }

    bool Vehicle::SetDamage(double health, uint32_t damageFlags, uint32_t detachedParts) {
        if (!std::isfinite(health)) {
            return false;
        }
        auto &server            = GetServer();
        auto *car               = ResolveCar();
        const float value       = static_cast<float>(health);
        const bool stateChanged = car && (car->health != value || car->damageFlags != damageFlags || car->detachedParts != detachedParts);
        const bool changed      = server.Cars().SetDamage(_id, value, damageFlags, detachedParts);
        if (changed && stateChanged) {
            Features::Script::EmitCarEvent(server, "vehicleDamageState", *ResolveCar());
        }
        return changed;
    }

    bool Vehicle::SetMechanicalDamage(double engineHealth, double gearboxHealth, double bodyDamage, double fuelTankHealth) {
        if (!std::isfinite(engineHealth) || !std::isfinite(gearboxHealth) || !std::isfinite(bodyDamage) || !std::isfinite(fuelTankHealth) ||
            fuelTankHealth < 0.0 || fuelTankHealth > 100000.0) {
            return false;
        }
        auto &server            = GetServer();
        const auto *car         = ResolveCar();
        const uint64_t revision = car ? car->nativeDamageRevision : 0;
        const bool accepted     = server.Cars().SetMechanicalDamage(_id, static_cast<float>(engineHealth), static_cast<float>(gearboxHealth), static_cast<float>(bodyDamage), static_cast<int32_t>(fuelTankHealth));
        if (accepted && ResolveCar()->nativeDamageRevision != revision) {
            Features::Script::EmitCarEvent(server, "vehicleDamage", *ResolveCar());
        }
        return accepted;
    }

    bool Vehicle::SetOpacity(double opacity) {
        if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
            return false;
        }
        auto &server = GetServer();
        auto *car = ResolveCar();
        const float value = static_cast<float>(opacity);
        const bool changed = car && car->opacity != value;
        const bool accepted = server.Cars().SetOpacity(_id, value);
        if (accepted && changed) {
            Features::Script::EmitCarEvent(server, "vehicleOpacityChange", *ResolveCar());
        }
        return accepted;
    }

    bool Vehicle::Repair() {
        auto &server = GetServer();
        if (!server.Cars().Repair(_id)) {
            return false;
        }
        Features::Script::EmitCarEvent(server, "vehicleRepair", *ResolveCar());
        return true;
    }

    bool Vehicle::SetTerminalState(uint32_t state) {
        if (state < 1 || state > 3) {
            return false;
        }
        auto &server = GetServer();
        auto *car    = ResolveCar();
        if (car && state == static_cast<uint32_t>(CarEntity::TerminalState::Exploded) && car->terminalState == CarEntity::TerminalState::Active) {
            const auto occupantIds         = car->occupantIds;
            const auto occupantGenerations = car->occupantGenerations;
            const uint8_t seatCount        = car->seatCount;
            for (uint8_t seat = 0; seat < seatCount; ++seat) {
                if (auto *player = server.Players().FindByNetworkId(occupantIds[seat]); player && player->spawnGeneration == occupantGenerations[seat]) {
                    server.Combat().SetHealth(player->GetNetworkID(), 0.0f, 0, std::nullopt, Features::Combat::DamageCause::Vehicle);
                }
            }
        }
        const bool changed = server.Cars().SetTerminalState(_id, static_cast<CarEntity::TerminalState>(state));
        if (changed) {
            if (state == static_cast<uint32_t>(CarEntity::TerminalState::Exploded)) {
                server.Debris().NoteExploded(_id);
            }
            Features::Script::EmitCarEvent(server, "vehicleTerminal", *ResolveCar());
        }
        return changed;
    }

    bool Vehicle::Explode() {
        return SetTerminalState(static_cast<uint32_t>(CarEntity::TerminalState::Exploded));
    }

    bool Vehicle::RecordSeatOutcome(uint64_t carId, uint8_t seat, uint64_t playerId, CarEntity::SeatResult result) {
        auto &server       = GetServer();
        auto *player       = server.Players().FindByNetworkId(playerId);
        auto *car          = server.Cars().Find(carId);
        const bool changed = player && car && player->spawned && player->alive && player->missionGeneration == car->missionGeneration && car->missionGeneration == server.MissionGeneration() &&
                             server.Cars().RecordSeatOutcome(carId, seat, playerId, player->spawnGeneration, result);
        if (changed) {
            Shared::Car::SeatEvent event;
            event.carId             = carId;
            event.playerId          = playerId;
            event.seat              = seat;
            event.spawnGeneration   = player->spawnGeneration;
            event.missionGeneration = car->missionGeneration;
            event.serverSequence    = car->seatSequence;
            switch (result) {
            case CarEntity::SeatResult::Entered: event.action = Shared::Car::SeatAction::Enter; break;
            case CarEntity::SeatResult::Stolen: event.action = Shared::Car::SeatAction::Steal; break;
            case CarEntity::SeatResult::Exited: event.action = Shared::Car::SeatAction::Exit; break;
            case CarEntity::SeatResult::ExitBlocked: event.action = Shared::Car::SeatAction::ExitBlocked; break;
            default: break;
            }
            Features::Script::EmitSeatEvent(server, event);
        }
        return changed;
    }

    std::string Vehicle::ToString() const {
        std::ostringstream stream;
        stream << "Vehicle{ id: " << _id << ", model: \"" << GetModel() << "\" }";
        return stream.str();
    }

    v8::Local<v8::Object> Vehicle::DamageObject(v8::Isolate *isolate, v8::Local<v8::Context> context, const CarEntity &car) {
        const auto &damage = car.nativeDamage;
        auto result        = v8::Object::New(isolate);
        SetField(isolate, context, result, "revision", v8::Number::New(isolate, static_cast<double>(car.nativeDamageRevision)));
        SetField(isolate, context, result, "engineHealth", v8::Number::New(isolate, damage.engineHealth));
        SetField(isolate, context, result, "gearboxHealth", v8::Number::New(isolate, damage.gearboxHealth));
        SetField(isolate, context, result, "bodyDamage", v8::Number::New(isolate, damage.bodyDamage));
        SetField(isolate, context, result, "fuelTankHealth", v8::Int32::New(isolate, damage.fuelTankHealth));
        SetField(isolate, context, result, "engineDamagePower", v8::Number::New(isolate, damage.engineDamagePower));
        SetField(isolate, context, result, "engineDestroyed", v8::Boolean::New(isolate, damage.engineDestroyed != 0));
        SetField(isolate, context, result, "burning", v8::Boolean::New(isolate, damage.burning != 0));
        auto lights = v8::Array::New(isolate, damage.lightCount);
        for (uint8_t index = 0; index < damage.lightCount; ++index) {
            auto value = v8::Object::New(isolate);
            SetField(isolate, context, value, "flags", v8::Uint32::New(isolate, damage.lights[index].flags));
            SetField(isolate, context, value, "damage", v8::Number::New(isolate, damage.lights[index].damage));
            lights->Set(context, index, value).Check();
        }
        SetField(isolate, context, result, "lights", lights);
        auto zones = v8::Array::New(isolate, damage.zoneCount);
        for (uint8_t index = 0; index < damage.zoneCount; ++index) {
            auto value = v8::Object::New(isolate);
            SetField(isolate, context, value, "flags", v8::Uint32::New(isolate, damage.zones[index].flags));
            SetField(isolate, context, value, "crackLevel", v8::Number::New(isolate, damage.zones[index].crackLevel));
            zones->Set(context, index, value).Check();
        }
        SetField(isolate, context, result, "zones", zones);
        auto wheels = v8::Array::New(isolate, damage.wheelCount);
        for (uint8_t index = 0; index < damage.wheelCount; ++index) {
            auto value = v8::Object::New(isolate);
            SetField(isolate, context, value, "flags", v8::Uint32::New(isolate, damage.wheels[index].flags));
            SetField(isolate, context, value, "health", v8::Number::New(isolate, damage.wheels[index].health));
            SetField(isolate, context, value, "deformAngle", v8::Number::New(isolate, damage.wheels[index].deformAngle));
            wheels->Set(context, index, value).Check();
        }
        SetField(isolate, context, result, "wheels", wheels);
        return result;
    }

    v8pp::class_<Vehicle> &Vehicle::GetClass(v8::Isolate *isolate) {
        if (_class) {
            return *_class;
        }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Vehicle>>(isolate, ServerCatalog(), "Vehicle",
            "A server car. The server owns engine, damage, seats and terminal state; the client chosen as simulation controller reports native physics and damage.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing vehicle; use Vehicle.spawn to create one."));
        cls.function("toString", &Vehicle::ToString, docs("string", {}, "Formats this vehicle for logging.", "The vehicle ID and model."));

        cls.property("model", &Vehicle::GetModel, property_docs("string", "Stock .i3d model filename."));
        cls.property("position", &Vehicle::GetCarPosition, property_docs("Vector3", "Last reported position. Read only; use setTransform."));
        cls.property("rotation", &Vehicle::GetCarRotation, property_docs("Quaternion", "Last reported rotation. Read only; use setTransform."));
        cls.property("velocity", &Vehicle::GetVelocity, property_docs("Vector3", "Linear velocity in units per second."));
        cls.property("angularVelocity", &Vehicle::GetAngularVelocity, property_docs("Vector3", "Angular velocity."));
        cls.property("steeringInput", &Vehicle::GetSteeringInput, property_docs("number", "Steering in native mapped angle units."));
        cls.property("fuel", &Vehicle::GetFuel, property_docs("number", "Fuel amount."));
        cls.property("fuelTankCapacity", &Vehicle::GetFuelTankCapacity, property_docs("number", "Tank capacity the controller reported."));
        cls.property("engineRotations", &Vehicle::GetEngineRotations, property_docs("number", "Engine rotations, owner telemetry."));
        cls.property("gear", &Vehicle::GetGear, property_docs("number", "Current gear, owner telemetry."));
        cls.property("maximumGear", &Vehicle::GetMaximumGear, property_docs("number", "Highest gear of the model."));
        cls.property("lightState", &Vehicle::GetLightState, property_docs("number",
            "Light bits: left indicator 0x1, right indicator 0x2, headlights 0x80, brake active 0x100, brake check 0x800, reverse active 0x1000, reverse check 0x8000, master lights 0x10000."));
        cls.property("hornOn", &Vehicle::GetHorn, property_docs("boolean", "Whether the horn sounds."));
        cls.property("sirenOn", &Vehicle::GetSiren, property_docs("boolean", "Whether the server-owned siren is on."));
        cls.property("engineOn", &Vehicle::GetEngineOn, property_docs("boolean", "Whether the engine runs."));
        cls.property("radarColor", &Vehicle::GetRadarColor, property_docs("number", "0xRRGGBB color of the radar marker."));
        cls.property("opacity", &Vehicle::GetOpacity, property_docs("number", "Server-authored visual opacity, 0 transparent and 1 opaque."));
        cls.property("repairRevision", &Vehicle::GetRepairRevision, property_docs("number", "Increments when repair resets native damage and deformation."));
        cls.property("radarVisible", &Vehicle::GetRadarVisible, property_docs("boolean", "Whether the vehicle is drawn on every player's radar."));
        cls.property("speedLimited", &Vehicle::GetSpeedLimited, property_docs("boolean", "Whether the native speed limiter is on."));
        cls.property("powerInput", &Vehicle::GetPowerInput, property_docs("number", "Resolved throttle from 0 to 1."));
        cls.property("brakeInput", &Vehicle::GetBrakeInput, property_docs("number", "Resolved brake from 0 to 1."));
        cls.property("handbrakeInput", &Vehicle::GetHandbrakeInput, property_docs("number", "Resolved handbrake from 0 to 1."));
        cls.property("dynamicsValid", &Vehicle::GetDynamicsValid, property_docs("boolean", "False until a controller reported native state; setFuel, setLights and setHorn need it."));
        cls.property("health", &Vehicle::GetHealth, property_docs("number", "Script metadata health set by setDamage."));
        cls.property("damageFlags", &Vehicle::GetDamageFlags, property_docs("number", "Script metadata damage flags."));
        cls.property("detachedParts", &Vehicle::GetDetachedParts, property_docs("number", "Script metadata detached part flags."));
        cls.property("nativeDamageValid", &Vehicle::GetNativeDamageValid, property_docs("boolean", "Whether a native damage snapshot exists."));
        cls.property("nativeDamageRevision", &Vehicle::GetNativeDamageRevision, property_docs("number", "Revision of the native damage snapshot."));
        cls.property("meshRevision", &Vehicle::GetMeshRevision, property_docs("number", "Revision of the accepted deformation checkpoint."));
        cls.property("seatCount", &Vehicle::GetSeatCount, property_docs("number", "Number of seats, 1 to 8."));
        cls.property("terminalState", &Vehicle::GetTerminalState, property_docs("number", "0 active, 1 exploded, 2 submerged, 3 out of bounds."));
        cls.property("missionGeneration", &Vehicle::GetMissionGeneration, property_docs("number", "Mission generation the vehicle belongs to."));
        cls.property("engineRevision", &Vehicle::GetEngineRevision, property_docs("number", "Revision of the engine state."));
        cls.property("dynamicsCommandRevision", &Vehicle::GetDynamicsCommandRevision, property_docs("number", "Revision of the last script fuel, lights or horn command."));
        cls.property("terminalSequence", &Vehicle::GetTerminalSequence, property_docs("number", "Sequence of the terminal transition."));
        cls.property("seatSequence", &Vehicle::GetSeatSequence, property_docs("number", "Sequence of the last accepted seat transition."));

        cls.function("destroy", &Vehicle::Destroy, docs("boolean", {}, "Fires vehicleDestroy, then removes the vehicle from every client.", "False when it no longer exists."));
        cls.prototype_function("getController", &JS_GetController, docs("Player | null", {}, "Returns the player whose client simulates this vehicle.", "The controller, or null."));
        cls.prototype_function("getDriver", &JS_GetDriver, docs("Player | null", {}, "Returns the current-life occupant of seat 0.", "The driver, or null."));
        cls.prototype_function("getOccupant", &JS_GetOccupant, docs("Player | null", {param("seat", "number", false, "Seat from 0 to 7.")}, "Returns the current-life occupant of a seat.", "The occupant, or null."));
        cls.prototype_function("getOccupants", &JS_GetOccupants, docs("(Player | null)[]", {}, "Returns the occupant of every seat.", "One entry per seat, null when empty."));
        cls.prototype_function("getDoors", &JS_GetDoors, docs("number[]", {}, "Returns each seat's door target, from 0 closed to 1 open.", "One entry per seat."));
        cls.prototype_function("getDamageState", &JS_GetDamageState,
            docs("VehicleDamage | null", {}, "Returns the controller's native damage snapshot.", "The snapshot, or null before the first report or while repair awaits a fresh report."));
        cls.prototype_function("setTransform", &JS_SetTransform,
            docs("boolean", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "New position; three numbers are accepted too."),
                                param("rotation", "number | Quaternion", true, "Heading in radians or a quaternion; keeps the current rotation when omitted."),
                                param("velocity", "Vector3 | { x: number; y: number; z: number }", true, "Velocity after the teleport; defaults to zero.")},
                "Sets the authoritative pose; the simulation controller applies it before it reports again.", "False when the vehicle no longer exists."));
        cls.function("setEngine", &Vehicle::SetEngine, docs("boolean", {param("on", "boolean", false, "Engine state.")}, "Starts or stops the engine. Fires vehicleEngineChange.", "False for a terminal vehicle."));
        cls.function("setFuel", &Vehicle::SetFuel, docs("boolean", {param("fuel", "number", false, "Fuel from 0 to fuelTankCapacity.")}, "Sets the fuel; the controller applies and acknowledges it. After the first native tank report, only this server call may increase fuel.", "False before native dynamics are available or when out of range."));
        cls.function("setLights", &Vehicle::SetLights, docs("boolean", {param("lightState", "number", false, "Light bits, see lightState.")}, "Sets the semantic light bits; native blink phase stays game controlled.", "False before native dynamics are available or for unsupported bits."));
        cls.function("setHorn", &Vehicle::SetHorn, docs("boolean", {param("on", "boolean", false, "Horn state.")}, "Sounds or silences the horn.", "False before native dynamics are available."));
        cls.function("setSiren", &Vehicle::SetSiren, docs("boolean", {param("on", "boolean", false, "Siren state.")}, "Switches the siren sound and light bar. Drivers toggle it with K.", "False for a terminal vehicle."));
        cls.prototype_function("setRadarMarker", &JS_SetRadarMarker,
            docs("boolean", {param("visible", "boolean", false, "Whether every player's radar shows this vehicle."), param("color", "number", true, "0xRRGGBB marker color; defaults to white.")},
                "Shows or hides this vehicle on every radar.", "False when the vehicle no longer exists."));
        cls.function("setOpacity", &Vehicle::SetOpacity,
            docs("boolean", {param("opacity", "number", false, "Visual opacity from 0 transparent to 1 opaque.")},
                "Changes the car model's opacity on every client. Fires vehicleOpacityChange.", "False for a terminal vehicle or a value outside 0 to 1."));
        cls.function("repair", &Vehicle::Repair,
            docs("boolean", {}, "Repairs native engine, gearbox, body, fuel tank, lights, attached wheels and deform meshes without resetting position or seats. Fires vehicleRepair.",
                "False for a terminal vehicle. Loose debris actors remain until their own lifetime ends."));
        cls.function("setSeatCount", &Vehicle::SetSeatCount, docs("boolean", {param("count", "number", false, "Seats from 1 to 8.")}, "Changes the seat count if the removed seats are empty.", "False when a removed seat is taken."));
        cls.function("setDamage", &Vehicle::SetDamage,
            docs("boolean", {param("health", "number", false, "Metadata health."), param("damageFlags", "number", false, "Metadata flags."), param("detachedParts", "number", false, "Metadata part flags.")},
                "Sets script metadata only; it does not deform the car. Fires vehicleDamageState.", "False when the vehicle no longer exists."));
        cls.function("setMechanicalDamage", &Vehicle::SetMechanicalDamage,
            docs("boolean", {param("engineHealth", "number", false, "Engine health; zero lets the car burn and explode."), param("gearboxHealth", "number", false, "Gearbox health."),
                                param("bodyDamage", "number", false, "Body damage."), param("fuelTankHealth", "number", false, "Fuel tank health.")},
                "Authors a native damage revision the simulation controller applies. Fires vehicleDamage.", "False before the first native damage snapshot or for out-of-range values."));
        cls.function("setTerminalState", &Vehicle::SetTerminalState,
            docs("boolean", {param("state", "number", false, "1 exploded, 2 submerged, 3 out of bounds.")}, "Ends the vehicle; an explosion kills current occupants through server combat first. Fires vehicleTerminal.", "False when already terminal."));
        cls.function("explode", &Vehicle::Explode, docs("boolean", {}, "Same as setTerminalState(1).", "False when already terminal."));
        cls.prototype_function("recordSeatOutcome", &JS_RecordSeatOutcome,
            docs("boolean", {param("seat", "number", false, "Seat from 0 to 7."), param("player", "Player", false, "Living player of this mission."), param("result", "number", false, "1 entered, 2 stolen, 3 exited, 4 exit blocked.")},
                "Records an administrative seat result without native animation and fires the matching seat event. Entering or stealing seat 0 makes the player's client the simulation controller.", "False when the result is not valid now."));
        return cls;
    }

    void Vehicle::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &damage = ServerCatalog().data_type("VehicleDamage", "The simulation controller's native per-part damage snapshot. Indexes are native model indexes.");
        damage.add_property("revision", "number", "Snapshot revision.");
        damage.add_property("engineHealth", "number", "Engine health.");
        damage.add_property("engineDamagePower", "number", "Native engine damage power.");
        damage.add_property("engineDestroyed", "boolean", "Whether the engine is destroyed.");
        damage.add_property("gearboxHealth", "number", "Gearbox health.");
        damage.add_property("bodyDamage", "number", "Body damage.");
        damage.add_property("fuelTankHealth", "number", "Fuel tank health.");
        damage.add_property("burning", "boolean", "Whether the car burns.");
        damage.add_property("lights", "{ flags: number; damage: number }[]", "Per-light damage.");
        damage.add_property("zones", "{ flags: number; crackLevel: number }[]", "Per-zone deformation; flag 1 is broken glass or a part.");
        damage.add_property("wheels", "{ flags: number; health: number; deformAngle: number }[]", "Per-wheel state; 0x80000000 flat, 0x40000000 detached, 0x400 broken.");

        auto &cls = GetClass(isolate);
        cls.static_function("spawn", &JS_VehicleSpawn,
            v8pp::metadata::docs("Vehicle | null",
                {v8pp::metadata::param("model", "string", false, "Stock .i3d model filename, for example thunderbird00.i3d."),
                    v8pp::metadata::param("position", "Vector3 | { x: number; y: number; z: number }", false, "Spawn position; three numbers are accepted too."),
                    v8pp::metadata::param("rotation", "number | Quaternion", true, "Heading in radians or a quaternion; defaults to 0."),
                    v8pp::metadata::param("controller", "Player", true, "Mission-ready player whose client simulates the car first; defaults to the first ready player.")},
                "Creates a car for the current mission. Fires vehicleSpawn.", "The vehicle, or null before every client is mission ready."));
        auto context = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "Vehicle"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }
} // namespace Mafia1Online::Scripting
