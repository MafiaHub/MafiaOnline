#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Npc {
    // What a scripted NPC is told to do; each replaces the one before.
    enum class TaskType : uint8_t {
        // Entity_ActionsClear: drop every action, stand.
        Stop = 0,
        // Entity_ActionFollow behind a player or NPC, until another task.
        Follow = 1,
        // EntityMoveToFrame to a point, walking or running.
        WalkTo = 2,
        // Entity_MoveToCar, then Enemy_UseCar's Use_Actor enter.
        EnterVehicle = 3,
        // Enemy_UseCar's Use_Actor leave.
        ExitVehicle = 4,
        // Entity_CarMoveTo: drive the car it sits in to a point.
        DriveTo = 5,
        // Entity_ActionFire at a player or NPC, until it is dead.
        Attack = 6,
    };

    inline bool KnownTaskType(TaskType type) {
        return static_cast<uint8_t>(type) <= static_cast<uint8_t>(TaskType::Attack);
    }

    inline const char *TaskName(TaskType type) {
        switch (type) {
        case TaskType::Stop: return "stop";
        case TaskType::Follow: return "follow";
        case TaskType::WalkTo: return "walkTo";
        case TaskType::EnterVehicle: return "enterVehicle";
        case TaskType::ExitVehicle: return "exitVehicle";
        case TaskType::DriveTo: return "driveTo";
        case TaskType::Attack: return "attack";
        }
        return "unknown";
    }

    // Server -> host: give a scripted NPC a task. The host reports how it
    // ends with a TaskDone of the same id.
    struct Task {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcTask");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint32_t taskId = 0;
        TaskType type = TaskType::Stop;
        // The player to follow or attack, or the car to enter (network id).
        uint64_t targetId = 0;
        // An NPC to follow or attack instead (its key): the host never binds
        // its own NPCs as network objects.
        uint32_t targetKey = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        // Follow distance, or how close WalkTo must come.
        float distance = 0.0f;
        bool run = false;
        uint8_t seat = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto typeValue = static_cast<uint8_t>(type);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, taskId);
            stream->Serialize(write, typeValue);
            stream->Serialize(write, targetId);
            stream->Serialize(write, targetKey);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, distance);
            stream->Serialize(write, run);
            stream->Serialize(write, seat);
            if (!write) {
                type = static_cast<TaskType>(typeValue);
            }
        }
    };
} // namespace Mafia1Online::Shared::Npc
