#include "simulation_config.h"

#include "features/quick_join/quick_join.h"

#include <logging/logger.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace Mafia1Online::Features::Simulation {
    namespace {
        Config gConfig;

        RenderMode ParseRenderMode(const std::string &name) {
            if (name == "full") return RenderMode::Full;
            if (name == "wireframe") return RenderMode::Wireframe;
            if (name == "headless") return RenderMode::Headless;
            throw std::invalid_argument("simulation.render must be full, wireframe or headless");
        }
    } // namespace

    void Initialize(const std::string &projectPath) {
        std::ifstream input(QuickJoin::ConfigPath(projectPath));
        if (!input) {
            return;
        }
        try {
            const auto root = nlohmann::json::parse(input);
            const auto settings = root.find("simulation");
            if (settings == root.end()) {
                return;
            }
            Config configured;
            configured.enabled = settings->at("enabled").get<bool>();
            if (!configured.enabled) {
                return;
            }
            configured.token = settings->at("token").get<std::string>();
            if (const auto render = settings->find("render"); render != settings->end()) {
                configured.render = ParseRenderMode(render->get<std::string>());
            }
            if (const auto rate = settings->find("tickHz"); rate != settings->end()) {
                configured.tickHz = rate->get<uint32_t>();
            }
            if (const auto scale = settings->find("pedestrianPoolScale"); scale != settings->end()) {
                configured.pedestrianPoolScale = scale->get<uint32_t>();
            }
            if (const auto density = settings->find("pedestrianDensity"); density != settings->end()) {
                configured.pedestrianDensity = density->get<float>();
            }
            if (configured.token.empty() || configured.token.size() > 256 || configured.tickHz < 15 || configured.tickHz > 60 ||
                configured.pedestrianPoolScale < 1 || configured.pedestrianPoolScale > 16 || !(configured.pedestrianDensity >= 0.0f && configured.pedestrianDensity <= 1.0f)) {
                throw std::invalid_argument("invalid simulation value");
            }
            if (!QuickJoin::GetConfig().enabled) {
                throw std::invalid_argument("simulation needs quickJoin enabled for the server address");
            }
            gConfig = std::move(configured);
            Framework::Logging::GetLogger("Mafia1Online")->info("Simulation host role enabled ({} rendering, {} Hz)", RenderModeName(gConfig.render), gConfig.tickHz);
        }
        catch (const std::exception &error) {
            Framework::Logging::GetLogger("Mafia1Online")->error("Invalid simulation configuration ({}); running as a player", error.what());
            gConfig = {};
        }
    }

    const Config &GetConfig() {
        return gConfig;
    }

    const char *RenderModeName(RenderMode mode) {
        switch (mode) {
        case RenderMode::Full: return "full";
        case RenderMode::Wireframe: return "wireframe";
        case RenderMode::Headless: return "headless";
        }
        return "unknown";
    }
} // namespace Mafia1Online::Features::Simulation
