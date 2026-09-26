#pragma once

#include <cstdint>

namespace Mafia1Online::SDK::Car {
    // G_CarDatabase at 0x6d4560. The supported Game.exe initializes it from
    // the highest-priority tables\carindex.def archive before a mission runs.
    // Its record count and the native getters are verified against reM and the
    // retail image. Do not parse a lower-priority archive's table instead.
    struct NativeCarCatalog {
        uint32_t count;
        void *records;

        const char *Name(uint32_t id) {
            using Call = const char *(__thiscall *)(NativeCarCatalog *, uint32_t);
            return reinterpret_cast<Call>(0x60a510)(this, id);
        }
        bool Model(uint32_t id, char *filename) {
            using Call = bool(__thiscall *)(NativeCarCatalog *, uint32_t, unsigned char *, bool);
            return reinterpret_cast<Call>(0x60a6a0)(this, id, reinterpret_cast<unsigned char *>(filename), false);
        }
    };

    inline NativeCarCatalog *GetCarCatalog() {
        return reinterpret_cast<NativeCarCatalog *>(0x6d4560);
    }
} // namespace Mafia1Online::SDK::Car
