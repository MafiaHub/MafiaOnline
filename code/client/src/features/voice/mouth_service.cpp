#include "mouth_service.h"

#include "features/world/world_service.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/scene/native_morph.h>
#include <networking/replication/replication_manager.h>
#include <voice/client/voice_client.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Mafia1Online::Features::Voice {
    namespace {
        using SDK::Player::NativeActor;
        using SDK::Player::NativeHuman;
        using SDK::Scene::NativeMorphWeights;

        // The level is full-scale RMS, floored at 0.02 (-34 dB) by the voice
        // client; normal speech peaks around -12 dB.
        constexpr float kQuietDb = -34.0f;
        constexpr float kLoudDb  = -12.0f;

        // Retail lip files (tables\dat) put loudness in FIRST_BLEND alone: it
        // tracks per-40 ms RMS at r 0.51, from 0.035 in silence to 0.43 in the
        // loudest 5% (p95 0.64). The other channels choose the mouth shape.
        constexpr float kMaxOpen = 0.65f;

        // Seconds. On top of the voice client's own envelope, so a frame
        // that drains no audio does not snap the jaw.
        constexpr float kOpenAttack      = 0.04f;
        constexpr float kOpenRelease     = 0.09f;
        constexpr float kPresenceAttack  = 0.08f;
        constexpr float kPresenceRelease = 0.25f;
        constexpr float kRested          = 0.01f;
        constexpr float kMaxStep         = 0.25f;

        // DabingSetHuman's speech start pose (reM DABING_INITIAL_*), which is
        // also the median of every retail lip file's shape channels.
        constexpr float kFirstSplit       = 0.46f;
        constexpr float kFirstSecondSplit = 0.53f;
        constexpr float kSharedBlend      = 1.0f;
        constexpr float kSecondSplit      = 0.11f;
        constexpr float kThirdSplit       = 0.6f;
        // FIRST_SPLIT picks between the two open shapes (retail p5 0.04,
        // p95 0.89); swinging it while the mouth moves keeps it from
        // flapping in one shape.
        constexpr float kSplitSwing = 0.3f;
        constexpr float kSplitRate  = 7.0f; // radians per second of open mouth

        constexpr int kGroups     = 3;
        constexpr int kMaxTargets = 5;
        using Pose = std::array<std::array<float, kMaxTargets>, kGroups>;

        float Follow(float current, float target, float dt, float attack, float release) {
            const float tau = target > current ? attack : release;
            return current + (target - current) * (1.0f - std::exp(-dt / tau));
        }

        float Openness(float level) {
            if (level <= 0.0f) {
                return 0.0f;
            }
            const float db = 20.0f * std::log10(level);
            return std::clamp((db - kQuietDb) / (kLoudDb - kQuietDb), 0.0f, 1.0f);
        }

        // reM DabingApplyMorphWeights: group 0 opens into targets 1 and 2
        // with FIRST_BLEND and rests in 3 and 4; groups 1 and 2 hold their
        // shared-blend shapes. presence fades it in from the rest face
        // DabingResetMorphWeights writes (target 0 alone).
        Pose SpeechPose(float open, float split, float presence) {
            Pose pose {};
            pose[0] = {0.0f, open * split, (1.0f - split) * open, (1.0f - kFirstSecondSplit) * (1.0f - open), (1.0f - open) * kFirstSecondSplit};
            pose[1] = {0.0f, kSecondSplit * kSharedBlend, (1.0f - kSecondSplit) * kSharedBlend, 1.0f - kSharedBlend, 0.0f};
            pose[2] = {0.0f, kThirdSplit * kSharedBlend, (1.0f - kThirdSplit) * kSharedBlend, 1.0f - kSharedBlend, 0.0f};
            for (auto &group : pose) {
                for (float &weight : group) {
                    weight *= presence;
                }
                group[0] += 1.0f - presence;
            }
            return pose;
        }

        // DabingResetMorphWeights writes five targets in each of three
        // groups; SetWeight only logs what a model lacks, so stay inside its
        // counts.
        void Write(NativeMorphWeights &weights, const Pose &pose) {
            const int groups  = std::min(weights.Groups(), kGroups);
            const int targets = std::min(weights.Targets(), kMaxTargets);
            for (int group = 0; group < groups; ++group) {
                for (int target = 0; target < targets; ++target) {
                    weights.SetWeight(group, target, pose[group][target]);
                }
            }
        }
    } // namespace

    void MouthService::Reset() {
        _mouths.clear();
        _last = {};
    }

    void MouthService::Update(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || !world.IsReady()) {
            Reset();
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const float dt = _last == std::chrono::steady_clock::time_point {} ? 0.0f : std::min(std::chrono::duration<float>(now - _last).count(), kMaxStep);
        _last          = now;
        ++_frame;

        auto *voice         = Framework::CoreModules::GetVoiceClient();
        const uint64_t mine = static_cast<uint64_t>(replication->GetMyGUID());
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (player->missionGeneration != world.LoadedMissionGeneration()) {
                return;
            }
            const bool speaking = player->spawned && player->alive;
            NativeActor *actor  = nullptr;
            bool talking        = false;
            float level         = 0.0f;
            if (player->controllerGuid == mine) {
                actor   = SDK::Player::CurrentPlayer();
                talking = speaking && voice && voice->IsLocalTalking();
                level   = voice ? voice->GetLocalLevel() : 0.0f;
            }
            else {
                actor   = static_cast<NativeActor *>(world.NativeObjects().Resolve(world.NativeObjects().FindByNetwork(player->GetNetworkID())));
                talking = speaking && voice && voice->IsSpeakerTalking(player->controllerGuid);
                level   = voice ? voice->GetSpeakerLevel(player->controllerGuid) : 0.0f;
            }
            if (!actor || !actor->Frame() || (actor->GetType() != NativeActor::Type::Player && actor->GetType() != NativeActor::Type::Entity)) {
                return;
            }
            Drive(*static_cast<NativeHuman *>(static_cast<void *>(actor)), talking, level, dt);
        });
        std::erase_if(_mouths, [this](const auto &entry) { return entry.second.seen != _frame; });
    }

    void MouthService::Drive(NativeHuman &human, bool talking, float level, float dt) {
        auto *visual  = human.BaseMesh();
        auto *weights = SDK::Scene::SingleMorphWeights(visual);
        if (!weights) {
            return;
        }
        auto &mouth = _mouths[visual];
        mouth.seen  = _frame;
        if (SDK::Scene::DabingAnimates(visual)) {
            // A retail line or cutscene owns the face, and leaves it as it ends.
            mouth      = {};
            mouth.seen = _frame;
            return;
        }
        if (!talking && mouth.rested) {
            return;
        }
        mouth.open     = Follow(mouth.open, talking ? Openness(level) : 0.0f, dt, kOpenAttack, kOpenRelease);
        mouth.presence = Follow(mouth.presence, talking ? 1.0f : 0.0f, dt, kPresenceAttack, kPresenceRelease);
        mouth.phase    = std::fmod(mouth.phase + dt * kSplitRate * mouth.open, 6.2831853f);
        if (!talking && mouth.presence < kRested) {
            Write(*weights, SpeechPose(0.0f, kFirstSplit, 0.0f));
            mouth      = {};
            mouth.seen = _frame;
            return;
        }
        mouth.rested = false;
        Write(*weights, SpeechPose(mouth.open * kMaxOpen, std::clamp(kFirstSplit + kSplitSwing * std::sin(mouth.phase), 0.0f, 1.0f), mouth.presence));
    }
} // namespace Mafia1Online::Features::Voice
