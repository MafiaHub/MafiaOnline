#pragma once

#include "key_catalog.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Mafia1Online::Features::Settings {
    // reM shared/GameKey.h: the profile-ABI binding record C_Input reads.
    struct NativeBinding {
        uint32_t direction   = 0;
        uint16_t device      = 0;
        uint16_t code        = 0;
        uint32_t deviceIndex = 0;

        bool operator==(const NativeBinding &) const = default;
    };

    inline constexpr int kNativeActionCount       = 60;
    inline constexpr uint16_t kDeviceNone         = 0;
    inline constexpr uint16_t kDeviceKeyboard     = 1;
    inline constexpr uint16_t kDeviceMouse        = 2;
    inline constexpr uint16_t kDeviceJoystick     = 3;
    inline constexpr uint32_t kDirectionNone      = 0;
    inline constexpr uint32_t kDirectionPositive  = 1;
    inline constexpr uint32_t kDirectionNegative  = 2;
    inline constexpr uint32_t kDirectionButton    = 3;
    inline constexpr uint16_t kMouseAxisX         = 0x100;
    inline constexpr uint16_t kMouseAxisY         = 0x200;
    inline constexpr uint16_t kMouseAxisZ         = 0x300;

    // Where a binding acts. Retail checks conflicts per on-foot and per car
    // group (G_LoadSaveClass::FindBindingSlot 0x607460); the mod's keys and
    // script keys work in both.
    enum class Scope : uint8_t {
        OnFoot,
        Car,
        Everywhere,
    };

    // One row of the controls page: a retail action and its primary and
    // optional secondary G_LoadSaveClass binding slot.
    struct NativeAction {
        std::string_view id;
        std::string_view label;
        Scope scope;
        int primary;
        int secondary;
    };

    std::span<const NativeAction> NativeActions();
    // The slot's stable name in the settings file ("forward", "forward2"),
    // empty for the unused slot 32 and the fixed mouse aim axes 18 and 19.
    std::string_view NativeSlotId(int slot);
    std::optional<int> NativeSlotFromId(std::string_view id);
    // G_LoadSaveClass::InitDefaultBindings 0x607600 with every mask.
    NativeBinding DefaultNativeBinding(int slot);

    // Keys the mod itself handles, outside C_Input.
    enum class ModKey : uint8_t {
        Chat,
        Command,
        PlayerList,
        PushToTalk,
        Siren,
        CarSync,
        Count,
    };
    inline constexpr size_t kModKeyCount = static_cast<size_t>(ModKey::Count);

    struct ModKeyInfo {
        std::string_view id;
        std::string_view label;
        std::string_view defaultKey;
        Scope scope;
    };
    const ModKeyInfo &GetModKeyInfo(ModKey key);
    std::optional<ModKey> ModKeyFromId(std::string_view id);

    // G_ControlSettings fields the options menu edits (reM G_LoadSaveClass.h),
    // with InitDefaultControlSettings 0x607ab0 defaults.
    struct NativeControls {
        float mouseX            = 0.5f;
        float mouseY            = 0.5f;
        float turnLimit         = 0.5f;
        float steering          = 0.2f;
        bool invertY            = false;
        bool doubleTapRoll      = true;
        bool mph                = true;
        uint32_t crosshair      = 0;

        bool operator==(const NativeControls &) const = default;
    };

    struct NativeAudio {
        float sound  = 1.0f;
        float music  = 1.0f;
        float car    = 1.0f;
        float speech = 1.0f;

        bool operator==(const NativeAudio &) const = default;
    };

    struct VoiceSettings {
        bool enabled          = true;
        // Framework VoiceClient master gain; the page offers 0-200 %.
        float volume          = 1.0f;
        bool voiceActivity    = false;
        // Full-scale RMS, the scale VoiceClient::GetInputLevel reports.
        float threshold       = 0.03f;
        uint32_t releaseMs    = 100;
        bool noiseSuppression = false;
        // Empty for the system default microphone.
        std::string device;

        bool operator==(const VoiceSettings &) const = default;
    };

    struct HudSettings {
        bool nametags      = true;
        bool nametagHealth = true;

        bool operator==(const HudSettings &) const = default;
    };

    // Everything the mod's settings file holds. Retail-owned values stay
    // empty until the player changes them here; until then the selected Mafia
    // profile's values apply, as they always have.
    struct ClientSettings {
        std::array<std::optional<NativeBinding>, kNativeActionCount> bindings;
        std::optional<NativeControls> controls;
        std::optional<NativeAudio> audio;
        std::array<std::string, kModKeyCount> modKeys;
        // "resource/key" to the key the player presses for it instead; empty
        // when the player unbound it.
        std::map<std::string, std::string> scriptKeys;
        // Gain for resources' web audio, such as the Free Ride radio.
        float mediaVolume = 1.0f;
        VoiceSettings voice;
        HudSettings hud;
    };

    inline constexpr int kSettingsVersion       = 1;
    inline constexpr size_t kMaxScriptKeys      = 256;
    inline constexpr size_t kMaxDeviceNameBytes = 256;
    inline constexpr size_t kMaxResourceName    = 64;

    ClientSettings DefaultSettings();
    // Never throws. Unknown, malformed or out-of-range entries fall back to
    // their defaults one by one; a non-object gives every default.
    ClientSettings ParseSettings(const nlohmann::json &root);
    nlohmann::json ToJson(const ClientSettings &settings);

    // A section the page can reset.
    enum class Section : uint8_t {
        Controls,
        Mouse,
        Audio,
        Voice,
        Hud,
    };
    std::optional<Section> SectionFromId(std::string_view id);
    void ResetSection(ClientSettings &settings, Section section);

    // Bindable keys per target: C_Input reads keyboard scan codes, mouse
    // buttons 1-3 and the wheel; the mod's keys are DirectInput scan codes
    // except push-to-talk, a held virtual key; script keys are virtual keys
    // that can be held.
    bool CanBindNative(const KeyInfo &key);
    bool CanBindMod(ModKey target, const KeyInfo &key);
    bool CanBindScript(const KeyInfo &key);
    NativeBinding NativeBindingFor(const KeyInfo &key);
    // The catalog key a binding names; nullptr for none, axes and joysticks.
    const KeyInfo *KeyOfBinding(const NativeBinding &binding);
    bool ValidNativeBinding(const NativeBinding &binding);

    bool ValidResourceName(std::string_view name);
    std::string ScriptKeyId(std::string_view resource, std::string_view key);

    struct BoundSlot {
        std::string slot;
        std::string key;
        Scope scope;
    };
    // For each slot, the other slots on the same key that can fire together:
    // the same scope, or either acting everywhere. Unbound slots never clash.
    std::map<std::string, std::vector<std::string>> FindConflicts(const std::vector<BoundSlot> &slots);
} // namespace Mafia1Online::Features::Settings
