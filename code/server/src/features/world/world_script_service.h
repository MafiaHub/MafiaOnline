#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::Shared::Entities {
    class SoundEntity;
    class WorldStateEntity;
    class DoorEntity;
} // namespace Mafia1Online::Shared::Entities
namespace Mafia1Online::Shared::Door { struct DoorIntent; }
namespace Mafia1Online::Features::Player {
    class PlayerService;
}
namespace Mafia1Online::Features::Car {
    class CarService;
}

namespace Mafia1Online::Features::World {
    // A mission change clears the sounds and frame overrides; weather and
    // city music carry over.
    class WorldScriptService final {
      public:
        static constexpr size_t kMaxSounds = 64;

        void Init(Player::PlayerService &players, Car::CarService &cars);
        void Shutdown();
        void ResetForMission(uint64_t missionGeneration);
        void Update();

        Shared::Entities::WorldStateEntity &State() {
            return *_state;
        }

        Shared::Entities::SoundEntity *CreateSound(const std::string &wave, const glm::vec3 &position, float radius, float volume, uint64_t missionGeneration);
        Shared::Entities::SoundEntity *FindSound(uint64_t id) const;
        bool DestroySound(uint64_t id);
        size_t DestroyAllSounds();
        Shared::Entities::DoorEntity *CreateDoor(std::string_view frameName, const glm::vec3 &position, uint64_t missionGeneration);
        Shared::Entities::DoorEntity *FindDoor(std::string_view frameName) const;
        Shared::Entities::DoorEntity *FindDoor(uint64_t id) const;
        bool RemoveDoor(uint64_t id);
        bool ApplyDoorIntent(const Shared::Door::DoorIntent &intent, uint64_t senderGuid, uint64_t missionGeneration);
        static bool ValidDoorName(std::string_view name);

      private:
        Player::PlayerService *_players = nullptr;
        Car::CarService *_cars = nullptr;
        Shared::Entities::WorldStateEntity *_state = nullptr;
        std::unordered_set<uint64_t> _sounds;
        std::unordered_map<std::string, uint64_t> _doors;
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _doorUseAt;
        std::chrono::steady_clock::time_point _semaphoreEpoch;
        std::chrono::steady_clock::time_point _lastSemaphoreSync;
    };
} // namespace Mafia1Online::Features::World
