#include "core/application.h"
#include "mod_service.h"
#include <utils/safe_win32.h>

#include <MinHook.h>
#include <core_modules.h>
#include <mafia1/sdk/core/data_file.h>

namespace Mafia1Online::Features::Mod {
    namespace {
        SDK::Core::DataFile::Open gOpenOriginal = nullptr;

        int __stdcall OpenHook(const char *path, unsigned char archiveFirst) {
            auto &application      = *static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            const auto replacement = path ? application.Mods().Resolve(path) : std::string();
            if (!replacement.empty())
                return gOpenOriginal(replacement.c_str(), 0);
            return gOpenOriginal(path, archiveFirst);
        }
    } // namespace

    bool ModService::Install() {
        _openAddress = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("rw_data.dll"), SDK::Core::DataFile::kOpenExport));
        if (!_openAddress)
            return false;
        if (MH_CreateHook(_openAddress, reinterpret_cast<void *>(&OpenHook), reinterpret_cast<void **>(&gOpenOriginal)) != MH_OK)
            return false;
        if (MH_EnableHook(_openAddress) == MH_OK)
            return true;
        MH_RemoveHook(_openAddress);
        return false;
    }

    void ModService::Shutdown() {
        Reset();
        // Drains canceled extraction before releasing the service at process shutdown.
        _retired.clear();
        if (_openAddress) {
            MH_DisableHook(_openAddress);
            MH_RemoveHook(_openAddress);
            _openAddress = nullptr;
        }
    }
} // namespace Mafia1Online::Features::Mod
