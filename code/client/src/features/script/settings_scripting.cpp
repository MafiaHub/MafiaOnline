#include "settings_scripting.h"

#include "script_runtime.h"
#include "visual_scripting.h"

#include "core/application.h"
#include "shared/scripting_catalog.h"

#include <core_modules.h>
#include <scripting/resource/resource_manager.h>
#include <utils/key_names.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <cctype>

namespace Mafia1Online::Scripting {
    namespace {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;

        Features::Settings::SettingsService &Service() {
            return static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance())->Settings();
        }

        bool ReadString(v8::Isolate *isolate, v8::Local<v8::Value> value, std::string &out) {
            if (!value->IsString()) {
                return false;
            }
            out = v8pp::from_v8<std::string>(isolate, value);
            return true;
        }

        // Key.bind accepts these names case-insensitively.
        std::string KeyName(std::string name) {
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return Framework::Utils::KeyNames::FromVirtualKey(Framework::Utils::KeyNames::ToVirtualKey(name));
        }

        void JS_DescribeKey(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            std::string key;
            std::string label;
            if (info.Length() != 2 || !ReadString(isolate, info[0], key) || !ReadString(isolate, info[1], label)) {
                Args::Throw(isolate, "Settings.describeKey(key, label) expects a Key.bind key name and a label");
                return;
            }
            const auto owner = ScriptOwner(isolate);
            const auto name  = KeyName(key);
            info.GetReturnValue().Set(!owner.empty() && !name.empty() && Service().DescribeScriptKey(owner, name, label));
        }

        void JS_GetKeyLabel(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            std::string key;
            if (info.Length() != 1 || !ReadString(isolate, info[0], key) || KeyName(key).empty()) {
                Args::Throw(isolate, "Settings.getKeyLabel(key) expects a Key.bind key name");
                return;
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, Service().ScriptKeyLabel(ScriptOwner(isolate), KeyName(key))));
        }

        void JS_DefineToggle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            std::string id;
            std::string label;
            if (info.Length() != 3 || !ReadString(isolate, info[0], id) || !ReadString(isolate, info[1], label) || !info[2]->IsBoolean()) {
                Args::Throw(isolate, "Settings.defineToggle(id, label, value) expects an id, a label and a boolean");
                return;
            }
            const auto owner = ScriptOwner(isolate);
            info.GetReturnValue().Set(!owner.empty() && Service().DefineToggle(owner, id, label, info[2]->BooleanValue(isolate)));
        }

        void JS_SetToggle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            std::string id;
            if (info.Length() != 2 || !ReadString(isolate, info[0], id) || !info[1]->IsBoolean()) {
                Args::Throw(isolate, "Settings.setToggle(id, value) expects an id and a boolean");
                return;
            }
            const auto owner = ScriptOwner(isolate);
            info.GetReturnValue().Set(!owner.empty() && Service().SetToggle(owner, id, info[1]->BooleanValue(isolate)));
        }

        void JS_GetMediaVolume(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<double>(Service().MediaVolume()));
        }

        void JS_Open(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const bool ready = GetWorld().IsReady();
            if (ready) {
                Service().RequestOpen();
            }
            info.GetReturnValue().Set(ready);
        }

        void CleanupResource(const std::string &owner) {
            Service().ClearResource(owner);
        }
    } // namespace

    void SettingsApi::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::module settings(isolate, ClientCatalog(), "Settings",
            "The player's settings screen (Esc, Settings). A resource lists the keys it binds and the switches it offers there; the player can move a "
            "key, and its Key.bind handlers and Key.isDown then follow the key the player chose. Entries are removed when the resource stops.");
        settings.function("describeKey", &JS_DescribeKey,
            docs("boolean", {param("key", "string", false, "The Key.bind name this resource binds, such as \"f9\"; not the wheel."), param("label", "string", false, "What the key does, at most 64 characters.")},
                 "Lists a key on the Controls page under this resource, so the player can move it. Describing it again updates the label.",
                 "False for an unknown key, the wheel or Escape, an empty label, outside a resource, or past 64 keys per resource."));
        settings.function("getKeyLabel", &JS_GetKeyLabel,
            docs("string", {param("key", "string", false, "The Key.bind name this resource binds.")},
                 "The label of the key the player now presses for this key, for prompts such as \"Press F9\". Ask again when showing the prompt.",
                 "The layout's label, such as \"F8\" or \"Mouse 4\"; \"Unbound\" when the player removed it."));
        settings.function("defineToggle", &JS_DefineToggle,
            docs("boolean",
                 {param("id", "string", false, "This resource's switch id: 1 to 32 letters, digits, '-', '_' or '.'."), param("label", "string", false, "What it switches, at most 64 characters."),
                  param("value", "boolean", false, "Its current state.")},
                 "Shows an on/off switch on the settings screen's Gameplay page. Flipping it raises `settingToggle`; the resource stores the choice itself.",
                 "False for an invalid id or label, outside a resource, or past 32 switches per resource."));
        settings.function("setToggle", &JS_SetToggle,
            docs("boolean", {param("id", "string", false, "A switch this resource defined."), param("value", "boolean", false, "Its new state.")},
                 "Updates a switch the resource changed another way, such as by a key or a command. Raises no event.", "False when this resource has no such switch."));
        settings.function("getMediaVolume", &JS_GetMediaVolume,
            docs("number", {}, "The player's volume for resource web audio, such as a car radio, from 0 to 1. Multiply page audio by it; `mediaVolumeChange` reports changes.", "0 to 1; 1 by default."));
        settings.function("open", &JS_Open, docs("boolean", {}, "Opens the settings screen over the game, as Esc then Settings does.", "False while no mission is loaded; the request is then dropped."));
        settings.publish(global);
    }

    void SettingsApi::RegisterResourceCleanup(Framework::Scripting::ResourceManager &manager) {
        manager.AddOnResourceStopped(&CleanupResource);
    }
} // namespace Mafia1Online::Scripting
