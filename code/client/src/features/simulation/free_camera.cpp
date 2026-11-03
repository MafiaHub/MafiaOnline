#include "free_camera.h"

#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Simulation {
    namespace {
        // DirectInput scan codes.
        constexpr uint8_t kKeyW        = 0x11;
        constexpr uint8_t kKeyA        = 0x1e;
        constexpr uint8_t kKeyS        = 0x1f;
        constexpr uint8_t kKeyD        = 0x20;
        constexpr uint8_t kKeyQ        = 0x10;
        constexpr uint8_t kKeyE        = 0x12;
        constexpr uint8_t kKeyShift    = 0x2a;
        constexpr uint8_t kKeyControl  = 0x1d;

        constexpr float kSpeed            = 20.0f;
        constexpr float kFastMultiplier   = 5.0f;
        constexpr float kSlowMultiplier   = 0.2f;
        constexpr float kMouseSensitivity = 0.0025f;
        constexpr float kPitchLimit       = 1.55f;
    } // namespace

    void FreeCamera::Reset(SDK::Scene::NativeScene &scene) {
        auto *camera = scene.ActiveCamera();
        _position = camera->WorldPosition();
        const auto forward = camera->WorldDirection();
        _yaw = std::atan2(forward.x, forward.z);
        _pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
    }

    SDK::Player::Vector3 FreeCamera::Forward() const {
        // Mafia is Y-up and left-handed: yaw 0 looks down +Z.
        return {std::sin(_yaw) * std::cos(_pitch), std::sin(_pitch), std::cos(_yaw) * std::cos(_pitch)};
    }

    void FreeCamera::Update(SDK::Graphics::NativeGraph &graph, float seconds) {
        _yaw += static_cast<float>(graph.MouseDeltaX()) * kMouseSensitivity;
        _pitch = std::clamp(_pitch - static_cast<float>(graph.MouseDeltaY()) * kMouseSensitivity, -kPitchLimit, kPitchLimit);

        const auto forward = Forward();
        const SDK::Player::Vector3 right {std::cos(_yaw), 0.0f, -std::sin(_yaw)};
        float speed = kSpeed;
        if (graph.TestKey(kKeyShift)) speed *= kFastMultiplier;
        if (graph.TestKey(kKeyControl)) speed *= kSlowMultiplier;
        const float step = speed * seconds;
        const float along = (graph.TestKey(kKeyW) ? 1.0f : 0.0f) - (graph.TestKey(kKeyS) ? 1.0f : 0.0f);
        const float side = (graph.TestKey(kKeyD) ? 1.0f : 0.0f) - (graph.TestKey(kKeyA) ? 1.0f : 0.0f);
        const float rise = (graph.TestKey(kKeyE) ? 1.0f : 0.0f) - (graph.TestKey(kKeyQ) ? 1.0f : 0.0f);
        _position.x += (forward.x * along + right.x * side) * step;
        _position.y += (forward.y * along + rise) * step;
        _position.z += (forward.z * along + right.z * side) * step;
    }

    void FreeCamera::Focus(const SDK::Player::Vector3 &target) {
        constexpr float kBack = 10.0f;
        constexpr float kUp   = 5.0f;
        _position = {target.x - std::sin(_yaw) * kBack, target.y + kUp, target.z - std::cos(_yaw) * kBack};
        const float dx = target.x - _position.x;
        const float dy = target.y - _position.y;
        const float dz = target.z - _position.z;
        _yaw = std::atan2(dx, dz);
        _pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
    }

    void FreeCamera::Apply(SDK::Scene::NativeScene &scene) const {
        auto *camera = scene.ActiveCamera();
        camera->SetWorldPosition(_position);
        camera->SetDirection(Forward());
        camera->Update();
    }
} // namespace Mafia1Online::Features::Simulation
