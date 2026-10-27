#include <utils/safe_win32.h>

#include "gangster_spawn.h"

#include "npc_host.h"

#include <MinHook.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/police/native_police_manager.h>
#include <mafia1/sdk/traffic/native_gangster.h>
#include <mafia1/sdk/traffic/native_traffic.h>

#include <string>

namespace Mafia1Online::Features::Npc {
    namespace {
        using SDK::Player::NativeActor;
        using SDK::Player::Vector3;
        using CreateMafianos = void(__thiscall *)(SDK::Traffic::NativeTrafficElement *, int32_t, const Vector3 *, uint32_t, NativeActor *, float);

        CreateMafianos gCreateMafianosOriginal = nullptr;
        NpcHost *gHost                         = nullptr;

        void __fastcall CreateMafianosHook(SDK::Traffic::NativeTrafficElement *element, void *, int32_t hitType, const Vector3 *position, uint32_t bodyType, NativeActor *attacker,
                                           float damage) {
            auto *police = SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game());
            const size_t before = police->RetirementCount();
            // The pool opened "Models\<name>"; replicas load the bare name.
            std::string model = element->ModelFileName();
            if (const auto slash = model.find_last_of("\\/"); slash != std::string::npos) {
                model.erase(0, slash + 1);
            }
            gCreateMafianosOriginal(element, hitType, position, bodyType, attacker, damage);
            // A successful Init ends with AddDoRiciActor of the new entity.
            if (police->RetirementCount() == before + 1) {
                gHost->Track(police->RetirementEnd()[-1], Shared::Entities::NpcKind::Gangster, model);
            }
        }
    } // namespace

    bool InstallGangsterSpawn(NpcHost &host) {
        gHost = &host;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Traffic::kCreateMafianos), reinterpret_cast<void *>(&CreateMafianosHook), reinterpret_cast<void **>(&gCreateMafianosOriginal)) != MH_OK) {
            return false;
        }
        if (MH_EnableHook(reinterpret_cast<void *>(SDK::Traffic::kCreateMafianos)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Traffic::kCreateMafianos));
            return false;
        }
        return true;
    }

    void UninstallGangsterSpawn() {
        MH_DisableHook(reinterpret_cast<void *>(SDK::Traffic::kCreateMafianos));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Traffic::kCreateMafianos));
        gHost = nullptr;
    }
} // namespace Mafia1Online::Features::Npc
