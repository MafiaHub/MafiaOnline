#include "settings_service.h"

#include "remapped_input.h"

#include "features/script/script_runtime.h"
#include "shared/features/chat/text_policy.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/profile/native_profile.h>
#include <mafia1/sdk/ui/native_indicators.h>
#include <utils/key_names.h>
#include <voice/client/voice_client.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace Mafia1Online::Features::Settings {
    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr auto kSaveDelay              = std::chrono::milliseconds(1000);
        constexpr size_t kMaxScriptKeysPerOwner = 64;
        constexpr size_t kMaxTogglesPerOwner    = 32;
        constexpr size_t kMaxLabel              = 64;
        constexpr size_t kMaxToggleId           = 32;

        // Next to the client config, as ui.json is, so a second local
        // instance started with its own config keeps its own settings.
        std::filesystem::path SettingsPath(const std::string &projectPath) {
            const char *configOverride = std::getenv("MAFIA1ONLINE_CLIENT_CONFIG");
            if (configOverride && *configOverride) {
                const std::filesystem::path config(configOverride);
                return config.parent_path() / (config.stem().string() + ".settings.json");
            }
            return std::filesystem::path(projectPath) / "config" / "settings.json";
        }

        SDK::Profile::NativeLoadSave &Profile() {
            return SDK::Profile::Get();
        }

        // Null between missions only if the game itself is gone; the menu
        // scene keeps a C_mission with its C_game.
        SDK::Core::Game::NativeGame *CurrentGame() {
            auto *mission = SDK::Core::Mission::Get();
            return mission ? mission->Game() : nullptr;
        }

        std::string KeyLabel(const KeyInfo &key) {
            // The Framework names the key in the player's layout where it can.
            if (key.vk != 0 && !Framework::Utils::KeyNames::FromVirtualKey(key.vk).empty()) {
                if (auto label = Framework::Utils::KeyNames::GetLabel(key.vk); !label.empty()) {
                    return label;
                }
            }
            return std::string(key.label);
        }

        std::string KeyLabel(std::string_view name) {
            const auto *key = FindKey(name);
            return key ? KeyLabel(*key) : std::string();
        }

        std::string BindingLabel(const NativeBinding &binding) {
            if (binding.device == kDeviceNone) {
                return {};
            }
            if (const auto *key = KeyOfBinding(binding)) {
                return KeyLabel(*key);
            }
            if (binding.device == kDeviceMouse) {
                return binding.code == kMouseAxisX ? "Mouse X axis" : binding.code == kMouseAxisY ? "Mouse Y axis" : "Mouse wheel";
            }
            if (binding.device == kDeviceKeyboard) {
                return "Key " + std::to_string(binding.code);
            }
            std::string label = "Controller";
            if (binding.deviceIndex > 0) {
                label += " " + std::to_string(binding.deviceIndex + 1);
            }
            if (binding.code < 8) {
                const char *sign = binding.direction == kDirectionPositive ? "+" : binding.direction == kDirectionNegative ? "-" : "";
                return label + ": axis " + std::to_string(binding.code + 1) + sign;
            }
            if (binding.code < 0x200) {
                return label + ": button " + std::to_string(binding.code - 0x100 + 1);
            }
            return label + ": hat " + std::to_string(((binding.code >> 4) & 0x0f) + 1);
        }

        NativeBinding FromNative(const SDK::Profile::NativeGameKey &key) {
            return {key.direction, key.device, key.code, key.deviceIndex};
        }

        std::string Clean(const std::string &text, size_t limit) {
            return Shared::Chat::SanitizeLine(text, limit);
        }

        bool ValidToggleId(const std::string &id) {
            return !id.empty() && id.size() <= kMaxToggleId && ValidResourceName(id);
        }

        nlohmann::json ControlsJson(const NativeControls &controls) {
            ClientSettings settings;
            settings.controls = controls;
            return ToJson(settings)["mouse"];
        }

        nlohmann::json AudioJson(const NativeAudio &audio) {
            ClientSettings settings;
            settings.audio = audio;
            return ToJson(settings)["audio"];
        }
    } // namespace

    void SettingsService::Load(const std::string &projectPath) {
        const auto logger = Framework::Logging::GetLogger("Settings");
        _path             = SettingsPath(projectPath);
        _settings         = DefaultSettings();
        std::ifstream input(_path);
        if (!input) {
            logger->info("No settings at {}; using defaults and the profile's controls", _path.string());
            return;
        }
        const auto root = nlohmann::json::parse(input, nullptr, false);
        input.close();
        if (root.is_discarded() || !root.is_object()) {
            // Keep the unreadable file for the player rather than overwrite it.
            std::error_code error;
            auto keep = _path;
            keep += ".invalid";
            std::filesystem::rename(_path, keep, error);
            logger->error("Settings at {} are not valid JSON; using defaults (the file was kept as {})", _path.string(), keep.string());
            return;
        }
        _settings = ParseSettings(root);
        logger->info("Settings loaded from {}", _path.string());
    }

    void SettingsService::AttachInput(RemappedInput *input) {
        _input = input;
        ApplyScriptKeys();
    }

    void SettingsService::ApplyAll() {
        ApplyBindings();
        ApplyControls();
        ApplyAudio();
        ApplyVoice();
        ApplyScriptKeys();
    }

    void SettingsService::OnMissionReady() {
        ApplyControls();
        ApplyAudio();
    }

    void SettingsService::Update() {
        if (_dirtySince && Clock::now() - *_dirtySince >= kSaveDelay) {
            Save();
        }
        // Script events are raised here, on the client tick, rather than from
        // inside the page's CEF callback.
        if (_mediaChanged) {
            _mediaChanged      = false;
            const float volume = _settings.mediaVolume;
            Scripting::EmitEvent("mediaVolumeChange", [volume](v8::Isolate *isolate, v8::Local<v8::Context>, Scripting::EventArguments &args) {
                args.push_back(v8pp::to_v8(isolate, volume));
            });
        }
        for (auto events = std::exchange(_toggleEvents, {}); const auto &event : events) {
            Scripting::EmitEvent("settingToggle", [&event](v8::Isolate *isolate, v8::Local<v8::Context>, Scripting::EventArguments &args) {
                args.push_back(v8pp::to_v8(isolate, event.id));
                args.push_back(v8pp::to_v8(isolate, event.value));
                args.push_back(v8pp::to_v8(isolate, event.resource));
            });
        }
    }

    void SettingsService::Shutdown() {
        Flush();
        _scriptKeys.clear();
        _toggles.clear();
        _toggleEvents.clear();
        _input = nullptr;
    }

    void SettingsService::Flush() {
        if (_dirtySince) {
            Save();
        }
    }

    void SettingsService::Save() {
        _dirtySince.reset();
        if (_path.empty()) {
            return;
        }
        const auto logger = Framework::Logging::GetLogger("Settings");
        std::error_code error;
        std::filesystem::create_directories(_path.parent_path(), error);
        auto temporary = _path;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::trunc);
            if (!output) {
                logger->error("Could not write settings to {}", temporary.string());
                return;
            }
            output << ToJson(_settings).dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
            if (!output) {
                logger->error("Could not write settings to {}", temporary.string());
                return;
            }
        }
        std::filesystem::rename(temporary, _path, error);
        if (error) {
            logger->error("Could not replace {}: {}", _path.string(), error.message());
        }
    }

    void SettingsService::Changed() {
        ++_revision;
        if (!_dirtySince) {
            _dirtySince = Clock::now();
        }
    }

    uint16_t SettingsService::ModKeyDik(ModKey key) const {
        const auto *info = FindKey(_settings.modKeys[static_cast<size_t>(key)]);
        return info ? info->dik : 0;
    }

    NativeBinding SettingsService::LiveBinding(int slot) const {
        return FromNative(Profile().Bindings()[slot]);
    }

    NativeControls SettingsService::LiveControls() const {
        const auto &live = Profile().Controls();
        NativeControls controls;
        controls.mouseX        = live.mouseSensitivityX;
        controls.mouseY        = live.mouseSensitivityY;
        controls.turnLimit     = live.mouseSpeedLimitX;
        controls.steering      = live.steeringLinearity;
        controls.invertY       = live.invertMouseY;
        controls.doubleTapRoll = live.doubleTapSideRoll;
        controls.mph           = live.useMph;
        controls.crosshair     = std::min<uint32_t>(live.indicatorMode, 2);
        return controls;
    }

    NativeAudio SettingsService::LiveAudio() const {
        const auto &live = Profile().Controls();
        return {live.soundVolume, live.musicVolume, live.carVolume, live.speechVolume};
    }

    // Once any binding is the player's, every slot is, so the file no longer
    // depends on which profile happens to be selected.
    void SettingsService::ImportLiveBindings() {
        for (int slot = 0; slot < kNativeActionCount; ++slot) {
            if (!_settings.bindings[slot]) {
                _settings.bindings[slot] = LiveBinding(slot);
            }
        }
    }

    // GM_Menu_optionscontrols::OnClick and OnDestroy write m_Bindings and
    // call PushBindingsToInput the same way.
    void SettingsService::ApplyBindings() {
        auto *native = Profile().Bindings();
        bool changed = false;
        for (int slot = 0; slot < kNativeActionCount; ++slot) {
            if (const auto &binding = _settings.bindings[slot]; binding && FromNative(native[slot]) != *binding) {
                native[slot] = {binding->direction, binding->device, binding->code, binding->deviceIndex};
                changed      = true;
            }
        }
        if (changed) {
            Profile().PushBindingsToInput();
        }
    }

    void SettingsService::ApplyControls() {
        if (!_settings.controls) {
            return;
        }
        const auto &controls = *_settings.controls;
        auto &live           = Profile().Controls();
        const bool mphChanged = live.useMph != controls.mph;
        live.mouseSensitivityX = controls.mouseX;
        live.mouseSensitivityY = controls.mouseY;
        live.mouseSpeedLimitX  = controls.turnLimit;
        live.steeringLinearity = controls.steering;
        live.invertMouseY      = controls.invertY;
        live.doubleTapSideRoll = controls.doubleTapRoll;
        live.useMph            = controls.mph;
        live.indicatorMode     = controls.crosshair;
        auto *game             = CurrentGame();
        if (!game || !game->IsInitialized()) {
            // C_game::Init picks both up with the next mission.
            return;
        }
        if (mphChanged) {
            SDK::UI::NativeIndicators::Get().GameInit();
        }
        game->UpdatePlayerControls();
    }

    // Gamma is left alone: the mod runs windowed, where SetGammaRamp does not apply.
    void SettingsService::ApplyAudio() {
        if (!_settings.audio) {
            return;
        }
        const auto &audio  = *_settings.audio;
        auto &live         = Profile().Controls();
        live.soundVolume   = audio.sound;
        live.musicVolume   = audio.music;
        live.carVolume     = audio.car;
        live.speechVolume  = audio.speech;
        uint32_t mask      = SDK::Profile::kRefreshSoundVolume | SDK::Profile::kRefreshVehicleVolume;
        if (CurrentGame()) {
            mask |= SDK::Profile::kRefreshMusicVolume;
        }
        Profile().Refresh(mask);
    }

    void SettingsService::ApplyVoice() {
        auto *voice = Framework::CoreModules::GetVoiceClient();
        if (!voice) {
            return;
        }
        const auto &settings = _settings.voice;
        voice->SetEnabled(settings.enabled);
        voice->SetMasterVolume(settings.volume);
        voice->SetTransmitMode(settings.voiceActivity ? Framework::Voice::TransmitMode::VoiceActivity : Framework::Voice::TransmitMode::PushToTalk);
        voice->SetVoiceActivationThreshold(settings.threshold);
        voice->SetPushToTalkReleaseDelay(settings.releaseMs);
        voice->SetNoiseSuppression(settings.noiseSuppression);
        voice->SetCaptureDevice(settings.device);
        const auto *key = FindKey(_settings.modKeys[static_cast<size_t>(ModKey::PushToTalk)]);
        voice->SetPushToTalkKey(key ? key->vk : 0);
    }

    std::string SettingsService::PhysicalScriptKey(const ScriptKey &key) const {
        const auto it = _settings.scriptKeys.find(ScriptKeyId(key.resource, key.key));
        return it == _settings.scriptKeys.end() ? key.key : it->second;
    }

    void SettingsService::ApplyScriptKeys() {
        if (!_input) {
            return;
        }
        _input->ResetMap();
        for (const auto &key : _scriptKeys) {
            const auto *logical  = FindKey(key.key);
            const auto *physical = FindKey(PhysicalScriptKey(key));
            if (logical) {
                // An unbound key reads virtual key 0, which is never down.
                _input->Map(logical->vk, physical ? physical->vk : 0);
            }
        }
    }

    void SettingsService::RefreshDevices() {
        auto *voice = Framework::CoreModules::GetVoiceClient();
        _devices    = voice ? voice->ListCaptureDevices() : std::vector<std::string> {};
        ++_revision;
    }

    nlohmann::json SettingsService::PageState() const {
        struct Row {
            std::string id;
            std::string label;
            std::vector<std::string> slots;
        };
        struct Group {
            std::string id;
            std::string label;
            std::vector<Row> rows;
        };
        struct Slot {
            std::string key;
            std::string label;
            std::string defaultLabel;
            std::string owner;
            bool mouse = false;
            bool wheel = false;
            bool clearable = true;
        };
        std::vector<Group> groups {{"onFoot", "On foot", {}}, {"car", "In a car", {}}, {"multiplayer", "Multiplayer", {}}};
        std::map<std::string, Slot> slots;
        std::vector<BoundSlot> bound;

        for (const auto &action : NativeActions()) {
            Row row {std::string(action.id), std::string(action.label), {}};
            for (const int index : {action.primary, action.secondary}) {
                if (index < 0) {
                    continue;
                }
                const auto binding = _settings.bindings[index].value_or(LiveBinding(index));
                const auto *key    = KeyOfBinding(binding);
                const std::string id = "native:" + std::string(NativeSlotId(index));
                Slot slot;
                slot.key          = key ? std::string(key->name) : binding.device == kDeviceNone ? std::string() : std::string("?");
                slot.label        = BindingLabel(binding);
                slot.defaultLabel = BindingLabel(DefaultNativeBinding(index));
                slot.owner        = row.label + (index == action.secondary ? " (2nd)" : "");
                slot.mouse        = true;
                slot.wheel        = true;
                slots[id]         = slot;
                bound.push_back({id, key ? std::string(key->name) : std::string(), action.scope});
                row.slots.push_back(id);
            }
            groups[action.scope == Scope::Car ? 1 : 0].rows.push_back(std::move(row));
        }
        for (size_t index = 0; index < kModKeyCount; ++index) {
            const auto target = static_cast<ModKey>(index);
            const auto &info  = GetModKeyInfo(target);
            const auto &name  = _settings.modKeys[index];
            const std::string id = "mod:" + std::string(info.id);
            Slot slot;
            slot.key          = name;
            slot.label        = KeyLabel(name);
            slot.defaultLabel = KeyLabel(info.defaultKey);
            slot.owner        = std::string(info.label);
            slot.mouse        = target == ModKey::PushToTalk;
            slot.clearable    = target != ModKey::Chat;
            slots[id]         = slot;
            bound.push_back({id, name, info.scope});
            groups[2].rows.push_back({std::string(info.id), std::string(info.label), {id}});
        }
        for (const auto &key : _scriptKeys) {
            const std::string groupId = "script:" + key.resource;
            auto group = std::find_if(groups.begin(), groups.end(), [&](const Group &candidate) { return candidate.id == groupId; });
            if (group == groups.end()) {
                groups.push_back({groupId, "Server: " + key.resource, {}});
                group = std::prev(groups.end());
            }
            const auto physical = PhysicalScriptKey(key);
            const std::string id = "script:" + ScriptKeyId(key.resource, key.key);
            Slot slot;
            slot.key          = physical;
            slot.label        = KeyLabel(physical);
            slot.defaultLabel = KeyLabel(key.key);
            slot.owner        = key.label;
            slot.mouse        = true;
            slots[id]         = slot;
            bound.push_back({id, physical, Scope::Everywhere});
            group->rows.push_back({key.key, key.label, {id}});
        }

        const auto conflicts = FindConflicts(bound);
        nlohmann::json slotJson = nlohmann::json::object();
        for (const auto &[id, slot] : slots) {
            nlohmann::json shared = nlohmann::json::array();
            if (const auto it = conflicts.find(id); it != conflicts.end()) {
                for (const auto &other : it->second) {
                    shared.push_back(slots.at(other).owner);
                }
            }
            slotJson[id] = {{"key", slot.key},
                            {"label", slot.label},
                            {"defaultLabel", slot.defaultLabel},
                            {"mouse", slot.mouse},
                            {"wheel", slot.wheel},
                            {"clearable", slot.clearable},
                            {"conflicts", shared}};
        }
        nlohmann::json groupJson = nlohmann::json::array();
        for (const auto &group : groups) {
            nlohmann::json rows = nlohmann::json::array();
            for (const auto &row : group.rows) {
                rows.push_back({{"id", row.id}, {"label", row.label}, {"slots", row.slots}});
            }
            groupJson.push_back({{"id", group.id}, {"label", group.label}, {"rows", rows}});
        }

        nlohmann::json toggles = nlohmann::json::array();
        for (const auto &toggle : _toggles) {
            toggles.push_back({{"resource", toggle.resource}, {"id", toggle.id}, {"label", toggle.label}, {"value", toggle.value}});
        }

        auto *voice = Framework::CoreModules::GetVoiceClient();
        auto file   = ToJson(_settings);
        return {
            {"groups", groupJson},
            {"slots", slotJson},
            {"mouse", ControlsJson(_settings.controls.value_or(LiveControls()))},
            {"audio", AudioJson(_settings.audio.value_or(LiveAudio()))},
            {"media", _settings.mediaVolume},
            {"voice", file["voice"]},
            {"hud", file["hud"]},
            {"devices", _devices},
            {"voiceAvailable", voice != nullptr},
            {"toggles", toggles},
            {"profile", {{"controls", !_settings.controls.has_value()}, {"audio", !_settings.audio.has_value()}}},
        };
    }

    std::optional<std::string> SettingsService::Set(const nlohmann::json &patch) {
        if (!patch.is_object()) {
            return "Unknown setting";
        }
        // Validate through the file parser: the page is no more trusted than the file.
        auto merged = ToJson(_settings);
        if (!_settings.controls) {
            merged["mouse"] = ControlsJson(LiveControls());
        }
        if (!_settings.audio) {
            merged["audio"] = AudioJson(LiveAudio());
        }
        bool touched = false;
        for (const char *section : {"mouse", "audio", "voice", "hud"}) {
            if (const auto it = patch.find(section); it != patch.end() && it->is_object()) {
                merged[section].update(*it);
                touched = true;
            }
        }
        if (const auto it = patch.find("media"); it != patch.end() && it->is_number()) {
            merged["media"] = *it;
            touched         = true;
        }
        if (!touched) {
            return "Unknown setting";
        }
        const ClientSettings parsed = ParseSettings(merged);
        bool changed = false;
        if (patch.contains("mouse") && parsed.controls != _settings.controls) {
            _settings.controls = parsed.controls;
            ApplyControls();
            changed = true;
        }
        if (patch.contains("audio") && parsed.audio != _settings.audio) {
            _settings.audio = parsed.audio;
            ApplyAudio();
            changed = true;
        }
        if (patch.contains("media") && parsed.mediaVolume != _settings.mediaVolume) {
            _settings.mediaVolume = parsed.mediaVolume;
            _mediaChanged         = true;
            changed               = true;
        }
        if (patch.contains("voice") && !(parsed.voice == _settings.voice)) {
            _settings.voice = parsed.voice;
            ApplyVoice();
            changed = true;
        }
        if (patch.contains("hud") && !(parsed.hud == _settings.hud)) {
            _settings.hud = parsed.hud;
            changed       = true;
        }
        if (changed) {
            Changed();
        }
        return std::nullopt;
    }

    std::optional<std::string> SettingsService::Bind(const std::string &slot, const std::string &keyName) {
        const KeyInfo *key = keyName.empty() ? nullptr : FindKey(keyName);
        if (!keyName.empty() && !key) {
            return "That key cannot be bound";
        }
        if (slot.starts_with("native:")) {
            const auto index = NativeSlotFromId(std::string_view(slot).substr(7));
            if (!index) {
                return "Unknown control";
            }
            if (key && !CanBindNative(*key)) {
                return "The game cannot use " + KeyLabel(*key) + " for this";
            }
            ImportLiveBindings();
            _settings.bindings[*index] = key ? NativeBindingFor(*key) : NativeBinding {};
            ApplyBindings();
        }
        else if (slot.starts_with("mod:")) {
            const auto target = ModKeyFromId(std::string_view(slot).substr(4));
            if (!target) {
                return "Unknown control";
            }
            if (!key && *target == ModKey::Chat) {
                return "Chat needs a key";
            }
            if (key && !CanBindMod(*target, *key)) {
                return KeyLabel(*key) + " cannot be used for this";
            }
            _settings.modKeys[static_cast<size_t>(*target)] = key ? std::string(key->name) : std::string();
            if (*target == ModKey::PushToTalk) {
                ApplyVoice();
            }
        }
        else if (slot.starts_with("script:")) {
            const auto id        = slot.substr(7);
            const auto described = std::find_if(_scriptKeys.begin(), _scriptKeys.end(), [&](const ScriptKey &candidate) {
                return ScriptKeyId(candidate.resource, candidate.key) == id;
            });
            if (described == _scriptKeys.end()) {
                return "That server control is no longer available";
            }
            if (key && !CanBindScript(*key)) {
                return KeyLabel(*key) + " cannot be used for this";
            }
            if (key && key->name == described->key) {
                _settings.scriptKeys.erase(id);
            }
            else if (_settings.scriptKeys.size() < kMaxScriptKeys || _settings.scriptKeys.contains(id)) {
                _settings.scriptKeys[id] = key ? std::string(key->name) : std::string();
            }
            else {
                return "Too many server controls are moved";
            }
            ApplyScriptKeys();
        }
        else {
            return "Unknown control";
        }
        Changed();
        return std::nullopt;
    }

    std::optional<std::string> SettingsService::Reset(const std::string &id) {
        const auto section = SectionFromId(id);
        if (!section) {
            return "Unknown section";
        }
        const auto voice = _settings.voice;
        ResetSection(_settings, *section);
        switch (*section) {
        case Section::Controls:
            ApplyBindings();
            ApplyScriptKeys();
            ApplyVoice();
            break;
        case Section::Mouse: ApplyControls(); break;
        case Section::Audio:
            ApplyAudio();
            _mediaChanged = true;
            if (!(voice == _settings.voice)) {
                ApplyVoice();
            }
            break;
        case Section::Voice: ApplyVoice(); break;
        case Section::Hud: break;
        }
        Changed();
        return std::nullopt;
    }

    void SettingsService::SetToggleFromPage(const std::string &resource, const std::string &id, bool value) {
        const auto it = std::find_if(_toggles.begin(), _toggles.end(), [&](const ScriptToggle &toggle) { return toggle.resource == resource && toggle.id == id; });
        if (it == _toggles.end() || it->value == value) {
            return;
        }
        it->value = value;
        _toggleEvents.push_back({resource, id, value});
        ++_revision;
    }

    bool SettingsService::DescribeScriptKey(const std::string &resource, const std::string &keyName, const std::string &label) {
        const auto *key = FindKey(keyName);
        if (!ValidResourceName(resource) || !key || !CanBindScript(*key)) {
            return false;
        }
        const std::string text = Clean(label, kMaxLabel);
        if (text.empty()) {
            return false;
        }
        const auto existing = std::find_if(_scriptKeys.begin(), _scriptKeys.end(), [&](const ScriptKey &candidate) {
            return candidate.resource == resource && candidate.key == key->name;
        });
        if (existing != _scriptKeys.end()) {
            if (existing->label != text) {
                existing->label = text;
                ++_revision;
            }
            return true;
        }
        const auto owned = std::count_if(_scriptKeys.begin(), _scriptKeys.end(), [&](const ScriptKey &candidate) { return candidate.resource == resource; });
        if (static_cast<size_t>(owned) >= kMaxScriptKeysPerOwner) {
            return false;
        }
        _scriptKeys.push_back({resource, std::string(key->name), text});
        ApplyScriptKeys();
        ++_revision;
        return true;
    }

    std::string SettingsService::ScriptKeyLabel(const std::string &resource, const std::string &keyName) const {
        const auto *key = FindKey(keyName);
        if (!key) {
            return {};
        }
        const auto it = _settings.scriptKeys.find(ScriptKeyId(resource, key->name));
        if (it == _settings.scriptKeys.end()) {
            return KeyLabel(*key);
        }
        return it->second.empty() ? std::string("Unbound") : KeyLabel(it->second);
    }

    bool SettingsService::DefineToggle(const std::string &resource, const std::string &id, const std::string &label, bool value) {
        const std::string text = Clean(label, kMaxLabel);
        if (!ValidResourceName(resource) || !ValidToggleId(id) || text.empty()) {
            return false;
        }
        const auto it = std::find_if(_toggles.begin(), _toggles.end(), [&](const ScriptToggle &toggle) { return toggle.resource == resource && toggle.id == id; });
        if (it != _toggles.end()) {
            it->label = text;
            it->value = value;
        }
        else {
            const auto owned = std::count_if(_toggles.begin(), _toggles.end(), [&](const ScriptToggle &toggle) { return toggle.resource == resource; });
            if (static_cast<size_t>(owned) >= kMaxTogglesPerOwner) {
                return false;
            }
            _toggles.push_back({resource, id, text, value});
        }
        ++_revision;
        return true;
    }

    bool SettingsService::SetToggle(const std::string &resource, const std::string &id, bool value) {
        const auto it = std::find_if(_toggles.begin(), _toggles.end(), [&](const ScriptToggle &toggle) { return toggle.resource == resource && toggle.id == id; });
        if (it == _toggles.end()) {
            return false;
        }
        if (it->value != value) {
            it->value = value;
            ++_revision;
        }
        return true;
    }

    void SettingsService::ClearResource(const std::string &resource) {
        const auto keys    = std::erase_if(_scriptKeys, [&](const ScriptKey &key) { return key.resource == resource; });
        const auto toggles = std::erase_if(_toggles, [&](const ScriptToggle &toggle) { return toggle.resource == resource; });
        std::erase_if(_toggleEvents, [&](const ToggleEvent &event) { return event.resource == resource; });
        if (keys > 0) {
            ApplyScriptKeys();
        }
        if (keys > 0 || toggles > 0) {
            ++_revision;
        }
    }
} // namespace Mafia1Online::Features::Settings
