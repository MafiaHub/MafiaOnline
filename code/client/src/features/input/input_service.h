#pragma once

#include <input/button_state.h>
#include <input/input.h>

namespace Mafia1Online::Features::Input {
    // The existing IGraph reader bypasses the web UI's native query filter.
    // The adapter publishes that state; the application supplies capture policy.
    class InputService final: public Framework::Input::IInput {
      public:
        void Update() override;
        void SetReady(bool ready) {
            _ready = ready;
        }
        bool IsAvailable() const override;
        bool IsKeyDown(int key) const override;
        bool IsKeyUp(int key) const override {
            return key >= 0 && key < 256 && IsAvailable() && !IsKeyDown(key);
        }
        bool IsKeyPressed(int key) const override {
            return IsAvailable() && _keys.IsPressed(key);
        }
        bool IsKeyReleased(int key) const override {
            return IsAvailable() && _keys.IsReleased(key);
        }
        bool IsMouseButtonDown(int button) const override;
        bool IsMouseButtonUp(int button) const override {
            return button >= 0 && button < 3 && IsAvailable() && !IsMouseButtonDown(button);
        }
        bool IsMouseButtonPressed(int button) const override;
        bool IsMouseButtonReleased(int button) const override;
        void GetMousePosition(int &x, int &y) const override;
        void SetMousePosition(int x, int y) override;
        void SetMouseVisible(bool) override {}
        bool IsMouseVisible() const override {
            return false;
        }
        void SetMouseLocked(bool) override {}
        bool IsMouseLocked() const override {
            return false;
        }
        void SetInputLocked(bool locked) override {
            _locked = locked;
        }
        bool IsInputLocked() const override {
            return _locked;
        }

      private:
        bool ReadKeyDown(int key) const;
        Framework::Input::KeySnapshot _keys;
        bool _ready  = false;
        bool _locked = false;
    };
} // namespace Mafia1Online::Features::Input
