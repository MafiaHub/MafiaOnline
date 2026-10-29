#pragma once

#include <cstdint>
#include <string>

namespace Mafia1Online::Features::Simulation {
    enum class RenderMode : uint8_t {
        Full,
        Wireframe,
        // The mission keeps ticking; nothing is drawn or presented and the
        // window stays hidden.
        Headless,
    };

    // The simulation host runs the native world for the server: no avatar,
    // menus, web UI, chat, HUD or sound. It reuses quickJoin's host and port.
    struct Config {
        bool enabled = false;
        std::string token;
        RenderMode render = RenderMode::Wireframe;
        uint32_t tickHz = 30;
        // Every generator pool holds this many times its retail walkers, so
        // players far apart each get the generator's full count.
        uint32_t pedestrianPoolScale = 16;
        // Replaces the host's own setup value; scales each generator's count.
        float pedestrianDensity = 1.0f;
        // The traffic car pool and its active slots hold this many times the
        // mission's retail maximum, shared out among the players.
        uint32_t trafficPoolScale = 8;
    };

    // Reads the "simulation" object of the client configuration. Call after
    // QuickJoin::Initialize: the host needs quick join enabled.
    void Initialize(const std::string &projectPath);
    const Config &GetConfig();

    inline bool IsHost() {
        return GetConfig().enabled;
    }

    const char *RenderModeName(RenderMode mode);

    // Host: a random nonzero id for this process, sent with the reports
    // players date by the host clock. A new id is another host, whose clock
    // starts their interpolation over.
    uint32_t HostSession();
} // namespace Mafia1Online::Features::Simulation
