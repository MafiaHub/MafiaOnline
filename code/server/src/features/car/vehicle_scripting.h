#pragma once

#include "shared/features/car/car_entity.h"

#include <scripting/builtins/entity.h>
#include <scripting/builtins/quaternion.h>
#include <scripting/builtins/vector3.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace Mafia1Online::Scripting {
    // Server scripting handle for a Mafia 1 car. The server owns engine,
    // damage, seats and terminal state; the simulation controller's client
    // reports native physics and damage.
    class Vehicle final: public Framework::Scripting::Builtins::Entity {
      public:
        Vehicle(uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        Shared::Entities::CarEntity *ResolveCar() const;

        std::string GetModel() const;
        Framework::Scripting::Builtins::Vector3 GetCarPosition() const;
        Framework::Scripting::Builtins::Quaternion GetCarRotation() const;
        Framework::Scripting::Builtins::Vector3 GetVelocity() const;
        Framework::Scripting::Builtins::Vector3 GetAngularVelocity() const;
        double GetSteeringInput() const;
        double GetFuel() const;
        double GetFuelTankCapacity() const;
        double GetEngineRotations() const;
        int32_t GetGear() const;
        int32_t GetMaximumGear() const;
        uint32_t GetLightState() const;
        bool GetHorn() const;
        bool GetSiren() const;
        bool GetEngineOn() const;
        uint32_t GetRadarColor() const;
        double GetOpacity() const;
        double GetRepairRevision() const;
        bool GetRadarVisible() const;
        bool GetSpeedLimited() const;
        double GetPowerInput() const;
        double GetBrakeInput() const;
        double GetHandbrakeInput() const;
        bool GetDynamicsValid() const;
        double GetHealth() const;
        uint32_t GetDamageFlags() const;
        uint32_t GetDetachedParts() const;
        bool GetNativeDamageValid() const;
        double GetNativeDamageRevision() const;
        double GetMeshRevision() const;
        uint32_t GetSeatCount() const;
        uint32_t GetTerminalState() const;
        double GetMissionGeneration() const;
        double GetEngineRevision() const;
        double GetDynamicsCommandRevision() const;
        double GetTerminalSequence() const;
        double GetSeatSequence() const;

        bool Destroy();
        bool SetEngine(bool on);
        bool SetFuel(double fuel);
        bool SetLights(uint32_t lightState);
        bool SetHorn(bool on);
        bool SetSiren(bool on);
        bool SetSeatCount(uint32_t count);
        bool SetDamage(double health, uint32_t damageFlags, uint32_t detachedParts);
        bool SetMechanicalDamage(double engineHealth, double gearboxHealth, double bodyDamage, double fuelTankHealth);
        bool SetOpacity(double opacity);
        bool Repair();
        std::string SaveState() const;
        bool RestoreState(const std::string &snapshot);
        bool SetTerminalState(uint32_t state);
        bool Explode();
        std::string ToString() const override;

        // The server seat path shared by Player.putInVehicle and
        // recordSeatOutcome; emits the matching seat event.
        static bool RecordSeatOutcome(uint64_t carId, uint8_t seat, uint64_t playerId, Shared::Entities::CarEntity::SeatResult result);
        // The VehicleDamage object of a car with a valid native snapshot.
        static v8::Local<v8::Object> DamageObject(v8::Isolate *isolate, v8::Local<v8::Context> context, const Shared::Entities::CarEntity &car);

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<Vehicle> &GetClass(v8::Isolate *isolate);

      private:
        static std::unique_ptr<v8pp::class_<Vehicle>> _class;
    };
} // namespace Mafia1Online::Scripting
