#include "sound_service.h"

#include "features/world/world_service.h"
#include "shared/features/sound/sound_entity.h"
#include "shared/features/world/world_effects.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/sound/native_sound.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Mafia1Online::Features::Sound {
    namespace {
        using Shared::Entities::SoundEntity;

        // The near range, as PLAYSOUND derives it from the script's range.
        constexpr float kInnerRangeScale = 0.5f;
        // Retail ENDOFMISSION plays its ambient sound with this range.
        constexpr float kAmbientMinimum = 20.0f;
        constexpr float kMovedDistance  = 0.05f;
        // A moving sound is refiled into its sector after this distance.
        constexpr float kSectorDistance = 10.0f;
        constexpr size_t kMaxMissingWaves = 64;
        constexpr size_t kMaxLocalLoops   = 32;

        SDK::Player::Vector3 Native(const glm::vec3 &v) {
            return {v.x, v.y, v.z};
        }

        bool Finite(const glm::vec3 &v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    } // namespace

    void SoundService::RegisterRPC(World::WorldService &world) {
        _world = &world;
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::World::PlaySound>([this](const Shared::World::PlaySound &play, MafiaNet::Packet *) {
            auto *mission = SDK::Core::Mission::Get();
            const glm::vec3 position {play.x, play.y, play.z};
            if (!_world->IsReady() || play.missionGeneration != _world->LoadedMissionGeneration() || !mission || !mission->Game() || !Finite(position)
                || !SoundEntity::ValidWave(play.wave) || _missingWaves.contains(play.wave)) {
                return;
            }
            const float radius = std::clamp(play.radius, 1.0f, SoundEntity::kMaxRadius);
            const float volume = std::clamp(play.volume, 0.0f, SoundEntity::kMaxVolume);
            SDK::Sound::Play3DSound(mission->Game(), play.wave.c_str(), SDK::Sound::kTypePoint, Native(position), radius * kInnerRangeScale, radius, volume, false);
        });
    }

    glm::vec3 SoundService::SoundPosition(World::WorldService &world, uint64_t attachedId, const glm::vec3 &fallback) const {
        if (attachedId == 0) {
            return fallback;
        }
        const auto handle = world.NativeObjects().FindByNetwork(attachedId);
        if (auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(handle)); actor && actor->Frame()) {
            const auto position = actor->Frame()->WorldPosition();
            return {position.x, position.y, position.z};
        }
        return fallback;
    }

    bool SoundService::Build(Voice &voice, const glm::vec3 &position) {
        auto *scene = SDK::Core::Mission::Get()->GetScene();
        auto *sound = SDK::Sound::CreateSound();
        if (!sound) {
            return false;
        }
        if (!SDK::Sound::OpenWave(sound, voice.wave.c_str())) {
            sound->Frame()->Release();
            if (_missingWaves.size() < kMaxMissingWaves && _missingWaves.insert(voice.wave).second) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Sound '{}' is not in this client's Sounds directory or archives", voice.wave);
            }
            return false;
        }
        auto *frame = sound->Frame();
        frame->SetName("mp_sound");
        frame->SetWorldPosition(Native(position));
        frame->LinkTo(scene->PrimarySector());
        scene->SetFrameSectorPos(frame, Native(position));
        sound->SetSoundType(SDK::Sound::kTypePoint);
        sound->SetRange(voice.radius * kInnerRangeScale, voice.radius);
        sound->SetOmnidirectional();
        sound->SetOutVolume(0.0f);
        sound->SetVolume(voice.volume);
        sound->SetLoop(true);
        frame->Update();
        frame->SetOn(true);
        voice.sound          = sound;
        voice.position       = position;
        voice.sectorPosition = position;
        return true;
    }

    void SoundService::Release(Voice &voice) {
        if (!voice.sound) {
            return;
        }
        voice.sound->Stop();
        voice.sound->Frame()->LinkTo(nullptr);
        voice.sound->Frame()->Release();
        voice.sound = nullptr;
    }

    void SoundService::Update(World::WorldService &world) {
        _world            = &world;
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || !world.IsReady()) {
            return;
        }
        std::unordered_set<uint64_t> seen;
        replication->ForEach<SoundEntity>([&](SoundEntity *entity) {
            if (entity->missionGeneration != world.LoadedMissionGeneration() || !SoundEntity::ValidWave(entity->wave)) {
                return;
            }
            const uint64_t id = entity->GetNetworkID();
            seen.insert(id);
            auto &voice          = _voices[id];
            const float radius   = std::clamp(entity->radius, 1.0f, SoundEntity::kMaxRadius);
            const float volume   = std::clamp(entity->volume, 0.0f, SoundEntity::kMaxVolume);
            const auto position  = SoundPosition(world, entity->attachedId, entity->position);
            const bool rebuild   = entity->wave != voice.wave || radius != voice.radius || entity->enabled != voice.enabled;
            if (rebuild) {
                Release(voice);
                voice.wave    = entity->wave;
                voice.radius  = radius;
                voice.volume  = volume;
                voice.enabled = entity->enabled;
                // A missing wave settles until the server changes a field.
                if (voice.enabled && Finite(position) && !_missingWaves.contains(voice.wave)) {
                    Build(voice, position);
                }
                return;
            }
            if (!voice.sound) {
                return;
            }
            if (volume != voice.volume) {
                voice.sound->SetVolume(volume);
                voice.volume = volume;
            }
            if (Finite(position) && glm::distance(position, voice.position) > kMovedDistance) {
                auto *frame = voice.sound->Frame();
                frame->SetWorldPosition(Native(position));
                if (glm::distance(position, voice.sectorPosition) > kSectorDistance) {
                    SDK::Core::Mission::Get()->GetScene()->SetFrameSectorPos(frame, Native(position));
                    voice.sectorPosition = position;
                }
                frame->Update();
                voice.position = position;
            }
        });
        for (auto it = _voices.begin(); it != _voices.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }
            Release(it->second);
            it = _voices.erase(it);
        }
    }

    int32_t SoundService::PlayLocal(const std::string &wave, const glm::vec3 *position, float radius, float volume, bool loop) {
        auto *mission = SDK::Core::Mission::Get();
        if (!_world || !_world->IsReady() || !mission || !mission->Game() || !SoundEntity::ValidWave(wave) || _missingWaves.contains(wave)
            || (loop && _local.size() >= kMaxLocalLoops)) {
            return -1;
        }
        int32_t id = -1;
        if (position) {
            id = SDK::Sound::Play3DSound(mission->Game(), wave.c_str(), SDK::Sound::kTypePoint, Native(*position), radius * kInnerRangeScale, radius, volume, loop);
        }
        else {
            auto *camera = mission->GetScene()->ActiveCamera();
            const auto where = camera ? camera->WorldPosition() : SDK::Player::Vector3 {};
            id = SDK::Sound::Play3DSound(mission->Game(), wave.c_str(), SDK::Sound::kTypeAmbient, where, kAmbientMinimum, kAmbientMinimum, volume, loop);
        }
        if (id < 0) {
            if (_missingWaves.size() < kMaxMissingWaves && _missingWaves.insert(wave).second) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Sound '{}' is not in this client's Sounds directory or archives", wave);
            }
            return -1;
        }
        if (loop) {
            _local.insert(id);
        }
        return id;
    }

    bool SoundService::StopLocal(int32_t id) {
        auto *mission = SDK::Core::Mission::Get();
        if (id < 0 || !_world || !_world->IsReady() || !mission || !mission->Game()) {
            return false;
        }
        SDK::Sound::Stop3DSound(mission->Game(), id);
        _local.erase(id);
        return true;
    }

    void SoundService::OnMissionClosing() {
        for (auto &[id, voice] : _voices) {
            Release(voice);
        }
        _voices.clear();
        _local.clear();
    }

    void SoundService::Reset() {
        OnMissionClosing();
        _missingWaves.clear();
    }
} // namespace Mafia1Online::Features::Sound
