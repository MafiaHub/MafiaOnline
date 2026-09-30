#include "input_service.h"

#include "features/web_ui/web_ui_hooks.h"

#include <input/detail/win32_keys.h>
#include <mafia1/sdk/graphics/native_graph.h>

namespace Mafia1Online::Features::Input {
    namespace {
        constexpr int kMouseKeys[] {FW_KEY_LBUTTON, FW_KEY_RBUTTON, FW_KEY_MBUTTON};
    }

    void InputService::Update() {
        _keys.Update(
            [this](int key) {
                return ReadKeyDown(key);
            },
            IsAvailable());
    }

    bool InputService::IsAvailable() const {
        return _ready && Framework::Input::detail::IsForeground();
    }

    bool InputService::IsKeyDown(int key) const {
        return key >= 0 && key < 256 && IsAvailable() && ReadKeyDown(key);
    }

    bool InputService::ReadKeyDown(int key) const {
        switch (key) {
        case FW_KEY_LBUTTON:
        case FW_KEY_RBUTTON:
        case FW_KEY_MBUTTON: {
            const int button = key == FW_KEY_LBUTTON ? 0 : key == FW_KEY_RBUTTON ? 1 : 2;
            return (Mafia1Online::Features::WebUi::Native::MouseButtons() & (1U << button)) != 0;
        }
        case FW_KEY_XBUTTON1:
        case FW_KEY_XBUTTON2: return false;
        case FW_KEY_SHIFT: return ReadKeyDown(FW_KEY_LSHIFT) || ReadKeyDown(FW_KEY_RSHIFT);
        case FW_KEY_CONTROL: return ReadKeyDown(FW_KEY_LCONTROL) || ReadKeyDown(FW_KEY_RCONTROL);
        case FW_KEY_MENU: return ReadKeyDown(FW_KEY_LMENU) || ReadKeyDown(FW_KEY_RMENU);
        default: break;
        }
        const UINT scan = Framework::Input::PhysicalKeys::ToDirectInputCode(static_cast<UINT>(key));
        return scan != 0 && Mafia1Online::Features::WebUi::Native::TestKey(static_cast<uint8_t>(scan));
    }

    bool InputService::IsMouseButtonDown(int button) const {
        return button >= 0 && button < 3 && IsKeyDown(kMouseKeys[button]);
    }
    bool InputService::IsMouseButtonPressed(int button) const {
        return button >= 0 && button < 3 && IsKeyPressed(kMouseKeys[button]);
    }
    bool InputService::IsMouseButtonReleased(int button) const {
        return button >= 0 && button < 3 && IsKeyReleased(kMouseKeys[button]);
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
