#pragma once

#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/ui/map_projection.h>
#include <mafia1/sdk/ui/native_hud.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::UI::Map {
    // The held-TAB city map, as G_IndicatorsClass::DrawAll (0x5fb060) draws
    // it: a window of the map texture centred on the followed car or the
    // active camera, stretched over 80% x 74% of the screen. Which texture
    // and projection (map_projection.h) is the client's choice per mission.
    inline constexpr uint32_t kMapFlag = 0x2000;
    // A point inside the window has |rel| <= kEdge on both axes.
    inline constexpr float kEdge = 0.5f;

    // Screen placement of the window for one frame.
    struct View {
        Projection projection = kRetail;
        float windowU = 0.0f;
        float windowV = 0.0f;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float extentX = 0.0f;
        float extentY = 0.0f;
        float scale   = 1.0f;
    };

    // Map-relative position: within [-kEdge, kEdge] on both axes when the
    // point lies inside the shown window.
    struct Relative {
        float u = 0.0f;
        float v = 0.0f;
    };

    // True while the player holds TAB with the map enabled (C_player sets
    // the flag, MapEnable the switch at +0x46ec).
    inline bool IsShown() {
        const auto *indicators = reinterpret_cast<const std::byte *>(&Indicators());
        return (Indicators()._flags & kMapFlag) != 0 && *reinterpret_cast<const bool *>(indicators + 0x46ec);
    }

    // The window DrawAll shows this frame in the given projection; false
    // without a mission camera.
    inline bool CurrentView(View &out, const Projection &projection) {
        auto *mission = Core::Mission::Get();
        if (!mission || !mission->Game() || !mission->GetScene()) return false;
        Player::Vector3 position {};
        if (auto *car = mission->Game()->Camera().Car()) {
            position = car->Position();
        }
        else if (auto *camera = mission->GetScene()->ActiveCamera()) {
            position = camera->WorldPosition();
        }
        else {
            return false;
        }
        const Point origin = WindowOrigin(projection, ToTexture(projection, position.x, position.z));
        out.projection = projection;
        out.windowU    = origin.u;
        out.windowV    = origin.v;
        // m_MenuScreenSize at +0x4b78 and m_MenuOffset at +0x4b80.
        const auto *indicators = reinterpret_cast<const std::byte *>(&Indicators());
        const float screenW = *reinterpret_cast<const float *>(indicators + 0x4b78);
        const float screenH = *reinterpret_cast<const float *>(indicators + 0x4b7c);
        const float offsetX = *reinterpret_cast<const float *>(indicators + 0x4b80);
        const float offsetY = *reinterpret_cast<const float *>(indicators + 0x4b84);
        out.extentX = screenW - offsetX;
        out.extentY = screenH - offsetY;
        out.centerX = offsetX + out.extentX * 0.5f;
        out.centerY = offsetY + out.extentY * 0.5f;
        out.scale   = Indicators().ScaleY();
        return out.extentX > 0.0f && out.extentY > 0.0f;
    }

    inline Relative ToRelative(const View &view, float worldX, float worldZ) {
        const Point texture = ToTexture(view.projection, worldX, worldZ);
        const float windowU = view.projection.windowU;
        const float windowV = view.projection.windowV;
        return {(texture.u - view.windowU - windowU * 0.5f) / windowU, (texture.v - view.windowV - windowV * 0.5f) / windowV};
    }

    // Screen pixels of a relative position; the window spans 80% x 74%.
    inline void ToScreen(const View &view, const Relative &relative, float &x, float &y) {
        x = view.centerX + relative.u * view.extentX * kScreenWidthRatio;
        y = view.centerY + relative.v * view.extentY * kScreenHeightRatio;
    }
} // namespace Mafia1Online::SDK::UI::Map
