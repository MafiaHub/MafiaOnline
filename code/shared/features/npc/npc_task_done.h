#pragma once

#include "shared/features/npc/npc_task.h"

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Npc {
    // Host -> server: how a scripted NPC's task ended.
    struct TaskDone {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcTaskDone");

        enum class Result : uint8_t {
            // Arrived, seated, out of the car, or the target is dead.
            Completed = 0,
            // The retail AI gave up: no way there, no car seat, the NPC died.
            Failed = 1,
            // Another task or Npc.stop replaced it.
            Cancelled = 2,
        };

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint32_t taskId = 0;
        TaskType type = TaskType::Stop;
        Result result = Result::Completed;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto typeValue = static_cast<uint8_t>(type);
            auto resultValue = static_cast<uint8_t>(result);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, taskId);
            stream->Serialize(write, typeValue);
            stream->Serialize(write, resultValue);
            if (!write) {
                type = static_cast<TaskType>(typeValue);
                result = static_cast<Result>(resultValue);
            }
        }
    };

    inline const char *TaskResultName(TaskDone::Result result) {
        switch (result) {
        case TaskDone::Result::Completed: return "completed";
        case TaskDone::Result::Failed: return "failed";
        case TaskDone::Result::Cancelled: return "cancelled";
        }
        return "failed";
    }
} // namespace Mafia1Online::Shared::Npc
