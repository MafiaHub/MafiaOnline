#pragma once

#include <v8.h>

#include <string>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Mafia1Online::Scripting {
    // Local scene frames belong to one loaded mission and never replicate.
    void RegisterVisualScripting(v8::Isolate *isolate, v8::Local<v8::Object> global);
    void RegisterVisualResourceCleanup(Framework::Scripting::ResourceManager &manager);
    void BeginDrawFrame();
    void RenderDrawCommands();
    // Runs inside the world scene's SCM_RENDER_DONE callback, so each preview
    // scene renders as a callback render without touching the sound listener.
    void RenderPreviewCommands();
    void ResetLocalVisuals();
    // True only while a collision-enabled, script-owned human remains active.
    bool IsSolidLocalHumanActor(const void *actor);
    SDK::Scene::NativeFrame *OnLocalHumanDestroyed(void *actor);
    void JS_FindWorldFrame(const v8::FunctionCallbackInfo<v8::Value> &info);
    // The resource whose script is calling; empty outside a resource.
    std::string ScriptOwner(v8::Isolate *isolate);
    // UTF-8 to the game's ANSI code page, control characters blanked.
    std::string ToNativeText(const std::string &utf8);
} // namespace Mafia1Online::Scripting
