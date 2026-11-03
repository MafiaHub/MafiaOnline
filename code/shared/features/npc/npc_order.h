#pragma once

#include <networking/rpc/rpc.h>

#include <cstdint>

namespace Mafia1Online::Shared::Npc {
    // Server -> host: change a scripted NPC's state. The host's next reports
    // carry the result (InventoryChanged, Snapshot health).
    struct Order {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcOrder");

        enum class Action : uint8_t {
            // HUMAN_ADDWEAPON: G_Inventory::Insert, ChangeWeaponModel,
            // WeaponChoose of the selected item.
            GiveWeapon = 0,
            // The current health (and the initial health when higher).
            SetHealth = 1,
            // PROGRAM_CMD_ACTOR_SETDIR: the actor's direction and its
            // frame's, turned about the vertical axis to heading.
            Face = 2,
        };

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        Action action = Action::GiveWeapon;
        uint8_t weaponId = 0;
        uint16_t loaded = 0;
        uint16_t reserve = 0;
        float health = 0.0f;
        // Face: radians, 0 along +z.
        float heading = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto actionValue = static_cast<uint8_t>(action);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, actionValue);
            stream->Serialize(write, weaponId);
            stream->Serialize(write, loaded);
            stream->Serialize(write, reserve);
            stream->Serialize(write, health);
            stream->Serialize(write, heading);
            if (!write) {
                action = static_cast<Action>(actionValue);
            }
        }
    };
} // namespace Mafia1Online::Shared::Npc
