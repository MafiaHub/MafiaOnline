#pragma once

#include "game/entities/native_object_registry.h"

#include <utils/snapshot_buffer.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <deque>
#include <string>
#include <unordered_map>

#include <glm/vec3.hpp>

namespace Mafia1Online::SDK::Player {
    enum class LocomotionAnimation : int32_t;
}
namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Player {
    class PlayerService final {
      public:
        void RegisterRPC();
        void Update(World::WorldService &world);
        // After a native Do_Climb started for the local player, from the
        // pose it started at.
        void OnLocalClimb(World::WorldService &world, const glm::vec3 &start, const glm::vec3 &direction);
        bool ReplayingClimb() const {
            return _replayingClimb;
        }
        bool SampleRemote(uint64_t networkId, Framework::Utils::TransformSnapshot &out) const;
        void Reset(World::WorldService &world);
        SDK::Scene::NativeFrame *OnNativeDestroyed(World::WorldService &world, void *actor);

      private:
        // The owner reports movement every 50 ms. One extra half-sample of
        // render delay gives remote turns steadier brackets without making
        // steering feel late.
        inline static const Framework::Utils::SnapshotBufferConfig kRemoteSnapshotConfig {75.0f, 150.0f, 4.0f, 4.0f, true, 75.0f, 200.0f};
        struct RemoteStream {
            RemoteStream(): snapshots(&kRemoteSnapshotConfig) {}
            Framework::Utils::TransformSnapshotBuffer snapshots;
            MafiaNet::Time lastPacketTime = 0;
            uint64_t missionGeneration    = 0;
            uint64_t spawnGeneration      = 0;
            bool creationFailed           = false;
            std::deque<std::pair<MafiaNet::Time, int16_t>> locomotion;
        };

        struct NativeHuman {
            Game::Entities::NativeObjectHandle handle;
            SDK::Scene::NativeFrame *frame = nullptr;
            uint64_t spawnGeneration       = 0;
            bool local                     = false;
            bool removalQueued             = false;
            int16_t lastAnimation          = 0;
        };
        struct RemoteAnimation {
            int16_t state;
            // Played once when the replicated state changes to it.
            bool oneShot;
        };
        std::optional<RemoteAnimation> RemoteLocomotion(const RemoteStream &stream) const;

        void ResetLocal(World::WorldService &world);
        void ReleaseLocal(World::WorldService &world);
        void QueueRemoval(World::WorldService &world, uint64_t networkId);
        bool CreateNative(World::WorldService &world, uint64_t networkId, uint64_t spawnGeneration, bool local,
                          const Framework::Utils::TransformSnapshot &pose, const std::string &model);
        void ApplyModel(World::WorldService &world, uint64_t networkId, const std::string &model);
        void ApplyRemotePose(World::WorldService &world, uint64_t networkId, const Framework::Utils::TransformSnapshot &pose, bool alive,
                             std::optional<RemoteAnimation> locomotion);
        std::optional<Framework::Utils::TransformSnapshot> SuggestedPose() const;
        void SendNativePose(uint64_t networkId, uint64_t missionGeneration, const Framework::Utils::TransformSnapshot &pose);

        Game::Entities::NativeObjectHandle _local;
        uint64_t _spawnGeneration   = 0;
        uint64_t _missionGeneration = 0;
        uint32_t _sequence          = 0;
        std::chrono::steady_clock::time_point _lastSent;
        std::unordered_map<uint64_t, RemoteStream> _remote;
        std::unordered_map<uint64_t, void *> _currentById;
        std::unordered_map<void *, NativeHuman> _ownedByNative;
        std::unordered_map<uint64_t, std::string> _appliedModels;
        std::unordered_map<uint64_t, std::string> _failedModels;
        struct PendingClimb {
            uint64_t networkId;
            uint64_t spawnGeneration;
            glm::vec3 position;
            glm::vec3 direction;
            std::chrono::steady_clock::time_point received;
        };
        std::deque<PendingClimb> _climbs;
        // While a replayed climb animation runs the pose stream would pull
        // the body straight to the ledge; it resumes when the climb ends.
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _climbingUntil;
        bool _replayingClimb = false;
        void ReplayClimbs(World::WorldService &world);
    };
} // namespace Mafia1Online::Features::Player
