#pragma once

#include <glm/vec3.hpp>
#include <v8.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace Framework::Scripting {
    class Engine;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Scripting {
    using EventArguments = std::vector<v8::Local<v8::Value>>;

    // Runs build and dispatches the event to every resource's Events.on
    // handlers; a no-op while no client scripting runtime is running.
    void EmitEvent(const char *name, const std::function<void(v8::Isolate *, v8::Local<v8::Context>, EventArguments &)> &build = {});

    namespace Args {
        // A Vector3 or any { x, y, z } object with finite members.
        bool ReadPosition(v8::Isolate *isolate, v8::Local<v8::Value> value, glm::vec3 &out);
        bool ReadFloat(v8::Local<v8::Value> value, float &out);
        bool OptionalFloat(v8::Isolate *isolate, v8::Local<v8::Value> options, const char *name, float minimum, float maximum, float &out);
        void Throw(v8::Isolate *isolate, const char *message);
        v8::Local<v8::Value> Position(v8::Isolate *isolate, const glm::vec3 &position);
    } // namespace Args

    v8::Local<v8::Value> WrapPlayer(v8::Isolate *isolate, uint64_t networkId);
    v8::Local<v8::Value> WrapVehicle(v8::Isolate *isolate, uint64_t networkId);
    v8::Local<v8::Value> WrapPickup(v8::Isolate *isolate, uint64_t networkId);
    Features::World::WorldService &GetWorld();

    void RegisterClientScripting(Framework::Scripting::Engine *engine);
} // namespace Mafia1Online::Scripting
