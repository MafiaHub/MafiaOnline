#pragma once

#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <string>

namespace Mafia1Online::SDK::Player {
    // Creates the same model/frame/actor ownership chain as retail freeride,
    // but registers the actor with the live game's temporary-actor list.
    // The caller invalidates its borrowed pointer before native removal.
    class NativeHumanFactory final {
      public:
        static bool CanLoadHumanModel(const std::string &modelName) {
            auto *model = Scene::GetDriver()->CreateModel();
            if (!model) {
                return false;
            }
            const bool opened = Scene::GetModelCache()->OpenModel(model, modelName.c_str());
            // C_ShotSkeleton::TestCollision dereferences these frames without
            // null checks. Reject any openable model that is not a full human.
            constexpr const char *kRequiredFrames[] {
                "r_hand", "r_elbow", "r_arm", "r_foot", "r_shin", "r_thigh",
                "l_hand", "l_elbow", "l_arm", "l_foot", "l_shin", "l_thigh",
                "base", "back1", "back2", "back3", "neck", "notify"
            };
            bool human = opened;
            for (const char *frameName : kRequiredFrames) {
                if (human && !model->FindChildFrame(frameName)) {
                    human = false;
                }
            }
            model->Release();
            return human;
        }

        static NativeActor *CreateTemporary(Core::Mission::NativeMission &mission, NativeActor::Type type, const char *frameName, const char *modelName, const Vector3 &position, const Vector3 &direction) {
            auto *scene = mission.GetScene();
            auto *frame = Scene::GetDriver()->CreateModel();
            if (!frame) {
                return nullptr;
            }
            if (!frame->SetName(frameName) || !Scene::GetModelCache()->OpenModel(frame, modelName) || !frame->LinkTo(scene->PrimarySector())) {
                frame->Release();
                return nullptr;
            }

            frame->SetWorldPosition(position);
            frame->SetDirection(direction);
            frame->Update();
            scene->AddFrame(frame);

            auto *actor = mission.CreateActor(type);
            if (!actor || !actor->Initialize(frame)) {
                if (actor) {
                    actor->Release();
                }
                scene->DeleteFrame(frame);
                frame->Release();
                return nullptr;
            }

            auto *game = mission.Game();
            if (type == NativeActor::Type::Player) {
                game->SetPlayerForInitialization(actor);
            }
            game->AddTemporaryActor(actor);
            if (type == NativeActor::Type::Player) {
                game->FocusPlayer(actor);
            }
            else {
                actor->SetNetworkControlled(true);
            }
            frame->Release();
            return actor;
        }
    };
} // namespace Mafia1Online::SDK::Player
