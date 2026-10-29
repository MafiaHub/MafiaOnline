#pragma once

namespace Mafia1Online::Features::Voice {
    // What the framework's VoiceClient leaves to the game: the listener
    // (the camera, distances from the player's body) and the push-to-talk
    // key, polled here and fed through SetPushToTalk.
    class VoiceInput final {
      public:
        // blocked: the chat line or a web view owns the keyboard.
        void Update(bool blocked);
    };
} // namespace Mafia1Online::Features::Voice
