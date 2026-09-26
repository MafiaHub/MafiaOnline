#include "hud_scripting.h"

#include "player_scripting.h"
#include "script_runtime.h"

#include "core/application.h"
#include "shared/features/sound/sound_entity.h"
#include "shared/scripting_catalog.h"

#include <core_modules.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Mafia1Online::Scripting {
    namespace {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;

        constexpr uint32_t kWhite           = 0xffffff;
        constexpr float kMaxSeconds         = 3600.0f;
        constexpr uint32_t kMaxCountdown    = 99 * 3600;
        constexpr float kMaxSwing           = 100.0f;
        constexpr float kDefaultAnnounce    = 3.0f;
        constexpr float kDefaultSoundRadius = 25.0f;

        Core::Application &App() {
            return *static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
        }

        Features::Hud::HudService &Hud() {
            return App().Hud();
        }

        bool ReadText(v8::Isolate *isolate, v8::Local<v8::Value> value, std::string &out) {
            if (!value->IsString()) {
                return false;
            }
            out = v8pp::from_v8<std::string>(isolate, value);
            return true;
        }

        bool ReadColor(const v8::FunctionCallbackInfo<v8::Value> &info, int index, uint32_t &out) {
            out = kWhite;
            if (index >= info.Length() || info[index]->IsUndefined()) {
                return true;
            }
            if (!info[index]->IsUint32() || info[index].As<v8::Uint32>()->Value() > 0xffffff) {
                return false;
            }
            out = info[index].As<v8::Uint32>()->Value();
            return true;
        }

        bool ReadSeconds(const v8::FunctionCallbackInfo<v8::Value> &info, int index, float fallback, float &out) {
            out = fallback;
            if (index >= info.Length() || info[index]->IsUndefined()) {
                return true;
            }
            if (!Args::ReadFloat(info[index], out)) {
                return false;
            }
            out = std::clamp(out, 0.0f, kMaxSeconds);
            return true;
        }

        bool ReadUInt(v8::Local<v8::Value> value, uint32_t maximum, uint32_t &out) {
            if (!value->IsNumber()) {
                return false;
            }
            const double read = value.As<v8::Number>()->Value();
            if (!std::isfinite(read) || read < 0.0 || read > maximum || std::trunc(read) != read) {
                return false;
            }
            out = static_cast<uint32_t>(read);
            return true;
        }

        void JS_ShowMessage(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            std::string text;
            uint32_t color = kWhite;
            if (info.Length() < 1 || info.Length() > 2 || !ReadText(isolate, info[0], text) || !ReadColor(info, 1, color)) {
                Args::Throw(isolate, "Hud.showMessage(text, color?) expects a string and a 0xRRGGBB color");
                return;
            }
            if (!Hud().Ready()) {
                info.GetReturnValue().Set(false);
                return;
            }
            Hud().ShowMessage(text, color);
            info.GetReturnValue().Set(true);
        }

        void JS_Announce(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            std::string text;
            float seconds = kDefaultAnnounce;
            if (info.Length() < 1 || info.Length() > 2 || !ReadText(isolate, info[0], text) || !ReadSeconds(info, 1, kDefaultAnnounce, seconds)) {
                Args::Throw(isolate, "Hud.announce(text, durationSeconds?) expects a string and a duration");
                return;
            }
            if (!Hud().Ready()) {
                info.GetReturnValue().Set(false);
                return;
            }
            Hud().Announce(text, seconds);
            info.GetReturnValue().Set(true);
        }

        void JS_ShowWatch(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t hours = 0, minutes = 0, seconds = 0;
            if (info.Length() < 2 || info.Length() > 3 || !ReadUInt(info[0], 23, hours) || !ReadUInt(info[1], 59, minutes)
                || (info.Length() == 3 && !info[2]->IsUndefined() && !ReadUInt(info[2], 59, seconds))) {
                Args::Throw(info.GetIsolate(), "Hud.showWatch(hours, minutes, seconds?) expects a clock time");
                return;
            }
            if (!Hud().Ready()) {
                info.GetReturnValue().Set(false);
                return;
            }
            Hud().ShowWatch(hours, minutes, seconds);
            info.GetReturnValue().Set(true);
        }

        void JS_StartCountdown(const v8::FunctionCallbackInfo<v8::Value> &info) {
            uint32_t seconds = 0;
            if (info.Length() != 1 || !ReadUInt(info[0], kMaxCountdown, seconds)) {
                Args::Throw(info.GetIsolate(), "Hud.startCountdown(seconds) expects whole seconds up to 356400");
                return;
            }
            if (!Hud().Ready()) {
                info.GetReturnValue().Set(false);
                return;
            }
            Hud().StartCountdown(seconds);
            info.GetReturnValue().Set(true);
        }

        void JS_GetCountdown(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto remaining = Hud().Ready() ? Hud().Countdown() : std::nullopt;
            info.GetReturnValue().Set(remaining ? v8::Number::New(info.GetIsolate(), *remaining).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_HideWatch(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (Hud().Ready()) {
                Hud().HideWatch();
            }
            (void)info;
        }

        bool ReadScore(v8::Local<v8::Value> value, int32_t &out) {
            if (!value->IsNumber()) {
                return false;
            }
            const double read = value.As<v8::Number>()->Value();
            if (!std::isfinite(read) || std::fabs(read) > 1e9) {
                return false;
            }
            out = static_cast<int32_t>(std::lround(read));
            return true;
        }

        void JS_SetScore(const v8::FunctionCallbackInfo<v8::Value> &info) {
            int32_t score = 0;
            if (info.Length() != 1 || !ReadScore(info[0], score)) {
                Args::Throw(info.GetIsolate(), "Hud.setScore(score) expects a number up to one billion");
                return;
            }
            if (!Hud().Ready()) {
                info.GetReturnValue().Set(false);
                return;
            }
            Hud().SetScore(score);
            info.GetReturnValue().Set(true);
        }

        void JS_AddScore(const v8::FunctionCallbackInfo<v8::Value> &info) {
            int32_t delta = 0;
            if (info.Length() != 1 || !ReadScore(info[0], delta)) {
                Args::Throw(info.GetIsolate(), "Hud.addScore(points) expects a number up to one billion");
                return;
            }
            if (!Hud().Ready()) {
                info.GetReturnValue().SetNull();
                return;
            }
            const int64_t next = std::clamp<int64_t>(static_cast<int64_t>(Hud().Score().value_or(0)) + delta, -1000000000, 1000000000);
            Hud().SetScore(static_cast<int32_t>(next));
            info.GetReturnValue().Set(static_cast<int32_t>(next));
        }

        void JS_GetScore(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto score = Hud().Score();
            info.GetReturnValue().Set(score ? v8::Integer::New(info.GetIsolate(), *score).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_IsScoreVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto visible = Hud().ScoreVisible();
            info.GetReturnValue().Set(visible ? v8::Boolean::New(info.GetIsolate(), *visible).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_SetScoreVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsBoolean()) {
                Args::Throw(info.GetIsolate(), "Hud.setScoreVisible(visible) expects a boolean");
                return;
            }
            if (!Hud().Ready()) {
                info.GetReturnValue().Set(false);
                return;
            }
            Hud().SetScoreVisible(info[0].As<v8::Boolean>()->Value());
            info.GetReturnValue().Set(true);
        }

        void JS_HideScore(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (Hud().Ready()) {
                Hud().HideScore();
            }
            (void)info;
        }

        void JS_SetCompassTarget(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            if (info.Length() != 1) {
                Args::Throw(isolate, "Hud.setCompassTarget(target) expects a position, a Player or a Vehicle");
                return;
            }
            uint64_t entity = 0;
            glm::vec3 position {0.0f};
            if (auto *player = v8pp::class_<Player>::unwrap_object(isolate, info[0])) {
                entity = player->GetId();
            }
            else if (auto *vehicle = v8pp::class_<Vehicle>::unwrap_object(isolate, info[0])) {
                entity = vehicle->GetId();
            }
            if (entity != 0) {
                auto *resolved = Framework::CoreModules::GetReplication() ? Framework::CoreModules::GetReplication()->GetEntityByNetworkID(entity) : nullptr;
                if (!resolved) {
                    info.GetReturnValue().Set(false);
                    return;
                }
                position = resolved->position;
            }
            else if (!Args::ReadPosition(isolate, info[0], position)) {
                Args::Throw(isolate, "Hud.setCompassTarget(target) expects a position, a Player or a Vehicle");
                return;
            }
            info.GetReturnValue().Set(Hud().Ready() && Hud().SetCompassTarget(&position, entity));
        }

        void JS_ClearCompassTarget(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Hud().ClearCompassTarget();
            (void)info;
        }

        void Fade(const v8::FunctionCallbackInfo<v8::Value> &info, bool toColor, const char *usage) {
            float seconds  = 0.0f;
            uint32_t color = 0;
            if (info.Length() < 1 || info.Length() > 2 || !ReadSeconds(info, 0, 0.0f, seconds) || !ReadColor(info, 1, color)) {
                Args::Throw(info.GetIsolate(), usage);
                return;
            }
            if (info.Length() < 2 || info[1]->IsUndefined()) {
                color = 0;
            }
            if (!Hud().Ready()) {
                info.GetReturnValue().Set(false);
                return;
            }
            Hud().Fade(toColor, static_cast<uint32_t>(std::lround(seconds * 1000.0f)), color);
            info.GetReturnValue().Set(true);
        }

        void JS_FadeOut(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Fade(info, true, "Fade.out(durationSeconds, color?) expects a duration and a 0xRRGGBB color");
        }

        void JS_FadeIn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Fade(info, false, "Fade.in(durationSeconds, color?) expects a duration and a 0xRRGGBB color");
        }

        void JS_SetSwing(const v8::FunctionCallbackInfo<v8::Value> &info) {
            float intensity = 0.0f;
            if (info.Length() != 1 || !Args::ReadFloat(info[0], intensity)) {
                Args::Throw(info.GetIsolate(), "Camera.setSwing(intensity) expects a number from 0 to 100");
                return;
            }
            if (!Hud().Ready()) {
                info.GetReturnValue().Set(false);
                return;
            }
            Hud().SetCameraSwing(std::clamp(intensity, 0.0f, kMaxSwing));
            info.GetReturnValue().Set(true);
        }

        void JS_GetCameraFov(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto degrees = Hud().CameraFov();
            info.GetReturnValue().Set(degrees ? v8::Number::New(info.GetIsolate(), *degrees).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_SetCameraFov(const v8::FunctionCallbackInfo<v8::Value> &info) {
            float degrees = 0.0f;
            if (info.Length() != 1 || !Args::ReadFloat(info[0], degrees) || degrees < 1.0f || degrees > 179.0f) {
                Args::Throw(info.GetIsolate(), "Camera.setFov(degrees) expects a finite number from 1 to 179");
                return;
            }
            info.GetReturnValue().Set(Hud().SetCameraFov(degrees));
        }

        void JS_SetCameraRange(const v8::FunctionCallbackInfo<v8::Value> &info) {
            float nearClip = 0.0f, farClip = 0.0f;
            if (info.Length() != 2 || !Args::ReadFloat(info[0], nearClip) || !Args::ReadFloat(info[1], farClip)
                || nearClip < 0.01f || nearClip > 10.0f || farClip <= nearClip + 0.01f || farClip > 5000.0f) {
                Args::Throw(info.GetIsolate(), "Camera.setRange(nearClip, farClip) expects near 0.01-10 and far above near up to 5000");
                return;
            }
            info.GetReturnValue().Set(Hud().SetCameraRange(nearClip, farClip));
        }

        void JS_LockCamera(const v8::FunctionCallbackInfo<v8::Value> &info) {
            glm::vec3 position {}, direction {};
            float roll = 0.0f;
            if (info.Length() < 2 || info.Length() > 3 || !Args::ReadPosition(info.GetIsolate(), info[0], position) || !Args::ReadPosition(info.GetIsolate(), info[1], direction) || (info.Length() == 3 && !info[2]->IsUndefined() && !Args::ReadFloat(info[2], roll))) {
                Args::Throw(info.GetIsolate(), "Camera.lock(position, direction, roll?) expects two finite Vector3 values and an optional finite roll in radians");
                return;
            }

            info.GetReturnValue().Set(Hud().LockCamera(position, direction, roll));
        }

        void JS_UnlockCamera(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 0) {
                Args::Throw(info.GetIsolate(), "Camera.unlock() takes no arguments");
                return;
            }
            info.GetReturnValue().Set(Hud().UnlockCamera());
        }

        struct SoundOptions {
            float radius = kDefaultSoundRadius;
            float volume = 1.0f;
            bool loop    = false;
        };

        bool ReadSoundOptions(v8::Isolate *isolate, v8::Local<v8::Value> options, SoundOptions &out) {
            if (!Args::OptionalFloat(isolate, options, "radius", 1.0f, Shared::Entities::SoundEntity::kMaxRadius, out.radius)
                || !Args::OptionalFloat(isolate, options, "volume", 0.0f, Shared::Entities::SoundEntity::kMaxVolume, out.volume)) {
                return false;
            }
            if (!options.IsEmpty() && options->IsObject()) {
                v8::Local<v8::Value> loop;
                if (options.As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "loop")).ToLocal(&loop) && !loop->IsUndefined()) {
                    if (!loop->IsBoolean()) {
                        return false;
                    }
                    out.loop = loop.As<v8::Boolean>()->Value();
                }
            }
            return true;
        }

        void ReturnSoundId(const v8::FunctionCallbackInfo<v8::Value> &info, int32_t id) {
            info.GetReturnValue().Set(id >= 0 ? v8::Integer::New(info.GetIsolate(), id).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_SoundPlay(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            std::string wave;
            SoundOptions options;
            if (info.Length() < 1 || info.Length() > 2 || !ReadText(isolate, info[0], wave) || !Shared::Entities::SoundEntity::ValidWave(wave)
                || !ReadSoundOptions(isolate, info.Length() == 2 ? info[1] : v8::Local<v8::Value>(), options)) {
                Args::Throw(isolate, "Sound.play(wave, { volume?, loop? }) expects a game sound file and valid options");
                return;
            }
            ReturnSoundId(info, App().Sounds().PlayLocal(wave, nullptr, options.radius, options.volume, options.loop));
        }

        void JS_SoundPlayAt(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            std::string wave;
            glm::vec3 position;
            SoundOptions options;
            if (info.Length() < 2 || info.Length() > 3 || !ReadText(isolate, info[0], wave) || !Shared::Entities::SoundEntity::ValidWave(wave)
                || !Args::ReadPosition(isolate, info[1], position) || !ReadSoundOptions(isolate, info.Length() == 3 ? info[2] : v8::Local<v8::Value>(), options)) {
                Args::Throw(isolate, "Sound.playAt(wave, position, { radius?, volume?, loop? }) expects a game sound file, a position and valid options");
                return;
            }
            ReturnSoundId(info, App().Sounds().PlayLocal(wave, &position, options.radius, options.volume, options.loop));
        }

        void JS_SoundStop(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 1 || !info[0]->IsInt32()) {
                Args::Throw(info.GetIsolate(), "Sound.stop(id) expects an id from Sound.play or Sound.playAt");
                return;
            }
            info.GetReturnValue().Set(App().Sounds().StopLocal(info[0].As<v8::Int32>()->Value()));
        }
    } // namespace

    void ClientHud::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        const auto position = "Vector3 | { x: number; y: number; z: number }";

        v8pp::module hud(isolate, ClientCatalog(), "Hud",
            "The retail Mafia 1 HUD for this client. Every call returns false while no mission is loaded, and the game clears the watch, score and compass on every mission change.");
        hud.function("showMessage", &JS_ShowMessage,
            docs("boolean", {param("text", "string", false, "Message line; at most 127 characters are shown."), param("color", "number", true, "0xRRGGBB color; defaults to white.")},
                "Adds a line to the native HUD console, which keeps the last five lines for five seconds each.", "False while no mission is loaded."));
        hud.function("announce", &JS_Announce,
            docs("boolean", {param("text", "string", false, "Announcement; at most 127 characters are shown."), param("durationSeconds", "number", true, "How long it stays; defaults to 3.")},
                "Shows large white centred text, the race flash text, fading out over its last second.", "False while no mission is loaded."));
        hud.function("showWatch", &JS_ShowWatch,
            docs("boolean", {param("hours", "number", false, "Clock hours from 0 to 23."), param("minutes", "number", false, "Clock minutes from 0 to 59."), param("seconds", "number", true, "Clock seconds from 0 to 59.")},
                "Shows the mission watch running in real time from this clock time.", "False while no mission is loaded."));
        hud.function("startCountdown", &JS_StartCountdown,
            docs("boolean", {param("seconds", "number", false, "Countdown length in whole seconds; 0 removes the countdown.")},
                "Shows the watch with its countdown wedge. The countdownEnd event fires when it runs out.", "False while no mission is loaded."));
        hud.function("getCountdown", &JS_GetCountdown, docs("number | null", {}, "Returns the seconds left on the countdown.", "The remaining seconds, or null when no countdown runs."));
        hud.function("hideWatch", &JS_HideWatch, docs("void", {}, "Hides the watch and stops its countdown without firing countdownEnd."));
        hud.function("setScore", &JS_SetScore, docs("boolean", {param("score", "number", false, "Score to show.")}, "Shows the freeride score counter with this value.", "False while no mission is loaded."));
        hud.function("addScore", &JS_AddScore, docs("number | null", {param("points", "number", false, "Points to add; negative subtracts.")}, "Adds to the score counter and shows it.", "The new score, or null while no mission is loaded."));
        hud.function("getScore", &JS_GetScore, docs("number | null", {}, "Reads the native Free Ride score value, even while its counter is hidden. Retail scripts may change it locally.", "The score, or null while no mission is loaded."));
        hud.function("isScoreVisible", &JS_IsScoreVisible, docs("boolean | null", {}, "Reads whether the native Free Ride score counter is shown.", "True or false, or null while no mission is loaded."));
        hud.function("setScoreVisible", &JS_SetScoreVisible,
            docs("boolean", {param("visible", "boolean", false, "Whether to show the existing score value.")}, "Shows or hides the native Free Ride score counter without changing its value.", "False while no mission is loaded."));
        hud.function("hideScore", &JS_HideScore, docs("void", {}, "Hides the score counter without changing its value."));
        hud.function("setCompassTarget", &JS_SetCompassTarget,
            docs("boolean", {param("target", std::string(position) + " | Player | Vehicle", false, "A fixed point, or a streamed player or vehicle the arrow follows.")},
                "Points the native compass arrow at a target.", "False while no mission is loaded or the entity is not streamed."));
        hud.function("clearCompassTarget", &JS_ClearCompassTarget, docs("void", {}, "Hides the compass arrow."));
        hud.publish(global);

        v8pp::module fade(isolate, ClientCatalog(), "Fade", "The retail full-screen fade, drawn over the scene and the HUD.");
        fade.function("out", &JS_FadeOut,
            docs("boolean", {param("durationSeconds", "number", false, "Fade duration; 0 is instant."), param("color", "number", true, "0xRRGGBB color to fade to; defaults to black.")},
                "Fades the screen to a color and holds it until Fade.in.", "False while no mission is loaded."));
        fade.function("in", &JS_FadeIn,
            docs("boolean", {param("durationSeconds", "number", false, "Fade duration; 0 is instant."), param("color", "number", true, "0xRRGGBB color to fade from; defaults to black.")},
                "Fades from a color back to the scene.", "False while no mission is loaded."));
        fade.publish(global);

        v8pp::module camera(isolate, ClientCatalog(), "Camera", "The retail game camera.");
        camera.function("setSwing", &JS_SetSwing,
            docs("boolean", {param("intensity", "number", false, "Roll strength from 0 to 100, as the retail CAMERA_SETSWING script command takes it; 0 stops the swing.")},
                "Rolls the camera and the sky slowly from side to side, the retail boat swing. Mafia 1 has no camera shake.", "False while no mission is loaded."));
        camera.function("getFov", &JS_GetCameraFov,
            docs("number | null", {}, "Returns the active camera's effective field of view in degrees.", "Null while no active mission camera exists."));
        camera.function("setFov", &JS_SetCameraFov,
            docs("boolean", {param("degrees", "number", false, "Field of view from 1 to 179 degrees.")},
                "Sets the active camera's field of view through the retail widescreen-aware CAMERA_SETFOV path.", "False while no active mission camera exists. Camera modes may subsequently update the value."));
        camera.function("setRange", &JS_SetCameraRange,
            docs("boolean", {param("nearClip", "number", false, "Near clip plane in world units, 0.01 to 10."), param("farClip", "number", false, "Far clip plane above nearClip and at most 5000 world units.")},
                "Sets the active camera's clip planes with the retail CAMERA_SETRANGE setter.", "False while no active mission camera exists. Camera modes may subsequently update the values."));
        camera.function("lock", &JS_LockCamera,
            docs("boolean",
                {param("position", position, false, "Fixed camera position in world coordinates."), param("direction", position, false, "Nonzero forward direction; normalized before use."),
                    param("roll", "number", true, "Bank around the forward direction in radians; defaults to zero.")},
                "Locks the camera at a world position and direction, as CAMERA_LOCK does with a frame. Call Camera.unlock to resume following the player.",
                "False while no active mission camera exists or direction is zero. A new local life or mission close restores the player camera."));
        camera.function("unlock", &JS_UnlockCamera,
            docs("boolean", {}, "Returns from a camera lock to the previous player camera mode and refreshes the light cache.", "False while no mission is loaded."));
        camera.publish(global);

        v8pp::module sound(isolate, ClientCatalog(), "Sound",
            "Sounds only this client hears. The game releases a sound once it has played; a looping one plays until Sound.stop or the mission closes. Use the server Sound class for sounds everyone hears.");
        sound.function("play", &JS_SoundPlay,
            docs("number | null", {param("wave", "string", false, "Game sound file under the Sounds directory or its archives, such as \"00_dog.wav\"."),
                                      param("options", "{ volume?: number; loop?: boolean }", true, "Volume 0 to 1 (default 1) and whether it loops (at most 32 loops).")},
                "Plays a non-positional sound.", "The sound id, or null when the file is missing or no mission is loaded."));
        sound.function("playAt", &JS_SoundPlayAt,
            docs("number | null", {param("wave", "string", false, "Game sound file."), param("position", position, false, "Where the sound plays."),
                                      param("options", "{ radius?: number; volume?: number; loop?: boolean }", true, "Radius 1 to 200 (default 25), volume 0 to 1 and looping.")},
                "Plays a positional sound.", "The sound id, or null when the file is missing or no mission is loaded."));
        sound.function("stop", &JS_SoundStop, docs("boolean", {param("id", "number", false, "Id from Sound.play or Sound.playAt.")}, "Stops a sound.", "False while no mission is loaded."));
        sound.publish(global);
    }
} // namespace Mafia1Online::Scripting
