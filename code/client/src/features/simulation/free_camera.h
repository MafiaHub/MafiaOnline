#pragma once

#include <mafia1/sdk/player/native_actor.h>

namespace Mafia1Online::SDK::Graphics {
    struct NativeGraph;
}
namespace Mafia1Online::SDK::Scene {
    struct NativeScene;
}

namespace Mafia1Online::Features::Simulation {
    // Debug fly camera of the simulation host. It owns the active scene
    // camera between the mission tick and the render; nothing in the native
    // simulation reads it, so moving it never changes what the host simulates.
    class FreeCamera final {
      public:
        // Starts from the active camera's current pose.
        void Reset(SDK::Scene::NativeScene &scene);
        // Reads the game's DirectInput keyboard and mouse, refreshed by the
        // loop. WASD move, Q/E down/up, Shift faster, Ctrl slower, mouse looks.
        // Tab (handled by the loop) jumps to the next player.
        void Update(SDK::Graphics::NativeGraph &graph, float seconds);
        void Apply(SDK::Scene::NativeScene &scene) const;
        // Moves behind and above target along the current heading and looks
        // at it.
        void Focus(const SDK::Player::Vector3 &target);

        const SDK::Player::Vector3 &Position() const {
            return _position;
        }

      private:
        SDK::Player::Vector3 Forward() const;

        SDK::Player::Vector3 _position {};
        float _yaw   = 0.0f;
        float _pitch = 0.0f;
    };
} // namespace Mafia1Online::Features::Simulation
