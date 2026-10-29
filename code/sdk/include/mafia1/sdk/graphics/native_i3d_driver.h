#pragma once

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Graphics {
    // LS3DF I3D_driver. InitSystem stores it at 0x647ed8 before any mission
    // opens; its vtable (LS3DF 0x1009b830) has SetState at +0x14
    // (0x10015a20, called as SetState(RS_USESHADOWS, 1) at 0x5bfb15).
    struct NativeI3DDriver;

    struct NativeI3DDriverVTable {
        void *_unused00[0x14 / sizeof(void *)];
        int(__stdcall *setState)(NativeI3DDriver *, int state, int value);
    };
    static_assert(offsetof(NativeI3DDriverVTable, setState) == 0x14);

    // I3D_RENDERSTATE values handled by I3D_driver::SetState.
    // RS_WIREFRAME switches D3DRS_FILLMODE to wireframe (2) or solid (3); the
    // driver's render passes restore it from the cached state.
    inline constexpr int kRenderStateWireframe = 9;

    struct NativeI3DDriver {
        NativeI3DDriverVTable *vtable;

        void SetState(int state, int value) {
            vtable->setState(this, state, value);
        }
    };

    inline NativeI3DDriver *GetI3DDriver() {
        return *reinterpret_cast<NativeI3DDriver **>(0x647ed8);
    }
} // namespace Mafia1Online::SDK::Graphics
