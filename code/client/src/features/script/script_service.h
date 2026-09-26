#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Script {
    // Raises the native client events from the replicated state, after the
    // other services have applied it for the frame.
    class ScriptService final {
      public:
        void Update(World::WorldService &world);
        // Runs first in the mission-closing callback, while the scene lives.
        void OnMissionClosing();
        void Reset();

      private:
        struct PlayerState {
            uint64_t spawnGeneration = 0;
            bool alive               = false;
        };
        struct PickupSnapshot {
            uint64_t id = 0;
            uint64_t missionGeneration = 0;
            uint8_t weaponId = 0;
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float heading = 0.0f;
            uint16_t loaded = 0;
            uint16_t reserve = 0;
        };
        static void EmitPickupStreamOut(const PickupSnapshot &snapshot);
        struct DoorSnapshot {
            uint64_t id = 0;
            uint64_t missionGeneration = 0;
            uint64_t revision = 0;
            std::string frameName;
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            bool open = false;
            bool locked = false;
            bool enabled = true;
            bool reverse = false;
            float openFraction = 0.0f;
        };
        static void EmitDoorStreamOut(const DoorSnapshot &snapshot);
        std::unordered_map<uint64_t, PlayerState> _players;
        std::unordered_map<uint64_t, PickupSnapshot> _pickups;
        std::unordered_map<uint64_t, DoorSnapshot> _doors;
        uint64_t _readyGeneration = 0;
        std::string _readyMission;
    };
} // namespace Mafia1Online::Features::Script
