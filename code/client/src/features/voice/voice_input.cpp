#include <utils/safe_win32.h>

#include "voice_input.h"

#include <core_modules.h>
#include <input/physical_keys.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/player/native_human.h>
#include <voice/client/voice_client.h>

#include <glm/geometric.hpp>

#include <cmath>

namespace Mafia1Online::Features::Voice {
    namespace {
        glm::vec3 Vec(const SDK::Player::Vector3 &v) {
            return {v.x, v.y, v.z};
        }

        bool Finite(const glm::vec3 &v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    } // namespace

    void VoiceInput::Update(bool blocked) {
        auto *voice = Framework::CoreModules::GetVoiceClient();
        if (!voice) {
            return;
        }
        auto *mission = SDK::Core::Mission::Get();
        auto *scene   = mission ? mission->GetScene() : nullptr;
        auto *camera  = scene ? scene->ActiveCamera() : nullptr;
        auto *player  = SDK::Player::CurrentPlayer();
        voice->SetTransmitBlocked(!player);
        if (camera) {
            // I3D frames are left-handed (x right, y up, z forward), so the
            // right axis is passed rather than derived.
            const auto basis = camera->GetWorldBasis();
            Framework::Voice::ListenerTransform listener;
            listener.position = Vec(camera->WorldPosition());
            listener.forward  = Vec(basis.forward);
            listener.up       = Vec(basis.up);
            listener.right    = Vec(basis.right);
            if (player) {
                listener.attenuationPosition = Vec(SDK::Player::WorldPosition(player));
            }
            if (Finite(listener.position) && Finite(listener.attenuationPosition) && glm::length(listener.forward) > 0.5f && glm::length(listener.up) > 0.5f &&
                glm::length(listener.right) > 0.5f) {
                listener.forward = glm::normalize(listener.forward);
                listener.up      = glm::normalize(listener.up);
                listener.right   = glm::normalize(listener.right);
                voice->SetListenerTransform(listener);
            }
        }
        auto *graph        = SDK::Graphics::GetGraph();
        const bool focused = graph && GetForegroundWindow() == static_cast<HWND>(graph->MainWindow());
        // The key names a US position, as the settings screen captured it;
        // GetAsyncKeyState wants that position's key in the active layout.
        const int key   = static_cast<int>(Framework::Input::PhysicalKeys::ToLayoutVirtualKey(static_cast<UINT>(voice->GetPushToTalkKey()), GetKeyboardLayout(0)));
        const bool down = key != 0 && (GetAsyncKeyState(key) & 0x8000) != 0;
        // Read while blocked too, so a key held through the block is latched.
        const bool gated = blocked || !focused;
        voice->SetPushToTalk(_pushToTalk.Update(key, down, gated));
        voice->SetPushToTalkBlocked(gated);
    }
} // namespace Mafia1Online::Features::Voice
