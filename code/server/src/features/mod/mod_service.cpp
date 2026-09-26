#include "mod_service.h"

#include "shared/features/mod/mod_archive.h"
#include <logging/logger.h>
#include <networking/asset_streamer.h>
#include <utils/crypto.h>

namespace Mafia1Online::Features::Mod {
    void ModService::Load(const std::filesystem::path &directory) {
        Reset();
        if (!std::filesystem::exists(directory))
            return;
        std::vector<std::filesystem::path> sources;
        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() && Shared::Mod::Lower(entry.path().extension().string()) == ".zip")
                sources.push_back(entry.path());
        }
        if (sources.size() > Shared::Mod::kMaxMods)
            throw std::runtime_error("Too many ZIPs in mods/");
        std::sort(sources.begin(), sources.end(), [](const auto &a, const auto &b) {
            return Shared::Mod::Lower(a.filename().string()) < Shared::Mod::Lower(b.filename().string());
        });
        std::unordered_set<std::string> names, paths;
        uint64_t expanded  = 0;
        const auto staging = std::filesystem::absolute(directory / ".cache");
        if (!sources.empty())
            std::filesystem::create_directories(staging);
        for (const auto &source : sources) {
            const auto name = source.filename().string();
            if (name.size() > 128 || !names.insert(Shared::Mod::Lower(name)).second)
                throw std::runtime_error("Duplicate or oversized mod ZIP name: " + name);
            if (std::filesystem::file_size(source) > Shared::Mod::kMaxZipBytes)
                throw std::runtime_error("Mod ZIP too large: " + name);
            const auto hash = Framework::Utils::Crypto::Sha256FileHex(source);
            if (!Shared::Mod::IsHash(hash))
                throw std::runtime_error("Could not hash mod: " + name);
            const auto snapshot = staging / Shared::Mod::DownloadName(hash);
            if (Framework::Utils::Crypto::Sha256FileHex(snapshot) != hash)
                std::filesystem::copy_file(source, snapshot, std::filesystem::copy_options::overwrite_existing);
            if (Framework::Utils::Crypto::Sha256FileHex(snapshot) != hash)
                throw std::runtime_error("Mod changed while reading: " + name);
            Shared::Mod::Archive archive(snapshot);
            const auto entries = archive.Inspect();
            archive.Validate();
            for (const auto &entry : entries) {
                if (entry.size > Shared::Mod::kMaxExpandedBytes - expanded)
                    throw std::runtime_error("Server mods exceed the expanded size limit");
                expanded += entry.size;
                paths.insert(entry.path);
            }
            _packages.push_back(snapshot);
            _manifest.push_back({{"name", name}, {"sha256", hash}, {"bytes", std::filesystem::file_size(snapshot)}});
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Server mod '{}': {} assets, sha256 {}", name, entries.size(), hash);
        }
        _missions = Shared::Mod::DetectMissions(paths);
        for (const auto &mission : _missions) Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Mod mission detected: {}", mission);
    }

    void ModService::AddUploads(Framework::Networking::AssetStreamer &streamer) const {
        for (const auto &path : _packages) {
            if (!streamer.AddFile(path.string().c_str(), path.filename().string().c_str()))
                throw std::runtime_error("Could not register mod for download: " + path.string());
        }
    }

    bool ModService::HasMission(std::string_view name) const {
        return std::find(_missions.begin(), _missions.end(), name) != _missions.end();
    }

    void ModService::Reset() {
        _packages.clear();
        _missions.clear();
        _manifest = nlohmann::json::array();
    }
} // namespace Mafia1Online::Features::Mod
