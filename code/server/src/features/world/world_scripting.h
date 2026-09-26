#pragma once

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace Mafia1Online::Shared::Entities {
    class SoundEntity;
}

namespace Mafia1Online::Scripting {
    // The global `World`: mission control, the player, vehicle and pickup
    // collections, replicated weather, night mode, frame visibility and city music, and
    // one-shot explosions and fires everyone sees. Per-player presentation
    // (HUD, fade, camera) is client scripting.
    class World final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };

    // A replicated positional sound every client in range hears the same way;
    // Sound.create places a looping one, Sound.play a one-shot. The handle
    // resolves its replica on every call, so a destroyed sound is inert.
    class Sound final: public Framework::Scripting::Builtins::Entity {
      public:
        Sound(uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        Shared::Entities::SoundEntity *ResolveSound() const;

        std::string GetWave() const;
        double GetVolume() const;
        void SetVolume(double volume);
        double GetRadius() const;
        void SetRadius(double radius);
        bool GetEnabled() const;
        void SetEnabled(bool enabled);
        bool Destroy();
        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<Sound> &GetClass(v8::Isolate *isolate);

      private:
        static std::unique_ptr<v8pp::class_<Sound>> _class;
    };
} // namespace Mafia1Online::Scripting
