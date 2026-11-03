#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace Mafia1Online::Features::Settings {
    // A key the player can bind, by its canonical name. The names are the
    // Framework's Key.bind names (utils/key_names.cpp) plus the punctuation
    // and numpad keys retail binds by scan code. Letters and punctuation name
    // US physical positions, as DirectInput scan codes and KeyboardEvent.code
    // do, so a binding follows the key, not its layout label.
    struct KeyInfo {
        std::string_view name;
        std::string_view label;
        // DirectInput scan code (extended keys in bit 7); 0 for none.
        uint16_t dik = 0;
        // Win32 virtual key of the US position; 0 for none.
        uint8_t vk = 0;
        // C_Input mouse button code (1 left, 2 right, 4 middle); 0 for none.
        uint8_t mouseButton = 0;
        // +1 wheel up, -1 wheel down: C_Input's mouse Z axis directions.
        int8_t wheel = 0;
    };

    std::span<const KeyInfo> AllKeys();
    // Case-sensitive; canonical names are lowercase. nullptr when unknown.
    const KeyInfo *FindKey(std::string_view name);
    const KeyInfo *FindKeyByDik(uint16_t dik);
    const KeyInfo *FindKeyByVirtualKey(uint8_t vk);
    const KeyInfo *FindKeyByMouseButton(uint16_t code);
    const KeyInfo *FindKeyByWheel(int direction);

    // Window messages carry the scan code with the extended flag in bit 8;
    // DirectInput folds it into bit 7.
    constexpr uint32_t DikToMessageKey(uint16_t dik) {
        return (dik & 0x7fu) | ((dik & 0x80u) ? 0x100u : 0u);
    }
} // namespace Mafia1Online::Features::Settings
