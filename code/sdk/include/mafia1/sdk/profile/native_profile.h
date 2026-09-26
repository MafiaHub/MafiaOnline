#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Profile {
    inline constexpr uintptr_t kLoadSave = 0x6d4628;
    inline constexpr uintptr_t kSelect = 0x605570;
    inline constexpr uintptr_t kCreate = 0x6055c0;
    inline constexpr uintptr_t kDelete = 0x605740;
    inline constexpr uintptr_t kSave = 0x6059f0;
    inline constexpr uintptr_t kSaveGame = 0x606ea0;

    struct NativeProfileHeader {
        std::byte _data[0x54];
    };
    static_assert(sizeof(NativeProfileHeader) == 0x54);

    // G_LoadSaveClass begins with MSVC's retail vector storage. It is valid
    // during ProfileEnumFiles..ProfileEnumRelease in the startup picker only.
    struct NativeLoadSave {
        [[nodiscard]] bool HasProfile(uint32_t index) const {
            return _begin && index < static_cast<uint32_t>(_end - _begin);
        }

        void SelectProfile(uint32_t index) {
            using Call = void(__thiscall *)(NativeLoadSave *, uint32_t);
            reinterpret_cast<Call>(kSelect)(this, index);
        }

        void *_allocator;
        NativeProfileHeader *_begin;
        NativeProfileHeader *_end;
        NativeProfileHeader *_capacity;
    };
    static_assert(offsetof(NativeLoadSave, _begin) == 0x04);
    static_assert(offsetof(NativeLoadSave, _end) == 0x08);

    inline NativeLoadSave &Get() {
        return *reinterpret_cast<NativeLoadSave *>(kLoadSave);
    }
} // namespace Mafia1Online::SDK::Profile
