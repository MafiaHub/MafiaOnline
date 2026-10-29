#pragma once

#include "game/entities/native_object_registry.h"
#include "ride_blend.h"
#include "shared/features/combat/vehicle_impact.h"
#include "shared/features/npc/npc_seized.h"
#include "shared/net/sample_clock.h"

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
        struct RemoteAnimation {
            int16_t state;
            // Played once when the replicated state changes to it.
            bool oneShot;
        };
        // The animation a human replica plays for a replicated native
        // locomotion state (C_human m_iAnimState).
        static RemoteAnimation ClassifyLocomotion(int16_t state);

        void RegisterRPC();
        void Update(World::WorldService &world);
        // After a native Do_Climb started for the local player, from the
        // pose it started at.
        void OnLocalClimb(World::WorldService &world, const glm::vec3 &start, const glm::vec3 &direction);
        bool ReplayingClimb() const {
            return _replayingClimb;
        }
        // A police arrest or fine animation plays on this replica (on the
        // simulation host natively, elsewhere from Npc::Seized): the pose
        // stream stays off it until the track ends.
        void HoldSeized(uint64_t networkId);
        // Simulation host: the police arrest walk placed this replica in
        // front of its officer; the pose stream stays off it for `hold`.
        void HoldAligned(uint64_t networkId, std::chrono::milliseconds hold);
        bool SampleRemote(uint64_t networkId, Framework::Utils::TransformSnapshot &out) const;
        void Reset(World::WorldService &world);
        SDK::Scene::NativeFrame *OnNativeDestroyed(World::WorldService &world, void *actor);

      private:
        // The owner reports movement every 50 ms, and the server's farther
        // bands every 100 or 200 ms. Two intervals of render delay, plus how
        // late packets come, keep a bracket through one lost packet.
        inline static const Framework::Utils::SnapshotBufferConfig kRemoteSnapshotConfig {75.0f, 150.0f, 4.0f, 4.0f, true, 75.0f, 500.0f, 2.0f, 0.1f};
        struct RemoteStream {
            RemoteStream(): snapshots(&kRemoteSnapshotConfig) {}
            Framework::Utils::TransformSnapshotBuffer snapshots;
            MafiaNet::Time lastPacketTime = 0;
            uint64_t missionGeneration    = 0;
            uint64_t spawnGeneration      = 0;
            bool creationFailed           = false;
            std::deque<std::pair<MafiaNet::Time, int16_t>> locomotion;
            // The ride each pose came with, so the offset on the wagon is
            // read at the same render time as the pose and locomotion.
            std::deque<std::pair<MafiaNet::Time, RemoteRide>> rides;
            // This frame's presentation time (the buffer's slewed clock).
            double renderTime = 0.0;
            // Dates each pose by when the controller sampled it.
            Shared::Net::SampleClockMapper clock;
        };

        struct NativeHuman {
            Game::Entities::NativeObjectHandle handle;
            SDK::Scene::NativeFrame *frame = nullptr;
            uint64_t spawnGeneration       = 0;
            bool local                     = false;
            bool removalQueued             = false;
            int16_t lastAnimation          = 0;
            // The end of a replayed pull-out (SeatService), if one is playing.
            std::chrono::steady_clock::time_point pullingUntil {};
            // Since when a door animation holds the pose stream off (diagnostic).
            std::chrono::steady_clock::time_point doorSince {};
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
        // Npc::Seized: both halves of a police arrest or fine.
        struct PendingSeized {
            Shared::Npc::Seized seized;
            std::chrono::steady_clock::time_point received;
        };
        std::deque<PendingSeized> _seizures;
        // Replicas held off the pose stream while the animation plays.
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _seizedUntil;
        // Simulation host: replicas kept where the arrest walk placed them.
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _alignedUntil;
        // Combat::VehicleBump on the local player, applied on the game thread.
        struct PendingBump {
            Shared::Combat::VehicleBump bump;
            std::chrono::steady_clock::time_point received;
        };
        std::deque<PendingBump> _bumps;
        void ReplayBumps(World::WorldService &world);
        // The local player's controls stay locked until its animation ends.
        std::chrono::steady_clock::time_point _localSeizedUntil {};
        void ReplaySeizures(World::WorldService &world);
        void ReleaseLocalSeized(World::WorldService &world, bool force);
    };
} // namespace Mafia1Online::Features::Player
