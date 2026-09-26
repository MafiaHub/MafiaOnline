#include <utils/safe_win32.h>

#include "player_hooks.h"

#include "core/application.h"
#include "features/script/visual_scripting.h"

#include <MinHook.h>
#include <core_modules.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/player/native_human.h>

#include <glm/vec3.hpp>

namespace Mafia1Online::Features::Player {
    namespace {
        using ActorDestructor                    = void(__thiscall *)(void *);
        ActorDestructor gActorDestructorOriginal = nullptr;

        void __fastcall ActorDestructorHook(void *actor, void *) {
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            application->Debris().OnNativeDestroyed(application->World(), actor);
            auto *frame       = application->Players().OnNativeDestroyed(application->World(), actor);
            if (auto *localHumanFrame = Scripting::OnLocalHumanDestroyed(actor)) {
                frame = localHumanFrame;
            }
            auto *scene       = frame ? SDK::Core::Mission::Get()->GetScene() : nullptr;
            application->World().NativeObjects().InvalidateNative(actor);
            gActorDestructorOriginal(actor);
            if (frame && !scene->DeleteFrame(frame)) {
                frame->Release();
            }
        }

        using DoClimb = bool(__thiscall *)(SDK::Player::NativeHuman *);
        DoClimb gDoClimbOriginal = nullptr;

        bool __fastcall DoClimbHook(SDK::Player::NativeHuman *human, void *) {
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            const bool local  = SDK::Player::CurrentPlayer() == &human->Actor() && !application->Players().ReplayingClimb();
            const auto start  = local ? SDK::Player::WorldPosition(&human->Actor()) : SDK::Player::Vector3 {};
            const bool result = gDoClimbOriginal(human);
            if (result && local) {
                // Do_Climb may turn the human to face the wall; send that.
                const auto direction = SDK::Player::WorldDirection(&human->Actor());
                application->Players().OnLocalClimb(application->World(), glm::vec3(start.x, start.y, start.z), glm::vec3(direction.x, direction.y, direction.z));
            }
            return result;
        }
    } // namespace

    bool InstallPlayerHooks() {
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kActorDestructor), reinterpret_cast<void *>(&ActorDestructorHook), reinterpret_cast<void **>(&gActorDestructorOriginal)) != MH_OK) {
            return false;
        }
        if (MH_EnableHook(reinterpret_cast<void *>(SDK::Player::kActorDestructor)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Player::kActorDestructor));
            return false;
        }
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanDoClimb), reinterpret_cast<void *>(&DoClimbHook), reinterpret_cast<void **>(&gDoClimbOriginal)) != MH_OK
            || MH_EnableHook(reinterpret_cast<void *>(SDK::Player::kHumanDoClimb)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Player::kHumanDoClimb));
            UninstallPlayerHooks();
            return false;
        }
        return true;
    }

    void UninstallPlayerHooks() {
        MH_DisableHook(reinterpret_cast<void *>(SDK::Player::kHumanDoClimb));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Player::kHumanDoClimb));
        MH_DisableHook(reinterpret_cast<void *>(SDK::Player::kActorDestructor));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Player::kActorDestructor));
    }
} // namespace Mafia1Online::Features::Player
