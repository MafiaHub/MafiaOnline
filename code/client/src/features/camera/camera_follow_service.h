#pragma once

#include <chrono>
#include <cstdint>

namespace Mafia1Online::SDK::Player {
    struct NativeCamera;
}

namespace Mafia1Online::Shared::Player {
    struct CameraView;
}

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Camera {
    class CameraFollowService final {
      public:
        void RegisterRPC();
        bool InstallTickHook();
        void UninstallTickHook();
        void Update(World::WorldService &world);
        bool OnNativeTick(SDK::Player::NativeCamera &camera);
        void OnView(const Shared::Player::CameraView &view);
        float FollowYaw() const { return _smoothedYaw; }
        float FollowPitch() const { return _smoothedPitch; }
        void Reset();

      private:
        bool _followingRemote = false;
        uint64_t _targetId = 0;
        uint64_t _targetSpawnGeneration = 0;
        uint32_t _restorePedestrianMode = 0;
        uint32_t _sourceSequence = 0;
        uint32_t _lastViewSequence = 0;
        float _sourcePitch = 0.5f;
        float _targetPitch = 0.5f;
        float _smoothedPitch = 0.5f;
        float _targetYaw = 0.0f;
        float _smoothedYaw = 0.0f;
        bool _hasView = false;
        std::chrono::steady_clock::time_point _lastViewReceived;
        std::chrono::steady_clock::time_point _lastTick;
        std::chrono::steady_clock::time_point _lastSent;
    };
} // namespace Mafia1Online::Features::Camera
