#pragma once

#include <networking/replication/network_entity.h>

#include <mafianet/string.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Mafia1Online::Shared::Entities {
    // A script-placed looping positional sound. Clients in range build their
    // own native sound frame for it and release it on stream-out or mission
    // close. An attached sound follows a player or vehicle.
    class SoundEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::Sound";
        static constexpr size_t kMaxWave       = 64;
        static constexpr float kMaxRadius      = 200.0f;
        static constexpr float kMaxVolume      = 1.0f;

        uint64_t missionGeneration = 0;
        std::string wave;
        float radius   = 25.0f;
        float volume   = 1.0f;
        bool enabled   = true;
        // Network ID of the followed player or vehicle, 0 for a fixed sound.
        uint64_t attachedId = 0;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(missionGeneration);
            MafiaNet::RakString name(wave.c_str());
            fields.Field(name);
            if (!fields.Writing()) {
                wave = name.C_String();
            }
            fields.Field(radius);
            fields.Field(volume);
            fields.Field(enabled);
            fields.Field(attachedId);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(wave);
            fields.Field(radius);
            fields.Field(volume);
            fields.Field(enabled);
            fields.Field(attachedId);
        }

        // A file under the game's Sounds directory or its archives, such as
        // "00_dog.wav". Shape only; whether it exists is a client fact.
        static bool ValidWave(std::string_view wave) {
            if (wave.empty() || wave.size() > kMaxWave || wave.find("..") != std::string_view::npos || wave.front() == '\\' || wave.front() == '/') {
                return false;
            }
            for (const char c : wave) {
                const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '\\' || c == '/';
                if (!allowed) {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace Mafia1Online::Shared::Entities
