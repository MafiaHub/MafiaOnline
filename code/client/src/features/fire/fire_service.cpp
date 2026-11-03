#include "fire_service.h"

#include "features/world/world_service.h"
#include "shared/features/fire/fire_entity.h"

#include <core_modules.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/sound/native_sound.h>
#include <mafia1/sdk/world/native_fire.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Mafia1Online::Features::Fire {
    namespace {
        namespace Native = SDK::World::Fire;
        using Shared::Entities::FireEntity;
        using Shared::Entities::WaterJetEntity;

        // Fires are drawn within this of the camera, with some slack before
        // one is torn down again; the hydrant jets retail draws within 150.
        constexpr float kFireRange    = 250.0f;
        constexpr float kFireSlack    = 20.0f;
        constexpr float kJetRange     = 150.0f;
        constexpr size_t kMaxFlames   = 10;
        constexpr float kGravity      = 9.8f;
        // The retail fire light is renewed with this much time left, so it
        // never reaches its fade while the fire burns.
        constexpr int32_t kLightMs    = 3000;
        constexpr auto kLightRefresh  = std::chrono::milliseconds(500);
        // A fire that moved this far from its light gets a new one.
        constexpr float kLightFollow  = 3.0f;
        // A put-out fire smokes this long.
        constexpr auto kSmoulder      = std::chrono::seconds(40);
        constexpr float kHandHeight   = 1.3f;
        constexpr float kHandReach    = 0.45f;
        constexpr float kRoofHeight   = 2.4f;
        constexpr float kRoofForward  = 0.8f;

        SDK::Player::Vector3 Native3(const glm::vec3 &v) {
            return {v.x, v.y, v.z};
        }

        glm::vec3 Glm(const SDK::Player::Vector3 &v) {
            return {v.x, v.y, v.z};
        }

        bool Finite(const glm::vec3 &v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        uint32_t Next(uint32_t &state) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        float Unit(uint32_t &state) {
            return static_cast<float>(Next(state) & 0xffffff) / static_cast<float>(0x1000000);
        }

        SDK::Sound::NativeSound *StartLoop(uint32_t id, const glm::vec3 &position, float volume) {
            auto *sound = Native::CreateTableSound(id);
            if (!sound) {
                return nullptr;
            }
            // As C_fire::Init and C_game::AddHydrant start their loops.
            auto *scene = SDK::Core::Mission::Get()->GetScene();
            auto *frame = sound->Frame();
            frame->SetName("mp_fire_sound");
            frame->SetWorldPosition(Native3(position));
            frame->LinkTo(scene->PrimarySector());
            scene->SetFrameSectorPos(frame, Native3(position));
            sound->SetVolume(volume);
            sound->SetLoop(true);
            frame->Update();
            frame->SetOn(true);
            return sound;
        }

        void StopLoop(SDK::Sound::NativeSound *&sound) {
            if (!sound) {
                return;
            }
            sound->Stop();
            sound->Frame()->LinkTo(nullptr);
            sound->Frame()->Release();
            sound = nullptr;
        }

        void MoveLoop(SDK::Sound::NativeSound *sound, const glm::vec3 &position) {
            auto *frame = sound->Frame();
            frame->SetWorldPosition(Native3(position));
            frame->Update();
        }

        SDK::Scene::NativeFrame *CreateAnchor(const char *name) {
            auto *frame = SDK::Scene::GetDriver()->CreateDummy();
            if (frame) {
                frame->SetName(name);
            }
            return frame;
        }

        // A disconnect can reset this after the mission is gone, and its
        // particle manager with it.
        // The shortest turn taking unit vector from onto unit vector to.
        glm::quat Turn(const glm::vec3 &from, const glm::vec3 &to) {
            const float cosine = glm::dot(from, to);
            if (cosine < -0.9999f) {
                return {0.0f, 0.0f, 1.0f, 0.0f}; // half a turn about y
            }
            const glm::vec3 axis = glm::cross(from, to);
            const float scale    = std::sqrt((1.0f + cosine) * 2.0f);
            return {scale * 0.5f, axis.x / scale, axis.y / scale, axis.z / scale};
        }

        void Kill(int32_t &particle) {
            if (particle >= 0 && SDK::Core::Mission::Get()) {
                Native::DestroyParticle(particle);
            }
            particle = -1;
        }
    } // namespace

    void FireService::Layout(FireVisual &visual, const FireEntity &fire) {
        for (auto &flame : visual.flames) {
            Kill(flame.particle);
        }
        visual.flames.clear();
        visual.seed   = fire.seed;
        visual.radius = fire.radius;
        visual.height = fire.height;
        visual.burning = 0;
        const size_t count = std::clamp<size_t>(static_cast<size_t>(std::lround(1.0f + fire.radius * 1.3f)), 1, kMaxFlames);
        uint32_t state     = fire.seed ? fire.seed : 1u;
        visual.flames.push_back({});
        for (size_t index = 1; index < count; ++index) {
            const float distance = fire.radius * 0.8f * std::sqrt(0.15f + 0.85f * Unit(state));
            const float angle    = Unit(state) * 6.2831855f;
            const float rise     = fire.height > 0.0f ? Unit(state) * fire.height : 0.0f;
            visual.flames.push_back({{std::sin(angle) * distance, rise, std::cos(angle) * distance}, -1});
        }
        // The outer flames go out first.
        std::sort(visual.flames.begin() + 1, visual.flames.end(), [](const Flame &a, const Flame &b) {
            return glm::length(glm::vec2(a.offset.x, a.offset.z)) < glm::length(glm::vec2(b.offset.x, b.offset.z));
        });
    }

    bool FireService::Build(FireVisual &visual, const FireEntity &fire) {
        visual.anchor = CreateAnchor("mp_fire");
        if (!visual.anchor) {
            return false;
        }
        visual.position = fire.position;
        visual.anchor->SetLocalPosition(Native3(fire.position));
        visual.anchor->Update();
        Layout(visual, fire);
        if (fire.intensity > 0.0f) {
            // C_game::NewFire's ignition sound, played once by the game.
            if (auto *ignition = Native::CreateTableSound(Native::kSoundIgnition)) {
                auto *frame = ignition->Frame();
                frame->SetWorldPosition(Native3(fire.position));
                frame->Update();
                Native::PlayOwnedSound(SDK::Core::Mission::Get()->Game(), ignition);
            }
        }
        return true;
    }

    void FireService::Apply(FireVisual &visual, const FireEntity &fire) {
        const auto now        = std::chrono::steady_clock::now();
        const float intensity = std::clamp(fire.intensity, 0.0f, 1.0f);
        const size_t count    = visual.flames.size();
        const size_t burning  = intensity <= 0.0f ? 0 : std::clamp<size_t>(static_cast<size_t>(std::ceil(intensity * static_cast<float>(count))), 1, count);
        for (size_t index = 0; index < count; ++index) {
            auto &flame = visual.flames[index];
            if (index < burning && flame.particle < 0) {
                // The centre flame carries the smoke column.
                flame.particle = Native::CreateParticle(index == 0 ? Native::kEffectFire : Native::kEffectFireNoSmoke, Native3(flame.offset), {0.0f, 0.0f, 1.0f}, visual.anchor);
            }
            else if (index >= burning && flame.particle >= 0) {
                Kill(flame.particle);
            }
        }
        visual.burning = burning;

        if (burning == 0) {
            if (visual.outSince == std::chrono::steady_clock::time_point {}) {
                visual.outSince = now;
            }
            const bool smouldering = now - visual.outSince < kSmoulder;
            if (smouldering && visual.smoke < 0) {
                visual.smoke = Native::CreateParticle(Native::kEffectSlowSmoke, {0.0f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, visual.anchor);
            }
            else if (!smouldering && visual.smoke >= 0) {
                Kill(visual.smoke);
            }
        }
        else {
            visual.outSince = {};
            if (visual.smoke >= 0) {
                Kill(visual.smoke);
            }
        }

        const bool steaming = fire.dousing && burning > 0;
        if (steaming && visual.steam < 0) {
            visual.steam = Native::CreateParticle(Native::kEffectSteam, {0.0f, 0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, visual.anchor);
        }
        else if (!steaming && visual.steam >= 0) {
            Kill(visual.steam);
        }

        const glm::vec3 centre = fire.position + glm::vec3(0.0f, 1.0f + fire.height * 0.5f, 0.0f);
        if (burning > 0) {
            const float volume = 0.35f + 0.65f * intensity;
            if (!visual.loop) {
                visual.loop   = StartLoop(Native::kSoundFireLoop, centre, volume);
                visual.volume = volume;
            }
            else if (std::abs(volume - visual.volume) > 0.02f) {
                visual.loop->SetVolume(volume);
                visual.volume = volume;
            }
        }
        else {
            StopLoop(visual.loop);
            visual.volume = -1.0f;
        }

        auto *game = SDK::Core::Mission::Get()->Game();
        auto *slots = Native::FireFrameSlots(game);
        if (burning > 0 && now - visual.lightAt >= kLightRefresh) {
            // A light not renewed before it could run out may have gone to a
            // retail fire or explosion since: it is no longer this fire's.
            const bool held = visual.light >= 0 && now - visual.lightAt < std::chrono::milliseconds(kLightMs - Native::kFireLightFadeMs) &&
                              slots[visual.light].timeRemaining > 0;
            if (held && glm::distance(visual.lightPosition, centre) <= kLightFollow) {
                slots[visual.light].timeRemaining = kLightMs;
            }
            else {
                if (held) {
                    // The pooled light does not move (a fire on a car).
                    slots[visual.light].timeRemaining = std::min(slots[visual.light].timeRemaining, Native::kFireLightFadeMs);
                }
                visual.light         = Native::SpawnFireLight(game, Native3(centre), kLightMs);
                visual.lightPosition = centre;
            }
            visual.lightAt = now;
        }
        else if (burning == 0 && visual.light >= 0) {
            // C_game::Tick fades it out from here, as a retail fire's ends.
            auto &slot = slots[visual.light];
            slot.timeRemaining = std::min(slot.timeRemaining, Native::kFireLightFadeMs);
            visual.light = -1;
        }
    }

    void FireService::Release(FireVisual &visual) {
        for (auto &flame : visual.flames) {
            Kill(flame.particle);
        }
        visual.flames.clear();
        Kill(visual.smoke);
        Kill(visual.steam);
        StopLoop(visual.loop);
        if (visual.light >= 0) {
            if (auto *mission = SDK::Core::Mission::Get(); mission && mission->Game()) {
                auto &slot = Native::FireFrameSlots(mission->Game())[visual.light];
                slot.timeRemaining = std::min(slot.timeRemaining, Native::kFireLightFadeMs);
            }
            visual.light = -1;
        }
        if (visual.anchor) {
            visual.anchor->Release();
            visual.anchor = nullptr;
        }
    }

    void FireService::Aim(World::WorldService &world, JetVisual &visual, const WaterJetEntity &jet, const glm::vec3 &) {
        glm::vec3 nozzle = jet.position;
        auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(world.NativeObjects().FindByNetwork(jet.sourceId)));
        if (actor && actor->Frame()) {
            const glm::vec3 base    = Glm(actor->Frame()->WorldPosition());
            glm::vec3 forward       = Glm(actor->Frame()->WorldDirection());
            forward.y               = 0.0f;
            forward                 = glm::length(forward) > 0.01f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, 1.0f);
            nozzle = jet.sourceIsVehicle ? base + glm::vec3(0.0f, kRoofHeight, 0.0f) + forward * kRoofForward
                                         : base + glm::vec3(0.0f, kHandHeight, 0.0f) + forward * kHandReach;
        }
        if (!Finite(nozzle)) {
            return;
        }
        // The retail drops fall with gravity at the effect's speed times the
        // frame's scale: pick the lower arc that lands on the target.
        const float speed      = std::clamp(jet.pressure, WaterJetEntity::kMinPressure, WaterJetEntity::kMaxPressure);
        const glm::vec3 offset = jet.target - nozzle;
        const float flat       = std::sqrt(offset.x * offset.x + offset.z * offset.z);
        glm::vec3 across       = flat > 0.05f ? glm::vec3(offset.x / flat, 0.0f, offset.z / flat) : glm::vec3(0.0f, 0.0f, 1.0f);
        float elevation        = 0.7853982f;
        const float v2         = speed * speed;
        const float root       = v2 * v2 - kGravity * (kGravity * flat * flat + 2.0f * offset.y * v2);
        if (flat > 0.05f && root >= 0.0f) {
            elevation = std::atan((v2 - std::sqrt(root)) / (kGravity * flat));
        }
        const glm::vec3 direction = across * std::cos(elevation) + glm::vec3(0.0f, std::sin(elevation), 0.0f);

        if (!visual.nozzle) {
            visual.nozzle = CreateAnchor("mp_water_jet");
            if (!visual.nozzle) {
                return;
            }
        }
        // Map objects turn the same way: LS3D takes the conjugate of glm's
        // quaternion, so the frame's x row becomes direction.
        const glm::quat turn = Turn(glm::vec3(1.0f, 0.0f, 0.0f), glm::normalize(direction));
        const float scale    = speed / Native::kHoseEffectSpeed;
        visual.nozzle->SetLocalScale({scale, scale, scale});
        visual.nozzle->SetLocalRotation(turn.w, -turn.x, -turn.y, -turn.z);
        visual.nozzle->SetLocalPosition(Native3(nozzle));
        visual.nozzle->Update();
        if (visual.particle < 0) {
            visual.particle = Native::CreateParticle(Native::kEffectHose, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, visual.nozzle);
        }
        if (!visual.loop) {
            visual.loop = StartLoop(Native::kSoundWaterLoop, nozzle, 0.8f);
        }
        else {
            MoveLoop(visual.loop, nozzle);
        }
    }

    void FireService::Release(JetVisual &visual) {
        Kill(visual.particle);
        StopLoop(visual.loop);
        if (visual.nozzle) {
            visual.nozzle->Release();
            visual.nozzle = nullptr;
        }
    }

    void FireService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *mission     = SDK::Core::Mission::Get();
        if (!replication || !world.IsReady() || !mission || !mission->Game() || !mission->GetScene()) {
            return;
        }
        auto *camera = mission->GetScene()->ActiveCamera();
        if (!camera) {
            return;
        }
        const glm::vec3 eye = Glm(camera->WorldPosition());
        std::unordered_set<uint64_t> seen;
        replication->ForEach<FireEntity>([&](FireEntity *fire) {
            if (fire->missionGeneration != world.LoadedMissionGeneration() || !Finite(fire->position)) {
                return;
            }
            const uint64_t id   = fire->GetNetworkID();
            const auto existing = _fires.find(id);
            const float range   = existing != _fires.end() ? kFireRange + kFireSlack : kFireRange;
            if (glm::distance(eye, fire->position) > range) {
                return;
            }
            seen.insert(id);
            auto &visual = _fires[id];
            if (!visual.anchor && !Build(visual, *fire)) {
                _fires.erase(id);
                return;
            }
            if (visual.seed != fire->seed || visual.radius != fire->radius || visual.height != fire->height) {
                Layout(visual, *fire);
            }
            if (glm::distance(visual.position, fire->position) > 0.05f) {
                const bool jumped = glm::distance(visual.position, fire->position) > 3.0f;
                visual.position = fire->position;
                visual.anchor->SetLocalPosition(Native3(fire->position));
                visual.anchor->Update();
                if (visual.loop) {
                    MoveLoop(visual.loop, fire->position + glm::vec3(0.0f, 1.0f + fire->height * 0.5f, 0.0f));
                }
                if (jumped && visual.light >= 0) {
                    // The pooled light does not move; a new one is lit there.
                    auto &slot = Native::FireFrameSlots(mission->Game())[visual.light];
                    slot.timeRemaining = std::min(slot.timeRemaining, Native::kFireLightFadeMs);
                    visual.light   = -1;
                    visual.lightAt = {};
                }
            }
            Apply(visual, *fire);
        });
        for (auto it = _fires.begin(); it != _fires.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }
            Release(it->second);
            it = _fires.erase(it);
        }

        seen.clear();
        replication->ForEach<WaterJetEntity>([&](WaterJetEntity *jet) {
            if (jet->missionGeneration != world.LoadedMissionGeneration() || !jet->active || !Finite(jet->target) || glm::distance(eye, jet->target) > kJetRange) {
                return;
            }
            const uint64_t id = jet->GetNetworkID();
            seen.insert(id);
            Aim(world, _jets[id], *jet, eye);
        });
        for (auto it = _jets.begin(); it != _jets.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }
            Release(it->second);
            it = _jets.erase(it);
        }
    }

    void FireService::OnMissionClosing() {
        for (auto &[id, visual] : _fires) {
            Release(visual);
        }
        _fires.clear();
        for (auto &[id, visual] : _jets) {
            Release(visual);
        }
        _jets.clear();
    }

    void FireService::Reset() {
        OnMissionClosing();
    }
} // namespace Mafia1Online::Features::Fire
