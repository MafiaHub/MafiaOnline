#pragma once

#include <scripting/builtins/entity.h>
#include <scripting/builtins/player.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>

namespace Mafia1Online::Shared::Entities {
    class PlayerEntity;
    class CarEntity;
} // namespace Mafia1Online::Shared::Entities

namespace Mafia1Online::Scripting {
    // A streamed player's replica. Properties read replicated state;
    // getWorldPosition/getWorldTransform read the live native frame instead.
    class Player: public Framework::Scripting::Builtins::Player {
      public:
        explicit Player(uint64_t networkId);

        Shared::Entities::PlayerEntity *ResolvePlayer() const;
        std::string GetNickname() const;
        std::string GetModel() const;
        double GetHealth() const;
        double GetMoney() const;
        bool IsAlive() const;
        bool IsSpawned() const;
        bool IsLocal() const;
        double GetMissionGeneration() const;
        double GetSpawnGeneration() const;
        std::string ToString() const override;

        static v8pp::class_<Player> &GetClass(v8::Isolate *isolate);
        // Classes belong to the runtime (isolate) that built them; drop them when it goes.
        static void UnregisterIsolate(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Player>>> _classes;
    };

    class Vehicle final: public Framework::Scripting::Builtins::Entity {
      public:
        explicit Vehicle(uint64_t networkId);

        Shared::Entities::CarEntity *ResolveCar() const;
        std::string GetModel() const;
        double GetHealth() const;
        bool IsEngineOn() const;
        bool IsSirenOn() const;
        bool IsHornOn() const;
        double GetFuel() const;
        double GetOpacity() const;
        int GetGear() const;
        int GetSeatCount() const;
        std::string ToString() const override;

        static v8pp::class_<Vehicle> &GetClass(v8::Isolate *isolate);
        // Classes belong to the runtime (isolate) that built them; drop them when it goes.
        static void UnregisterIsolate(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Vehicle>>> _classes;
    };

    // The LocalPlayer global and the World queries over streamed replicas.
    void RegisterLocalPlayer(v8::Isolate *isolate, v8::Local<v8::Object> global);
    uint64_t LocalPlayerId();
} // namespace Mafia1Online::Scripting
