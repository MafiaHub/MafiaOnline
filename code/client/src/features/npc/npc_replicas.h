#pragma once

#include "game/entities/native_object_registry.h"
#include "shared/features/npc/npc_animation.h"
#include "shared/features/npc/npc_seized.h"
#include "shared/net/sample_clock.h"

#include <utils/snapshot_buffer.h>

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

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
        struct Stance {
            bool aiming = false;
            bool crouching = false;
            uint8_t weapon = 0;
        };

        void Update(World::WorldService &world);
        void Reset(World::WorldService &world);
        // The actor destructor: the frame to delete after it, if it was ours.
        SDK::Scene::NativeFrame *OnNativeDestroyed(World::WorldService &world, void *actor);
        // Npc::Seized: the officer's half of a police arrest or fine on its
        // replica, from where the host's officer stands (state 10's place).
        // The pose stream stays off it until the track ends.
        void PlaySeizing(World::WorldService &world, uint64_t officerId, const Shared::Npc::Seized::Pose &pose, Shared::Npc::Caught::Outcome outcome);
        // Whether an action the host dated `hostClock` on the clock it samples
        // this NPC's poses by (an attack, a shot or reload, a hit landing), received
        // at `received`, may play now: once the replica, drawn a render delay
        // behind the host, shows that moment. True for anything else.
        [[nodiscard]] bool Reached(uint64_t networkId, uint16_t hostClock, std::chrono::steady_clock::time_point received) const;
        // Whether this NPC's death may play: its replica draws the body at
        // the moment the host's died. True for any other network id.
        [[nodiscard]] bool DeathShown(uint64_t networkId) const;
        // The aim and crouch the host's NPC had at the moment its replica
        // shows, which CombatService applies in place of the Combat::State's
        // newest; nothing for any other network id.
        [[nodiscard]] std::optional<Stance> DisplayedStance(uint64_t networkId) const;
        // This NPC's attack is aimed at the local player: until `until` its
        // replica closes to striking range of it (ApplyContact). Only this
        // screen's picture; the server checks hits by the host's poses.
        void Engage(uint64_t networkId, std::chrono::steady_clock::time_point until);
        // A copy fading out after its NPC went (Remove) that was alive when
        // it began to: bound to no network id any more, it is still only a
        // picture of the host's NPC, which no local hit may kill
        // (DeathService::Protect).
        [[nodiscard]] bool FadingAlive(const void *actor) const;

      private:
        // The host reports every 100 ms, and the server's far band every
        // 200 ms. Two intervals of render delay, plus how late packets come,
        // keep a bracket for interpolation through one lost packet.
        inline static const Framework::Utils::SnapshotBufferConfig kSnapshotConfig {200.0f, 250.0f, 4.0f, 4.0f, true, 150.0f, 600.0f, 2.0f, 0.1f};

        struct Replica {
            Replica(): snapshots(&kSnapshotConfig) {}
            Framework::Utils::TransformSnapshotBuffer snapshots;
            // What rides with each pose: the locomotion state and stance.
            struct Gait {
                MafiaNet::Time at = 0;
                int16_t locomotion = 1;
                bool aiming = false;
                bool crouching = false;
                uint8_t weapon = 0;
            };
            std::deque<Gait> locomotion;
            // The stance at this frame's presentation time, once a pose came.
            std::optional<Stance> stance;
            // The newest pose's velocity: a stall holds a moving body still.
            glm::vec3 newestVelocity {0.0f};
            MafiaNet::Time lastTransformTime = 0;
            // This frame's presentation time, shared by pose and locomotion.
            double renderTime = 0.0;
            // Dates each pose by when the host sampled it.
            Shared::Net::SampleClockMapper clock;
            Game::Entities::NativeObjectHandle handle;
            SDK::Scene::NativeFrame *frame = nullptr;
            int16_t lastAnimation = 0;
            // Frames the cover call for the streamed state was refused.
            int cornerTries = 0;
            // The end of a replayed pull-out (SeatService), if one is playing.
            std::chrono::steady_clock::time_point pullingUntil {};
            // The script clip (NpcEntity::animation) last applied, and until
            // when it holds the locomotion stream off (max() for a loop).
            uint16_t animationRevision = 0;
            bool animationSynced = false;
            // A newer clip held for its moment: its revision, when it came
            // and when its date (NpcEntity::animationClock) did.
            uint16_t clipRevisionSeen = 0;
            std::chrono::steady_clock::time_point clipSeenAt {};
            std::chrono::steady_clock::time_point clipDatedAt {};
            // The last npc.say count heard (NpcEntity::speechRevision).
            uint16_t speechRevision = 0;
            std::chrono::steady_clock::time_point animatingUntil {};
            // The script clip playing on this copy, empty for none.
            Shared::Npc::ScriptClip clip;
            // A police arrest half (PlaySeizing) holds the pose too: the
            // clip moves the body from the aligned place.
            std::chrono::steady_clock::time_point seizingUntil {};
            // Engage: how far (0..1) the replica turns to the player, how
            // far it stands from its pose toward `contactSpot`, and when
            // that was last eased. Released, it walks back from
            // contactReleaseLength away, turning from contactReleaseWeight.
            std::chrono::steady_clock::time_point engagedUntil {};
            float contact = 0.0f;
            glm::vec3 contactSpot {0.0f};
            glm::vec3 contactOffset {0.0f};
            float contactReleaseLength = 0.0f;
            float contactReleaseWeight = 0.0f;
            std::chrono::steady_clock::time_point contactAt {};
            // Its death: when a channel first said so, whether this copy
            // got to the host's moment of it (DeathShown), and whether it
            // ever drew the body alive. SettleCorpse last eased it at
            // settledAt.
            std::chrono::steady_clock::time_point killedAt {};
            bool deathShown = false;
            bool seenAlive = false;
            std::chrono::steady_clock::time_point settledAt {};
            // Since when its own animation has moved the body (a hit
            // reaction, a timed action, a tracked clip), and whether any
            // hold (that, a door, a pull-out, an arrest) kept the stream off
            // it. On release it eases back to the stream from
            // releaseOffset and releaseDirection (BlendFromHold).
            std::chrono::steady_clock::time_point rootMotionSince {};
            bool poseHeld = false;
            glm::vec3 releaseOffset {0.0f};
            glm::vec3 releaseDirection {0.0f};
            std::chrono::steady_clock::time_point releasedAt {};
            bool failed = false;
        };

        // A copy drawn this pass fades out first, as a retail walker leaving
        // its despawn radius in view does (C_traffic_element::Tick); `fade`
        // false removes it at once.
        void Remove(World::WorldService &world, uint64_t networkId, Replica &replica, bool fade);
        // The NPC streamed back while its copy still fades: that copy, its
        // state kept, is opaque and bound to it again.
        void Reclaim(World::WorldService &world, uint64_t networkId);
        void AdvanceFades();
        // What rode with the pose drawn at `time`: the newest at or before
        // it, else the oldest held.
        static const Replica::Gait *GaitAt(const Replica &replica, double time);
        void ApplyPose(Replica &replica, void *actor);
        // The stream stays off the body this frame.
        static void HoldPose(Replica &replica);
        // After a hold: from where the body was left back to the stream's
        // `position` and `direction`, which it adjusts.
        static void BlendFromHold(Replica &replica, void *actor, glm::vec3 &position, glm::vec3 &direction, std::chrono::steady_clock::time_point now);
        // Once the death plays: draws the corpse onto the host's, as it
        // falls and where it rests.
        void SettleCorpse(Replica &replica, void *actor);
        // Eases the pose toward striking range of the local player while
        // engaged and its way there is clear, and walks it back to the pose
        // after: the walk to show meanwhile if the stream stands.
        std::optional<int16_t> ApplyContact(Replica &replica, const Framework::Utils::TransformSnapshot &pose, glm::vec3 &position, glm::vec3 &direction);
        // Replays npc.playAnimation / stopAnimation as PERSON_PLAYANIM and
        // PERSON_STOPANIM do on the host.
        void ApplyAnimation(Replica &replica, void *actor, const Shared::Entities::NpcEntity &npc);

        std::unordered_map<uint64_t, Replica> _replicas;
        // Every human this created, until its destructor hands the frame back.
        std::unordered_map<void *, SDK::Scene::NativeFrame *> _frames;
        // Copies whose NPC went, fading out: its network id and replica
        // (Reclaim), the actor, its "base" visual (SetTransparency), the
        // opacity left and whether it was alive when it began to fade.
        struct Fading {
            uint64_t networkId = 0;
            Replica replica;
            void *actor = nullptr;
            SDK::Scene::NativeFrame *base = nullptr;
            float alpha = 1.0f;
            bool alive = false;
        };
        std::vector<Fading> _fading;
        std::chrono::steady_clock::time_point _fadedAt {};
    };
} // namespace Mafia1Online::Features::Npc
