#include <utils/safe_win32.h>

#include "ls3df_index_leak.h"

#include <MinHook.h>
#include <mafia1/sdk/graphics/native_graph.h>

#include <intrin.h>

#include <cstdint>

namespace Mafia1Online::Features::Compat {
    namespace {
        // LS3DF.dll offsets from its image base.
        // C_IBManager::m_Alloc: bool __thiscall(C_IBManager*, usage, count,
        // COMPLETE_IB_STRUCT&), ret 0Ch. It overwrites the handle.
        constexpr uintptr_t kIndexManagerAlloc = 0x5b6c0;
        // C_IBuffer::m_Free: __stdcall(COMPLETE_IB_STRUCT*), ret 4; returns the
        // handle's block to its buffer.
        constexpr uintptr_t kIndexBufferFree = 0x5b940;
        // The two locked-mesh grow calls that skip m_Free (reM ShadowVisual.cpp
        // ProcessShadow, handle +0x20c; I3D_projector::ProcessRecList, +0x224):
        // the return addresses after `call m_Alloc` at 0x53c1f and 0x47cff.
        constexpr uintptr_t kShadowGrowReturn    = 0x53c24;
        constexpr uintptr_t kProjectorGrowReturn = 0x47d04;

        struct IndexHandle {
            void *buffer;
            void *indexBuffer;
            uint32_t offset;
        };

        using Alloc = bool(__thiscall *)(void *, uint32_t, uint32_t, IndexHandle *);
        using Free  = char(__stdcall *)(IndexHandle *);

        Alloc gAllocOriginal     = nullptr;
        Free gFree               = nullptr;
        uintptr_t gShadowGrow    = 0;
        uintptr_t gProjectorGrow = 0;

        bool __fastcall AllocHook(void *manager, void *, uint32_t usage, uint32_t count, IndexHandle *handle) {
            // Both constructors zero the handle and the unlocked branches clear
            // it after their own m_Free, so a set buffer is the visual's live
            // block, which the grow replaces.
            const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
            if ((caller == gShadowGrow || caller == gProjectorGrow) && handle->buffer) {
                gFree(handle);
            }
            return gAllocOriginal(manager, usage, count, handle);
        }
    } // namespace

    bool InstallLs3dfIndexLeakFix() {
        const uintptr_t base = SDK::Graphics::Ls3dfBase();
        if (!base) {
            return false;
        }
        auto *target   = reinterpret_cast<void *>(base + kIndexManagerAlloc);
        gFree          = reinterpret_cast<Free>(base + kIndexBufferFree);
        gShadowGrow    = base + kShadowGrowReturn;
        gProjectorGrow = base + kProjectorGrowReturn;
        if (MH_CreateHook(target, reinterpret_cast<void *>(&AllocHook), reinterpret_cast<void **>(&gAllocOriginal)) != MH_OK) {
            return false;
        }
        if (MH_EnableHook(target) != MH_OK) {
            MH_RemoveHook(target);
            return false;
        }
        return true;
    }
} // namespace Mafia1Online::Features::Compat
