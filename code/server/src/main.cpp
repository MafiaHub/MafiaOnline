#include "core/server.h"
#include "shared/features/world/mission_catalog.h"
#include "shared/version.h"

#include <cstdio>
#include <exception>
#include <logging/logger.h>

int main(int argc, char **argv) {
    Framework::Integrations::Server::InstanceOptions opts;
    opts.bindHost        = "0.0.0.0";
    opts.bindPort        = 27015;
    opts.webBindHost     = "0.0.0.0";
    opts.webBindPort     = 27016;
    opts.maxPlayers      = 32;
    opts.modName         = "Mafia1Online";
    opts.modSlug         = "mafia1online_server";
    opts.modVersion      = Mafia1Online::Version::rel;
    opts.gameName        = "Mafia 1";
    opts.gameVersion     = "1.2";
    opts.enableSignals   = true;
    opts.modConfigSchema = {
        {"mission", Framework::Utils::ConfigFieldType::String, "", true, true, {}, "Stock or detected server mod mission directory name", true},
    };
    opts.argc = argc;
    opts.argv = argv;

    Mafia1Online::Core::Server server;
    try {
        server.LoadMods();
        if (const auto result = server.Init(opts); !result) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to start Mafia1OnlineServer: {}", result.GetError().message);
            return 1;
        }
        server.Run();
        server.Shutdown();
    }
    catch (const std::exception &error) {
        std::fprintf(stderr, "Mafia1OnlineServer: %s\n", error.what());
        return 1;
    }
    return 0;
}
