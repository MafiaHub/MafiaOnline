#include "mod_service.h"

#include "shared/features/mod/mod_archive.h"
#include <chrono>
#include <utility>
#include <utils/crypto.h>

namespace Mafia1Online::Features::Mod {
    namespace {
        PreparedMods Prepare(uint64_t generation, const nlohmann::json &config, const std::filesystem::path &downloads, const std::filesystem::path &cache, const std::shared_ptr<std::atomic<bool>> &cancel) {
            PreparedMods result;
            result.generation = generation;
            try {
                const auto mods = config.value("mods", nlohmann::json::array());
                if (!mods.is_array() || mods.size() > Shared::Mod::kMaxMods)
                    throw std::runtime_error("Invalid server mod list");
                uint64_t expanded = 0;
                std::unordered_set<std::string> paths;
                for (const auto &mod : mods) {
                    if (cancel->load())
                        return result;
                    const auto name  = mod.at("name").get<std::string>();
                    const auto hash  = mod.at("sha256").get<std::string>();
                    const auto bytes = mod.at("bytes").get<uint64_t>();
                    if (!Shared::Mod::IsHash(hash) || bytes > Shared::Mod::kMaxZipBytes)
                        throw std::runtime_error("Invalid server mod checksum or size");
                    const auto zip = downloads / Shared::Mod::DownloadName(hash);
                    if (std::filesystem::file_size(zip) != bytes || Framework::Utils::Crypto::Sha256FileHex(zip) != hash) {
                        std::filesystem::remove(zip); // next join repairs a corrupt download
                        throw std::runtime_error("Checksum mismatch for " + name + ". Please reconnect.");
                    }
                    Shared::Mod::Archive archive(zip);
                    const auto entries = archive.Inspect();
                    // Outside the framework's servers/ tree: its legacy script-cache cleanup
                    // deliberately removes every plaintext directory there on each connection.
                    const auto root = std::filesystem::absolute(cache / hash);
                    std::filesystem::create_directories(root);
                    for (const auto &entry : entries) {
                        if (cancel->load())
                            return result;
                        if (entry.size > Shared::Mod::kMaxExpandedBytes - expanded)
                            throw std::runtime_error("Server mods exceed the expanded size limit");
                        expanded += entry.size;
                        // Flat native paths also keep rw_data's fixed 260-byte path buffer safe.
                        const auto destination = root / std::to_string(entry.index);
                        if (destination.string().size() >= 260)
                            throw std::runtime_error("Mod cache path is too long; move the client to a shorter path");
                        archive.Extract(entry, destination);
                        result.files[entry.path] = destination.string();
                        paths.insert(entry.path);
                    }
                }
                result.missions = Shared::Mod::DetectMissions(paths);
            }
            catch (const std::exception &error) {
                result.files.clear();
                result.missions.clear();
                result.error = error.what();
            }
            return result;
        }
    } // namespace

    void ModService::Begin(uint64_t generation, const nlohmann::json &config, const std::filesystem::path &downloads, const std::filesystem::path &cache) {
        Reset();
        _cancel = std::make_shared<std::atomic<bool>>(false);
        // A quick cancel/rejoin can target the same cache. Drain canceled writers on this
        // worker, before a new extraction, without stalling the menu thread.
        _pending = std::async(std::launch::async, [generation, config, downloads, cache, cancel = _cancel, previous = std::move(_retired)]() mutable {
            for (auto &job : previous) job.wait();
            return Prepare(generation, config, downloads, cache, cancel);
        });
    }

    void ModService::Update() {
        using namespace std::chrono_literals;
        std::erase_if(_retired, [](auto &job) {
            return job.wait_for(0ms) == std::future_status::ready;
        });
        if (!_pending.valid() || _pending.wait_for(0ms) != std::future_status::ready)
            return;
        _completed = _pending.get();
        if (_completed->error.empty()) {
            std::unique_lock lock(_mutex);
            _files    = std::move(_completed->files);
            _missions = _completed->missions;
        }
    }

    std::optional<PreparedMods> ModService::TakeCompleted() {
        return std::exchange(_completed, std::nullopt);
    }

    void ModService::Reset() {
        if (_cancel)
            _cancel->store(true);
        if (_pending.valid())
            _retired.push_back(std::move(_pending));
        _completed.reset();
        std::unique_lock lock(_mutex);
        _files.clear();
        _missions.clear();
    }

    std::string ModService::Resolve(std::string_view path) const {
        const auto key = Shared::Mod::NormalizePath(path);
        std::shared_lock lock(_mutex);
        const auto it = _files.find(key);
        return it == _files.end() ? std::string() : it->second;
    }

    bool ModService::HasMission(std::string_view name) const {
        std::shared_lock lock(_mutex);
        return std::find(_missions.begin(), _missions.end(), name) != _missions.end();
    }
} // namespace Mafia1Online::Features::Mod
