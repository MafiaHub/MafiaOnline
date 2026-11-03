#include "cheat_hooks.h"

#include <MinHook.h>
#include <mafia1/sdk/core/game.h>

namespace Mafia1Online::Features::Cheats {
    namespace {
        // No code is ever in progress, so no key is taken from the game.
        bool __fastcall ProcessCheatCodeHook(void *, void *, char) {
            return false;
        }
    } // namespace

    bool InstallHooks() {
        return MH_CreateHook(reinterpret_cast<void *>(SDK::Core::Game::kProcessCheatCode), reinterpret_cast<void *>(&ProcessCheatCodeHook), nullptr) == MH_OK;
    }
} // namespace Mafia1Online::Features::Cheats
