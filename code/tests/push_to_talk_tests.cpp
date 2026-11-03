#include "features/voice/push_to_talk_key.h"

#include <cstdio>

using Mafia1Online::Features::Voice::PushToTalkKey;

namespace {
    constexpr int kV = 0x56;
    constexpr int kX = 0x58;

    int Fail(int code, const char *what) {
        std::printf("push_to_talk_tests: %s\n", what);
        return code;
    }
} // namespace

int main() {
    {
        // A plain press and release.
        PushToTalkKey key;
        if (key.Update(kV, false, false) || !key.Update(kV, true, false) || !key.Update(kV, true, false) || key.Update(kV, false, false))
            return Fail(1, "press and release");
    }
    {
        // "v" typed in the chat, still down on the tick Enter closes it.
        PushToTalkKey key;
        key.Update(kV, false, false);
        if (key.Update(kV, true, true))
            return Fail(2, "held while blocked");
        if (key.Update(kV, true, false))
            return Fail(3, "held as the block lifts");
        if (key.Update(kV, false, false) || !key.Update(kV, true, false))
            return Fail(4, "fresh press after the latch");
    }
    {
        // Held when the chat opens: cut at once, and not resumed on close.
        PushToTalkKey key;
        key.Update(kV, false, false);
        if (!key.Update(kV, true, false) || key.Update(kV, true, true) || key.Update(kV, true, false))
            return Fail(5, "held through the chat");
    }
    {
        // Focus lost while talking, released elsewhere, focus back: closed.
        PushToTalkKey key;
        key.Update(kV, false, false);
        if (!key.Update(kV, true, false) || key.Update(kV, true, true) || key.Update(kV, false, true) || key.Update(kV, false, false))
            return Fail(6, "released while unfocused");
    }
    {
        // Rebound to a key that is already down; unbound.
        PushToTalkKey key;
        key.Update(kV, false, false);
        if (key.Update(kX, true, false))
            return Fail(7, "rebound onto a held key");
        if (key.Update(kX, false, false) || !key.Update(kX, true, false))
            return Fail(8, "rebound key pressed afresh");
        if (key.Update(0, true, false) || key.Update(0, false, false))
            return Fail(9, "unbound");
    }
    std::printf("push_to_talk_tests: ok\n");
    return 0;
}
