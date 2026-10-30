#pragma once

namespace Mafia1Online::Features::Voice {
    // Whether the polled push-to-talk key counts as held. A key that is down
    // while something else owns the keyboard (the chat line, a menu, the
    // settings key capture, another window) stays latched until it comes up,
    // so the "v" typed just before Enter closes the chat, or a key held
    // through Alt+Tab, never opens the microphone when the block lifts. The
    // web UI latches its own keys the same way (docs/ui.md, Input).
    class PushToTalkKey final {
      public:
        // key: the bound virtual key, 0 when unbound. down: its raw state.
        bool Update(int key, bool down, bool blocked) {
            if (key != _key) {
                // A key that was already down when it was bound was pressed
                // for something else.
                _key     = key;
                _latched = down;
            }
            if (blocked) {
                _latched = down;
            }
            else if (!down) {
                _latched = false;
            }
            return key != 0 && down && !blocked && !_latched;
        }

      private:
        int _key      = 0;
        bool _latched = false;
    };
} // namespace Mafia1Online::Features::Voice
