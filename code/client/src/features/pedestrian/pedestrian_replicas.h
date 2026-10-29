#pragma once

#include "shared/net/sample_clock.h"

#include <utils/snapshot_buffer.h>

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
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
    // plays is played here through retail SetAnim. Its death takes retail's
    // own steps: Hit's when the host's walker dies, Tick's when its last
    // clip ends.
    class PedestrianReplicaService final {
      public:
        void Update(World::WorldService &world);
        // Returns every borrowed element to its pool while the mission lives.
        void Reset(World::WorldService &world);
        // The walker whose replica wears this model (C_don_ocas::Collision's
        // frame), or 0.
        uint64_t WalkerOfModel(const SDK::Scene::NativeFrame *model) const;
        // The NPC the host made of this walker takes its place: the pose the
        // walker shows on this screen now, and the walker leaves at once and
        // is not shown again. Nothing when this client shows no such walker.
        std::optional<Framework::Utils::TransformSnapshot> HandOff(World::WorldService &world, uint64_t walkerId);

      private:
        // The host reports every 100 ms, and the server's far band every
        // 200 ms. Two intervals of render delay, plus how late packets come,
        // keep a bracket for interpolation through one lost packet.
        inline static const Framework::Utils::SnapshotBufferConfig kSnapshotConfig {200.0f, 250.0f, 4.0f, 4.0f, true, 150.0f, 600.0f, 2.0f, 0.1f};

        struct Replica {
            Replica(): snapshots(&kSnapshotConfig) {}
            Framework::Utils::TransformSnapshotBuffer snapshots;
            MafiaNet::Time lastTransformTime = 0;
            // The newest pose's time and velocity: a stall holds a moving
            // body still.
            double newestTime = 0.0;
            glm::vec3 newestVelocity {0.0f};
            // Dates each pose by when the host sampled it.
            Shared::Net::SampleClockMapper clock;
            // The host's animation and life, each change played when this
            // copy draws the body at its moment (animationClock).
            struct Animation {
                int16_t id = 0;
                bool flag = false;
                bool alive = true;
                bool operator==(const Animation &) const = default;
            };
            Shared::Net::TimedChanges<Animation> animation;
            SDK::Traffic::NativeTrafficElement *element = nullptr;
            // A clip ended: hold its last pose until the host plays the next
            // animation. A corpse never gets one.
            bool holding = false;
            // The host's walker died: retail Hit's death steps ran here.
            bool dead = false;
            // Its last clip ended: the corpse lies as retail leaves it and
            // never animates again.
            bool settled = false;
            // Borrowed already dead: its last clip runs to its end at once.
            bool fallen = false;
            int32_t appliedAlpha = -1;
            // The taxi wave shown, Shared::Taxi::kWave*.
            uint8_t appliedWave = 0;
            std::chrono::steady_clock::time_point holdingSince;
            bool failed = false;
        };

        SDK::Traffic::NativeTrafficElement *Borrow(const std::vector<SDK::Traffic::NativeTrafficGenerator *> &generators, Shared::Entities::PedestrianEntity &walker,
                                                   const Framework::Utils::TransformSnapshot &pose);
        void Release(World::WorldService &world, Replica &replica);

        std::unordered_map<uint64_t, Replica> _replicas;
        // Walkers an NPC replica replaced, while the server still streams them.
        std::unordered_set<uint64_t> _handedOff;
        std::chrono::steady_clock::time_point _lastUpdate;
    };
} // namespace Mafia1Online::Features::Pedestrian
