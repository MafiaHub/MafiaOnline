#include "features/settings/key_catalog.h"
#include "features/settings/settings_model.h"

#include <cstdio>
#include <set>
#include <string>

using namespace Mafia1Online::Features::Settings;

namespace {
    int Fail(int code, const char *what) {
        std::printf("settings_tests: %s\n", what);
        return code;
    }
} // namespace

int main() {
    // The catalog is consistent: unique names, scan codes and virtual keys,
    // and each lookup finds the entry it came from.
    std::set<std::string_view> names;
    std::set<uint16_t> diks;
    std::set<uint8_t> vks;
    for (const auto &key : AllKeys()) {
        if (!names.insert(key.name).second)
            return Fail(1, "duplicate key name");
        if (key.dik != 0 && (!diks.insert(key.dik).second || FindKeyByDik(key.dik) != &key))
            return Fail(2, "duplicate or unfindable scan code");
        if (key.vk != 0 && (!vks.insert(key.vk).second || FindKeyByVirtualKey(key.vk) != &key))
            return Fail(3, "duplicate or unfindable virtual key");
        if (FindKey(key.name) != &key)
            return Fail(4, "name lookup");
    }
    if (!FindKey("w") || FindKey("w")->dik != 0x11 || FindKey("w")->vk != 'W' || FindKey("W") || FindKey("") || FindKey("bogus"))
        return Fail(5, "key lookup");
    if (FindKey("rctrl")->dik != 0x9d || DikToMessageKey(0x9d) != 0x11d || DikToMessageKey(0x14) != 0x14)
        return Fail(6, "extended scan codes");

    // Retail defaults mirror InitDefaultBindings, and every editable slot
    // has a unique name that round-trips.
    if (DefaultNativeBinding(0) != NativeBinding {0, kDeviceKeyboard, 0xc8, 0} || DefaultNativeBinding(12) != NativeBinding {0, kDeviceMouse, 1, 0} ||
        DefaultNativeBinding(20) != NativeBinding {kDirectionPositive, kDeviceMouse, kMouseAxisZ, 0} || DefaultNativeBinding(32) != NativeBinding {} ||
        DefaultNativeBinding(59) != NativeBinding {0, kDeviceKeyboard, 0x52, 0})
        return Fail(7, "retail defaults");
    std::set<int> slots;
    for (const auto &action : NativeActions()) {
        for (const int slot : {action.primary, action.secondary}) {
            if (slot < 0)
                continue;
            if (slot >= kNativeActionCount || slot == 18 || slot == 19 || slot == 32 || !slots.insert(slot).second)
                return Fail(8, "action slot table");
            if (NativeSlotFromId(NativeSlotId(slot)) != slot)
                return Fail(9, "slot id round trip");
        }
    }
    if (slots.size() != kNativeActionCount - 3 || NativeSlotFromId("") || NativeSlotFromId("aim"))
        return Fail(10, "every editable slot is listed once");

    // Keys per target.
    if (!CanBindNative(*FindKey("comma")) || !CanBindNative(*FindKey("mouse2")) || !CanBindNative(*FindKey("wheeldown")) || CanBindNative(*FindKey("mouse4")) ||
        CanBindNative(*FindKey("escape")))
        return Fail(11, "native targets");
    if (!CanBindMod(ModKey::PushToTalk, *FindKey("mouse4")) || CanBindMod(ModKey::Chat, *FindKey("mouse1")) || CanBindMod(ModKey::PushToTalk, *FindKey("wheelup")) ||
        CanBindMod(ModKey::Chat, *FindKey("escape")) || !CanBindMod(ModKey::Siren, *FindKey("numpadenter")) || CanBindMod(ModKey::PushToTalk, *FindKey("numpadenter")))
        return Fail(12, "mod key targets");
    if (!CanBindScript(*FindKey("f8")) || CanBindScript(*FindKey("wheelup")) || CanBindScript(*FindKey("numpadenter")))
        return Fail(13, "script key targets");
    if (KeyOfBinding(NativeBindingFor(*FindKey("wheelup"))) != FindKey("wheelup") || KeyOfBinding(NativeBindingFor(*FindKey("mouse3"))) != FindKey("mouse3") ||
        KeyOfBinding(DefaultNativeBinding(18)) != nullptr || KeyOfBinding(DefaultNativeBinding(34)) != nullptr)
        return Fail(14, "binding to key");

    // Missing or broken input gives defaults and never throws.
    for (const char *text : {"null", "[]", "42", "\"x\"", "{}", "{\"bindings\":7,\"keys\":[],\"mouse\":\"no\",\"voice\":null}"}) {
        const auto parsed = ParseSettings(nlohmann::json::parse(text));
        if (parsed.controls || parsed.audio || parsed.bindings[0] || parsed.modKeys[0] != "t" || parsed.mediaVolume != 1.0f || !parsed.voice.enabled)
            return Fail(15, "defaults for malformed input");
    }

    // Values are clamped and bad entries are dropped one by one.
    const auto parsed = ParseSettings(nlohmann::json::parse(R"({
        "bindings": {"forward": "f", "forward2": "", "fire": "mouse4", "jump": "bogus", "aim": "w", "use": {"device": 3, "code": 261, "index": 1, "direction": 0},
                     "crouch": {"device": 3, "code": 900}, "reload": {"device": 2, "code": 768, "direction": 2}},
        "keys": {"chat": "", "command": "mouse1", "pushToTalk": "mouse5", "siren": "", "playerList": "f3"},
        "scriptKeys": {"freeride/f9": "f8", "freeride/g": "", "bad name/f9": "f8", "freeride/wheelup": "f8", "freeride/u": "wheeldown", "noslash": "f8"},
        "mouse": {"sensitivityX": 7, "sensitivityY": -1, "invertY": true, "crosshair": 9, "mph": "yes"},
        "audio": {"sound": 0.25, "music": 2},
        "media": -3,
        "voice": {"volume": 9, "mode": "shout", "threshold": 0, "releaseMs": 99999, "device": "Mic\u0001"},
        "hud": {"nametags": false}
    })"));
    if (parsed.bindings[0] != NativeBinding {0, kDeviceKeyboard, 0x21, 0} || parsed.bindings[1] != NativeBinding {} || parsed.bindings[12] || parsed.bindings[16])
        return Fail(16, "binding entries");
    if (parsed.bindings[10] != NativeBinding {0, kDeviceJoystick, 261, 1} || parsed.bindings[14] || parsed.bindings[28] != NativeBinding {kDirectionNegative, kDeviceMouse, kMouseAxisZ, 0})
        return Fail(17, "binding objects");
    if (parsed.modKeys[static_cast<size_t>(ModKey::Chat)] != "t" || parsed.modKeys[static_cast<size_t>(ModKey::Command)] != "slash" ||
        parsed.modKeys[static_cast<size_t>(ModKey::PushToTalk)] != "mouse5" || !parsed.modKeys[static_cast<size_t>(ModKey::Siren)].empty() ||
        parsed.modKeys[static_cast<size_t>(ModKey::PlayerList)] != "f3")
        return Fail(18, "mod keys");
    if (parsed.scriptKeys.size() != 2 || parsed.scriptKeys.at("freeride/f9") != "f8" || !parsed.scriptKeys.at("freeride/g").empty())
        return Fail(19, "script keys");
    if (!parsed.controls || parsed.controls->mouseX != 1.0f || parsed.controls->mouseY != 0.05f || !parsed.controls->invertY || parsed.controls->crosshair != 2 ||
        !parsed.controls->mph || parsed.controls->steering != 0.2f)
        return Fail(20, "mouse");
    if (!parsed.audio || parsed.audio->sound != 0.25f || parsed.audio->music != 1.0f || parsed.audio->speech != 1.0f || parsed.mediaVolume != 0.0f)
        return Fail(21, "audio");
    if (parsed.voice.volume != 2.0f || parsed.voice.voiceActivity || parsed.voice.threshold != 0.001f || parsed.voice.releaseMs != 2000 || !parsed.voice.device.empty())
        return Fail(22, "voice");
    if (parsed.hud.nametags || !parsed.hud.nametagHealth)
        return Fail(23, "hud");

    // What is written reads back the same.
    const auto again = ParseSettings(ToJson(parsed));
    if (again.bindings != parsed.bindings || again.modKeys != parsed.modKeys || again.scriptKeys != parsed.scriptKeys || again.controls != parsed.controls ||
        again.audio != parsed.audio || again.mediaVolume != parsed.mediaVolume || !(again.voice == parsed.voice) || !(again.hud == parsed.hud))
        return Fail(24, "round trip");
    if (ToJson(DefaultSettings()).contains("bindings") || ToJson(DefaultSettings()).contains("mouse") || ToJson(DefaultSettings()).contains("audio"))
        return Fail(25, "profile values stay out of the file until changed");

    // Resets.
    auto reset = parsed;
    ResetSection(reset, Section::Controls);
    if (reset.bindings[0] != DefaultNativeBinding(0) || reset.modKeys[static_cast<size_t>(ModKey::PushToTalk)] != "v" || !reset.scriptKeys.empty())
        return Fail(26, "controls reset");
    ResetSection(reset, Section::Voice);
    if (reset.voice.volume != 2.0f || reset.voice.releaseMs != 100)
        return Fail(27, "voice reset keeps the volume");
    ResetSection(reset, Section::Audio);
    if (reset.voice.volume != 1.0f || reset.audio != NativeAudio {} || reset.mediaVolume != 1.0f)
        return Fail(28, "audio reset");
    if (SectionFromId("controls") != Section::Controls || SectionFromId("nope"))
        return Fail(29, "section ids");

    // Conflicts: on foot and car keys never clash with each other, the mod's
    // keys clash with both, and unbound slots with nothing.
    const auto conflicts = FindConflicts({{"native:forward", "w", Scope::OnFoot},
                                          {"native:accelerate", "w", Scope::Car},
                                          {"native:jump", "w", Scope::OnFoot},
                                          {"mod:chat", "k", Scope::Everywhere},
                                          {"native:horn", "k", Scope::Car},
                                          {"native:a", "", Scope::OnFoot},
                                          {"native:b", "", Scope::OnFoot}});
    if (conflicts.at("native:forward") != std::vector<std::string> {"native:jump"} || conflicts.contains("native:accelerate") ||
        conflicts.at("mod:chat") != std::vector<std::string> {"native:horn"} || conflicts.contains("native:a"))
        return Fail(30, "conflicts");

    if (!ValidResourceName("freeride") || ValidResourceName("") || ValidResourceName("a/b") || ScriptKeyId("freeride", "f9") != "freeride/f9")
        return Fail(31, "resource names");

    std::printf("settings_tests: ok\n");
    return 0;
}
