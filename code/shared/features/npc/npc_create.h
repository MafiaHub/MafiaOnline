#pragma once

#include "shared/features/npc/npc_entity.h"

#include <networking/rpc/rpc.h>

#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Npc {
    // Keys of script-spawned NPCs carry this bit; the server picks them, the
    // host's own NPCs count up from 1 and never reach it.
    inline constexpr uint32_t kScriptedKeyBit = 0x80000000u;

    inline bool IsScriptedKey(uint32_t key) {
        return (key & kScriptedKeyBit) != 0;
    }

    // How a scripted NPC rates players and other NPCs: the retail behaviour
    // class C_entity::GameInit derives from the human's behaviour mask, which
    // ai_logs::Event uses to pick an attitude (players are class 0, guard).
    enum class Behaviour : uint8_t {
        // Reacts on hit (class 3): no attitude to players until one aims at
        // or shoots it.
        Neutral = 0,
        // Guard (class 0): every player and other friendly NPC is a friend,
        // gangsters (class 4) are enemies.
        Friendly = 1,
        // Moves away (class 4), as the gangsters CreateMafianos makes: every
        // player and friendly NPC is an enemy.
        Hostile = 2,
    };

    inline bool KnownBehaviour(Behaviour behaviour) {
        return behaviour == Behaviour::Neutral || behaviour == Behaviour::Friendly || behaviour == Behaviour::Hostile;
    }

    inline const char *BehaviourName(Behaviour behaviour) {
        switch (behaviour) {
        case Behaviour::Neutral: return "neutral";
        case Behaviour::Friendly: return "friendly";
        case Behaviour::Hostile: return "hostile";
        }
        return "neutral";
    }

    // Server -> host: create a scripted NPC (Npc.spawn). The host answers
    // with a Spawned of the same key, or a Retired when it cannot.
    struct Create {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcCreate");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        std::string model;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        float health = 0.0f;
        Behaviour behaviour = Behaviour::Neutral;
        // A weapon to hold from the start, or 0.
        uint8_t weaponId = 0;
        uint16_t loaded = 0;
        uint16_t reserve = 0;
        // The reported walker (Pedestrian::Spawned key) it takes the place
        // of, or 0: the host makes it where that walker stands and retires
        // the walker, as retail turns a hailing walker into its taxi
        // passenger (MavaNaTaxi 0x452335-0x452c46).
        uint32_t walkerKey = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto behaviourValue = static_cast<uint8_t>(behaviour);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, model);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, yaw);
            stream->Serialize(write, health);
            stream->Serialize(write, behaviourValue);
            stream->Serialize(write, weaponId);
            stream->Serialize(write, loaded);
            stream->Serialize(write, reserve);
            stream->Serialize(write, walkerKey);
            if (!write) {
                behaviour = static_cast<Behaviour>(behaviourValue);
                if (model.size() > Entities::NpcEntity::kMaxModelLength) {
                    model.clear();
                }
            }
        }
    };
} // namespace Mafia1Online::Shared::Npc
