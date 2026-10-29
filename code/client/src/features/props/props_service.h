#pragma once

#include "shared/features/world/world_props.h"

#include <mafia1/sdk/world/native_props.h>

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Props {
    // The mission's breakables and physical props, the same for everyone.
    // A prop this client breaks natively (its player's car, a bullet, an
    // explosion, a replica's car here) is reported and the server tells
    // the others, who break theirs the same way. Physical props run on every
    // client; one this client's player (on the simulation host: its traffic,
    // NPCs and trains) sent to sleep away from the server's pose reports it,
    // one a remote player's copy pushed here waits for that client's report
    // and otherwise goes back, and the newest pose the server sends is
    // placed, asleep. A late joiner gets the whole state once its mission has
    // loaded, without debris; a repair puts every breakable back up and
    // every physical prop where it began.
    class PropsService final {
      public:
        bool Install();
        void Uninstall();
        void RegisterRPC();
        void Update(World::WorldService &world);
        // The mission's props are going (mission close, disconnect).
        void Reset();

        void OnPhysicalGameInit(SDK::World::NativePhysicalProp *prop);
        void OnPhysicalGameDone(SDK::World::NativePhysicalProp *prop);
        // C_box_new::Collision or Hit: actor woke the prop.
        void OnPhysicalDisturbed(SDK::World::NativePhysicalProp *prop, SDK::Player::NativeActor *actor);
        // A native SpawnCrashActorFromFrame on this client.
        bool OnSpawnCrash(SDK::Scene::NativeFrame *struck, const SDK::Player::Vector3 *origin, const SDK::Player::Vector3 *impulse, float force);

      private:
        // Who set a physical prop moving since it last slept where the
        // server has it, by precedence.
        enum class Cause : uint8_t {
            None,
            // A copy of someone else's player, car or NPC: its own client
            // (the simulation host for traffic and NPCs) reports.
            Remote,
            // No actor, or one nobody owns (a blast, an empty car): the
            // player nearest to the prop reports.
            Unattributed,
            // This client's player or the car it drives; on the simulation
            // host, everything but the players and their cars.
            Local,
        };
        // A breakable this client turned off, with the collision primitives
        // that break switched off, to switch them back on.
        struct Broken {
            SDK::Scene::NativeFrame *model = nullptr;
            std::vector<uint32_t *> primitives;
        };
        struct Physical {
            SDK::World::NativePhysicalProp *prop = nullptr;
            uint32_t nameHash = 0;
            SDK::World::NativePhysicalProp::Transform spawnTransform;
            Shared::World::Props::Pose spawn;
            // The pose the server has (or this client just reported).
            Shared::World::Props::Pose synced;
            Cause cause = Cause::None;
            std::chrono::steady_clock::time_point lastReport {};
            // Asleep away from synced without a report of its own since.
            std::chrono::steady_clock::time_point strayedAt {};
        };

        // Runs the native break and records what it switched off; the
        // model it broke, or null. live: the model's primitives still on,
        // when the caller has collected them already.
        SDK::Scene::NativeFrame *Spawn(SDK::Scene::NativeFrame *struck, const SDK::Player::Vector3 &origin, const SDK::Player::Vector3 &impulse, float force,
                                       std::vector<uint32_t *> live = {});
        void SendBreak(const Shared::World::Props::Break &prop);
        void ApplyBreak(Shared::World::Props::Break prop, bool silent);
        void ApplyPose(Physical &entry, const Shared::World::Props::Pose &pose);
        void Place(Physical &entry, const Shared::World::Props::Pose &pose);
        void ScanPhysical();
        void RepairAll();
        [[nodiscard]] Shared::World::Props::Pose PoseOf(size_t index) const;
        [[nodiscard]] Cause CauseOf(SDK::Player::NativeActor *actor) const;
        [[nodiscard]] bool IsPlayerReplica(SDK::Player::NativeActor *human) const;
        // This client's player is spawned and no other player is closer.
        [[nodiscard]] bool IsNearestPlayer(const Shared::World::Props::Vector &position) const;

        std::vector<Broken> _broken;
        std::vector<Physical> _physical;
        std::unordered_map<const void *, size_t> _physicalIndex;
        // Breaks from before this client knew the mission's generation.
        std::vector<Shared::World::Props::Break> _pendingBreaks;
        World::WorldService *_world = nullptr;
        uint64_t _missionGeneration = 0;
        std::chrono::steady_clock::time_point _lastScan {};
        bool _reportedMismatch = false;
    };
} // namespace Mafia1Online::Features::Props
