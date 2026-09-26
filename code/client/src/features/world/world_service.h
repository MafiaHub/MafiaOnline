#pragma once

#include <functional>

#include "game/entities/native_object_registry.h"

#include <cstdint>
#include <string>

namespace Mafia1Online::Features::World {
    class WorldService final {
      public:
        void Update();
        void Reset();
        void NotifyMissionClosing();
        // Runs first in NotifyMissionClosing, while native systems are alive.
        void SetMissionClosingCallback(std::function<void()> callback) {
            _missionClosing = std::move(callback);
        }
        bool RequestEnterGame();
        void RunRequestedMission();
        void ExitGameLoop();

        bool IsReady() const {
            return _loadedMissionGeneration != 0;
        }

        uint64_t LoadedMissionGeneration() const {
            return _loadedMissionGeneration;
        }

        Game::Entities::NativeObjectRegistry &NativeObjects() {
            return _nativeObjects;
        }

        uint64_t Generation() const {
            return _generation;
        }

        const std::string &SelectedMission() const {
            return _selectedMission;
        }

        uint64_t SelectedMissionGeneration() const {
            return _selectedMissionGeneration;
        }

      private:
        void ReportLoadState(uint64_t generation, uint8_t state);

        Game::Entities::NativeObjectRegistry _nativeObjects;
        uint64_t _generation = 0;
        std::string _selectedMission;
        uint64_t _selectedMissionGeneration = 0;
        std::string _rejectedMission;
        uint64_t _rejectedMissionGeneration = 0;
        bool _hasRejectedMission            = false;
        uint64_t _loadedMissionGeneration = 0;
        bool _enterRequested = false;
        bool _closingMenu = false;
        bool _inGameLoop = false;
        std::function<void()> _missionClosing;
    };
} // namespace Mafia1Online::Features::World
