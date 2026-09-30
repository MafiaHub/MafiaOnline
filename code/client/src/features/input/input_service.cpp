#include "input_service.h"

#include "features/web_ui/web_ui_hooks.h"

#include <input/physical_key_state.h>
#include <mafia1/sdk/graphics/native_graph.h>

namespace Mafia1Online::Features::Input {
    namespace {
        constexpr int kMouseKeys[] {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
    }

    void InputService::Update() {
        _keys.Update(
            [this](int key) {
                return IsKeyDown(key);
            },
            _ready && Framework::Input::PhysicalKeyState::IsForeground());
    }

    uint32_t InputService::MapKey(uint32_t key) const {
        // DirectInput folds an E0 prefix into bit 7. Pause is E1 and has its
        // own DIK code; NumLock shares its PC scan position, not its DIK code.
        if (key == VK_PAUSE)
            return 0xC5;
        if (key == VK_NUMLOCK)
            return 0x45;
        const UINT scan = Framework::Input::PhysicalKeys::ToScanCode(key);
        return (scan & 0xFFU) | ((scan & 0xFF00U) != 0 ? 0x80U : 0U);
    }

    bool InputService::IsKeyDown(int key) const {
        if (!_ready || key < 0 || key > 255 || !Framework::Input::PhysicalKeyState::IsForeground())
            return false;
        switch (key) {
        case VK_LBUTTON: return IsMouseButtonDown(0);
        case VK_RBUTTON: return IsMouseButtonDown(1);
        case VK_MBUTTON: return IsMouseButtonDown(2);
        case VK_XBUTTON1:
        case VK_XBUTTON2: return false;
        case VK_SHIFT: return IsKeyDown(VK_LSHIFT) || IsKeyDown(VK_RSHIFT);
        case VK_CONTROL: return IsKeyDown(VK_LCONTROL) || IsKeyDown(VK_RCONTROL);
        case VK_MENU: return IsKeyDown(VK_LMENU) || IsKeyDown(VK_RMENU);
        default: break;
        }
        const uint32_t scan = MapKey(static_cast<uint32_t>(key));
        return scan != 0 && Mafia1Online::Features::WebUi::Native::TestKey(static_cast<uint8_t>(scan));
    }

    bool InputService::IsMouseButtonDown(int button) const {
        if (!_ready || button < 0 || button >= 3 || !Framework::Input::PhysicalKeyState::IsForeground())
            return false;
        return (Mafia1Online::Features::WebUi::Native::MouseButtons() & (1U << button)) != 0;
    }
    bool InputService::IsMouseButtonPressed(int button) const {
        return button >= 0 && button < 3 && _keys.IsPressed(kMouseKeys[button]);
    }
    bool InputService::IsMouseButtonReleased(int button) const {
        return button >= 0 && button < 3 && _keys.IsReleased(kMouseKeys[button]);
    }

    void InputService::GetMousePosition(int &x, int &y) const {
        POINT position {};
        if (_ready) {
            GetCursorPos(&position);
            ScreenToClient(static_cast<HWND>(Mafia1Online::SDK::Graphics::GetGraph()->vtable->getMainHwnd(Mafia1Online::SDK::Graphics::GetGraph())), &position);
        }
        x = position.x;
        y = position.y;
    }
    void InputService::SetMousePosition(int x, int y) {
        if (!_ready)
            return;
        POINT position {x, y};
        ClientToScreen(static_cast<HWND>(Mafia1Online::SDK::Graphics::GetGraph()->vtable->getMainHwnd(Mafia1Online::SDK::Graphics::GetGraph())), &position);
        SetCursorPos(position.x, position.y);
    }
} // namespace Mafia1Online::Features::Input
