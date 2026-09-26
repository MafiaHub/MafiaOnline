#pragma once

#include <glm/vec3.hpp>

#include <v8.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace Mafia1Online::Core {
    class Server;
}

namespace Mafia1Online::Scripting {
    // The running server. Server::ModuleRegister sets it before any binding
    // is installed, and scripts only run while the server is alive.
    void SetServer(Core::Server *server);
    Core::Server &GetServer();

    // Argument readers shared by every binding. They are static members so a
    // unity build cannot collide per-file helpers.
    class ScriptArgs final {
      public:
        // A Player/Vehicle/Pickup handle, a positive integer Number or a
        // BigInt. Zero when the value is none of them.
        static uint64_t ReadEntityId(v8::Isolate *isolate, v8::Local<v8::Value> value);
        // A finite JS number that fits a float.
        static bool ReadFloat(v8::Local<v8::Value> value, float &out);
        // A nonnegative integer JS number no larger than max.
        static bool ReadUInt(v8::Local<v8::Value> value, uint32_t max, uint32_t &out);
        // A Vector3 or any object with finite numeric x, y and z.
        static bool ReadVector(v8::Isolate *isolate, v8::Local<v8::Value> value, glm::vec3 &out);
        // A position given either as (x, y, z) or as one vector at index.
        // next receives the index after the position.
        static bool ReadPosition(const v8::FunctionCallbackInfo<v8::Value> &info, int index, glm::vec3 &out, int &next);
        // An optional 0xRRGGBB script color, returned in the chat wire format
        // 0xRRGGBBAA.
        static bool ReadColor(v8::Local<v8::Value> value, uint32_t &out);
        static void Throw(v8::Isolate *isolate, const char *message);
    };

    // Wrappers for event payloads and return values. Null when the entity
    // does not exist.
    v8::Local<v8::Value> WrapPlayer(v8::Isolate *isolate, uint64_t networkId);
    v8::Local<v8::Value> WrapVehicle(v8::Isolate *isolate, uint64_t networkId);
    v8::Local<v8::Value> WrapPickup(v8::Isolate *isolate, uint64_t networkId);
    v8::Local<v8::Object> PlainVector(v8::Isolate *isolate, v8::Local<v8::Context> context, const glm::vec3 &value);
    void SetField(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char *key, v8::Local<v8::Value> value);

    using EventArguments = std::vector<v8::Local<v8::Value>>;
    // Emits a reserved server event with the arguments build appends. Does
    // nothing before the scripting engine has started.
    void EmitEvent(const char *name, const std::function<void(v8::Isolate *, v8::Local<v8::Context>, EventArguments &)> &build);
} // namespace Mafia1Online::Scripting
