#pragma once

#include <cstdint>

namespace Mafia1Online::Features::World::FrameClock {
    // The clock replicated motion is dated and shown by: advanced once a
    // frame by the frame time C_game::Tick moves the world by
    // (IGraph::GetTimer, whole milliseconds), and leaning toward the wall
    // clock, closing its gap at 1/s; a gap past 250 ms (a load, a long
    // hitch) is jumped. The wall clock read during a frame runs up to a
    // frame off the frame times the game advances everything else by, so
    // motion dated or sampled by it surges and lags against the world.
    //
    // A controller dates each pose by the frame whose step produced it, and
    // an observer samples its buffers at the same clock, so an owner's
    // stamps and an observer's render times differ by a steady offset.
    void Advance(unsigned int frameMs);
    // This frame's time, in milliseconds on the MafiaNet::GetTimeUS clock.
    double NowMs();
} // namespace Mafia1Online::Features::World::FrameClock
