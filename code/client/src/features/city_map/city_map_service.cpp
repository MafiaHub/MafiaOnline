#include <utils/safe_win32.h>

#include "city_map_service.h"

#include "features/world/world_service.h"
#include "map_image.h"

#include <MinHook.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/data_file.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/ui/native_indicators.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string_view>
#include <vector>

namespace Mafia1Online::Features::CityMap {
    namespace {
        namespace Map       = SDK::UI::Map;
        using NativeTexture = SDK::Graphics::NativeTexture;

        // reM G_IndicatorsClass::Tick, thiscall, ret 4. It creates
        // m_pMapTexture from "0mapar.bmp" (0x639310) at 0x602812 when the
        // map is enabled and the pointer is null.
        using IndicatorsTick                  = void(__thiscall *)(void *, uint32_t);
        constexpr uintptr_t kIndicatorsTick   = 0x6027b0;
        constexpr uintptr_t kMapTextureOffset = 0x4090;
        constexpr const char *kImagePath      = "maps\\fullmap.bmp";
        constexpr auto kCountrysideMissions   = std::to_array<std::string_view>({"freekrajina", "freekrajinanoc", "freeride_extended"});

        // DrawAll instructions `D8 /r [disp32]` (fadd, fmul, fsub on a float)
        // whose disp32 at +2 names a retail .rdata constant; they are pointed
        // at gDraw instead. Every listed constant except 0x623b08 is read by
        // DrawAll alone; 0x623b08 (0.55) is shared with dog, traffic and
        // pursuit AI, which is why it is redirected rather than overwritten.
        struct OperandPatch {
            uintptr_t instruction;
            size_t field;
            uint32_t original;
        };
        constexpr auto kOperands              = std::to_array<OperandPatch>({
            {0x5ff585, offsetof(Map::DrawConstants, uScale), 0x627928},
            {0x5ff5a1, offsetof(Map::DrawConstants, vScale), 0x627924},
            {0x5ff5ad, offsetof(Map::DrawConstants, uBase), 0x627920},
            {0x5ff5bf, offsetof(Map::DrawConstants, vBase), 0x62791c},
            {0x5ff759, offsetof(Map::DrawConstants, uScale), 0x627928},
            {0x5ff778, offsetof(Map::DrawConstants, vScale), 0x627924},
            {0x5ff784, offsetof(Map::DrawConstants, uBase), 0x627920},
            {0x5ff796, offsetof(Map::DrawConstants, vBase), 0x62791c},
            {0x5ff7d2, offsetof(Map::DrawConstants, halfWindowV), 0x627918},
            {0x5ff833, offsetof(Map::DrawConstants, windowV), 0x623b08},
            {0x5ff9b2, offsetof(Map::DrawConstants, windowV), 0x623b08},
            {0x5ffdad, offsetof(Map::DrawConstants, halfWindowV), 0x627918},
            {0x5ffdcb, offsetof(Map::DrawConstants, playerOffsetScale), 0x627910},
            {0x600174, offsetof(Map::DrawConstants, halfWindowV), 0x627918},
            {0x60018c, offsetof(Map::DrawConstants, destinationScale), 0x627908},
        });
        constexpr uintptr_t kOperandSpanBegin = 0x5ff585;
        constexpr uintptr_t kOperandSpanEnd   = 0x600192;
        // `mov dword [esp+1Ch], 3EE66666h` at 0x5ff848: the lowest window
        // origin, 1 - 0.55. An immediate, so each projection writes it.
        constexpr uintptr_t kMaxWindowVImmediate = 0x5ff84c;

        // What DrawAll reads once the operands point here. Lives as long as
        // the client DLL; Uninstall points the operands back first.
        Map::DrawConstants gDraw     = Map::kRetailDraw;
        Map::Projection gProjection  = Map::kRetail;
        World::WorldService *gWorld  = nullptr;
        IndicatorsTick gTickOriginal = nullptr;
        bool gOperandsPatched        = false;

        auto Log() {
            return Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT);
        }

        // The span is in the verified image's .text; VirtualProtect is the
        // only thing here that can fail.
        bool WriteCode(uintptr_t begin, uintptr_t end, const std::function<void()> &write) {
            DWORD previous = 0;
            if (!VirtualProtect(reinterpret_cast<void *>(begin), end - begin, PAGE_EXECUTE_READWRITE, &previous)) {
                return false;
            }
            write();
            VirtualProtect(reinterpret_cast<void *>(begin), end - begin, previous, &previous);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(begin), end - begin);
            return true;
        }

        bool PointOperands(bool atClient) {
            return WriteCode(kOperandSpanBegin, kOperandSpanEnd, [atClient] {
                for (const auto &patch : kOperands) {
                    const uint32_t target = atClient ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gDraw) + patch.field) : patch.original;
                    std::memcpy(reinterpret_cast<void *>(patch.instruction + 2), &target, sizeof(target));
                }
            });
        }

        bool Apply(const Map::Projection &projection, const Map::DrawConstants &constants) {
            gDraw       = constants;
            gProjection = projection;
            return WriteCode(kMaxWindowVImmediate, kMaxWindowVImmediate + sizeof(float), [&constants] {
                std::memcpy(reinterpret_cast<void *>(kMaxWindowVImmediate), &constants.maxWindowV, sizeof(float));
            });
        }

        NativeTexture *&MapTexture() {
            return *reinterpret_cast<NativeTexture **>(SDK::UI::kIndicators + kMapTextureOffset);
        }

        bool IsCountrysideMission() {
            if (!gWorld->IsReady() || gWorld->SelectedMissionGeneration() != gWorld->LoadedMissionGeneration()) {
                return false;
            }
            const std::string_view mission = gWorld->SelectedMission();
            return std::any_of(kCountrysideMissions.begin(), kCountrysideMissions.end(), [mission](std::string_view name) {
                return std::equal(name.begin(), name.end(), mission.begin(), mission.end(), [](char a, char b) {
                    return a == std::tolower(static_cast<unsigned char>(b));
                });
            });
        }

        // Read through rw_data, so a server asset ZIP's maps/fullmap.bmp wins;
        // decoded and scaled here and handed to LS3D from memory, which keeps
        // LS3D from resampling it to 4096 x 4096 and from writing a presaved
        // fullmap_1.565 next to it.
        NativeTexture *LoadCountrysideTexture() {
            std::vector<uint8_t> file;
            if (!SDK::Core::DataFile::ReadAll(kImagePath, file)) {
                Log()->info("City map: no {} in this session; '{}' keeps the retail city map", kImagePath, gWorld->SelectedMission());
                return nullptr;
            }
            const auto view = ParseBitmap(file);
            if (!view) {
                Log()->warn("City map: {} is not an uncompressed 24 or 32 bit bitmap; keeping the retail city map", kImagePath);
                return nullptr;
            }
            const uint32_t width        = TextureSide(view->width);
            const uint32_t height       = TextureSide(view->height);
            std::vector<uint8_t> bitmap = BuildMemoryBitmap(*view, width, height);
            // The 24 MB file is not needed while LS3D copies the bitmap.
            file.clear();
            file.shrink_to_fit();
            NativeTexture *texture = nullptr;
            auto *graph            = SDK::Graphics::GetGraph();
            if (graph->vtable->createTexture(graph, bitmap.data(), nullptr, SDK::Graphics::kTextureFromMemory, &texture) < 0 || !texture) {
                Log()->warn("City map: LS3D could not create the {}x{} countryside map texture; keeping the retail city map", width, height);
                return nullptr;
            }
            Log()->info("City map: countryside map {}x{} (from {}x{}) for '{}'", width, height, view->width, view->height, gWorld->SelectedMission());
            return texture;
        }

        // Runs while Tick is about to create the mission's map texture, so
        // the texture and the projection DrawAll uses change together, and
        // only while no map texture is held (GameDone releases it).
        void BeforeTick() {
            if (!SDK::UI::NativeIndicators::Get().MapEnabled() || MapTexture()) {
                return;
            }
            if (IsCountrysideMission()) {
                if (auto *texture = LoadCountrysideTexture()) {
                    MapTexture() = texture;
                    if (!Apply(Map::kCountryside, Map::Derive(Map::kCountryside))) {
                        Log()->error("City map: could not write the countryside window clamp at {:#x}", kMaxWindowVImmediate);
                    }
                    return;
                }
            }
            if (!Apply(Map::kRetail, Map::kRetailDraw)) {
                Log()->error("City map: could not restore the retail window clamp at {:#x}", kMaxWindowVImmediate);
            }
        }

        void __fastcall TickHook(void *indicators, void *, uint32_t frameTimeMs) {
            BeforeTick();
            gTickOriginal(indicators, frameTimeMs);
        }
    } // namespace

    bool CityMapService::Install(World::WorldService &world) {
        gWorld      = &world;
        gDraw       = Map::kRetailDraw;
        gProjection = Map::kRetail;
        if (!PointOperands(true)) {
            return false;
        }
        gOperandsPatched = true;
        if (MH_CreateHook(reinterpret_cast<void *>(kIndicatorsTick), reinterpret_cast<void *>(&TickHook), reinterpret_cast<void **>(&gTickOriginal)) != MH_OK || MH_EnableHook(reinterpret_cast<void *>(kIndicatorsTick)) != MH_OK) {
            Uninstall();
            return false;
        }
        return true;
    }

    void CityMapService::Uninstall() {
        MH_DisableHook(reinterpret_cast<void *>(kIndicatorsTick));
        MH_RemoveHook(reinterpret_cast<void *>(kIndicatorsTick));
        gTickOriginal = nullptr;
        if (gOperandsPatched) {
            Apply(Map::kRetail, Map::kRetailDraw);
            PointOperands(false);
            gOperandsPatched = false;
        }
        gWorld = nullptr;
    }

    const Map::Projection &ActiveProjection() {
        return gProjection;
    }
} // namespace Mafia1Online::Features::CityMap
