#pragma once

#include "shared/features/npc/npc_entity.h"

#include <networking/rpc/rpc.h>

#include <algorithm>
#include <cctype>
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

    // A looping script clip, as the host and every replica keep it looping.
    // C_human::Do_PlayAnim (0x585e80) passes the loop flag to SetAnim as
    // finite = !loop, but SetAnim (0x40e950) forces finite playback once it
    // finds the clip's displacement track (0x40eac7-0x40eaee), and only then
    // does Do_PlayAnim put the human in work state 4 (0x585f16). Such a clip
    // plays once whatever the flag; at its end C_human::Update drops the
    // human to work state 1 and the stop pose. Untracked clips loop on the
    // animation stage by themselves, the work state untouched.
    inline constexpr int32_t kScriptClipIdleWorkState = 1;
    inline constexpr int32_t kScriptClipPlayingWorkState = 4;

    struct ScriptClip {
        std::string name;
        bool loop = false;
        // Do_PlayAnim found a displacement track (work state 4 right after).
        bool tracked = false;

        // Only idle (an untracked clip on the animation stage) and the
        // clip's own work state keep it; anything else (a hit reaction,
        // a move, a death) replaced it, and the record is dropped.
        [[nodiscard]] static bool Holds(int32_t workState) {
            return workState == kScriptClipIdleWorkState || workState == kScriptClipPlayingWorkState;
        }
        // The clip still runs: an untracked one loops by itself.
        [[nodiscard]] bool Running(int32_t workState) const {
            return !tracked || workState == kScriptClipPlayingWorkState;
        }
        // A tracked loop that ended and left a living human standing idle
        // plays again; a hit reaction, task or death in its place wins.
        [[nodiscard]] bool NeedsReplay(bool alive, int32_t workState) const {
            return loop && tracked && alive && workState == kScriptClipIdleWorkState;
        }
        // npc.playAnimation of the loop already running (a routine's
        // refresh) keeps it going instead of restarting it.
        [[nodiscard]] bool Continues(const std::string &requested, bool requestedLoop, int32_t workState) const {
            return loop && requestedLoop && Holds(workState) && Running(workState) && requested.size() == name.size() &&
                   std::equal(requested.begin(), requested.end(), name.begin(), [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                   });
        }
    };

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
