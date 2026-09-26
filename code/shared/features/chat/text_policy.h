#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Mafia1Online::Shared::Chat {
    inline constexpr size_t kMaxNicknameCodePoints = 24;
    inline constexpr size_t kMaxMessageCodePoints  = 128;

    // Decodes one UTF-8 code point at `index`; returns false on malformed or
    // overlong input so the caller can drop the byte.
    inline bool DecodeUtf8(std::string_view text, size_t &index, uint32_t &codePoint) {
        const auto byte = static_cast<unsigned char>(text[index]);
        size_t length = 0;
        if (byte < 0x80) { codePoint = byte; length = 1; }
        else if ((byte & 0xe0) == 0xc0) { codePoint = byte & 0x1f; length = 2; }
        else if ((byte & 0xf0) == 0xe0) { codePoint = byte & 0x0f; length = 3; }
        else if ((byte & 0xf8) == 0xf0) { codePoint = byte & 0x07; length = 4; }
        else { ++index; return false; }
        if (index + length > text.size()) { ++index; return false; }
        for (size_t i = 1; i < length; ++i) {
            const auto next = static_cast<unsigned char>(text[index + i]);
            if ((next & 0xc0) != 0x80) { ++index; return false; }
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        static constexpr uint32_t kMinimum[] = {0, 0, 0x80, 0x800, 0x10000};
        if (codePoint < kMinimum[length] || codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
            ++index;
            return false;
        }
        index += length;
        return true;
    }

    // Valid UTF-8 without control or formatting characters, whitespace runs
    // collapsed to one space, trimmed, and at most `maxCodePoints` long.
    inline std::string SanitizeLine(std::string_view text, size_t maxCodePoints) {
        std::string out;
        size_t count = 0;
        bool pendingSpace = false;
        for (size_t index = 0; index < text.size() && count < maxCodePoints;) {
            const size_t start = index;
            uint32_t codePoint = 0;
            if (!DecodeUtf8(text, index, codePoint)) {
                continue;
            }
            const bool space = codePoint == ' ' || codePoint == '\t' || codePoint == 0xa0 || codePoint == 0x3000;
            const bool control = codePoint < 0x20 || (codePoint >= 0x7f && codePoint < 0xa0) ||
                                 (codePoint >= 0x200b && codePoint <= 0x200f) || (codePoint >= 0x202a && codePoint <= 0x202e) ||
                                 (codePoint >= 0x2060 && codePoint <= 0x206f) || codePoint == 0xfeff;
            if (space) {
                pendingSpace = !out.empty();
                continue;
            }
            if (control) {
                continue;
            }
            if (pendingSpace) {
                if (count + 1 >= maxCodePoints) {
                    break;
                }
                out.push_back(' ');
                ++count;
                pendingSpace = false;
            }
            out.append(text.substr(start, index - start));
            ++count;
        }
        return out;
    }

    // ASCII case folding is enough to stop look-alike duplicates such as
    // "Tommy" and "tommy"; other scripts compare exactly.
    inline std::string FoldNickname(std::string_view nickname) {
        std::string folded(nickname);
        for (char &c : folded) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return folded;
    }
} // namespace Mafia1Online::Shared::Chat
