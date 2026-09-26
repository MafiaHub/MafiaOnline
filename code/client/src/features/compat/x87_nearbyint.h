#pragma once

namespace Mafia1Online::Features::Compat {
    // Round using the current x87 rounding mode, without raising inexact.
    double __cdecl X87NearbyInt(double value);
}
