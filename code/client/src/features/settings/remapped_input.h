#pragma once

#include <input/polling_input.h>

#include <array>
#include <cstdint>

namespace Mafia1Online::Features::Settings {
    // The input scripted Key.bind polls (Instance::GetBaseInput). A script
    // binds the key it was written for; when the player moves that key, a
    // read of it answers for the key the player chose instead. Held state and
    // the press edges both come from ReadKeyDown, so binds, isDown and the
    // edge detector all follow the remap.
    class RemappedInput final: public Framework::Input::PollingInput {
      public:
        RemappedInput() {
            ResetMap();
        }

        void ResetMap() {
            for (size_t key = 0; key < _map.size(); ++key) {
                _map[key] = static_cast<uint8_t>(key);
            }
        }

        // logical: the virtual key scripts bind; physical: the one to read.
        void Map(uint8_t logical, uint8_t physical) {
            _map[logical] = physical;
        }

      protected:
        bool ReadKeyDown(int key) const override {
            return PollingInput::ReadKeyDown(key >= 0 && key < static_cast<int>(_map.size()) ? _map[key] : key);
        }

      private:
        std::array<uint8_t, 256> _map {};
    };
} // namespace Mafia1Online::Features::Settings
