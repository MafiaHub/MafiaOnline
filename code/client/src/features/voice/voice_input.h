#pragma once

#include "push_to_talk_key.h"

namespace Mafia1Online::Features::Voice {
    // What the framework's VoiceClient leaves to the game: the listener
    // (the camera, distances from the player's body) and the push-to-talk
    // key, polled here and fed through SetPushToTalk.
    class VoiceInput final {
      public:
        // blocked: the chat line, a menu or a web view owns the keyboard.
        void Update(bool blocked);

      private:
        PushToTalkKey _pushToTalk;
    };
} // namespace Mafia1Online::Features::Voice
