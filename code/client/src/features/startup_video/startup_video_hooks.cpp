#include "startup_video_hooks.h"

#include <MinHook.h>
#include <mafia1/sdk/video/native_video.h>

#include <cstring>

namespace Mafia1Online::Features::StartupVideo {
    namespace {
        using PlayVideo              = bool(__fastcall *)(const char *, float, float);
        PlayVideo gOriginalPlayVideo = nullptr;

        bool __fastcall PlayVideoHook(const char *name, void *, float width, float height) {
            if (std::strcmp(name, "logo1") == 0 || std::strcmp(name, "logo2") == 0 || std::strcmp(name, "logo3") == 0 || std::strcmp(name, "intro") == 0) {
                return true;
            }
            return gOriginalPlayVideo(name, width, height);
        }
    } // namespace

    bool InstallHooks() {
        return MH_CreateHook(reinterpret_cast<void *>(SDK::Video::kPlayVideo), reinterpret_cast<void *>(&PlayVideoHook), reinterpret_cast<void **>(&gOriginalPlayVideo)) == MH_OK;
    }

    void UninstallHooks() {
        MH_DisableHook(reinterpret_cast<void *>(SDK::Video::kPlayVideo));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Video::kPlayVideo));
    }
} // namespace Mafia1Online::Features::StartupVideo
