#include <utils/safe_win32.h>

#include "hud_service.h"

#include "features/script/script_runtime.h"
#include "features/world/world_service.h"
#include "game/entities/native_object_registry.h"

#include <core_modules.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/ui/native_indicators.h>
#include <mafia1/sdk/world/native_environment.h>
#include <networking/replication/network_entity.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Mafia1Online::Features::Hud {
    namespace {
        // CAMERA_SETSWING takes a percentage.
        constexpr float kSwingScale = 0.01f;
        constexpr size_t kMaxText   = 127;

        SDK::UI::NativeIndicators &Indicators() {
            return SDK::UI::NativeIndicators::Get();
        }

        SDK::Core::Game::NativeGame *Game() {
            auto *mission = SDK::Core::Mission::Get();
            return mission ? mission->Game() : nullptr;
        }

        // The HUD font is indexed by the ANSI code page, as the chat is.
        std::string Utf8ToNative(const std::string &utf8) {
            if (utf8.empty()) {
                return {};
            }
            const int wideCount = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            if (wideCount <= 0) {
                return {};
            }
            std::wstring wide(static_cast<size_t>(wideCount), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), wideCount);
            const int nativeCount = WideCharToMultiByte(CP_ACP, 0, wide.data(), wideCount, nullptr, 0, nullptr, nullptr);
            if (nativeCount <= 0) {
                return {};
            }
            std::string native(static_cast<size_t>(nativeCount), '\0');
            WideCharToMultiByte(CP_ACP, 0, wide.data(), wideCount, native.data(), nativeCount, nullptr, nullptr);
            std::replace_if(native.begin(), native.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }, ' ');
            if (native.size() > kMaxText) {
                native.resize(kMaxText);
            }
            return native;
        }
    } // namespace

    bool HudService::Ready() const {
        return _world && _world->IsReady() && Game();
    }

    void HudService::Update(World::WorldService &world) {
        _world = &world;
        if (!world.IsReady()) {
            return;
        }
        if (_countdownRunning && Indicators().TimerGetInterval() == 0) {
            _countdownRunning = false;
            Scripting::EmitEvent("countdownEnd");
        }
        if (_compassFrame && _compassEntity != 0) {
            glm::vec3 target {0.0f};
            bool found        = false;
            const auto handle = world.NativeObjects().FindByNetwork(_compassEntity);
            if (auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(handle)); actor && actor->Frame()) {
                const auto position = actor->Frame()->WorldPosition();
                target              = {position.x, position.y, position.z};
                found               = true;
            }
            else if (auto *entity = Framework::CoreModules::GetReplication()->GetEntityByNetworkID(_compassEntity)) {
                target = entity->position;
                found  = true;
            }
            if (found) {
                _compassFrame->SetWorldPosition({target.x, target.y, target.z});
                _compassFrame->Update();
            }
        }
    }

    void HudService::ShowMessage(const std::string &utf8, uint32_t rgb) {
        const auto text = Utf8ToNative(utf8);
        Indicators().ConsoleAddText(text.c_str(), rgb & 0xffffff);
    }

    void HudService::Announce(const std::string &utf8, float seconds) {
        const auto text = Utf8ToNative(utf8);
        Indicators().RaceFlashText(text.c_str(), seconds);
    }

    void HudService::ShowWatch(uint32_t hours, uint32_t minutes, uint32_t seconds) {
        Indicators().TimerSetTime(hours, minutes, seconds);
        Indicators().AddFlag(SDK::UI::kTimerFlag);
    }

    void HudService::StartCountdown(uint32_t seconds) {
        if (!Indicators().TestFlag(SDK::UI::kTimerFlag)) {
            Indicators().TimerSetTime(0, 0, 0);
            Indicators().AddFlag(SDK::UI::kTimerFlag);
        }
        Indicators().TimerSetInterval(seconds);
        _countdownRunning = seconds > 0;
    }

    std::optional<uint32_t> HudService::Countdown() const {
        if (!_countdownRunning) {
            return std::nullopt;
        }
        return Indicators().TimerGetInterval();
    }

    void HudService::HideWatch() {
        Indicators().TimerSetInterval(0);
        Indicators().RemoveFlag(SDK::UI::kTimerFlag);
        _countdownRunning = false;
    }

    void HudService::SetScore(int32_t score) {
        SDK::World::ScoreSetOn(Game(), true);
        SDK::World::ScoreSet(Game(), score);
    }

    std::optional<int32_t> HudService::Score() const {
        return Ready() ? std::optional<int32_t>(SDK::World::ScoreValue(Game())) : std::nullopt;
    }

    std::optional<bool> HudService::ScoreVisible() const {
        return Ready() ? std::optional<bool>(SDK::World::ScoreVisible(Game())) : std::nullopt;
    }

    void HudService::SetScoreVisible(bool visible) {
        SDK::World::ScoreSetOn(Game(), visible);
    }

    void HudService::HideScore() {
        SetScoreVisible(false);
    }

    bool HudService::SetCompassTarget(const glm::vec3 *position, uint64_t entityId) {
        if (!_compassFrame) {
            _compassFrame = SDK::Scene::GetDriver()->CreateDummy();
            if (!_compassFrame) {
                return false;
            }
            _compassFrame->SetName("mp_compass");
        }
        _compassEntity = entityId;
        if (position) {
            _compassFrame->SetWorldPosition({position->x, position->y, position->z});
            _compassFrame->Update();
        }
        Indicators().CompassSetDestination(_compassFrame);
        Indicators().AddFlag(SDK::UI::kCompassFlag);
        return true;
    }

    void HudService::ClearCompassTarget() {
        ReleaseCompass();
    }

    void HudService::ReleaseCompass() {
        if (!_compassFrame) {
            return;
        }
        Indicators().RemoveFlag(SDK::UI::kCompassFlag);
        Indicators().CompassSetDestination(nullptr);
        _compassFrame->Release();
        _compassFrame  = nullptr;
        _compassEntity = 0;
    }

    void HudService::Fade(bool toColor, uint32_t milliseconds, uint32_t rgb) {
        Indicators().FadeInOut(toColor, milliseconds, rgb & 0xffffff);
    }

    void HudService::SetCameraSwing(float intensity) {
        if (intensity <= 0.0f) {
            ReleaseSwing();
            return;
        }
        if (!_swingFrame) {
            _swingFrame = SDK::Scene::GetDriver()->CreateDummy();
            if (!_swingFrame) {
                return;
            }
            _swingFrame->SetName("mp_swing");
        }
        SDK::World::CameraSetSwing(Game(), true, intensity * kSwingScale, _swingFrame);
    }

    std::optional<float> HudService::CameraFov() const {
        if (!Ready()) {
            return std::nullopt;
        }
        auto *mission = SDK::Core::Mission::Get();
        auto *camera = mission->GetScene() ? mission->GetScene()->ActiveCamera() : nullptr;
        return camera ? std::optional<float>(SDK::Scene::CameraFovRadians(camera) * 180.0f / std::numbers::pi_v<float>) : std::nullopt;
    }

    bool HudService::SetCameraFov(float degrees) {
        if (!Ready()) {
            return false;
        }
        auto *mission = SDK::Core::Mission::Get();
        auto *camera = mission->GetScene() ? mission->GetScene()->ActiveCamera() : nullptr;
        if (!camera) {
            return false;
        }
        Indicators().ParheliaSetFov(camera, degrees * std::numbers::pi_v<float> / 180.0f);
        return true;
    }

    bool HudService::SetCameraRange(float nearClip, float farClip) {
        if (!Ready()) {
            return false;
        }
        auto *mission = SDK::Core::Mission::Get();
        auto *camera = mission->GetScene() ? mission->GetScene()->ActiveCamera() : nullptr;
        if (!camera) {
            return false;
        }
        SDK::Scene::CameraSetRange(camera, nearClip, farClip);
        return true;
    }

    bool HudService::LockCamera(const glm::vec3 &position, const glm::vec3 &direction) {
        if (!Ready()) {
            return false;
        }
        auto *mission = SDK::Core::Mission::Get();
        if (!mission->GetScene() || !mission->GetScene()->ActiveCamera()) {
            return false;
        }
        const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (!(length > 0.0001f) || !std::isfinite(length)) {
            return false;
        }
        auto *game = Game();
        game->Camera().LockAt({position.x, position.y, position.z}, {direction.x / length, direction.y / length, direction.z / length});
        if (game->StartupTickCount() < 6) {
            game->SetCameraRotRepair();
        }
        _cameraLocked = true;
        return true;
    }

    bool HudService::UnlockCamera() {
        if (!Ready()) {
            return false;
        }
        auto *game = Game();
        game->Camera().Unlock();
        game->RecomputeLightCache();
        _cameraLocked = false;
        return true;
    }

    void HudService::ReleaseSwing() {
        if (!_swingFrame) {
            return;
        }
        if (auto *game = Game()) {
            // Only a disable with a target straightens the backdrop sector;
            // the second call drops the camera's raw pointer to our frame.
            SDK::World::CameraSetSwing(game, false, 0.0f, _swingFrame);
            SDK::World::CameraSetSwing(game, false, 0.0f, nullptr);
        }
        _swingFrame->Release();
        _swingFrame = nullptr;
    }

    void HudService::OnMissionClosing() {
        if (_cameraLocked) {
            if (auto *game = Game()) {
                game->Camera().Unlock();
                game->RecomputeLightCache();
            }
            _cameraLocked = false;
        }
        ReleaseCompass();
        ReleaseSwing();
        _countdownRunning = false;
    }

    void HudService::Reset() {
        OnMissionClosing();
    }
} // namespace Mafia1Online::Features::Hud
