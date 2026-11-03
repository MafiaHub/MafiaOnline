#include "key_catalog.h"

#include <algorithm>

namespace Mafia1Online::Features::Settings {
    namespace {
        // DirectInput scan codes (dinput.h DIK_*) and US virtual keys.
        constexpr KeyInfo kKeys[] {
            {"a", "A", 0x1e, 'A'},
            {"b", "B", 0x30, 'B'},
            {"c", "C", 0x2e, 'C'},
            {"d", "D", 0x20, 'D'},
            {"e", "E", 0x12, 'E'},
            {"f", "F", 0x21, 'F'},
            {"g", "G", 0x22, 'G'},
            {"h", "H", 0x23, 'H'},
            {"i", "I", 0x17, 'I'},
            {"j", "J", 0x24, 'J'},
            {"k", "K", 0x25, 'K'},
            {"l", "L", 0x26, 'L'},
            {"m", "M", 0x32, 'M'},
            {"n", "N", 0x31, 'N'},
            {"o", "O", 0x18, 'O'},
            {"p", "P", 0x19, 'P'},
            {"q", "Q", 0x10, 'Q'},
            {"r", "R", 0x13, 'R'},
            {"s", "S", 0x1f, 'S'},
            {"t", "T", 0x14, 'T'},
            {"u", "U", 0x16, 'U'},
            {"v", "V", 0x2f, 'V'},
            {"w", "W", 0x11, 'W'},
            {"x", "X", 0x2d, 'X'},
            {"y", "Y", 0x15, 'Y'},
            {"z", "Z", 0x2c, 'Z'},
            {"1", "1", 0x02, '1'},
            {"2", "2", 0x03, '2'},
            {"3", "3", 0x04, '3'},
            {"4", "4", 0x05, '4'},
            {"5", "5", 0x06, '5'},
            {"6", "6", 0x07, '6'},
            {"7", "7", 0x08, '7'},
            {"8", "8", 0x09, '8'},
            {"9", "9", 0x0a, '9'},
            {"0", "0", 0x0b, '0'},
            {"f1", "F1", 0x3b, 0x70},
            {"f2", "F2", 0x3c, 0x71},
            {"f3", "F3", 0x3d, 0x72},
            {"f4", "F4", 0x3e, 0x73},
            {"f5", "F5", 0x3f, 0x74},
            {"f6", "F6", 0x40, 0x75},
            {"f7", "F7", 0x41, 0x76},
            {"f8", "F8", 0x42, 0x77},
            {"f9", "F9", 0x43, 0x78},
            {"f10", "F10", 0x44, 0x79},
            {"f11", "F11", 0x57, 0x7a},
            {"f12", "F12", 0x58, 0x7b},
            {"numpad0", "Numpad 0", 0x52, 0x60},
            {"numpad1", "Numpad 1", 0x4f, 0x61},
            {"numpad2", "Numpad 2", 0x50, 0x62},
            {"numpad3", "Numpad 3", 0x51, 0x63},
            {"numpad4", "Numpad 4", 0x4b, 0x64},
            {"numpad5", "Numpad 5", 0x4c, 0x65},
            {"numpad6", "Numpad 6", 0x4d, 0x66},
            {"numpad7", "Numpad 7", 0x47, 0x67},
            {"numpad8", "Numpad 8", 0x48, 0x68},
            {"numpad9", "Numpad 9", 0x49, 0x69},
            {"numpadmultiply", "Numpad *", 0x37, 0x6a},
            {"numpadplus", "Numpad +", 0x4e, 0x6b},
            {"numpadminus", "Numpad -", 0x4a, 0x6d},
            {"numpaddecimal", "Numpad .", 0x53, 0x6e},
            {"numpaddivide", "Numpad /", 0xb5, 0x6f},
            // Shares VK_RETURN with Enter; only the scan code tells them apart.
            {"numpadenter", "Numpad Enter", 0x9c, 0},
            {"space", "Space", 0x39, 0x20},
            {"enter", "Enter", 0x1c, 0x0d},
            {"escape", "Escape", 0x01, 0x1b},
            {"tab", "Tab", 0x0f, 0x09},
            {"backspace", "Backspace", 0x0e, 0x08},
            {"capslock", "Caps Lock", 0x3a, 0x14},
            {"lshift", "Left Shift", 0x2a, 0xa0},
            {"rshift", "Right Shift", 0x36, 0xa1},
            {"lctrl", "Left Ctrl", 0x1d, 0xa2},
            {"rctrl", "Right Ctrl", 0x9d, 0xa3},
            {"lalt", "Left Alt", 0x38, 0xa4},
            {"ralt", "Right Alt", 0xb8, 0xa5},
            {"up", "Up", 0xc8, 0x26},
            {"down", "Down", 0xd0, 0x28},
            {"left", "Left", 0xcb, 0x25},
            {"right", "Right", 0xcd, 0x27},
            {"insert", "Insert", 0xd2, 0x2d},
            {"delete", "Delete", 0xd3, 0x2e},
            {"home", "Home", 0xc7, 0x24},
            {"end", "End", 0xcf, 0x23},
            {"pageup", "Page Up", 0xc9, 0x21},
            {"pagedown", "Page Down", 0xd1, 0x22},
            {"minus", "-", 0x0c, 0xbd},
            {"equals", "=", 0x0d, 0xbb},
            {"lbracket", "[", 0x1a, 0xdb},
            {"rbracket", "]", 0x1b, 0xdd},
            {"semicolon", ";", 0x27, 0xba},
            {"apostrophe", "'", 0x28, 0xde},
            {"grave", "`", 0x29, 0xc0},
            {"backslash", "\\", 0x2b, 0xdc},
            {"comma", ",", 0x33, 0xbc},
            {"period", ".", 0x34, 0xbe},
            {"slash", "/", 0x35, 0xbf},
            {"mouse1", "Mouse 1", 0, 0x01, 1},
            {"mouse2", "Mouse 2", 0, 0x02, 2},
            {"mouse3", "Mouse 3", 0, 0x04, 4},
            {"mouse4", "Mouse 4", 0, 0x05},
            {"mouse5", "Mouse 5", 0, 0x06},
            // FW_KEY_WHEELUP/FW_KEY_WHEELDOWN: notches, never held.
            {"wheelup", "Wheel Up", 0, 0x0a, 0, 1},
            {"wheeldown", "Wheel Down", 0, 0x0b, 0, -1},
        };

        template <typename Match>
        const KeyInfo *Find(Match &&match) {
            const auto it = std::find_if(std::begin(kKeys), std::end(kKeys), match);
            return it == std::end(kKeys) ? nullptr : &*it;
        }
    } // namespace

    std::span<const KeyInfo> AllKeys() {
        return kKeys;
    }

    const KeyInfo *FindKey(std::string_view name) {
        return name.empty() ? nullptr : Find([name](const KeyInfo &key) { return key.name == name; });
    }

    const KeyInfo *FindKeyByDik(uint16_t dik) {
        return dik == 0 ? nullptr : Find([dik](const KeyInfo &key) { return key.dik == dik; });
    }

    const KeyInfo *FindKeyByVirtualKey(uint8_t vk) {
        return vk == 0 ? nullptr : Find([vk](const KeyInfo &key) { return key.vk == vk; });
    }

    const KeyInfo *FindKeyByMouseButton(uint16_t code) {
        return code == 0 ? nullptr : Find([code](const KeyInfo &key) { return key.mouseButton == code; });
    }

    const KeyInfo *FindKeyByWheel(int direction) {
        return direction == 0 ? nullptr : Find([direction](const KeyInfo &key) { return key.wheel == direction; });
    }
} // namespace Mafia1Online::Features::Settings
