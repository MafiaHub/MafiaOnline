#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Mafia1Online::SDK::Graphics {
    // LS3DF IGraph, the 2D/3D device wrapper. Game.exe keeps the singleton
    // pointer at 0x647ee0; the HUD's widescreen bars (0x5fb50a-0x5fb529) use
    // SetTexture (+0x0c) with no texture and DrawPrimitiveList (+0x58) with
    // a pre-transformed triangle list.
    struct NativeGraph;

    struct NativeTLVertex {
        float x;
        float y;
        float z;
        float rhw;
        uint32_t diffuse;
        uint32_t specular;
        float u;
        float v;
    };
    static_assert(sizeof(NativeTLVertex) == 0x20);

    // LS3D_VIEWPORT is byte-identical to the Direct3D 8 viewport. The engine
    // stores this as its own cached viewport and compares it during rendering.
    struct NativeViewport {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
        float minZ;
        float maxZ;
    };
    static_assert(sizeof(NativeViewport) == 0x18);

    struct NativeGraphVTable {
        void *_unused00[0x0c / sizeof(void *)];
        bool(__stdcall *setTexture)(NativeGraph *, void *);
        void *(__stdcall *getMainHwnd)(NativeGraph *);
        void *_unused14[(0x30 - 0x14) / sizeof(void *)];
        void(__stdcall *clear)(NativeGraph *, uint32_t color, float depth, uint32_t flags);
        void *_unused34[(0x3c - 0x34) / sizeof(void *)];
        void(__stdcall *present)(NativeGraph *);
        void *_unused40[(0x44 - 0x40) / sizeof(void *)];
        void(__stdcall *setState)(NativeGraph *, int state, uint32_t value);
        void *_unused48;
        int(__stdcall *setViewport)(NativeGraph *, NativeViewport *);
        NativeViewport *(__stdcall *getViewport)(NativeGraph *);
        void *_unused54;
        int(__stdcall *drawPrimitiveList)(NativeGraph *, int type, uint32_t count, void *vertices, int stream);
        void *_unused5c[(0x70 - 0x5c) / sizeof(void *)];
        int(__stdcall *screenWidth)(NativeGraph *);
        int(__stdcall *screenHeight)(NativeGraph *);
        void *_unused78[(0x8c - 0x78) / sizeof(void *)];
        int(__stdcall *keyboardInit)(NativeGraph *, uint32_t flags);
        void *_unused90[(0xac - 0x90) / sizeof(void *)];
        bool(__stdcall *testKey)(NativeGraph *, uint8_t scanCode);
        void *_unusedb0[(0xb4 - 0xb0) / sizeof(void *)];
        int64_t(__stdcall *readKeys)(NativeGraph *);
        void *_unusedb8[(0xe0 - 0xb8) / sizeof(void *)];
        int(__stdcall *mouseDeltaX)(NativeGraph *);
        int(__stdcall *mouseDeltaY)(NativeGraph *);
        int(__stdcall *mouseWheel)(NativeGraph *);
        void *_unusedec[(0xf8 - 0xec) / sizeof(void *)];
        uint32_t(__stdcall *mouseButtons)(NativeGraph *);
    };
    static_assert(offsetof(NativeGraphVTable, setTexture) == 0x0c);
    static_assert(offsetof(NativeGraphVTable, getMainHwnd) == 0x10);
    static_assert(offsetof(NativeGraphVTable, present) == 0x3c);
    static_assert(offsetof(NativeGraphVTable, clear) == 0x30);
    static_assert(offsetof(NativeGraphVTable, setState) == 0x44);
    static_assert(offsetof(NativeGraphVTable, setViewport) == 0x4c);
    static_assert(offsetof(NativeGraphVTable, getViewport) == 0x50);
    static_assert(offsetof(NativeGraphVTable, drawPrimitiveList) == 0x58);
    static_assert(offsetof(NativeGraphVTable, screenWidth) == 0x70);
    static_assert(offsetof(NativeGraphVTable, screenHeight) == 0x74);
    static_assert(offsetof(NativeGraphVTable, keyboardInit) == 0x8c);
    static_assert(offsetof(NativeGraphVTable, testKey) == 0xac);
    static_assert(offsetof(NativeGraphVTable, readKeys) == 0xb4);
    static_assert(offsetof(NativeGraphVTable, mouseDeltaX) == 0xe0);
    static_assert(offsetof(NativeGraphVTable, mouseDeltaY) == 0xe4);
    static_assert(offsetof(NativeGraphVTable, mouseWheel) == 0xe8);
    static_assert(offsetof(NativeGraphVTable, mouseButtons) == 0xf8);

    // G_IndicatorsClass::DrawAll (0x5fb060) begins with
    // SetState(DX_ALPHASTATE, 1) at 0x5fb079 so translucent quads blend over
    // the scene; a scene callback may otherwise inherit an additive mode.
    inline constexpr int kAlphaState     = 0;
    inline constexpr uint32_t kAlphaBlend = 1;

    // IGraph::KeyboardInit flag bits (LS3DF 0x100716a0): bit 0 acquires the
    // DirectInput keyboard exclusively, bit 1 switches ReadKey to the
    // WM_CHAR/WM_KEYDOWN buffer that ProcessWinMessages (0x1006ca40) fills.
    inline constexpr uint32_t kKeyboardExclusive = 0x1;
    inline constexpr uint32_t kKeyboardBuffered  = 0x2;

    // IGraph::GetMouseButtons (LS3DF 0x100720c0) result bits.
    inline constexpr uint32_t kMouseLeft   = 0x1;
    inline constexpr uint32_t kMouseRight  = 0x2;
    inline constexpr uint32_t kMouseMiddle = 0x4;

    // LS3DF globals, as offsets from the loaded module base. IGraph::BeginScene
    // (0x1006d470) reads the IDirect3DDevice8 from 0x101c597c; KeyboardInit
    // stores its flags at 0x101c52bc and ReadKey (0x10071960) tests them.
    inline constexpr uintptr_t kD3DDeviceOffset     = 0x1c597c;
    inline constexpr uintptr_t kKeyboardFlagsOffset = 0x1c52bc;

    struct NativeGraph {
        NativeGraphVTable *vtable;

        void *MainWindow() { return vtable->getMainHwnd(this); }
        int ScreenWidth() { return vtable->screenWidth(this); }
        int ScreenHeight() { return vtable->screenHeight(this); }
        NativeViewport Viewport() { return *vtable->getViewport(this); }
        bool SetViewport(NativeViewport viewport) { return vtable->setViewport(this, &viewport) >= 0; }
        void Clear(uint32_t color, float depth, uint32_t flags) { vtable->clear(this, color, depth, flags); }
        void KeyboardInit(uint32_t flags) { vtable->keyboardInit(this, flags); }
        bool TestKey(uint8_t scanCode) { return vtable->testKey(this, scanCode); }
        int MouseWheel() { return vtable->mouseWheel(this); }
        uint32_t MouseButtons() { return vtable->mouseButtons(this); }
    };

    inline NativeGraph *GetGraph() {
        return *reinterpret_cast<NativeGraph **>(0x647ee0);
    }

#if defined(_WIN32)
    inline uintptr_t Ls3dfBase() {
        return reinterpret_cast<uintptr_t>(GetModuleHandleW(L"LS3DF.dll"));
    }

    // The engine's IDirect3DDevice8. IGraph::Init creates it before any menu
    // or mission runs and IGraph::Close releases it after CloseSystem.
    inline void *GetD3DDevice() {
        return *reinterpret_cast<void **>(Ls3dfBase() + kD3DDeviceOffset);
    }

    inline uint32_t KeyboardFlags() {
        return *reinterpret_cast<const uint32_t *>(Ls3dfBase() + kKeyboardFlagsOffset);
    }
#endif

    // Untextured screen rectangle in pixels; color is 0xAARRGGBB.
    inline void FillRect(float x, float y, float width, float height, uint32_t color) {
        constexpr int kTriangleList = 3;
        constexpr int kTLVertexStream = 1;
        const float right = x + width;
        const float bottom = y + height;
        NativeTLVertex vertices[6] = {
            {x, y, 0.0f, 1.0f, color, 0, 0.0f, 0.0f},     {right, y, 0.0f, 1.0f, color, 0, 1.0f, 0.0f},
            {x, bottom, 0.0f, 1.0f, color, 0, 0.0f, 1.0f}, {right, y, 0.0f, 1.0f, color, 0, 1.0f, 0.0f},
            {right, bottom, 0.0f, 1.0f, color, 0, 1.0f, 1.0f}, {x, bottom, 0.0f, 1.0f, color, 0, 0.0f, 1.0f},
        };
        auto *graph = GetGraph();
        graph->vtable->setState(graph, kAlphaState, kAlphaBlend);
        graph->vtable->setTexture(graph, nullptr);
        graph->vtable->drawPrimitiveList(graph, kTriangleList, 2, vertices, kTLVertexStream);
    }
} // namespace Mafia1Online::SDK::Graphics
