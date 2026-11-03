#pragma once

#include "shared/features/pedestrian/pedestrian_reaction.h"
#include "shared/net/sample_clock.h"

#include <utils/snapshot_buffer.h>

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <random>
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

namespace Mafia1Online::SDK::Car {
    struct NativeCar;
}

namespace Mafia1Online::Shared::Entities {
    class PedestrianEntity;
}

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Car {
    class CarService;
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
        // How a walker reacts to a car is decided where the car really is:
        // on its simulation controller, which runs retail's own rules for it
        // (Shared::Pedestrian::Reaction) and plays the reaction at once. Every
        // other player plays it once its copy of the car is at that moment,
        // and the host applies it to the walker the replicas follow.
        //
        // Controller: a car this client simulates met the replica wearing
        // this model (C_don_ocas::Collision, the car's velocity as the
        // impact): Hit's side-step below knockdown damage, else its fall.
        void OnCarContact(World::WorldService &world, const SDK::Scene::NativeFrame *model, uint64_t carId, SDK::Car::NativeCar &car, const glm::vec3 &velocity);
        // A reaction another car's controller decided, the server checked.
        void OnReaction(const Shared::Pedestrian::Reaction &reaction);

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
            // A car's reaction this copy plays itself, from where it stood,
            // moved by the clips' own root motion: the clip, the one queued
            // behind a fall, then the breath until the host's walker, a
            // render delay behind, is past the reaction too.
            struct Reacting {
                Shared::Pedestrian::ReactionKind kind = Shared::Pedestrian::ReactionKind::Dodge;
                int32_t animation = 0;
                int32_t queued = 0;
                bool fatal = false;
                glm::vec3 facing {0.0f, 0.0f, 1.0f};
                enum class Phase : uint8_t { Clip, Queued, Done } phase = Phase::Clip;
                // The host's walker showed it.
                bool confirmed = false;
                std::chrono::steady_clock::time_point since;
            };
            std::optional<Reacting> reaction;
            // Back on the host's poses after a reaction: the gap to them
            // closes over Shared::Pedestrian::kHandOverSeconds.
            struct HandOver {
                glm::vec3 from {0.0f};
                glm::vec3 fromDirection {0.0f, 0.0f, 1.0f};
                bool measured = false;
                glm::vec3 offset {0.0f};
                std::chrono::steady_clock::time_point since;
            };
            std::optional<HandOver> handOver;
            // Controller: the last contact it decided for this walker.
            std::chrono::steady_clock::time_point lastContact;
            int32_t appliedAlpha = -1;
            // The taxi wave shown, Shared::Taxi::kWave*.
            uint8_t appliedWave = 0;
            std::chrono::steady_clock::time_point holdingSince;
            bool failed = false;
        };

        SDK::Traffic::NativeTrafficElement *Borrow(const std::vector<SDK::Traffic::NativeTrafficGenerator *> &generators, Shared::Entities::PedestrianEntity &walker,
                                                   const Framework::Utils::TransformSnapshot &pose);
        void Release(World::WorldService &world, Replica &replica);
        // A walker the stream dropped: one drawn this pass fades out first,
        // the rest go at once (Release).
        void Retire(World::WorldService &world, uint64_t walkerId, Replica &replica);
        // The walker streamed back while its element still fades: that
        // element, its replica's state kept, shows it again.
        void Reclaim(uint64_t walkerId);
        void AdvanceFades(uint32_t frameMs);
        // Controller: Tick's threat test of every replica against each car
        // this client simulates (0x454221-0x45488d).
        void DetectThreats(World::WorldService &world, Car::CarService &cars);
        // Controller: sends a reaction it decided and plays it here.
        void Decide(World::WorldService &world, Replica &replica, Shared::Pedestrian::Reaction &reaction);
        // Plays a reaction on this copy from now.
        void StartReaction(Replica &replica, const Shared::Pedestrian::Reaction &reaction, uint8_t voice);
        // A reaction's frame; false once this copy follows the host's poses
        // again.
        bool PlayReaction(Replica &replica, int32_t shownAnimation, bool shownAlive, uint32_t frameMs, std::chrono::steady_clock::time_point now);
        // Players: reactions waiting for this client's copy of their car.
        void PlayDueReactions(Car::CarService &cars);

        std::unordered_map<uint64_t, Replica> _replicas;
        // Walkers an NPC replica replaced, while the server still streams them.
        std::unordered_set<uint64_t> _handedOff;
        // Walkers fading out: the element (still active, so no replica
        // borrows it), the opacity left, where it stands, whether its clip
        // still plays there, and the replica it was (Reclaim).
        struct Fading {
            uint64_t walkerId = 0;
            SDK::Traffic::NativeTrafficElement *element = nullptr;
            float alpha = 1.0f;
            glm::vec3 position {0.0f};
            glm::vec3 direction {0.0f};
            bool animating = false;
            Replica replica;
        };
        std::vector<Fading> _fading;
        struct PendingReaction {
            Shared::Pedestrian::Reaction reaction;
            std::chrono::steady_clock::time_point received;
        };
        std::deque<PendingReaction> _pendingReactions;
        // Retail's rnd draws a controller makes: the scream, a fatal fall.
        std::minstd_rand _random {std::random_device {}()};
        std::chrono::steady_clock::time_point _lastUpdate;
    };
} // namespace Mafia1Online::Features::Pedestrian
