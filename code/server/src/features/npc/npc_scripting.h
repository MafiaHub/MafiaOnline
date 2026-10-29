#pragma once

#include "shared/features/npc/npc_entity.h"

#include <scripting/builtins/entity.h>
#include <scripting/builtins/vector3.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Mafia1Online::Scripting {
    // Server scripting handle for one NPC human the simulation host runs, of
    // any kind. The host decides its AI, aim, shots and death; scripts read
    // the state and its inventory.
    class Npc final: public Framework::Scripting::Builtins::Entity {
      public:
        Npc(uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        Shared::Entities::NpcEntity *ResolveNpc() const;

        std::string GetKind() const;
        std::string GetModel() const;
        uint32_t GetVoice() const;
        Framework::Scripting::Builtins::Vector3 GetNpcPosition() const;
        double GetHeading() const;
        bool GetAlive() const;
        double GetHealth() const;
        uint32_t GetWeapon() const;
        v8::Local<v8::Value> GetInventory(v8::Isolate *isolate) const;
        std::string GetBehaviour() const;
        std::string GetAnimation() const;
        bool Remove();
        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<Npc> &GetClass(v8::Isolate *isolate);
        // Classes belong to the runtime (isolate) that built them; drop them when it goes.
        static void UnregisterIsolate(v8::Isolate *isolate);

      private:
        static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Npc>>> _classes;
    };
} // namespace Mafia1Online::Scripting
