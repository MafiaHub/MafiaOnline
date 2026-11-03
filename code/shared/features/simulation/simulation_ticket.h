#pragma once

#include <string>
#include <string_view>

namespace Mafia1Online::Shared::Simulation {
    // The simulation host joins with this prefix and the server's
    // simulationToken as its connection ticket. The ticket travels in the
    // connection request, so the host belongs on the server's own machine or
    // a private network.
    inline constexpr std::string_view kTicketPrefix = "simulation:";

    inline std::string MakeTicket(std::string_view token) {
        return std::string(kTicketPrefix) + std::string(token);
    }
} // namespace Mafia1Online::Shared::Simulation
