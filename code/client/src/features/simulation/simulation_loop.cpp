#include <utils/safe_win32.h>

#include <psapi.h>

#include "simulation_loop.h"

#include "free_camera.h"
#include "simulation_config.h"

#include "features/pedestrian/pedestrian_service.h"

#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <logging/logger.h>
#include <mafia1/sdk/core/game.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/core/system.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/graphics/native_i3d_driver.h>

#include <chrono>
#include <cstdio>
#include <vector>

namespace Mafia1Online::Features::Simulation {
    namespace {
        using Clock = std::chrono::steady_clock;

        // GameLoopSingle1 caps a frame at 83 ms (GetTimer(16, 83) at 0x5be7d1);
        // a longer stall is simulated as several slower frames, not one jump.
        constexpr uint32_t kMaximumFrameMs = 83;
        // DIK_F1 switches the debug view between full and wireframe.
        constexpr uint8_t kKeyToggleView = 0x3b;
        // DIK_TAB moves the debug camera to the next spawned player.
        constexpr uint8_t kKeyNextPlayer = 0x0f;
        // Retail reloads this x87 control word every frame (0x5bea88):
        // 24-bit precision, round to nearest, all exceptions masked.
        constexpr uint16_t kFpuControlWord = 0x3f;

        void LoadFpuControlWord() {
            uint16_t controlWord = kFpuControlWord;
            __asm fldcw controlWord
        }

        void MissionTick(SDK::Core::Mission::NativeMission *mission, uint32_t frameMs) {
            // Through the installed C_mission::Tick hook, so the application
            // tick runs after the native world, as in the retail loop.
            using Call = void(__thiscall *)(SDK::Core::Mission::NativeMission *, unsigned int);
            reinterpret_cast<Call>(SDK::Core::Mission::kTick)(mission, frameMs);
        }

        // Private bytes: the 32-bit host's address space is the budget the
        // pedestrian pools spend.
        double PrivateMegabytes() {
            PROCESS_MEMORY_COUNTERS_EX counters {};
            counters.cb = sizeof(counters);
            if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters))) {
                return 0.0;
            }
            return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
        }

        void SetWireframe(bool enabled) {
            SDK::Graphics::GetI3DDriver()->SetState(SDK::Graphics::kRenderStateWireframe, enabled ? 1 : 0);
        }

        std::vector<SDK::Player::Vector3> SpawnedPlayerPositions() {
            std::vector<SDK::Player::Vector3> positions;
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
                    if (player->spawned) {
                        positions.push_back({player->position.x, player->position.y, player->position.z});
                    }
                });
            }
            return positions;
        }

        struct Stats {
            Clock::time_point windowStart = Clock::now();
            Clock::time_point lastLog     = Clock::now();
            uint32_t frames               = 0;
            double tickSeconds            = 0.0;
            double renderSeconds          = 0.0;
        };
    } // namespace

    void RunLoop() {
        auto *graph   = SDK::Graphics::GetGraph();
        auto *mission = SDK::Core::Mission::Get();
        auto *game    = mission->Game();
        auto *scene   = mission->GetScene();
        const auto &config = GetConfig();
        const uint32_t frameMs = 1000 / config.tickHz;
        const auto window = static_cast<HWND>(graph->MainWindow());

        RenderMode mode = config.render;
        if (mode == RenderMode::Headless) {
            ShowWindow(window, SW_HIDE);
        }
        SetWireframe(mode == RenderMode::Wireframe);
        bool toggleHeld = false;
        bool nextHeld = false;
        // The host has no player for the native camera to follow, so the
        // view starts at the first player to spawn.
        bool focusedFirstPlayer = false;
        size_t focusIndex = 0;
        FreeCamera camera;
        camera.Reset(*scene);
        Stats stats;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Simulation loop started: {} rendering at {} Hz", RenderModeName(mode), config.tickHz);

        graph->NullTimer();
        while (game->IsInitialized()) {
            graph->ProcessWinMessages();
            const uint32_t elapsedMs = graph->GetTimer(frameMs, kMaximumFrameMs);

            const auto tickStart = Clock::now();
            MissionTick(mission, elapsedMs);
            stats.tickSeconds += std::chrono::duration<double>(Clock::now() - tickStart).count();
            if (game->GetState() == SDK::Core::Game::NativeGame::State::Exit) {
                break;
            }

            if (mode == RenderMode::Headless) {
                SDK::Core::System::TickCacheBaseBlock(elapsedMs);
            }
            else {
                graph->UpdateMouseData();
                graph->UpdateKeyboardData();
                const bool toggle = GetForegroundWindow() == window && graph->TestKey(kKeyToggleView);
                if (toggle && !toggleHeld) {
                    mode = mode == RenderMode::Full ? RenderMode::Wireframe : RenderMode::Full;
                    SetWireframe(mode == RenderMode::Wireframe);
                }
                toggleHeld = toggle;
                const bool next = GetForegroundWindow() == window && graph->TestKey(kKeyNextPlayer);
                if ((next && !nextHeld) || !focusedFirstPlayer) {
                    const auto players = SpawnedPlayerPositions();
                    if (!players.empty()) {
                        focusIndex = focusedFirstPlayer ? (focusIndex + 1) % players.size() : 0;
                        camera.Focus(players[focusIndex]);
                        focusedFirstPlayer = true;
                    }
                }
                nextHeld = next;
                if (GetForegroundWindow() == window) {
                    camera.Update(*graph, static_cast<float>(elapsedMs) / 1000.0f);
                }
                // G_Camera ran inside the mission tick; the debug view wins,
                // and the city cache streams around it.
                camera.Apply(*scene);
                SDK::Core::System::TickCacheBaseBlock(elapsedMs);
                const auto renderStart = Clock::now();
                scene->Render();
                graph->Present();
                stats.renderSeconds += std::chrono::duration<double>(Clock::now() - renderStart).count();
            }
            LoadFpuControlWord();

            ++stats.frames;
            const auto now = Clock::now();
            const double windowSeconds = std::chrono::duration<double>(now - stats.windowStart).count();
            if (windowSeconds >= 0.5) {
                const double rate = stats.frames / windowSeconds;
                const double tickMs = stats.tickSeconds * 1000.0 / stats.frames;
                const double renderMs = stats.renderSeconds * 1000.0 / stats.frames;
                const auto &position = camera.Position();
                const auto pedestrians = Pedestrian::GetHostStats();
                char title[256];
                std::snprintf(title, sizeof(title), "Mafia1Online simulation host | %s (F1) | Tab: next player | %.1f Hz | tick %.2f ms | render %.2f ms | %u walkers, %u generators, %u clusters, %u players | camera %.0f %.0f %.0f",
                    RenderModeName(mode), rate, tickMs, renderMs, pedestrians.walkers, pedestrians.activeGenerators, pedestrians.clusters, pedestrians.observers, position.x, position.y, position.z);
                SetWindowTextA(window, title);
                if (now - stats.lastLog >= std::chrono::seconds(10)) {
                    stats.lastLog = now;
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Simulation loop: {:.1f} Hz, tick {:.2f} ms, render {:.2f} ms ({}); {} walkers from {} generators in {} clusters for {} players; {:.0f} MB private",
                        rate, tickMs, renderMs, RenderModeName(mode), pedestrians.walkers, pedestrians.activeGenerators, pedestrians.clusters, pedestrians.observers, PrivateMegabytes());
                }
                stats.windowStart = now;
                stats.frames = 0;
                stats.tickSeconds = 0.0;
                stats.renderSeconds = 0.0;
            }
        }

        SetWireframe(false);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Simulation loop ended (game state {})", static_cast<uint32_t>(game->GetState()));
    }
} // namespace Mafia1Online::Features::Simulation
