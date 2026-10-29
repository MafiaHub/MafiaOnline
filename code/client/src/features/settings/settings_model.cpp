#include "settings_model.h"

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Settings {
    namespace {
        constexpr NativeBinding Key(uint16_t dik) {
            return {kDirectionNone, kDeviceKeyboard, dik, 0};
        }
        constexpr NativeBinding Mouse(uint16_t code, uint32_t direction = kDirectionNone) {
            return {direction, kDeviceMouse, code, 0};
        }
        constexpr NativeBinding Joystick(uint16_t code, uint32_t direction = kDirectionNone) {
            return {direction, kDeviceJoystick, code, 0};
        }
        constexpr NativeBinding kNone {};

        // InitDefaultBindings 0x607600, slot by slot (reM LOADSAVE_CONTROL_*).
        constexpr NativeBinding kDefaults[kNativeActionCount] {
            Key(0xc8), Key(0x11), Key(0xd0), Key(0x1f), Key(0xcb), Key(0x1e), Key(0xcd), Key(0x20),
            Key(0x36), Key(0x2a), Mouse(2), Key(0x16), Mouse(1), Key(0x21), Key(0x9d), Key(0x10),
            Key(0x52), Key(0x12), Mouse(kMouseAxisX), Mouse(kMouseAxisY), Mouse(kMouseAxisZ, kDirectionPositive), Key(0xc9),
            Mouse(kMouseAxisZ, kDirectionNegative), Key(0xd1), Key(0x17), Key(0x23), Key(0x0e), Key(0x3a), Key(0x26), Key(0x13),
            Key(0x3b), Key(0x0f), kNone,
            Key(0xc8), Joystick(1, kDirectionNegative), Key(0xd0), Joystick(1, kDirectionPositive), Key(0xcb), Joystick(0, kDirectionNegative),
            Key(0xcd), Joystick(0, kDirectionPositive), Key(0x39), kNone, Key(0x3f), kNone, Key(0x1e), kNone, Key(0x2c), kNone,
            Key(0x25), kNone, Key(0x33), Joystick(0x218), Key(0x34), Joystick(0x212), Key(0x2d), kNone, Key(0x32), Key(0x2e),
            Key(0x52),
        };

        constexpr std::string_view kSlotIds[kNativeActionCount] {
            "forward", "forward2", "backward", "backward2", "left", "left2", "right", "right2",
            "walk", "walk2", "use", "use2", "fire", "fire2", "crouch", "crouch2",
            "jump", "jump2", "", "", "nextWeapon", "nextWeapon2", "previousWeapon", "previousWeapon2",
            "inventory", "holster", "dropWeapon", "runToggle", "reload", "sniper", "objectives", "cityMap", "",
            "accelerate", "accelerate2", "brake", "brake2", "steerLeft", "steerLeft2", "steerRight", "steerRight2",
            "handbrake", "handbrake2", "speedLimiter", "speedLimiter2", "gearUp", "gearUp2", "gearDown", "gearDown2",
            "horn", "horn2", "lookLeft", "lookLeft2", "lookRight", "lookRight2", "clutch", "clutch2",
            "gearbox", "carCamera", "carReplace",
        };

        constexpr NativeAction kActions[] {
            {"forward", "Move forward", Scope::OnFoot, 0, 1},
            {"backward", "Move back", Scope::OnFoot, 2, 3},
            {"left", "Step left", Scope::OnFoot, 4, 5},
            {"right", "Step right", Scope::OnFoot, 6, 7},
            {"walk", "Walk (hold)", Scope::OnFoot, 8, 9},
            {"runToggle", "Run / walk toggle", Scope::OnFoot, 27, -1},
            {"crouch", "Crouch", Scope::OnFoot, 14, 15},
            {"jump", "Jump / climb", Scope::OnFoot, 16, 17},
            {"use", "Use / enter car", Scope::OnFoot, 10, 11},
            {"fire", "Fire / punch", Scope::OnFoot, 12, 13},
            {"reload", "Reload", Scope::OnFoot, 28, -1},
            {"sniper", "Sniper view", Scope::OnFoot, 29, -1},
            {"nextWeapon", "Next weapon", Scope::OnFoot, 20, 21},
            {"previousWeapon", "Previous weapon", Scope::OnFoot, 22, 23},
            {"holster", "Holster weapon", Scope::OnFoot, 25, -1},
            {"dropWeapon", "Drop weapon", Scope::OnFoot, 26, -1},
            {"inventory", "Inventory", Scope::OnFoot, 24, -1},
            {"cityMap", "City map (hold)", Scope::OnFoot, 31, -1},
            {"objectives", "Objectives", Scope::OnFoot, 30, -1},
            {"accelerate", "Accelerate", Scope::Car, 33, 34},
            {"brake", "Brake / reverse", Scope::Car, 35, 36},
            {"steerLeft", "Steer left", Scope::Car, 37, 38},
            {"steerRight", "Steer right", Scope::Car, 39, 40},
            {"handbrake", "Handbrake", Scope::Car, 41, 42},
            {"horn", "Horn", Scope::Car, 49, 50},
            {"gearUp", "Gear up", Scope::Car, 45, 46},
            {"gearDown", "Gear down", Scope::Car, 47, 48},
            {"clutch", "Clutch", Scope::Car, 55, 56},
            {"gearbox", "Manual / automatic gearbox", Scope::Car, 57, -1},
            {"speedLimiter", "Speed limiter", Scope::Car, 43, 44},
            {"lookLeft", "Look left", Scope::Car, 51, 52},
            {"lookRight", "Look right", Scope::Car, 53, 54},
            {"carCamera", "Change camera", Scope::Car, 58, -1},
            {"carReplace", "Put the car back on its wheels", Scope::Car, 59, -1},
        };

        constexpr ModKeyInfo kModKeys[kModKeyCount] {
            {"chat", "Chat", "t", Scope::Everywhere},
            {"command", "Chat with a command (/)", "slash", Scope::Everywhere},
            {"playerList", "Player list (hold)", "f1", Scope::Everywhere},
            {"pushToTalk", "Voice: push to talk", "v", Scope::Everywhere},
            {"siren", "Siren (police car)", "k", Scope::Car},
            {"carSync", "Car sync mode (debug)", "f10", Scope::Everywhere},
        };

        const nlohmann::json *Member(const nlohmann::json &object, const char *name) {
            if (!object.is_object()) {
                return nullptr;
            }
            const auto it = object.find(name);
            return it == object.end() ? nullptr : &*it;
        }

        void ReadFloat(const nlohmann::json &object, const char *name, float minimum, float maximum, float &out) {
            const auto *value = Member(object, name);
            if (value && value->is_number()) {
                const double read = value->get<double>();
                if (std::isfinite(read)) {
                    out = static_cast<float>(std::clamp(read, static_cast<double>(minimum), static_cast<double>(maximum)));
                }
            }
        }

        void ReadBool(const nlohmann::json &object, const char *name, bool &out) {
            const auto *value = Member(object, name);
            if (value && value->is_boolean()) {
                out = value->get<bool>();
            }
        }

        void ReadUInt(const nlohmann::json &object, const char *name, uint32_t maximum, uint32_t &out) {
            const auto *value = Member(object, name);
            if (value && value->is_number_unsigned()) {
                out = static_cast<uint32_t>(std::min<uint64_t>(value->get<uint64_t>(), maximum));
            }
        }

        // Printable text with no control characters; the device list comes
        // from the OS, so anything else cannot name a real device.
        bool ValidDeviceName(const std::string &name) {
            return name.size() <= kMaxDeviceNameBytes && std::none_of(name.begin(), name.end(), [](unsigned char ch) {
                return ch < 0x20 || ch == 0x7f;
            });
        }

        std::optional<NativeBinding> ReadBinding(const nlohmann::json &value) {
            if (value.is_string()) {
                const auto name = value.get<std::string>();
                if (name.empty()) {
                    return kNone;
                }
                const auto *key = FindKey(name);
                if (!key || !CanBindNative(*key)) {
                    return std::nullopt;
                }
                return NativeBindingFor(*key);
            }
            if (!value.is_object()) {
                return std::nullopt;
            }
            const auto field = [&](const char *name, uint64_t maximum) -> std::optional<uint64_t> {
                const auto *member = Member(value, name);
                if (!member) {
                    return 0;
                }
                if (!member->is_number_unsigned() || member->get<uint64_t>() > maximum) {
                    return std::nullopt;
                }
                return member->get<uint64_t>();
            };
            const auto device    = field("device", 0xffff);
            const auto code      = field("code", 0xffff);
            const auto index     = field("index", 0xffffffff);
            const auto direction = field("direction", 0xffffffff);
            if (!device || !code || !index || !direction) {
                return std::nullopt;
            }
            const NativeBinding binding {static_cast<uint32_t>(*direction), static_cast<uint16_t>(*device), static_cast<uint16_t>(*code), static_cast<uint32_t>(*index)};
            if (!ValidNativeBinding(binding)) {
                return std::nullopt;
            }
            return binding;
        }

        nlohmann::json WriteBinding(const NativeBinding &binding) {
            if (binding.device == kDeviceNone) {
                return "";
            }
            if (const auto *key = KeyOfBinding(binding)) {
                return std::string(key->name);
            }
            return {{"device", binding.device}, {"code", binding.code}, {"index", binding.deviceIndex}, {"direction", binding.direction}};
        }

        std::optional<std::string> ReadModKey(ModKey target, const nlohmann::json &value) {
            if (!value.is_string()) {
                return std::nullopt;
            }
            const auto name = value.get<std::string>();
            if (name.empty()) {
                // Chat has no other way in.
                return target == ModKey::Chat ? std::nullopt : std::optional<std::string>(name);
            }
            const auto *key = FindKey(name);
            if (!key || !CanBindMod(target, *key)) {
                return std::nullopt;
            }
            return name;
        }
    } // namespace

    std::span<const NativeAction> NativeActions() {
        return kActions;
    }

    std::string_view NativeSlotId(int slot) {
        return slot >= 0 && slot < kNativeActionCount ? kSlotIds[slot] : std::string_view();
    }

    std::optional<int> NativeSlotFromId(std::string_view id) {
        if (id.empty()) {
            return std::nullopt;
        }
        for (int slot = 0; slot < kNativeActionCount; ++slot) {
            if (kSlotIds[slot] == id) {
                return slot;
            }
        }
        return std::nullopt;
    }

    NativeBinding DefaultNativeBinding(int slot) {
        return slot >= 0 && slot < kNativeActionCount ? kDefaults[slot] : kNone;
    }

    const ModKeyInfo &GetModKeyInfo(ModKey key) {
        return kModKeys[std::min(static_cast<size_t>(key), kModKeyCount - 1)];
    }

    std::optional<ModKey> ModKeyFromId(std::string_view id) {
        for (size_t index = 0; index < kModKeyCount; ++index) {
            if (kModKeys[index].id == id) {
                return static_cast<ModKey>(index);
            }
        }
        return std::nullopt;
    }

    bool CanBindNative(const KeyInfo &key) {
        // Escape opens the pause menu before C_Input sees it.
        return key.name != "escape" && (key.dik != 0 || key.mouseButton != 0 || key.wheel != 0);
    }

    bool CanBindMod(ModKey target, const KeyInfo &key) {
        if (key.name == "escape") {
            return false;
        }
        // Push-to-talk is polled as a held virtual key; the others arrive as
        // DirectInput scan codes or keyboard window messages.
        if (target == ModKey::PushToTalk) {
            return key.vk != 0 && key.wheel == 0;
        }
        return key.dik != 0;
    }

    bool CanBindScript(const KeyInfo &key) {
        // The remap answers "is it held", which a wheel notch never is.
        return key.name != "escape" && key.vk != 0 && key.wheel == 0;
    }

    NativeBinding NativeBindingFor(const KeyInfo &key) {
        if (key.dik != 0) {
            return Key(key.dik);
        }
        if (key.mouseButton != 0) {
            return Mouse(key.mouseButton);
        }
        if (key.wheel != 0) {
            return Mouse(kMouseAxisZ, key.wheel > 0 ? kDirectionPositive : kDirectionNegative);
        }
        return kNone;
    }

    const KeyInfo *KeyOfBinding(const NativeBinding &binding) {
        if (binding.deviceIndex != 0) {
            return nullptr;
        }
        if (binding.device == kDeviceKeyboard && binding.direction == kDirectionNone) {
            return FindKeyByDik(binding.code);
        }
        if (binding.device != kDeviceMouse) {
            return nullptr;
        }
        if (binding.code == kMouseAxisZ) {
            return binding.direction == kDirectionPositive ? FindKeyByWheel(1) : binding.direction == kDirectionNegative ? FindKeyByWheel(-1) : nullptr;
        }
        return binding.direction == kDirectionNone ? FindKeyByMouseButton(binding.code) : nullptr;
    }

    // C_Input::BindKey 0x4f02b0 and Update 0x4f0620 decode these: keyboard
    // scan codes, mouse buttons 1/2/4 and axes 0x100-0x300, and joystick
    // axes below 8, buttons 0x100-0x11f and POV hats 0x200-0x23f.
    bool ValidNativeBinding(const NativeBinding &binding) {
        switch (binding.device) {
        case kDeviceNone: return binding.code == 0 && binding.direction == kDirectionNone && binding.deviceIndex == 0;
        case kDeviceKeyboard: return binding.code > 0 && binding.code <= 0xff && binding.direction == kDirectionNone && binding.deviceIndex == 0;
        case kDeviceMouse:
            if (binding.deviceIndex != 0) {
                return false;
            }
            if (binding.code == 1 || binding.code == 2 || binding.code == 4) {
                return binding.direction == kDirectionNone;
            }
            return (binding.code == kMouseAxisX || binding.code == kMouseAxisY || binding.code == kMouseAxisZ) && binding.direction <= kDirectionNegative;
        case kDeviceJoystick:
            if (binding.deviceIndex >= 16 || binding.direction > kDirectionButton) {
                return false;
            }
            return binding.code < 8 || (binding.code >= 0x100 && binding.code < 0x120) || (binding.code >= 0x200 && binding.code < 0x240);
        default: return false;
        }
    }

    bool ValidResourceName(std::string_view name) {
        return !name.empty() && name.size() <= kMaxResourceName && std::all_of(name.begin(), name.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
        });
    }

    std::string ScriptKeyId(std::string_view resource, std::string_view key) {
        std::string id(resource);
        id += '/';
        id += key;
        return id;
    }

    ClientSettings DefaultSettings() {
        ClientSettings settings;
        for (size_t index = 0; index < kModKeyCount; ++index) {
            settings.modKeys[index] = std::string(kModKeys[index].defaultKey);
        }
        return settings;
    }

    ClientSettings ParseSettings(const nlohmann::json &root) {
        ClientSettings settings = DefaultSettings();
        if (!root.is_object()) {
            return settings;
        }
        if (const auto *bindings = Member(root, "bindings"); bindings && bindings->is_object()) {
            for (const auto &[id, value] : bindings->items()) {
                const auto slot = NativeSlotFromId(id);
                if (!slot) {
                    continue;
                }
                if (const auto binding = ReadBinding(value)) {
                    settings.bindings[*slot] = *binding;
                }
            }
        }
        if (const auto *keys = Member(root, "keys"); keys && keys->is_object()) {
            for (size_t index = 0; index < kModKeyCount; ++index) {
                const auto target = static_cast<ModKey>(index);
                if (const auto *value = Member(*keys, std::string(kModKeys[index].id).c_str())) {
                    if (const auto name = ReadModKey(target, *value)) {
                        settings.modKeys[index] = *name;
                    }
                }
            }
        }
        if (const auto *scriptKeys = Member(root, "scriptKeys"); scriptKeys && scriptKeys->is_object()) {
            for (const auto &[id, value] : scriptKeys->items()) {
                if (settings.scriptKeys.size() >= kMaxScriptKeys) {
                    break;
                }
                const auto separator = id.find('/');
                if (separator == std::string::npos || !ValidResourceName(std::string_view(id).substr(0, separator)) || !value.is_string()) {
                    continue;
                }
                // An empty value unbinds the key.
                const auto name      = value.get<std::string>();
                const auto *logical  = FindKey(std::string_view(id).substr(separator + 1));
                const auto *physical = FindKey(name);
                if (logical && CanBindScript(*logical) && (name.empty() || (physical && CanBindScript(*physical)))) {
                    settings.scriptKeys[id] = name;
                }
            }
        }
        if (const auto *mouse = Member(root, "mouse"); mouse && mouse->is_object()) {
            NativeControls controls;
            ReadFloat(*mouse, "sensitivityX", 0.05f, 1.0f, controls.mouseX);
            ReadFloat(*mouse, "sensitivityY", 0.05f, 1.0f, controls.mouseY);
            ReadFloat(*mouse, "turnLimit", 0.05f, 1.0f, controls.turnLimit);
            ReadFloat(*mouse, "steering", 0.0f, 1.0f, controls.steering);
            ReadBool(*mouse, "invertY", controls.invertY);
            ReadBool(*mouse, "doubleTapRoll", controls.doubleTapRoll);
            ReadBool(*mouse, "mph", controls.mph);
            ReadUInt(*mouse, "crosshair", 2, controls.crosshair);
            settings.controls = controls;
        }
        if (const auto *audio = Member(root, "audio"); audio && audio->is_object()) {
            NativeAudio volumes;
            ReadFloat(*audio, "sound", 0.0f, 1.0f, volumes.sound);
            ReadFloat(*audio, "music", 0.0f, 1.0f, volumes.music);
            ReadFloat(*audio, "car", 0.0f, 1.0f, volumes.car);
            ReadFloat(*audio, "speech", 0.0f, 1.0f, volumes.speech);
            settings.audio = volumes;
        }
        ReadFloat(root, "media", 0.0f, 1.0f, settings.mediaVolume);
        if (const auto *voice = Member(root, "voice"); voice && voice->is_object()) {
            ReadBool(*voice, "enabled", settings.voice.enabled);
            ReadFloat(*voice, "volume", 0.0f, 2.0f, settings.voice.volume);
            if (const auto *mode = Member(*voice, "mode"); mode && mode->is_string()) {
                const auto name = mode->get<std::string>();
                if (name == "voiceActivity" || name == "pushToTalk") {
                    settings.voice.voiceActivity = name == "voiceActivity";
                }
            }
            ReadFloat(*voice, "threshold", 0.001f, 0.5f, settings.voice.threshold);
            ReadUInt(*voice, "releaseMs", 2000, settings.voice.releaseMs);
            ReadBool(*voice, "noiseSuppression", settings.voice.noiseSuppression);
            if (const auto *device = Member(*voice, "device"); device && device->is_string() && ValidDeviceName(device->get<std::string>())) {
                settings.voice.device = device->get<std::string>();
            }
        }
        if (const auto *hud = Member(root, "hud"); hud && hud->is_object()) {
            ReadBool(*hud, "nametags", settings.hud.nametags);
            ReadBool(*hud, "nametagHealth", settings.hud.nametagHealth);
        }
        return settings;
    }

    nlohmann::json ToJson(const ClientSettings &settings) {
        nlohmann::json root = {{"version", kSettingsVersion}};
        nlohmann::json bindings = nlohmann::json::object();
        for (int slot = 0; slot < kNativeActionCount; ++slot) {
            if (settings.bindings[slot] && !kSlotIds[slot].empty()) {
                bindings[std::string(kSlotIds[slot])] = WriteBinding(*settings.bindings[slot]);
            }
        }
        if (!bindings.empty()) {
            root["bindings"] = std::move(bindings);
        }
        nlohmann::json keys = nlohmann::json::object();
        for (size_t index = 0; index < kModKeyCount; ++index) {
            keys[std::string(kModKeys[index].id)] = settings.modKeys[index];
        }
        root["keys"]       = std::move(keys);
        root["scriptKeys"] = settings.scriptKeys;
        if (settings.controls) {
            const auto &controls = *settings.controls;
            root["mouse"]        = {{"sensitivityX", controls.mouseX},
                                    {"sensitivityY", controls.mouseY},
                                    {"turnLimit", controls.turnLimit},
                                    {"steering", controls.steering},
                                    {"invertY", controls.invertY},
                                    {"doubleTapRoll", controls.doubleTapRoll},
                                    {"mph", controls.mph},
                                    {"crosshair", controls.crosshair}};
        }
        if (settings.audio) {
            const auto &audio = *settings.audio;
            root["audio"]     = {{"sound", audio.sound}, {"music", audio.music}, {"car", audio.car}, {"speech", audio.speech}};
        }
        root["media"] = settings.mediaVolume;
        root["voice"] = {{"enabled", settings.voice.enabled},
                         {"volume", settings.voice.volume},
                         {"mode", settings.voice.voiceActivity ? "voiceActivity" : "pushToTalk"},
                         {"threshold", settings.voice.threshold},
                         {"releaseMs", settings.voice.releaseMs},
                         {"noiseSuppression", settings.voice.noiseSuppression},
                         {"device", settings.voice.device}};
        root["hud"]   = {{"nametags", settings.hud.nametags}, {"nametagHealth", settings.hud.nametagHealth}};
        return root;
    }

    std::optional<Section> SectionFromId(std::string_view id) {
        if (id == "controls") return Section::Controls;
        if (id == "mouse") return Section::Mouse;
        if (id == "audio") return Section::Audio;
        if (id == "voice") return Section::Voice;
        if (id == "hud") return Section::Hud;
        return std::nullopt;
    }

    void ResetSection(ClientSettings &settings, Section section) {
        const ClientSettings defaults = DefaultSettings();
        switch (section) {
        case Section::Controls:
            for (int slot = 0; slot < kNativeActionCount; ++slot) {
                settings.bindings[slot] = kDefaults[slot];
            }
            settings.modKeys = defaults.modKeys;
            settings.scriptKeys.clear();
            break;
        case Section::Mouse: settings.controls = NativeControls {}; break;
        case Section::Audio:
            settings.audio        = NativeAudio {};
            settings.mediaVolume  = defaults.mediaVolume;
            settings.voice.volume = defaults.voice.volume;
            break;
        case Section::Voice: {
            // The voice volume sits with the other volumes on the audio page.
            const float volume    = settings.voice.volume;
            settings.voice        = defaults.voice;
            settings.voice.volume = volume;
            break;
        }
        case Section::Hud: settings.hud = defaults.hud; break;
        }
    }

    std::map<std::string, std::vector<std::string>> FindConflicts(const std::vector<BoundSlot> &slots) {
        std::map<std::string, std::vector<std::string>> conflicts;
        for (size_t a = 0; a < slots.size(); ++a) {
            for (size_t b = a + 1; b < slots.size(); ++b) {
                const auto &first  = slots[a];
                const auto &second = slots[b];
                if (first.key.empty() || first.key != second.key || first.slot == second.slot) {
                    continue;
                }
                if (first.scope != second.scope && first.scope != Scope::Everywhere && second.scope != Scope::Everywhere) {
                    continue;
                }
                conflicts[first.slot].push_back(second.slot);
                conflicts[second.slot].push_back(first.slot);
            }
        }
        return conflicts;
    }
} // namespace Mafia1Online::Features::Settings
