#include "features/mod/mod_service.h"
#include "shared/features/mod/mod_archive.h"
#include <utils/crypto.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace Mod = Mafia1Online::Shared::Mod;
using Mafia1Online::Features::Mod::ModService;

void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

void Zip(const std::filesystem::path &path, const std::vector<std::pair<std::string, std::string>> &files) {
    mz_zip_archive zip {};
    Check(mz_zip_writer_init_file(&zip, path.string().c_str(), 0), "writer init");
    for (const auto &[name, data] : files) Check(mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), 0), "writer add");
    Check(mz_zip_writer_finalize_archive(&zip), "writer finalize");
    mz_zip_writer_end(&zip);
}

auto Wait(ModService &service) {
    for (int i = 0; i < 1000; ++i) {
        service.Update();
        if (auto result = service.TakeCompleted())
            return *result;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("preparation timeout");
}

std::string Read(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main(int argc, char **argv) {
    try {
        Check(argc == 2, "Pass a repository-local test directory");
        const std::filesystem::path root = argv[1];
        const auto downloads             = root / "downloads";
        std::filesystem::create_directories(downloads);
        for (const auto *path : {"../maps/a.bmp", "/maps/a.bmp", "C:\\maps\\a.bmp", "maps/../a.bmp", "maps/con.bmp", "maps/a.bmp:", "maps/a. ", "maps//a.bmp"}) Check(Mod::NormalizePath(path).empty(), "unsafe path accepted");
        Check(Mod::NormalizePath("MAPS\\Tex.BMP") == "maps/tex.bmp", "case and slash normalization");
        const auto zip = root / "fixture.zip";
        for (const auto &files : std::vector<std::vector<std::pair<std::string, std::string>>> {{{"../outside", "bad"}}, {{"models/payload.dll", "bad"}}, {{"maps/A.bmp", "a"}, {"MAPS/a.BMP", "b"}}}) {
            Zip(zip, files);
            bool refused = false;
            try {
                Mod::Archive(zip).Inspect();
            }
            catch (const std::exception &) {
                refused = true;
            }
            Check(refused, "malformed archive accepted");
        }
        Zip(zip, {{"missions/NEW_CITY/scene.4ds", "scene"}, {"maps/texture.bmp", "first"}});
        auto hash = Framework::Utils::Crypto::Sha256FileHex(zip);
        std::filesystem::copy_file(zip, downloads / Mod::DownloadName(hash), std::filesystem::copy_options::overwrite_existing);
        nlohmann::json config = {{"mods", nlohmann::json::array({{{"name", "first.zip"}, {"sha256", hash}, {"bytes", std::filesystem::file_size(zip)}}})}};
        Zip(zip, {{"missions/new_city/tree.klz", "collision"}, {"maps/texture.bmp", "second"}});
        const auto secondHash     = Framework::Utils::Crypto::Sha256FileHex(zip);
        const auto secondDownload = downloads / Mod::DownloadName(secondHash);
        std::filesystem::copy_file(zip, secondDownload, std::filesystem::copy_options::overwrite_existing);
        config["mods"].push_back({{"name", "second.zip"}, {"sha256", secondHash}, {"bytes", std::filesystem::file_size(zip)}});
        ModService service;
        service.Begin(1, config, downloads, root / "assets");
        auto result = Wait(service);
        Check(result.error.empty() && service.HasMission("new_city"), "cross-ZIP mission detection failed");
        Check(Read(service.Resolve("MAPS\\texture.bmp")) == "second", "last ZIP did not win");
        Check(service.Resolve("maps/missing.bmp").empty(), "missing path must use native fallback");
        const auto originalTime = std::filesystem::last_write_time(service.Resolve("maps/texture.bmp"));
        service.Reset();
        Check(service.Resolve("maps/texture.bmp").empty() && !service.HasMission("new_city"), "disconnect leaked overrides");
        service.Begin(2, config, downloads, root / "assets");
        Check(Wait(service).error.empty(), "cached reconnect failed");
        Check(std::filesystem::last_write_time(service.Resolve("maps/texture.bmp")) == originalTime, "unchanged extraction was rewritten");
        ModService secondClient;
        service.Begin(20, config, downloads, root / "shared-assets");
        secondClient.Begin(21, config, downloads, root / "shared-assets");
        Check(Wait(service).error.empty() && Wait(secondClient).error.empty(), "concurrent clients could not share the cache");
        Check(Read(secondClient.Resolve("maps/texture.bmp")) == "second", "shared cache contents differ");
        secondClient.Reset();
        // Existing extraction is never trusted in place of the verified ZIP.
        {
            std::ofstream output(service.Resolve("maps/texture.bmp"));
            output << "tampered";
        }
        service.Begin(3, config, downloads, root / "shared-assets");
        Check(Wait(service).error.empty() && Read(service.Resolve("maps/texture.bmp")) == "second", "damaged extraction was not repaired");
        service.Begin(4, config, downloads, root / "assets");
        service.Reset();
        service.Begin(5, nlohmann::json::object(), downloads, root / "assets");
        Check(Wait(service).generation == 5 && service.Resolve("maps/texture.bmp").empty(), "canceled generation leaked into a new session");
        {
            std::fstream output(secondDownload, std::ios::in | std::ios::out | std::ios::binary);
            output.put('X');
        }
        service.Begin(6, config, downloads, root / "assets");
        Check(!Wait(service).error.empty() && service.Resolve("maps/texture.bmp").empty(), "checksum mismatch activated mods");
        Check(!std::filesystem::exists(secondDownload), "corrupt download was not removed for retry");
        service.Reset();
        std::cout << "Mod archive, precedence, cache integrity, mission detection and cancellation tests passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
