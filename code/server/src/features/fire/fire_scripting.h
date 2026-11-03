#pragma once

#include "shared/features/fire/fire_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Mafia1Online::Core {
    class Server;
}

namespace Mafia1Online::Scripting {
    // A script fire that burns until water or a script puts it out.
    class Fire final: public Framework::Scripting::Builtins::Entity {
      public:
        explicit Fire(uint64_t id): Framework::Scripting::Builtins::Entity(id) {}

        Shared::Entities::FireEntity *ResolveFire() const;
        double GetHeight() const;
        bool GetBurning() const;
        bool GetDousing() const;
        double GetMissionGeneration() const;
        bool Extinguish();
        bool Destroy();
        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<Fire> &GetClass(v8::Isolate *isolate);
        static void UnregisterIsolate(v8::Isolate *isolate);

      private:
        static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Fire>>> _classes;
    };

    // A fire hose or water cannon held by a player or mounted on a vehicle.
    class WaterJet final: public Framework::Scripting::Builtins::Entity {
      public:
        explicit WaterJet(uint64_t id): Framework::Scripting::Builtins::Entity(id) {}

        Shared::Entities::WaterJetEntity *ResolveJet() const;
        bool GetActive() const;
        bool Stop();
        bool Destroy();
        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<WaterJet> &GetClass(v8::Isolate *isolate);
        static void UnregisterIsolate(v8::Isolate *isolate);

      private:
        static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<WaterJet>>> _classes;
    };

    v8::Local<v8::Value> WrapFire(v8::Isolate *isolate, uint64_t id);
    v8::Local<v8::Value> WrapWaterJet(v8::Isolate *isolate, uint64_t id);

    // Raises fireExtinguished from the fire service.
    void InstallFireEvents(Core::Server &server);
} // namespace Mafia1Online::Scripting
