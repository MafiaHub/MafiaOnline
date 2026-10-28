#pragma once

#include "shared/features/npc/npc_entity.h"

#include <networking/rpc/rpc.h>

#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Npc {
    // A bare .i3d clip name, as the retail scripts give PERSON_PLAYANIM:
    // letters, digits, spaces, dashes, underscores and dots only, so it
    // never names a path.
    inline bool AnimationNameValid(const std::string &name) {
        if (name.size() < 5 || name.size() > Entities::NpcEntity::kMaxAnimationLength) {
            return false;
        }
        for (const char c : name) {
            const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '.';
            if (!allowed) {
                return false;
            }
        }
        const std::string extension = name.substr(name.size() - 4);
        return (extension[0] == '.') && (extension[1] == 'i' || extension[1] == 'I') && extension[2] == '3' && (extension[3] == 'd' || extension[3] == 'D');
    }

    // Server -> host: play a clip on a scripted NPC the way PERSON_PLAYANIM
    // does (C_human::Do_PlayAnim(name, simple, 1)), or stop it
    // (PERSON_STOPANIM, C_human::Do_StopAnim) when the name is empty. It
    // cancels the running task. Players replay the same clip on their copy
    // from NpcEntity's animation fields.
    struct Animation {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcAnimation");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        std::string name;
        bool loop = true;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, name);
            stream->Serialize(write, loop);
            if (!write && !name.empty() && !AnimationNameValid(name)) {
                name.clear();
            }
        }
    };
} // namespace Mafia1Online::Shared::Npc
