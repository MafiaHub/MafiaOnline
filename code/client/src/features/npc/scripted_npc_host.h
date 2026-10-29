#pragma once

#include "shared/features/npc/npc_animation.h"
#include "shared/features/npc/npc_create.h"
#include "shared/features/npc/npc_order.h"
#include "shared/features/npc/npc_task.h"
#include "shared/features/npc/npc_task_done.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::SDK::Player {
    struct NativeActor;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Npc {
    class NpcHost;

    // Simulation host: the C_entity humans server scripts spawn (Npc.spawn)
    // and the tasks they give them. Each task drives the NPC through the
    // calls the retail entity script commands make (SDK EntityAi), so the
    // retail AI walks, follows, boards, drives and fights. NpcHost tracks
    // and reports them like any other NPC.
    class ScriptedNpcHost final {
      public:
        // Orders for a mission the host has not loaded yet wait this long.
        static constexpr std::chrono::seconds kQueueWait {10};
        static constexpr size_t kMaxQueued = 512;
        // A lost job (a fight, a blocked path) is issued again at most this
        // often, and a walk at most kMaxRetries times.
        static constexpr std::chrono::milliseconds kRetryInterval {1000};
        static constexpr int kMaxRetries = 3;
        // Use_Actor's door animation and the exit must end within this.
        static constexpr std::chrono::seconds kSeatTimeout {10};
        // Enemy_UseCar enters within 3 of the car (reM C_entity.cpp 3431).
        static constexpr float kEnterReachSq = 9.0f;
        // A straight walk that has not come this far closer within
        // kDirectStall is blocked (a wall, a closed door) and fails.
        static constexpr std::chrono::seconds kDirectStall {3};
        static constexpr float kDirectProgress = 0.3f;
        // The longest step one update takes (a hitch, the first update).
        static constexpr uint32_t kDirectMaxStepMs = 100;

        explicit ScriptedNpcHost(NpcHost &host): _host(host) {}

        void RegisterRPC(World::WorldService &world);
        void Update(World::WorldService &world, uint64_t generation);
        void Reset();
        // Npc.remove / destroy of a scripted key.
        void RequestRemove(uint64_t generation, uint32_t key);
        // NpcHost stopped tracking it (retired, destroyed, mission closed).
        void Forget(uint32_t key);

      private:
        enum class MessageKind : uint8_t { Create, Task, Order, Remove, Animation };
        struct Message {
            MessageKind kind = MessageKind::Create;
            uint64_t generation = 0;
            uint32_t key = 0;
            Shared::Npc::Create create;
            Shared::Npc::Task task;
            Shared::Npc::Order order;
            Shared::Npc::Animation animation;
            std::chrono::steady_clock::time_point received;
        };
        enum class Phase : uint8_t { Running, Boarding, Leaving };
        struct Running {
            Shared::Npc::Task task;
            Phase phase = Phase::Running;
            uint32_t jobUid = 0;
            int retries = 0;
            bool driving = false;
            std::chrono::steady_clock::time_point issuedAt;
            std::chrono::steady_clock::time_point phaseAt;
            // WalkDirect: the game clock of the last step, and where it last
            // made progress.
            uint32_t gameTime = 0;
            float progressX = 0.0f;
            float progressZ = 0.0f;
        };

        // false: the message must wait (world not ready, mission not yet
        // loaded).
        bool Apply(World::WorldService &world, uint64_t generation, Message &message);
        void Create(World::WorldService &world, const Shared::Npc::Create &create);
        void Begin(World::WorldService &world, SDK::Player::NativeActor *npc, const Shared::Npc::Task &task);
        void ApplyOrder(SDK::Player::NativeActor *npc, const Shared::Npc::Order &order);
        // PERSON_PLAYANIM / PERSON_STOPANIM; a clip ends the running task.
        void ApplyAnimation(SDK::Player::NativeActor *npc, const Shared::Npc::Animation &animation);
        void Destroy(uint32_t key);
        // Ends the native side of the current task: the car route, the
        // followship and every action.
        void Halt(SDK::Player::NativeActor *npc, Running &running);
        // Issues the task's retail actions; false when its target is gone.
        bool Issue(World::WorldService &world, SDK::Player::NativeActor *npc, Running &running);
        // true: the task ended (reported).
        bool Tick(World::WorldService &world, SDK::Player::NativeActor *npc, Running &running);
        void Finish(uint32_t key, const Running &running, Shared::Npc::TaskDone::Result result);
        SDK::Player::NativeActor *ResolveTarget(World::WorldService &world, const Shared::Npc::Task &task) const;
        SDK::Player::NativeActor *ResolveCar(World::WorldService &world, uint64_t carId) const;

        NpcHost &_host;
        uint64_t _generation = 0;
        std::deque<Message> _queue;
        std::unordered_map<uint32_t, Running> _running;
        // NPCs playing a script clip.
        std::unordered_set<uint32_t> _animating;
    };
} // namespace Mafia1Online::Features::Npc
