#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Hud {
    // Script control of the retail HUD: console lines, flash text, the watch
    // and countdown, the freeride score, the compass, the screen fade and the
    // camera effects and projection. The game resets all of them at C_game::Done and Init, so
    // nothing carries over a mission change.
    class HudService final {
      public:
        void Update(World::WorldService &world);
        // Before C_game::Done: the compass and swing keep raw pointers to
        // frames this service owns.
        void OnMissionClosing();
        void Reset();

        bool Ready() const;
        void ShowMessage(const std::string &utf8, uint32_t rgb);
        void Announce(const std::string &utf8, float seconds);
        void ShowWatch(uint32_t hours, uint32_t minutes, uint32_t seconds);
        void StartCountdown(uint32_t seconds);
        std::optional<uint32_t> Countdown() const;
        void HideWatch();
        void SetScore(int32_t score);
        std::optional<int32_t> Score() const;
        std::optional<bool> ScoreVisible() const;
        void SetScoreVisible(bool visible);
        void HideScore();
        // A fixed point, or the replicated player or vehicle to follow.
        bool SetCompassTarget(const glm::vec3 *position, uint64_t entityId);
        void ClearCompassTarget();
        void Fade(bool toColor, uint32_t milliseconds, uint32_t rgb);
        void SetCameraSwing(float intensity);
        std::optional<float> CameraFov() const;
        bool SetCameraFov(float degrees);
        bool SetCameraRange(float nearClip, float farClip);
        bool LockCamera(const glm::vec3 &position, const glm::vec3 &direction);
        bool UnlockCamera();

      private:
        void ReleaseCompass();
        void ReleaseSwing();

        World::WorldService *_world = nullptr;
        SDK::Scene::NativeFrame *_compassFrame = nullptr;
        uint64_t _compassEntity                = 0;
        SDK::Scene::NativeFrame *_swingFrame   = nullptr;
        bool _cameraLocked                     = false;
        bool _countdownRunning                 = false;
    };
} // namespace Mafia1Online::Features::Hud
