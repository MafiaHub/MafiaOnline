#pragma once

#include "shared/features/map_object/map_object_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Mafia1Online::Scripting {
    // A script-placed world object (SA-MP's CreateObject/MoveObject).
    class MapObject final: public Framework::Scripting::Builtins::Entity {
      public:
        explicit MapObject(uint64_t id): Framework::Scripting::Builtins::Entity(id) {}

        Shared::Entities::MapObjectEntity *ResolveObject() const;
        std::string GetModel() const;
        bool GetCollision() const;
        bool GetMoving() const;
        double GetMissionGeneration() const;
        bool Stop();
        bool Destroy();
        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<MapObject> &GetClass(v8::Isolate *isolate);
        // Classes belong to the runtime (isolate) that built them; drop them when it goes.
        static void UnregisterIsolate(v8::Isolate *isolate);

      private:
        static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<MapObject>>> _classes;
    };
    v8::Local<v8::Value> WrapMapObject(v8::Isolate *isolate, uint64_t id);
} // namespace Mafia1Online::Scripting
