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
        void Update(World::WorldService &world);
        void Reset(World::WorldService &world);
        // The actor destructor: the frame to delete after it, if it was ours.
        SDK::Scene::NativeFrame *OnNativeDestroyed(World::WorldService &world, void *actor);
        // Npc::Seized: the officer's half of a police arrest or fine on its
        // replica, from where the host's officer stands (state 10's place).
        // The pose stream stays off it until the track ends.
        void PlaySeizing(World::WorldService &world, uint64_t officerId, const Shared::Npc::Seized::Pose &pose, Shared::Npc::Caught::Outcome outcome);
        // Whether an action the host dated `hostClock` on the clock it samples
        // this NPC's poses by (a melee attack, or its blow landing), received
        // at `received`, may play now: once the replica, drawn a render delay
        // behind the host, shows that moment. True for anything else.
        [[nodiscard]] bool Reached(uint64_t networkId, uint16_t hostClock, std::chrono::steady_clock::time_point received) const;
        // Whether this NPC's death may play: its replica draws the body at
        // the moment the host's died. True for any other network id.
        [[nodiscard]] bool DeathShown(uint64_t networkId) const;
        // This NPC's attack is aimed at the local player: until `until` its
        // replica closes to striking range of it (ApplyContact). Only this
        // screen's picture; the server checks hits by the host's poses.
        void Engage(uint64_t networkId, std::chrono::steady_clock::time_point until);

      private:
        // The host reports every 100 ms, and the server's far band every
        // 200 ms. Two intervals of render delay, plus how late packets come,
        // keep a bracket for interpolation through one lost packet.
        inline static const Framework::Utils::SnapshotBufferConfig kSnapshotConfig {200.0f, 250.0f, 4.0f, 4.0f, true, 150.0f, 600.0f, 2.0f, 0.1f};

        struct Replica {
            Replica(): snapshots(&kSnapshotConfig) {}
            Framework::Utils::TransformSnapshotBuffer snapshots;
            std::deque<std::pair<MafiaNet::Time, int16_t>> locomotion;
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
            // The end of a replayed pull-out (SeatService), if one is playing.
            std::chrono::steady_clock::time_point pullingUntil {};
            // The script clip (NpcEntity::animation) last applied, and until
            // when it holds the locomotion stream off (max() for a loop).
            uint16_t animationRevision = 0;
            bool animationSynced = false;
            // The last npc.say count heard (NpcEntity::speechRevision).
            uint16_t speechRevision = 0;
            std::chrono::steady_clock::time_point animatingUntil {};
            // The script clip playing on this copy, empty for none.
            Shared::Npc::ScriptClip clip;
            // A police arrest half (PlaySeizing) holds the pose too: the
            // clip moves the body from the aligned place.
            std::chrono::steady_clock::time_point seizingUntil {};
            // Engage: how far (0..1) the replica stands at `contactSpot`
            // instead of its pose, and when that was last eased.
            std::chrono::steady_clock::time_point engagedUntil {};
            float contact = 0.0f;
            glm::vec3 contactSpot {0.0f};
            std::chrono::steady_clock::time_point contactAt {};
            // Its death: when a channel first said so, whether this copy
            // got to the host's moment of it (DeathShown), and whether it
            // ever drew the body alive. SettleCorpse last eased it at
            // settledAt.
            std::chrono::steady_clock::time_point killedAt {};
            bool deathShown = false;
            bool seenAlive = false;
            std::chrono::steady_clock::time_point settledAt {};
            bool failed = false;
        };

        // A copy drawn this pass fades out first, as a retail walker leaving
        // its despawn radius in view does (C_traffic_element::Tick); `fade`
        // false removes it at once.
        void Remove(World::WorldService &world, uint64_t networkId, const Replica &replica, bool fade);
        void AdvanceFades();
        void ApplyPose(Replica &replica, void *actor);
        // Once the death plays: draws the corpse onto the host's, as it
        // falls and where it rests.
        void SettleCorpse(Replica &replica, void *actor);
        // Eases the pose toward striking range of the local player while
        // engaged, and back to the pose after.
        void ApplyContact(Replica &replica, const Framework::Utils::TransformSnapshot &pose, glm::vec3 &position, glm::vec3 &direction);
        // Replays npc.playAnimation / stopAnimation as PERSON_PLAYANIM and
        // PERSON_STOPANIM do on the host.
        void ApplyAnimation(Replica &replica, void *actor, const Shared::Entities::NpcEntity &npc);

        std::unordered_map<uint64_t, Replica> _replicas;
        // Every human this created, until its destructor hands the frame back.
        std::unordered_map<void *, SDK::Scene::NativeFrame *> _frames;
        // Copies whose NPC went, fading out: the actor, its "base" visual
        // (SetTransparency) and the opacity left.
        struct Fading {
            void *actor = nullptr;
            SDK::Scene::NativeFrame *base = nullptr;
            float alpha = 1.0f;
        };
        std::vector<Fading> _fading;
        std::chrono::steady_clock::time_point _fadedAt {};
    };
} // namespace Mafia1Online::Features::Npc
