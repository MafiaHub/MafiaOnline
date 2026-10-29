#pragma once

#include "settings_model.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Mafia1Online::Features::Settings {
    class RemappedInput;

    // The player's controls, volumes, voice and HUD choices. They live in the
    // mod's own settings file and are applied in-process: retail bindings and
    // control settings into the loaded profile's G_LoadSaveClass copy (whose
    // writes the mod blocks), voice into the Framework VoiceClient, and script
    // key moves into the input Key.bind polls. Absent on the simulation host.
    class SettingsService final {
      public:
        void Load(const std::string &projectPath);
        void AttachInput(RemappedInput *input);
        // Everything, once the native profile is loaded.
        void ApplyAll();
        // C_game::Init reloads volumes and the dashboard from the profile copy.
        void OnMissionReady();
        // Debounced save and queued script events.
        void Update();
        void Shutdown();

        // DirectInput scan code of a mod key; 0 while unbound.
        uint16_t ModKeyDik(ModKey key) const;
        // The same key as the window messages carry it.
        uint32_t ModKeyMessage(ModKey key) const {
            return DikToMessageKey(ModKeyDik(key));
        }
        const HudSettings &Hud() const {
            return _settings.hud;
        }
        float MediaVolume() const {
            return _settings.mediaVolume;
        }

        // --- the settings page ---

        // Bumped on every change the page should redraw.
        uint64_t Revision() const {
            return _revision;
        }
        nlohmann::json PageState() const;
        // Re-lists microphones; ListCaptureDevices opens its own audio context.
        void RefreshDevices();
        // A partial settings object ({ mouse?, audio?, media?, voice?, hud? }),
        // validated like the file. Error text for the player, or nullopt.
        std::optional<std::string> Set(const nlohmann::json &patch);
        std::optional<std::string> Bind(const std::string &slot, const std::string &key);
        std::optional<std::string> Reset(const std::string &section);
        void SetToggleFromPage(const std::string &resource, const std::string &id, bool value);
        // Writes pending changes now (the page closed).
        void Flush();

        // --- client scripts ---

        bool DescribeScriptKey(const std::string &resource, const std::string &key, const std::string &label);
        // The key the player presses for a resource's logical key.
        std::string ScriptKeyLabel(const std::string &resource, const std::string &key) const;
        bool DefineToggle(const std::string &resource, const std::string &id, const std::string &label, bool value);
        bool SetToggle(const std::string &resource, const std::string &id, bool value);
        void ClearResource(const std::string &resource);
        void RequestOpen() {
            _openRequested = true;
        }
        bool TakeOpenRequest() {
            return std::exchange(_openRequested, false);
        }

      private:
        struct ScriptKey {
            std::string resource;
            std::string key;
            std::string label;
        };
        struct ScriptToggle {
            std::string resource;
            std::string id;
            std::string label;
            bool value = false;
        };
        struct ToggleEvent {
            std::string resource;
            std::string id;
            bool value = false;
        };

        void Save();
        void Changed();
        void ApplyBindings();
        void ApplyControls();
        void ApplyAudio();
        void ApplyVoice();
        void ApplyScriptKeys();
        NativeControls LiveControls() const;
        NativeAudio LiveAudio() const;
        NativeBinding LiveBinding(int slot) const;
        void ImportLiveBindings();
        std::string PhysicalScriptKey(const ScriptKey &key) const;

        std::filesystem::path _path;
        ClientSettings _settings = DefaultSettings();
        RemappedInput *_input = nullptr;
        std::vector<ScriptKey> _scriptKeys;
        std::vector<ScriptToggle> _toggles;
        std::vector<ToggleEvent> _toggleEvents;
        std::vector<std::string> _devices;
        bool _mediaChanged  = false;
        bool _openRequested = false;
        uint64_t _revision  = 1;
        std::optional<std::chrono::steady_clock::time_point> _dirtySince;
    };
} // namespace Mafia1Online::Features::Settings
