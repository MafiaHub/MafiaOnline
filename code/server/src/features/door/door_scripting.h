#pragma once

#include "shared/features/door/door_entity.h"

#include <scripting/builtins/entity.h>
#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace Mafia1Online::Scripting {
    class Door final: public Framework::Scripting::Builtins::Entity {
      public:
        explicit Door(uint64_t id): Framework::Scripting::Builtins::Entity(id) {}

        Shared::Entities::DoorEntity *Resolve() const;
        std::string GetName() const;
        bool GetOpen() const;
        bool GetLocked() const;
        void SetLocked(bool locked);
        bool GetEnabled() const;
        void SetEnabled(bool enabled);
        bool GetReverse() const;
        double GetOpenFraction() const;
        bool Open(bool reverse, bool pairedReverse);
        bool Close();
        bool SetOpenFraction(double fraction, bool reverse, bool pairedReverse);
        bool Destroy();
        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<Door> &GetClass(v8::Isolate *isolate);

      private:
        static std::unique_ptr<v8pp::class_<Door>> _class;
    };
    v8::Local<v8::Value> WrapDoor(v8::Isolate *isolate, uint64_t id);
} // namespace Mafia1Online::Scripting
