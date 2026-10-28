#pragma once

#include "game/entities/native_object_registry.h"

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

      private:
        // The host reports every 100 ms. Two samples of render delay keep a
        // bracket for interpolation through one lost packet.
        inline static const Framework::Utils::SnapshotBufferConfig kSnapshotConfig {200.0f, 250.0f, 4.0f, 4.0f, true, 150.0f, 300.0f};

        struct Replica {
            Replica(): snapshots(&kSnapshotConfig) {}
            Framework::Utils::TransformSnapshotBuffer snapshots;
            std::deque<std::pair<MafiaNet::Time, int16_t>> locomotion;
            MafiaNet::Time lastTransformTime = 0;
            Game::Entities::NativeObjectHandle handle;
            SDK::Scene::NativeFrame *frame = nullptr;
            int16_t lastAnimation = 0;
            // The end of a replayed pull-out (SeatService), if one is playing.
            std::chrono::steady_clock::time_point pullingUntil {};
            bool failed = false;
        };

        void Remove(World::WorldService &world, uint64_t networkId, const Replica &replica);
        void ApplyPose(Replica &replica, void *actor, bool alive);

        std::unordered_map<uint64_t, Replica> _replicas;
        // Every human this created, until its destructor hands the frame back.
        std::unordered_map<void *, SDK::Scene::NativeFrame *> _frames;
    };
} // namespace Mafia1Online::Features::Npc
