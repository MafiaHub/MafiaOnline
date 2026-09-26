#include <utils/safe_win32.h>

#include "profile_write_hooks.h"

#include <MinHook.h>
#include <mafia1/sdk/profile/native_profile.h>

#include <array>

namespace Mafia1Online::Features::Profile {
    namespace {
        bool __fastcall ProfileCreateHook(SDK::Profile::NativeLoadSave *, void *, unsigned char *) {
            return false;
        }

        void __fastcall ProfileDeleteHook(SDK::Profile::NativeLoadSave *, void *, uint32_t) {}
        void __fastcall ProfileSaveHook(SDK::Profile::NativeLoadSave *, void *) {}
        void __fastcall SaveGameHook(SDK::Profile::NativeLoadSave *, void *, bool) {}

        constexpr std::array<uintptr_t, 4> kHookAddresses {
            SDK::Profile::kCreate,
            SDK::Profile::kDelete,
            SDK::Profile::kSave,
            SDK::Profile::kSaveGame,
        };
    } // namespace

    bool InstallWriteHooks() {
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Profile::kCreate), reinterpret_cast<void *>(&ProfileCreateHook), nullptr) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Profile::kDelete), reinterpret_cast<void *>(&ProfileDeleteHook), nullptr) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Profile::kSave), reinterpret_cast<void *>(&ProfileSaveHook), nullptr) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::Profile::kSaveGame), reinterpret_cast<void *>(&SaveGameHook), nullptr) != MH_OK) {
            UninstallWriteHooks();
            return false;
        }
        return true;
    }

    void UninstallWriteHooks() {
        for (const uintptr_t address : kHookAddresses) {
            MH_DisableHook(reinterpret_cast<void *>(address));
            MH_RemoveHook(reinterpret_cast<void *>(address));
        }
    }
} // namespace Mafia1Online::Features::Profile
