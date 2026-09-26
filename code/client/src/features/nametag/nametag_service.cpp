#include <utils/safe_win32.h>

#include "nametag_service.h"

#include "features/world/world_service.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <mafia1/sdk/collision/native_collision.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/ui/native_hud.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace Mafia1Online::Features::Nametag {
    namespace {
        namespace Tags = Framework::Integrations::Client::UI::Nametags;
        using SDK::Player::NativeActor;

        // Names ride on the "neck" bone (the one retail's aim pose drives),
        // a little above the head.
        constexpr const char *kNeckFrame = "neck";
        constexpr float kAboveNeck       = 0.32f;
        constexpr float kFallbackHead    = 2.05f;
        constexpr float kVirtualHeight = 600.0f;

        std::string Utf8ToNative(std::string_view utf8) {
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
            return native;
        }

        struct Target {
            SDK::Player::Vector3 head;
        };

        uint32_t HealthColor(float health, float alpha) {
            const float t = std::clamp(health, 0.0f, 1.0f);
            const auto red   = static_cast<uint32_t>(std::clamp((1.0f - t) * 2.0f, 0.0f, 1.0f) * 220.0f + 30.0f);
            const auto green = static_cast<uint32_t>(std::clamp(t * 2.0f, 0.0f, 1.0f) * 200.0f + 30.0f);
            return Tags::ModulateAlpha(0xff000000u | (red << 16) | (green << 8) | 0x30u, alpha);
        }
    } // namespace

    NametagService::NametagService() {
        _config.layout.drawDistance = 45.0f;
        _config.layout.scaleStart   = 5.0f;
        _config.layout.scaleEnd     = 45.0f;
        _config.layout.nearScale    = 1.15f;
        _config.layout.farScale     = 0.7f;
        _config.layout.shiftStart   = 10.0f;
        _config.maxVisible          = 16;
    }

    void NametagService::Reset() {
        _labels.clear();
        _necks.clear();
    }

    void NametagService::Render(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *mission     = SDK::Core::Mission::Get();
        auto *scene       = mission ? mission->GetScene() : nullptr;
        auto *camera      = scene ? scene->ActiveCamera() : nullptr;
        if (!replication || !world.IsReady() || !camera) {
            return;
        }
        const auto cameraPosition = camera->ValidWorldPosition();
        const glm::vec3 eye(cameraPosition.x, cameraPosition.y, cameraPosition.z);
        const uint64_t myGuid = static_cast<uint64_t>(replication->GetMyGUID());

        bool localSpawned = false;
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (player->controllerGuid == myGuid && player->missionGeneration == world.LoadedMissionGeneration()) {
                localSpawned = player->spawned;
            }
        });
        if (!localSpawned) {
            return;
        }

        std::unordered_map<uint64_t, Target> targets;
        _list.Begin(_config);
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (player->controllerGuid == myGuid || !player->spawned || !player->alive ||
                player->missionGeneration != world.LoadedMissionGeneration()) {
                return;
            }
            const uint64_t id = player->GetNetworkID();
            const auto handle = world.NativeObjects().FindByNetwork(id);
            auto *actor       = static_cast<NativeActor *>(world.NativeObjects().Resolve(handle));
            if (!actor || !actor->Frame() || (actor->GetType() != NativeActor::Type::Player && actor->GetType() != NativeActor::Type::Entity)) {
                return;
            }
            // The bone lives as long as the human's model. The registry
            // generation changes whenever the native human is replaced, so a
            // reused address can never hand back a stale bone.
            auto &neck = _necks[id];
            if (neck.generation != handle.generation || !neck.frame) {
                neck = {handle.generation, actor->Frame()->FindChildFrame(kNeckFrame)};
            }
            SDK::Player::Vector3 head;
            if (neck.frame) {
                head = neck.frame->ValidWorldPosition();
                head.y += kAboveNeck;
            }
            else {
                head = actor->Frame()->ValidWorldPosition();
                head.y += kFallbackHead;
            }
            const float distance = glm::distance(eye, glm::vec3(head.x, head.y, head.z));

            auto &label = _labels[id];
            const std::string &source = player->nametag.text.empty() ? player->nickname : player->nametag.text;
            label = Utf8ToNative(source);

            Tags::Candidate candidate;
            candidate.id            = id;
            candidate.distance      = distance;
            candidate.components    = player->nametag.components;
            candidate.color         = player->nametag.color;
            candidate.healthPercent = std::clamp(player->health / 100.0f, 0.0f, 1.0f);
            candidate.label         = label.c_str();
            if (_list.Add(candidate)) {
                targets[id] = {head};
            }
        });

        const auto &indicators = SDK::UI::Indicators();
        const float screenHeight = kVirtualHeight * indicators.ScaleY();
        const float screenWidth  = screenHeight * 4.0f / 3.0f;
        const auto &appearance   = _config.appearance;
        for (const auto &tag : _list.Resolve()) {
            const auto target = targets.find(tag.id);
            if (target == targets.end()) {
                continue;
            }
            const auto &head = target->second.head;
            const SDK::Player::Vector3 toHead {head.x - eye.x, head.y - eye.y, head.z - eye.z};
            if (SDK::Collision::StaticLineBlocked(cameraPosition, toHead)) {
                continue;
            }
            float x = 0.0f, y = 0.0f;
            if (!scene->ProjectToScreen(head, x, y)) {
                continue;
            }
            y -= tag.shift * screenHeight;
            const float alpha      = tag.distanceAlpha;
            const float fontHeight = appearance.fontHeight * screenHeight * 1.35f * tag.scale;
            const float boxWidth   = screenWidth * 0.3f;
            float barTop           = y;
            if (tag.label) {
                SDK::UI::DrawShadowedText(tag.label, x - boxWidth * 0.5f, y - fontHeight, boxWidth, fontHeight,
                                  Tags::ModulateAlpha(tag.color, alpha), SDK::UI::kTextCentered);
                barTop = y + appearance.healthBarGap * screenHeight * tag.scale;
            }
            if (tag.healthPercent >= 0.0f) {
                const float width  = appearance.healthBarWidth * screenWidth * 1.4f * tag.scale;
                const float height = std::max(2.0f, appearance.healthBarHeight * screenHeight * 1.2f * tag.scale);
                const float border = std::max(1.0f, appearance.healthBarBorder * screenHeight);
                const float left   = x - width * 0.5f;
                SDK::Graphics::FillRect(left - border, barTop - border, width + border * 2.0f, height + border * 2.0f,
                                        Tags::ModulateAlpha(appearance.barTrackColor, alpha));
                SDK::Graphics::FillRect(left, barTop, width * tag.healthPercent, height, HealthColor(tag.healthPercent, alpha));
            }
        }
    }
} // namespace Mafia1Online::Features::Nametag
