#include <utils/safe_win32.h>

#include "sync_trace.h"

#include "features/quick_join/quick_join.h"

#include <logging/logger.h>

#include <nlohmann/json.hpp>

#include <glm/geometric.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace Mafia1Online::Features::Debug {
    namespace {
        struct Settings {
            // Records farther than this from every focus point are dropped.
            float radius = 100.0f;
            // Truth and observed records a frame, at most; events always pass.
            uint32_t frameBudget = 400;
            // A file this large moves to <name>.1.jsonl and a new one starts.
            uint64_t maxFileBytes = 256ull * 1024 * 1024;
        };

        Settings gSettings;
        std::filesystem::path gDirectory;
        std::string gRole = "client";
        FILE *gFile          = nullptr;
        uint64_t gFileBytes  = 0;
        int64_t gLastFlushUs = 0;
        uint32_t gFrameCount = 0;
        std::vector<glm::vec3> gFocus;
        // Records are built on the game thread, one at a time.
        std::string gLine;
        int64_t gFrequency = 0;

        std::string FileStem() {
            std::string stem;
            for (const char ch : gRole) {
                const bool plain = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
                stem.push_back(plain ? ch : '_');
            }
            return stem + "-" + std::to_string(GetCurrentProcessId());
        }

        void AppendEscaped(std::string &out, std::string_view value) {
            for (const char ch : value) {
                if (ch == '"' || ch == '\\') {
                    out.push_back('\\');
                    out.push_back(ch);
                }
                else if (static_cast<unsigned char>(ch) < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
                    out += escaped;
                }
                else {
                    out.push_back(ch);
                }
            }
        }

        void AppendNumber(std::string &out, double value) {
            if (!std::isfinite(value)) {
                out += "null";
                return;
            }
            // Positions to 0.1 mm or better; clocks (ms or larger) to 1 us.
            char text[40];
            const int length = std::fabs(value) >= 1.0e5 ? std::snprintf(text, sizeof(text), "%.3f", value) : std::snprintf(text, sizeof(text), "%.9g", value);
            out.append(text, static_cast<size_t>(std::max(length, 0)));
        }
    } // namespace

    SyncTrace::Record::Record(std::string *line): _line(line), _active(line != nullptr) {}

    SyncTrace::Record::Record(Record &&other) noexcept: _line(other._line), _active(other._active) {
        other._active = false;
        other._line   = nullptr;
    }

    SyncTrace::Record::~Record() {
        if (!_active || !gFile) {
            return;
        }
        _line->append("}\n");
        std::fwrite(_line->data(), 1, _line->size(), gFile);
        gFileBytes += _line->size();
        if (gFileBytes >= gSettings.maxFileBytes) {
            Close();
            std::error_code error;
            const auto current = gDirectory / (FileStem() + ".jsonl");
            std::filesystem::rename(current, gDirectory / (FileStem() + ".1.jsonl"), error);
            Open();
        }
    }

    void SyncTrace::Record::Key(std::string_view key) {
        _line->append(",\"");
        _line->append(key);
        _line->append("\":");
    }

    SyncTrace::Record &SyncTrace::Record::Num(std::string_view key, double value) {
        if (_active) {
            Key(key);
            AppendNumber(*_line, value);
        }
        return *this;
    }

    SyncTrace::Record &SyncTrace::Record::Int(std::string_view key, int64_t value) {
        if (_active) {
            Key(key);
            _line->append(std::to_string(value));
        }
        return *this;
    }

    SyncTrace::Record &SyncTrace::Record::Bool(std::string_view key, bool value) {
        if (_active) {
            Key(key);
            _line->append(value ? "true" : "false");
        }
        return *this;
    }

    SyncTrace::Record &SyncTrace::Record::Str(std::string_view key, std::string_view value) {
        if (_active) {
            Key(key);
            _line->push_back('"');
            AppendEscaped(*_line, value);
            _line->push_back('"');
        }
        return *this;
    }

    SyncTrace::Record &SyncTrace::Record::Vec(std::string_view key, const glm::vec3 &value) {
        if (_active) {
            Key(key);
            _line->push_back('[');
            AppendNumber(*_line, value.x);
            _line->push_back(',');
            AppendNumber(*_line, value.y);
            _line->push_back(',');
            AppendNumber(*_line, value.z);
            _line->push_back(']');
        }
        return *this;
    }

    SyncTrace::Record &SyncTrace::Record::Quat(std::string_view key, const glm::quat &value) {
        if (_active) {
            Key(key);
            _line->push_back('[');
            AppendNumber(*_line, value.w);
            _line->push_back(',');
            AppendNumber(*_line, value.x);
            _line->push_back(',');
            AppendNumber(*_line, value.y);
            _line->push_back(',');
            AppendNumber(*_line, value.z);
            _line->push_back(']');
        }
        return *this;
    }

    void SyncTrace::Initialize(const std::string &projectPath, const std::string &role) {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        gFrequency = frequency.QuadPart;
        gRole      = role.empty() ? "client" : role;
        gDirectory = std::filesystem::path(projectPath) / "logs" / "synctrace";
        bool enabled = false;
        if (std::ifstream input(QuickJoin::ConfigPath(projectPath)); input) {
            try {
                const auto root = nlohmann::json::parse(input);
                if (const auto settings = root.find("syncTrace"); settings != root.end() && settings->is_object()) {
                    enabled                  = settings->value("enabled", false);
                    gSettings.radius         = settings->value("radius", gSettings.radius);
                    gSettings.frameBudget    = settings->value("frameBudget", gSettings.frameBudget);
                    gSettings.maxFileBytes   = settings->value("maxFileMB", gSettings.maxFileBytes / (1024 * 1024)) * 1024 * 1024;
                }
            }
            catch (const std::exception &) {
                Framework::Logging::GetLogger("Mafia1Online")->warn("Sync trace: the client config's syncTrace object is invalid; using defaults");
            }
        }
        if (const char *variable = std::getenv("MAFIA1ONLINE_SYNCTRACE"); variable && *variable) {
            enabled = std::string_view(variable) != "0";
        }
        if (enabled) {
            Toggle();
        }
    }

    void SyncTrace::Shutdown() {
        Close();
        _enabled = false;
    }

    bool SyncTrace::Toggle() {
        if (_enabled) {
            Close();
            _enabled = false;
        }
        else {
            Open();
            _enabled = gFile != nullptr;
        }
        Framework::Logging::GetLogger("Mafia1Online")->info("Sync trace {} ({})", _enabled ? "on" : "off", (gDirectory / (FileStem() + ".jsonl")).string());
        return _enabled;
    }

    int64_t SyncTrace::ClockUs() {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        const int64_t frequency = gFrequency > 0 ? gFrequency : 1;
        return counter.QuadPart / frequency * 1'000'000 + counter.QuadPart % frequency * 1'000'000 / frequency;
    }

    void SyncTrace::BeginFrame(std::vector<glm::vec3> focus) {
        ++_frame;
        gFrameCount = 0;
        gFocus      = std::move(focus);
    }

    bool SyncTrace::Near(const glm::vec3 &position) {
        if (gFocus.empty()) {
            return true;
        }
        const float radius = gSettings.radius * gSettings.radius;
        for (const auto &focus : gFocus) {
            const glm::vec3 offset = position - focus;
            if (glm::dot(offset, offset) <= radius) {
                return true;
            }
        }
        return false;
    }

    SyncTrace::Record SyncTrace::Truth(std::string_view kind, uint64_t networkId) {
        return Begin("truth", kind, networkId, true);
    }

    SyncTrace::Record SyncTrace::Observed(std::string_view kind, uint64_t networkId) {
        return Begin("obs", kind, networkId, true);
    }

    SyncTrace::Record SyncTrace::Received(std::string_view kind, uint64_t networkId) {
        return Begin("rx", kind, networkId, true);
    }

    SyncTrace::Record SyncTrace::Event(std::string_view kind, uint64_t networkId, std::string_view name) {
        auto record = Begin("event", kind, networkId, false);
        record.Str("name", name);
        return record;
    }

    SyncTrace::Record SyncTrace::Begin(std::string_view type, std::string_view kind, uint64_t networkId, bool budgeted) {
        if (!_enabled || !gFile || (budgeted && gFrameCount >= gSettings.frameBudget)) {
            return Record();
        }
        if (budgeted) {
            ++gFrameCount;
        }
        gLine.clear();
        gLine += "{\"t\":";
        gLine += std::to_string(ClockUs());
        gLine += ",\"f\":";
        gLine += std::to_string(_frame);
        gLine += ",\"k\":\"";
        gLine.append(type);
        gLine += "\",\"e\":\"";
        gLine.append(kind);
        gLine += "\",\"id\":";
        gLine += std::to_string(networkId);
        return Record(&gLine);
    }

    void SyncTrace::EndFrame() {
        if (!gFile) {
            return;
        }
        const int64_t now = ClockUs();
        if (now - gLastFlushUs >= 1'000'000) {
            gLastFlushUs = now;
            std::fflush(gFile);
        }
    }

    void SyncTrace::Open() {
        std::error_code error;
        std::filesystem::create_directories(gDirectory, error);
        const auto path = gDirectory / (FileStem() + ".jsonl");
        gFile           = _wfopen(path.c_str(), L"ab");
        if (!gFile) {
            Framework::Logging::GetLogger("Mafia1Online")->error("Sync trace: could not open {}", path.string());
            return;
        }
        std::setvbuf(gFile, nullptr, _IOFBF, 1 << 20);
        gFileBytes = static_cast<uint64_t>(std::filesystem::file_size(path, error));
        gLine.clear();
        gLine += "{\"t\":" + std::to_string(ClockUs()) + ",\"k\":\"hdr\",\"role\":\"";
        AppendEscaped(gLine, gRole);
        gLine += "\",\"pid\":" + std::to_string(GetCurrentProcessId()) + ",\"clock\":\"qpc-us\",\"version\":1}\n";
        std::fwrite(gLine.data(), 1, gLine.size(), gFile);
        gFileBytes += gLine.size();
    }

    void SyncTrace::Close() {
        if (gFile) {
            std::fclose(gFile);
            gFile = nullptr;
        }
    }
} // namespace Mafia1Online::Features::Debug
