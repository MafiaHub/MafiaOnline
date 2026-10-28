#pragma once

namespace Mafia1Online::Features::Compat {
    // LS3DF grows the pooled index buffer of a locked shadow or projector
    // without freeing the old block. Every orphan keeps a pointer into its
    // visual, and IGraph::Close writes through it after CloseSystem freed
    // the visuals. Install once InitSystem has loaded LS3DF.dll; it stays
    // until the process exits.
    bool InstallLs3dfIndexLeakFix();
} // namespace Mafia1Online::Features::Compat
