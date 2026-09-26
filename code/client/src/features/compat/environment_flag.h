#pragma once

#include <utils/safe_win32.h>

#include <iterator>

namespace Mafia1Online::Features::Compat {
    inline bool EnvironmentFlagEnabled(const wchar_t *name) {
        wchar_t value[16] {};
        const DWORD length = GetEnvironmentVariableW(name, value, static_cast<DWORD>(std::size(value)));
        return length != 0 && !(length == 1 && value[0] == L'0');
    }
} // namespace Mafia1Online::Features::Compat
