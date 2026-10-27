#pragma once

#include <scripting/builtins/entity.h>
#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>

namespace Mafia1Online::Shared::Entities { class DoorEntity; }

namespace Mafia1Online::Scripting {
    class Door final: public Framework::Scripting::Builtins::Entity {
      public:
        explicit Door(uint64_t networkId);
        Shared::Entities::DoorEntity *ResolveDoor() const;
        Framework::Scripting::Builtins::Vector3 GetDoorPosition() const;
        std::string GetFrameName() const;
        bool GetOpen() const;
        bool GetLocked() const;
        bool GetEnabled() const;
        bool GetReverse() const;
        double GetOpenFraction() const;
        double GetMissionGeneration() const;
        std::string ToString() const override;
        static v8pp::class_<Door> &GetClass(v8::Isolate *isolate);
        // Classes belong to the runtime (isolate) that built them; drop them when it goes.
        static void UnregisterIsolate(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
      private:
        static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Door>>> _classes;
    };
    v8::Local<v8::Value> WrapDoor(v8::Isolate *isolate, uint64_t id);
} // namespace Mafia1Online::Scripting
