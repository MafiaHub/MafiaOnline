#include <utils/safe_win32.h>

#include "debug_overlay.h"

#include "features/pedestrian/pedestrian_service.h"

#include <MinHook.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/traffic/native_traffic.h>
#include <mafia1/sdk/ui/native_hud.h>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Simulation {
    namespace {
        // cbScene (0x5bd8d0) is stdcall, retn 0Ch. Message 2 is the HUD
        // stage: inventory, indicators, menus and console.
        using SceneCallback = uint32_t(__stdcall *)(uint32_t, uint32_t, uint32_t);
        SceneCallback gSceneCallbackOriginal = nullptr;
        constexpr uint32_t kHudStage = 2;

        constexpr uint32_t kWalkerColor          = 0xff40e040;
        constexpr uint32_t kPlayerColor          = 0xffff40ff;
        constexpr uint32_t kActiveGeneratorColor = 0xffffd020;
        constexpr uint32_t kIdleGeneratorColor   = 0xff808080;

        void Marker(SDK::Scene::NativeScene &scene, const SDK::Player::Vector3 &world, const SDK::Player::Vector3 &camera, float height, float scale, uint32_t color) {
            const SDK::Player::Vector3 point {world.x, world.y + height, world.z};
            float x = 0.0f;
            float y = 0.0f;
            if (!scene.ProjectToScreen(point, x, y)) {
                return;
            }
            auto *graph = SDK::Graphics::GetGraph();
            if (x < 0.0f || y < 0.0f || x > static_cast<float>(graph->ScreenWidth()) || y > static_cast<float>(graph->ScreenHeight())) {
                return;
            }
            const float dx = point.x - camera.x;
            const float dy = point.y - camera.y;
            const float dz = point.z - camera.z;
            const float size = std::clamp(scale / std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 1.0f), 3.0f, 14.0f);
            SDK::Graphics::FillRect(x - size * 0.5f, y - size * 0.5f, size, size, color);
        }

        void DrawMarkers() {
            auto *mission = SDK::Core::Mission::Get();
            auto &scene = *mission->GetScene();
            const auto camera = scene.ActiveCamera()->WorldPosition();
            const auto *pedestrians = Pedestrian::GetService();

            mission->ForEachActor([&](SDK::Player::NativeActor *actor) {
                if (static_cast<uint32_t>(actor->GetType()) != SDK::Traffic::kGeneratorActorType) {
                    return;
                }
                auto *generator = reinterpret_cast<SDK::Traffic::NativeTrafficGenerator *>(actor);
                const bool active = pedestrians && pedestrians->IsSelected(generator);
                Marker(scene, generator->WorldPosition(), camera, 3.0f, 600.0f, active ? kActiveGeneratorColor : kIdleGeneratorColor);
            });
            for (auto **element = SDK::Traffic::ElementsBegin(); element != SDK::Traffic::ElementsEnd(); ++element) {
                if ((*element)->IsActive()) {
                    Marker(scene, (*element)->Position(), camera, 2.2f, 250.0f, kWalkerColor);
                }
            }
            if (pedestrians) {
                for (const auto &observer : pedestrians->Observers()) {
                    Marker(scene, observer.position, camera, 2.4f, 400.0f, kPlayerColor);
                }
            }
        }

        uint32_t __stdcall SceneCallbackHook(uint32_t message, uint32_t arg1, uint32_t arg2) {
            if (message != kHudStage) {
                return gSceneCallbackOriginal(message, arg1, arg2);
            }
            if (SDK::Core::Mission::Get()->Game()->IsInitialized()) {
                DrawMarkers();
            }
            return 0;
        }
    } // namespace

    bool InstallDebugOverlay() {
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::UI::kSceneCallback), reinterpret_cast<void *>(&SceneCallbackHook), reinterpret_cast<void **>(&gSceneCallbackOriginal)) != MH_OK) {
            return false;
        }
        if (MH_EnableHook(reinterpret_cast<void *>(SDK::UI::kSceneCallback)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::UI::kSceneCallback));
            return false;
        }
        return true;
    }

    void UninstallDebugOverlay() {
        MH_DisableHook(reinterpret_cast<void *>(SDK::UI::kSceneCallback));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::UI::kSceneCallback));
    }
} // namespace Mafia1Online::Features::Simulation
