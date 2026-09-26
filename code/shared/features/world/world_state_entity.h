#pragma once

#include <networking/replication/network_entity.h>

#include <mafianet/string.h>

#include <cstddef>
#include <cstdint>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>

namespace Mafia1Online::Shared::Entities {
    // World state every client applies after each mission load: scripted
    // weather, frames, city music and night mode, plus the server's semaphore
    // cycle. One global replica also supplies late joiners.
    class WorldStateEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::WorldState";

        enum class Weather : uint8_t {
            // The mission's own weather parameters.
            Default,
            Clear,
            Rain,
            Snow,
        };
        static constexpr uint8_t kWeatherCount = 4;
        static constexpr size_t kMaxFrames      = 128;
        static constexpr size_t kMaxFrameOpacities = 128;
        static constexpr size_t kMaxFrameName   = 63;
        // WSP_10 particle count at intensity 100; retail rain uses 1000.
        static constexpr float kMaxParticles = 3000.0f;

        uint8_t weather = static_cast<uint8_t>(Weather::Default);
        // 0 to 100; negative keeps the preset's own particle count.
        float rainIntensity = -1.0f;
        bool cityMusicEnabled = true;
        // -1 follows the mission script; 0/1 overrides it for every client.
        int8_t nightMode = -1;
        // Milliseconds in retail's 10-phase, 30-second semaphore cycle.
        uint16_t semaphoreCycleMs = 0;
        // Frame overrides belong to one mission generation; the frame names
        // come from that mission's scene.
        uint64_t frameGeneration = 0;
        // "name=1\n" per shown and "name=0\n" per hidden frame.
        std::string frames;
        // "name=0.5\n" per visual; the same mission generation as frames.
        std::string frameOpacities;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(weather);
            fields.Field(rainIntensity);
            fields.Field(cityMusicEnabled);
            fields.Field(nightMode);
            fields.Field(semaphoreCycleMs);
            fields.Field(frameGeneration);
            MafiaNet::RakString text(frames.c_str());
            fields.Field(text);
            if (!fields.Writing()) {
                frames = text.C_String();
            }
            MafiaNet::RakString opacityText(frameOpacities.c_str());
            fields.Field(opacityText);
            if (!fields.Writing()) {
                frameOpacities = opacityText.C_String();
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(weather);
            fields.Field(rainIntensity);
            fields.Field(cityMusicEnabled);
            fields.Field(nightMode);
            fields.Field(semaphoreCycleMs);
            fields.Field(frameGeneration);
            fields.Field(frames);
            fields.Field(frameOpacities);
        }

        // Frame names the scripts may address: printable ASCII without the
        // separators, never one of the mod's own frames.
        static bool ValidFrameName(std::string_view name) {
            if (name.empty() || name.size() > kMaxFrameName || name.starts_with("mp_") || name.starts_with("Mafia1Online")) {
                return false;
            }
            for (const char c : name) {
                if (c < 0x21 || c > 0x7e || c == '=') {
                    return false;
                }
            }
            return true;
        }

        template <typename Fn>
        static void ForEachFrame(std::string_view frames, Fn &&fn) {
            while (!frames.empty()) {
                const size_t end  = frames.find('\n');
                const auto line   = frames.substr(0, end);
                const size_t mark = line.rfind('=');
                if (mark != std::string_view::npos && mark + 2 == line.size() && ValidFrameName(line.substr(0, mark))) {
                    fn(line.substr(0, mark), line[mark + 1] == '1');
                }
                if (end == std::string_view::npos) {
                    break;
                }
                frames.remove_prefix(end + 1);
            }
        }

        template <typename Fn>
        static void ForEachFrameOpacity(std::string_view opacities, Fn &&fn) {
            while (!opacities.empty()) {
                const size_t end = opacities.find('\n');
                const auto line = opacities.substr(0, end);
                const size_t mark = line.rfind('=');
                if (mark != std::string_view::npos && ValidFrameName(line.substr(0, mark))) {
                    float opacity = 0.0f;
                    const char *begin = line.data() + mark + 1;
                    const auto parsed = std::from_chars(begin, line.data() + line.size(), opacity);
                    if (parsed.ec == std::errc {} && parsed.ptr == line.data() + line.size() && std::isfinite(opacity) && opacity >= 0.0f && opacity <= 1.0f) {
                        fn(line.substr(0, mark), opacity);
                    }
                }
                if (end == std::string_view::npos) {
                    break;
                }
                opacities.remove_prefix(end + 1);
            }
        }
    };
} // namespace Mafia1Online::Shared::Entities
