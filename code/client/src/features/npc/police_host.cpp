#include <utils/safe_win32.h>

#include "police_host.h"

#include "npc_host.h"

#include "core/application.h"
#include "features/world/world_service.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/police/native_police_action.h>
#include <mafia1/sdk/police/native_police_manager.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <mafia1/sdk/traffic/native_traffic.h>

#include <array>
#include <chrono>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::Features::Npc {
    namespace {
        using SDK::Player::NativeActor;
        using SDK::Player::Vector3;
        using SDK::Police::NativeAiAction;
        using SDK::Police::NativePoliceAction;

        using RecruitPolice    = void(__fastcall *)(NativeActor *, uint32_t, void *, bool, float, bool);
        using ActionTick       = int(__thiscall *)(NativeActor *, NativeAiAction *, uint32_t, uint32_t, uint32_t *);
        using BoardArrested    = void(__thiscall *)(void *);
        using LockControls     = bool(__thiscall *)(NativeActor *, bool);
        using CameraArrestView = void(__thiscall *)(void *, NativeActor *, NativeActor *);
        using ManagerTick       = void(__thiscall *)(SDK::Police::NativePoliceManager *, uint32_t);
        using BoardBusy         = char(__thiscall *)(void *, bool);

        RecruitPolice gRecruitPoliceOriginal         = nullptr;
        ActionTick gActionTickOriginal               = nullptr;
        BoardArrested gBoardArrestedOriginal         = nullptr;
        LockControls gLockControlsOriginal           = nullptr;
        CameraArrestView gCameraArrestViewOriginal   = nullptr;
        ManagerTick gPoliceTickOriginal              = nullptr;
        BoardBusy gBoardBusyOriginal                 = nullptr;
        NpcHost *gHost                               = nullptr;

        // The police action C_entity::actionTick is running, for the hooks
        // its arrest and fine reach.
        struct PoliceTick {
            NativeActor *officer = nullptr;
            NativePoliceAction *action = nullptr;
        };
        thread_local PoliceTick gPoliceTick;

        struct Candidate {
            Vector3 position;
            std::string model;
            uint32_t walkerKey = 0;
        };

        void __fastcall RecruitPoliceHook(NativeActor *offender, uint32_t mode, void *police, bool announce, float radius, bool directional) {
            // Each officer is created at its walker's position from that
            // walker's model; replicas load the bare model name.
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            std::vector<Candidate> candidates;
            for (auto **element = SDK::Traffic::ElementsBegin(); element && element != SDK::Traffic::ElementsEnd(); ++element) {
                const uint8_t type = (*element)->PedestrianType();
                if ((*element)->IsActive() && (type == SDK::Traffic::kPolicePedestrian || type == SDK::Traffic::kPoliceSecondaryPedestrian)) {
                    std::string model = (*element)->ModelFileName();
                    if (const auto slash = model.find_last_of("\\/"); slash != std::string::npos) {
                        model.erase(0, slash + 1);
                    }
                    candidates.push_back({(*element)->Position(), std::move(model), application->Pedestrians().WalkerKey(*element)});
                }
            }
            auto *manager       = SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game());
            const size_t before = manager->RetirementCount();
            gRecruitPoliceOriginal(offender, mode, police, announce, radius, directional);
            const size_t after = manager->RetirementCount();
            for (size_t i = before; i < after; ++i) {
                NativeActor *officer = manager->RetirementBegin()[i];
                const auto position  = SDK::Player::WorldPosition(officer);
                const Candidate *source = nullptr;
                float best = std::numeric_limits<float>::max();
                for (const auto &candidate : candidates) {
                    const float dx = candidate.position.x - position.x;
                    const float dy = candidate.position.y - position.y;
                    const float dz = candidate.position.z - position.z;
                    if (const float distance = dx * dx + dy * dy + dz * dz; distance < best) {
                        best = distance;
                        source = &candidate;
                    }
                }
                if (source) {
                    gHost->Track(officer, Shared::Entities::NpcKind::Police, source->model, source->walkerKey);
                }
            }
        }

        // Once a second per officer: the police action and what its states
        // wait on in a player's replica (work state +0x40c, car transition
        // +0x410, seated car +0x98, door car +0x9c).
        void LogPoliceAction(NativeActor *officer, const NativePoliceAction &police) {
            static std::unordered_map<const void *, std::chrono::steady_clock::time_point> logged;
            auto *target = police.Target();
            auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
            const uint64_t targetId = target ? application->World().NativeObjects().FindByNative(target).networkId : 0;
            const auto now = std::chrono::steady_clock::now();
            if (!targetId || now - logged[officer] < std::chrono::seconds(1)) {
                return;
            }
            logged[officer] = now;
            if (logged.size() > 64) {
                logged.clear();
            }
            const auto *seat = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(target));
            const auto *human = static_cast<const SDK::Player::NativeHuman *>(static_cast<const void *>(target));
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Police {} mode {} state {} on {}: work {} transition {} seated {} door {}", static_cast<const void *>(officer),
                                                                         police.Mode(), police.State(), targetId, human->Melee().canWork, seat->transitionState,
                                                                         static_cast<const void *>(seat->usedActorEnter), static_cast<const void *>(seat->usedActorLeave));
        }

        int __fastcall ActionTickHook(NativeActor *entity, void *, NativeAiAction *action, uint32_t frameTime, uint32_t time, uint32_t *flags) {
            const PoliceTick previous = gPoliceTick;
            gPoliceTick = {entity, action ? action->Police() : nullptr};
            if (gPoliceTick.action) {
                gHost->OnPoliceActionTick(entity, gPoliceTick.action->State());
            }
            const int result = gActionTickOriginal(entity, action, frameTime, time, flags);
            if (gPoliceTick.action) {
                LogPoliceAction(entity, *gPoliceTick.action);
            }
            gPoliceTick = previous;
            return result;
        }

        // The arrest's only call (0x517260), as its animations start: leave
        // the shift timer alone, so no game-failed screen follows.
        void __fastcall BoardArrestedHook(void *) {}

        // The grab raises the flag (0x515cf5) with the action already in
        // state 10. State 12 of the police action, both animations over,
        // clears it: an arrest is complete here, as the fine is at the
        // LockControls that follows. An officer killed or called off before
        // then arrested nobody.
        char __fastcall BoardBusyHook(void *board, void *, bool busy) {
            if (busy && gPoliceTick.action && gPoliceTick.action->State() == NativePoliceAction::kStateArrestWalk &&
                gPoliceTick.action->Target()->GetType() != NativeActor::Type::Player) {
                gHost->OnGrab(gPoliceTick.officer, gPoliceTick.action->Target());
            }
            if (!busy && gPoliceTick.action && gPoliceTick.action->Mode() == NativePoliceAction::kModeArrest &&
                gPoliceTick.action->State() == NativePoliceAction::kStateDone) {
                gHost->OnCaught(gPoliceTick.officer, gPoliceTick.action->Target(), Shared::Npc::Caught::Outcome::Arrest);
            }
            return gBoardBusyOriginal(board, busy);
        }

        // The fine ends with LockControls(false) on its target, a player's
        // replica (C_entity) on the host, which has no such field.
        bool __fastcall LockControlsHook(NativeActor *human, void *, bool locked) {
            if (human->GetType() == NativeActor::Type::Player) {
                return gLockControlsOriginal(human, locked);
            }
            if (!locked && gPoliceTick.action && gPoliceTick.action->Mode() == NativePoliceAction::kModeFine && gPoliceTick.action->Target() == human &&
                gPoliceTick.action->State() == NativePoliceAction::kStateDone) {
                gHost->OnCaught(gPoliceTick.officer, human, Shared::Npc::Caught::Outcome::Fine);
            }
            return false;
        }

        // State 11 calls it for every target just before both halves of the
        // animation start (0x5171c1). The host's camera is its free debug
        // view, which an arrest would lock; a player's replica gets its half
        // on the player's own client.
        void __fastcall CameraArrestViewHook(void *, void *, NativeActor *officer, NativeActor *target) {
            if (gPoliceTick.action && target->GetType() != NativeActor::Type::Player) {
                gHost->OnSeized(officer, target,
                                gPoliceTick.action->Mode() == NativePoliceAction::kModeFine ? Shared::Npc::Caught::Outcome::Fine : Shared::Npc::Caught::Outcome::Arrest);
            }
        }

        // Script orders go in before the evaluation, record changes go out
        // after it.
        void __fastcall PoliceTickHook(SDK::Police::NativePoliceManager *manager, void *, uint32_t frameMs) {
            gHost->BeforePoliceTick(*manager);
            // The evaluation adds the frame to its timer and runs once the
            // timer passes the period, which it then takes off; an early
            // return (inactive, starting up) leaves the timer alone.
            const uint32_t before = manager->EvaluationTimer();
            gPoliceTickOriginal(manager, frameMs);
            gHost->AfterPoliceTick(*manager, manager->EvaluationTimer() != before + frameMs);
        }

        struct Hook {
            uintptr_t address;
            void *detour;
            void **original;
        };

        const std::array<Hook, 7> &Hooks() {
            static const std::array<Hook, 7> hooks {{
                {SDK::Traffic::kRecruitPolice, reinterpret_cast<void *>(&RecruitPoliceHook), reinterpret_cast<void **>(&gRecruitPoliceOriginal)},
                {SDK::Police::kEntityActionTick, reinterpret_cast<void *>(&ActionTickHook), reinterpret_cast<void **>(&gActionTickOriginal)},
                {SDK::Police::kPoliceBoardArrested, reinterpret_cast<void *>(&BoardArrestedHook), reinterpret_cast<void **>(&gBoardArrestedOriginal)},
                {SDK::Police::kPlayerLockControls, reinterpret_cast<void *>(&LockControlsHook), reinterpret_cast<void **>(&gLockControlsOriginal)},
                {SDK::Police::kCameraArrestView, reinterpret_cast<void *>(&CameraArrestViewHook), reinterpret_cast<void **>(&gCameraArrestViewOriginal)},
                {SDK::Police::kTick, reinterpret_cast<void *>(&PoliceTickHook), reinterpret_cast<void **>(&gPoliceTickOriginal)},
                {SDK::Police::kPoliceBoardBusy, reinterpret_cast<void *>(&BoardBusyHook), reinterpret_cast<void **>(&gBoardBusyOriginal)},
            }};
            return hooks;
        }
    } // namespace

    bool InstallPolice(NpcHost &host) {
        gHost = &host;
        for (const auto &hook : Hooks()) {
            if (MH_CreateHook(reinterpret_cast<void *>(hook.address), hook.detour, hook.original) != MH_OK ||
                MH_EnableHook(reinterpret_cast<void *>(hook.address)) != MH_OK) {
                UninstallPolice();
                return false;
            }
        }
        return true;
    }

    void UninstallPolice() {
        for (const auto &hook : Hooks()) {
            MH_DisableHook(reinterpret_cast<void *>(hook.address));
            MH_RemoveHook(reinterpret_cast<void *>(hook.address));
        }
        gHost = nullptr;
    }
} // namespace Mafia1Online::Features::Npc
