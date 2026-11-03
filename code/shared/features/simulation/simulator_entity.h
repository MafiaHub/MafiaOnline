#pragma once

#include <networking/replication/network_entity.h>

namespace Mafia1Online::Shared::Entities {
    // The simulation host's viewer. The server streams it only to that
    // connection, so players never learn the host exists; it carries no
    // state of its own.
    class SimulatorEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::Simulator";
    };
} // namespace Mafia1Online::Shared::Entities
