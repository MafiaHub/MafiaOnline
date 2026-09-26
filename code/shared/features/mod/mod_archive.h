#pragma once

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Mafia1Online::Shared::Mod {
    inline constexpr size_t kMaxMods            = 64;
    inline constexpr uint64_t kMaxZipBytes      = 512ull * 1024 * 1024 - 1;
    inline constexpr uint64_t kMaxExpandedBytes = 2ull * 1024 * 1024 * 1024;
    inline constexpr uint64_t kMaxFileBytes     = 256ull * 1024 * 1024;
    inline constexpr size_t kMaxEntries         = 32768;

    inline std::string Lower(std::string_view value) {
        std::string result(value);
        for (char &c : result) {
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        }
        return result;
    }

    // The same spelling is used by the Linux server, Windows engine and ZIP index.
    // Reject Win32 aliases as well as traversal so an archive has one unambiguous tree.
    inline std::string NormalizePath(std::string_view value) {
        if (value.empty() || value.size() >= 240)
            return {};
        auto path = Lower(value);
        std::replace(path.begin(), path.end(), '\\', '/');
        size_t start = 0;
        while (start < path.size()) {
            const auto end  = path.find('/', start);
            const auto part = path.substr(start, end == std::string::npos ? end : end - start);
            if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
                return {};
            for (const unsigned char c : part) {
                if (c < 32 || c >= 127 || std::string_view(":*?\"<>|").find(c) != std::string_view::npos)
                    return {};
            }
            const auto stem = part.substr(0, part.find('.'));
            if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '0' && stem[3] <= '9'))
                return {};
            if (end == std::string::npos)
                return path;
            start = end + 1;
        }
        return {}; // trailing separator: callers trim directory entries explicitly
    }

    inline bool IsAssetPath(std::string_view path) {
        const auto slash = path.find('/');
        const auto root  = path.substr(0, slash);
        if (slash == std::string_view::npos || (root != "missions" && root != "models" && root != "maps" && root != "sounds" && root != "tables" && root != "anims" && root != "fonts"))
            return false;
        const auto ext = path.substr(path.find_last_of('.') == std::string_view::npos ? path.size() : path.find_last_of('.'));
        return ext != ".exe" && ext != ".dll" && ext != ".asi" && ext != ".com" && ext != ".bat" && ext != ".cmd" && ext != ".ps1" && ext != ".vbs" && ext != ".js" && ext != ".lnk";
    }

    inline bool IsHash(std::string_view hash) {
        return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
    }

    inline std::string DownloadName(const std::string &hash) {
        return "mod-" + hash + ".zip";
    }

    struct Entry {
        std::string path;
        uint32_t index;
        uint64_t size;
    };

    // Owns the ZIP reader; never mounts unverified bytes into a scripting VFS.
    class Archive final {
      public:
        explicit Archive(const std::filesystem::path &path) {
            if (std::filesystem::file_size(path) > kMaxZipBytes || !mz_zip_reader_init_file(&_zip, path.string().c_str(), 0))
                throw std::runtime_error("Unreadable or oversized mod ZIP: " + path.string());
        }
        ~Archive() {
            mz_zip_reader_end(&_zip);
        }
        Archive(const Archive &)            = delete;
        Archive &operator=(const Archive &) = delete;

        std::vector<Entry> Inspect() {
            const auto count = mz_zip_reader_get_num_files(&_zip);
            if (count > kMaxEntries)
                throw std::runtime_error("Too many files in mod ZIP");
            std::vector<Entry> entries;
            std::unordered_set<std::string> seen;
            uint64_t total = 0;
            for (uint32_t i = 0; i < count; ++i) {
                mz_zip_archive_file_stat stat {};
                if (!mz_zip_reader_file_stat(&_zip, i, &stat))
                    throw std::runtime_error("Invalid ZIP entry");
                const auto length = mz_zip_reader_get_filename(&_zip, i, nullptr, 0);
                if (length == 0 || length > 240)
                    throw std::runtime_error("Invalid ZIP filename length");
                std::string name(length, '\0');
                mz_zip_reader_get_filename(&_zip, i, name.data(), length);
                name.pop_back();
                if (name.find('\0') != std::string::npos)
                    throw std::runtime_error("NUL in ZIP filename");
                if (stat.m_is_directory && !name.empty() && (name.back() == '/' || name.back() == '\\'))
                    name.pop_back();
                const auto normalized = NormalizePath(name);
                if (normalized.empty() || ((stat.m_external_attr >> 16) & 0170000) == 0120000)
                    throw std::runtime_error("Unsafe mod path: " + name);
                if (stat.m_is_directory)
                    continue;
                if (!IsAssetPath(normalized))
                    throw std::runtime_error("Not a game asset: " + name);
                if (!seen.insert(normalized).second)
                    throw std::runtime_error("Duplicate mod path (case insensitive): " + name);
                if (stat.m_is_encrypted || !stat.m_is_supported || stat.m_uncomp_size > kMaxFileBytes || stat.m_uncomp_size > kMaxExpandedBytes - total)
                    throw std::runtime_error("Unsupported or oversized ZIP entry: " + name);
                total += stat.m_uncomp_size;
                entries.push_back({normalized, i, stat.m_uncomp_size});
            }
            if (entries.empty())
                throw std::runtime_error("Mod ZIP contains no assets");
            return entries;
        }

        void Extract(const Entry &entry, const std::filesystem::path &destination) {
            // Compare with the verified archive, not merely size/mtime. In particular, do not
            // truncate an unchanged sound that another client has open in the shared cache.
            if (Matches(entry, destination))
                return;
            const auto temporary = std::filesystem::path(destination.string() + ".part-" + std::to_string(std::random_device {}()));
            std::error_code error;
            if (!mz_zip_reader_extract_to_file(&_zip, entry.index, temporary.string().c_str(), 0)) {
                std::filesystem::remove(temporary, error);
                throw std::runtime_error("Could not extract mod asset: " + entry.path);
            }
            // Another client may have published identical bytes while we were extracting.
            if (Matches(entry, destination)) {
                std::filesystem::remove(temporary, error);
                return;
            }
            std::filesystem::remove(destination, error);
            error.clear();
            std::filesystem::rename(temporary, destination, error);
            if (error) {
                const bool matched = Matches(entry, destination);
                std::filesystem::remove(temporary, error);
                if (!matched)
                    throw std::runtime_error("Could not publish mod asset: " + entry.path);
            }
        }

        void Validate() {
            if (!mz_zip_validate_archive(&_zip, 0))
                throw std::runtime_error("Corrupt mod ZIP");
        }

      private:
        bool Matches(const Entry &entry, const std::filesystem::path &path) {
            std::error_code error;
            if (std::filesystem::file_size(path, error) != entry.size || error)
                return false;
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return false;
            return mz_zip_reader_extract_to_callback(
                       &_zip, entry.index,
                       [](void *opaque, mz_uint64, const void *data, size_t size) -> size_t {
                           auto &input = *static_cast<std::ifstream *>(opaque);
                           std::array<char, 32768> buffer;
                           for (size_t offset = 0; offset < size;) {
                               const auto count = std::min(buffer.size(), size - offset);
                               if (!input.read(buffer.data(), static_cast<std::streamsize>(count)) || std::memcmp(buffer.data(), static_cast<const char *>(data) + offset, count) != 0)
                                   return 0;
                               offset += count;
                           }
                           return size;
                       },
                       &file, 0)
                   != 0;
        }

        mz_zip_archive _zip {};
    };

    inline std::vector<std::string> DetectMissions(const std::unordered_set<std::string> &paths) {
        std::vector<std::string> result;
        for (const auto &path : paths) {
            if (!path.starts_with("missions/") || !path.ends_with("/scene.4ds"))
                continue;
            const auto name = path.substr(9, path.size() - 9 - 10);
            if (name.empty() || name.find('/') != std::string::npos || name.size() > 63)
                continue;
            if (paths.contains("missions/" + name + "/tree.klz"))
                result.push_back(name);
        }
        std::sort(result.begin(), result.end());
        return result;
    }
} // namespace Mafia1Online::Shared::Mod
