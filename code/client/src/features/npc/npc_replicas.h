#pragma once

#include "game/entities/native_object_registry.h"
#include "shared/features/npc/npc_seized.h"
#include "shared/net/sample_clock.h"

#include <utils/snapshot_buffer.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <utility>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Mafia1Online::Features::World {
    class WorldService;
}
namespace Mafia1Online::Shared::Entities {
    class NpcEntity;
}

namespace Mafia1Online::Features::Npc {
    // Players: shows the simulation host's NPCs the server streams here
    // as network-controlled native humans, which run no AI. The pose and
    // locomotion follow the host's snapshots; each is bound to its network
    // id, so CombatService and DeathService apply its Combat::State and
    // replay its shots, reloads, hits and death as they do a remote player's.
    class NpcReplicaService final {
      public:
        void Update(World::WorldService &world);
        void Reset(World::WorldService &world);
        // The actor destructor: the frame to delete after it, if it was ours.
        SDK::Scene::NativeFrame *OnNativeDestroyed(World::WorldService &world, void *actor);
        // Npc::Seized: the officer's half of a police arrest or fine on its
        // replica, from where the host's officer stands (state 10's place).
        // The pose stream stays off it until the track ends.
        void PlaySeizing(World::WorldService &world, uint64_t officerId, const Shared::Npc::Seized::Pose &pose, Shared::Npc::Caught::Outcome outcome);

      private:
        // The host reports every 100 ms, and the server's far band every
        // 200 ms. Two intervals of render delay, plus how late packets come,
        // keep a bracket for interpolation through one lost packet.
        inline static const Framework::Utils::SnapshotBufferConfig kSnapshotConfig {200.0f, 250.0f, 4.0f, 4.0f, true, 150.0f, 600.0f, 2.0f, 0.1f};

        struct Replica {
            Replica(): snapshots(&kSnapshotConfig) {}
            Framework::Utils::TransformSnapshotBuffer snapshots;
            std::deque<std::pair<MafiaNet::Time, int16_t>> locomotion;
            MafiaNet::Time lastTransformTime = 0;
            // This frame's presentation time, shared by pose and locomotion.
            double renderTime = 0.0;
            // Dates each pose by when the host sampled it.
            Shared::Net::SampleClockMapper clock;
            Game::Entities::NativeObjectHandle handle;
            SDK::Scene::NativeFrame *frame = nullptr;
            int16_t lastAnimation = 0;
            // The end of a replayed pull-out (SeatService), if one is playing.
            std::chrono::steady_clock::time_point pullingUntil {};
            // The script clip (NpcEntity::animation) last applied, and until
            // when it holds the locomotion stream off (max() for a loop).
            uint16_t animationRevision = 0;
            bool animationSynced = false;
            // The last npc.say count heard (NpcEntity::speechRevision).
            uint16_t speechRevision = 0;
            std::chrono::steady_clock::time_point animatingUntil {};
            // A police arrest half (PlaySeizing) holds the pose too: the
            // clip moves the body from the aligned place.
            std::chrono::steady_clock::time_point seizingUntil {};
            bool failed = false;
        };

        void Remove(World::WorldService &world, uint64_t networkId, const Replica &replica);
        void ApplyPose(Replica &replica, void *actor, bool alive);
        // Replays npc.playAnimation / stopAnimation as PERSON_PLAYANIM and
        // PERSON_STOPANIM do on the host.
        void ApplyAnimation(Replica &replica, void *actor, const Shared::Entities::NpcEntity &npc);

        std::unordered_map<uint64_t, Replica> _replicas;
        // Every human this created, until its destructor hands the frame back.
        std::unordered_map<void *, SDK::Scene::NativeFrame *> _frames;
    };
} // namespace Mafia1Online::Features::Npc
