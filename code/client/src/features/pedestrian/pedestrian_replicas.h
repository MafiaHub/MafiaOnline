#pragma once

#include <utils/snapshot_buffer.h>

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::SDK::Traffic {
    struct NativeTrafficElement;
    struct NativeTrafficGenerator;
}

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Mafia1Online::Shared::Entities {
    class PedestrianEntity;
}

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Pedestrian {
    // Players: shows the simulation host's walkers that the server streams
    // here as retail walkers. Each one takes an idle element of the same
    // generator and model from this client's own pools, so it has the same
    // mesh, animation set and collision as on the host. Its AI never runs:
    // the pose follows the host's snapshots and every animation the host
    // plays is played here through retail SetAnim.
    class PedestrianReplicaService final {
      public:
        void Update(World::WorldService &world);
        // Returns every borrowed element to its pool while the mission lives.
        void Reset(World::WorldService &world);
        // The walker whose replica wears this model (C_don_ocas::Collision's
        // frame), or 0.
        uint64_t WalkerOfModel(const SDK::Scene::NativeFrame *model) const;

      private:
        // The host reports every 100 ms. Two samples of render delay keep a
        // bracket for interpolation through one lost packet.
        inline static const Framework::Utils::SnapshotBufferConfig kSnapshotConfig {200.0f, 250.0f, 4.0f, 4.0f, true, 150.0f, 300.0f};

        struct Replica {
            Replica(): snapshots(&kSnapshotConfig) {}
            Framework::Utils::TransformSnapshotBuffer snapshots;
            MafiaNet::Time lastTransformTime = 0;
            SDK::Traffic::NativeTrafficElement *element = nullptr;
            // A clip ended: hold its last pose until the host plays the next
            // animation. A corpse never gets one.
            bool holding = false;
            int32_t appliedAlpha = -1;
            std::chrono::steady_clock::time_point holdingSince;
            bool failed = false;
        };

        SDK::Traffic::NativeTrafficElement *Borrow(const std::vector<SDK::Traffic::NativeTrafficGenerator *> &generators, Shared::Entities::PedestrianEntity &walker,
                                                   const Framework::Utils::TransformSnapshot &pose);
        void Release(World::WorldService &world, Replica &replica);

        std::unordered_map<uint64_t, Replica> _replicas;
        std::chrono::steady_clock::time_point _lastUpdate;
    };
} // namespace Mafia1Online::Features::Pedestrian
