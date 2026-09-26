#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include <windows.h>

namespace Mafia1Online::SDK::Core::DataFile {
    // rw_data.dll export #7, audited in docs/server_mods.md. Handles are native
    // table indices (-1 on failure), not Win32 HANDLEs. The flag occupies a byte.
    using Open                               = int(__stdcall *)(const char *path, unsigned char archiveFirst);
    inline constexpr const char *kOpenExport = "_dtaOpen@8";

    inline bool ReadAll(const char *path, std::vector<uint8_t> &bytes) {
        // Verified rw_data exports; calls use the normal mod-aware open hook.
        static const auto module = GetModuleHandleA("rw_data.dll");
        static const auto open   = reinterpret_cast<Open>(GetProcAddress(module, kOpenExport));
        static const auto read   = reinterpret_cast<unsigned int(__stdcall *)(unsigned int, void *, unsigned int)>(GetProcAddress(module, "_dtaRead@12"));
        static const auto seek   = reinterpret_cast<int(__stdcall *)(unsigned int, int, int)>(GetProcAddress(module, "_dtaSeek@12"));
        static const auto close  = reinterpret_cast<bool(__stdcall *)(unsigned int)>(GetProcAddress(module, "_dtaClose@4"));
        const int handle         = open(path, 0);
        if (handle < 0)
            return false;
        const int size = seek(handle, 0, 2);
        if (size < 6 || size > 64 * 1024 * 1024 || seek(handle, 0, 0) != 0) {
            close(handle);
            return false;
        }
        std::vector<uint8_t> content(static_cast<size_t>(size));
        const auto count = read(handle, content.data(), static_cast<unsigned int>(size));
        close(handle);
        if (count != static_cast<unsigned int>(size))
            return false;
        bytes = std::move(content);
        return true;
    }
} // namespace Mafia1Online::SDK::Core::DataFile
