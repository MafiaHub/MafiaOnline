#pragma once

#include <windows.h>

namespace Mafia1Online::SDK::Core::DataFile {
    // rw_data.dll export #7, audited in docs/server_mods.md. Handles are native
    // table indices (-1 on failure), not Win32 HANDLEs. The flag occupies a byte.
    using Open                               = int(__stdcall *)(const char *path, unsigned char archiveFirst);
    inline constexpr const char *kOpenExport = "_dtaOpen@8";
} // namespace Mafia1Online::SDK::Core::DataFile
