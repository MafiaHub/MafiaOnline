#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::SDK::Door { struct NativeDoor; enum class State : uint32_t; }
namespace Mafia1Online::Features::World { class WorldService; }
namespace Mafia1Online::Shared::Entities { class DoorEntity; }

namespace Mafia1Online::Features::Door {
    // Mirrors mission C_door actors from server replicas. No native pointer is
    // retained between updates, so mission changes cannot leave stale actors.
    class DoorService final {
      public:
        bool Install();
        void Shutdown();
        void Update(const World::WorldService &world);
        void Reset();
        void OnNativeTransition(SDK::Door::NativeDoor &door, void *activator, SDK::Door::State next);

      private:
        void Apply(Shared::Entities::DoorEntity &state);
        struct Applied {
            uint64_t id = 0;
            uint64_t revision = 0;
        };
        uint64_t _generation = 0;
        std::unordered_map<std::string, Applied> _applied;
        std::unordered_set<std::string> _unresolved;
    };
} // namespace Mafia1Online::Features::Door
