#include <utils/safe_win32.h>

#include "door_service.h"

#include "features/world/world_service.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/door/door_intent.h"
#include "shared/features/player/player_entity.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/door/native_door.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Mafia1Online::Features::Door {
    namespace {
        using NativeDoor = SDK::Door::NativeDoor;
        using State = SDK::Door::State;
        using SetStateCall = int(__thiscall *)(NativeDoor *, State, SDK::Player::NativeActor *, bool, bool);
        SetStateCall gSetStateOriginal = nullptr;
        DoorService *gService = nullptr;
        bool gApplyingReplica = false;

        int __fastcall SetStateHook(NativeDoor *door, void *, State state, SDK::Player::NativeActor *actor, bool immediate, bool wholeChain) {
            const int result = gSetStateOriginal(door, state, actor, immediate, wholeChain);
            if (gService && !gApplyingReplica && door == door->Root() && door->state == state) {
                gService->OnNativeTransition(*door, actor, state);
            }
            return result;
        }

        NativeDoor *FindNativeDoor(std::string_view name) {
            auto *mission = SDK::Core::Mission::Get();
            auto *owner = mission ? mission->FindActorByName(std::string(name).c_str()) : nullptr;
            return owner && owner->GetType() == SDK::Player::NativeActor::Type::Door ? static_cast<NativeDoor *>(static_cast<void *>(owner)) : nullptr;
        }

        void UpdateDoorSector(NativeDoor &door, bool open) {
            if (door.useOmniSector || !door.Frame()) { return; }
            auto *scene = SDK::Core::Mission::Get()->GetScene();
            auto position = door.Frame()->ValidWorldPosition();
            if (open && door.opensBothDirections) {
                const float side = door.reverseDirection ? -1.0f : 1.0f;
                position.x += door.openDirection.x * side;
                position.y += door.openDirection.y * side;
                position.z += door.openDirection.z * side;
            }
            scene->SetFrameSectorPos(door.Frame(), position);
        }
    } // namespace

    bool DoorService::Install() {
        gService = this;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Door::kSetState), reinterpret_cast<void *>(&SetStateHook), reinterpret_cast<void **>(&gSetStateOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(SDK::Door::kSetState)) != MH_OK) {
            Shutdown();
            return false;
        }
        return true;
    }

    void DoorService::Shutdown() {
        gService = nullptr;
        MH_DisableHook(reinterpret_cast<void *>(SDK::Door::kSetState));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Door::kSetState));
        gSetStateOriginal = nullptr;
        Reset();
    }

    void DoorService::Reset() {
        _applied.clear();
        _unresolved.clear();
        _generation = 0;
    }

    void DoorService::OnNativeTransition(NativeDoor &door, void *activator, State next) {
        if (activator != SDK::Player::CurrentPlayer() || (next != State::Opening && next != State::Closing)) {
            return;
        }
        auto *frame = door.Frame();
        if (!frame || !frame->Name()[0]) {
            return;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        Shared::Entities::PlayerEntity *local = nullptr;
        const auto guid = static_cast<uint64_t>(replication->GetMyGUID());
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (player->controllerGuid == guid && player->spawned && player->alive) {
                local = player;
            }
        });
        if (!local) {
            return;
        }
        const auto position = frame->ValidWorldPosition();
        Shared::Door::DoorIntent intent;
        intent.playerId = local->GetNetworkID();
        intent.missionGeneration = local->missionGeneration;
        intent.spawnGeneration = local->spawnGeneration;
        intent.frameName = frame->Name();
        intent.x = position.x;
        intent.y = position.y;
        intent.z = position.z;
        intent.open = next == State::Opening;
        intent.reverse = door.reverseDirection != 0;
        intent.pairedReverse = door.pairedDoor && door.pairedDoor->reverseDirection != 0;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(intent);
    }

    void DoorService::Apply(Shared::Entities::DoorEntity &state) {
        auto *door = FindNativeDoor(state.frameName);
        if (!door) {
            if (_unresolved.insert(state.frameName).second) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Could not resolve mission door frame '{}' for replica {}", state.frameName, state.GetNetworkID());
            }
            return;
        }
        _unresolved.erase(state.frameName);
        const auto prior = _applied.find(state.frameName);
        if (prior != _applied.end() && prior->second.id == state.GetNetworkID() && prior->second.revision == state.revision) {
            return;
        }
        const bool first = prior == _applied.end() || prior->second.id != state.GetNetworkID();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Applying door '{}' replica {} revision {}: target {} fraction {}, native state {} angle {}",
            state.frameName, state.GetNetworkID(), state.revision, state.open ? "open" : "closed", state.openFraction,
            static_cast<uint32_t>(door->state), door->angle);
        _applied[state.frameName] = {state.GetNetworkID(), state.revision};
        door->EnableUsingObject(state.enabled);
        door->reverseDirection = state.reverse;
        if (door->pairedDoor) {
            door->pairedDoor->reverseDirection = state.pairedReverse;
        }
        const State target = state.open ? State::Opening : State::Closing;
        if (first && state.revision == 1) {
            // The activating client has already started the same native swing.
            if (door->state == target) {
                door->locked = state.locked;
                if (door->pairedDoor) { door->pairedDoor->locked = state.locked; }
                return;
            }
            door->SetOpenAngle(state.open ? state.openFraction : 0.0f);
            UpdateDoorSector(*door, state.open);
            door->state = state.open ? State::Open : State::Closed;
            door->SetUsePrompt(state.open);
            if (door->pairedDoor) {
                door->pairedDoor->SetOpenAngle(state.open ? state.openFraction : 0.0f);
                UpdateDoorSector(*door->pairedDoor, state.open);
                door->pairedDoor->state = door->state;
                door->pairedDoor->SetUsePrompt(state.open);
            }
            door->locked = state.locked;
            if (door->pairedDoor) { door->pairedDoor->locked = state.locked; }
            return;
        }
        if (state.openFraction < 1.0f) {
            door->SetOpenAngle(state.open ? state.openFraction : 0.0f);
            UpdateDoorSector(*door, state.open);
            door->state = state.open ? State::Open : State::Closed;
            door->SetUsePrompt(state.open);
            if (door->pairedDoor) {
                door->pairedDoor->SetOpenAngle(state.open ? state.openFraction : 0.0f);
                UpdateDoorSector(*door->pairedDoor, state.open);
                door->pairedDoor->state = door->state;
                door->pairedDoor->SetUsePrompt(state.open);
            }
            door->locked = state.locked;
            if (door->pairedDoor) { door->pairedDoor->locked = state.locked; }
            return;
        }
        if (door->state == target || (state.open ? door->state == State::Open : door->state == State::Closed)) {
            door->locked = state.locked;
            if (door->pairedDoor) { door->pairedDoor->locked = state.locked; }
            return;
        }
        gApplyingReplica = true;
        door->locked = false;
        if (door->pairedDoor) { door->pairedDoor->locked = false; }
        if (state.open) {
            UpdateDoorSector(*door, true);
            if (door->pairedDoor) { UpdateDoorSector(*door->pairedDoor, true); }
        }
        door->SetState(target);
        door->locked = state.locked;
        if (door->pairedDoor) { door->pairedDoor->locked = state.locked; }
        gApplyingReplica = false;
    }

    void DoorService::Update(const World::WorldService &world) {
        if (!world.IsReady()) {
            Reset();
            return;
        }
        if (_generation != world.LoadedMissionGeneration()) {
            _generation = world.LoadedMissionGeneration();
            _applied.clear();
            _unresolved.clear();
        }
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        std::unordered_set<std::string> live;
        replication->ForEach<Shared::Entities::DoorEntity>([&](Shared::Entities::DoorEntity *state) {
            if (state->missionGeneration == _generation) {
                live.insert(state->frameName);
                Apply(*state);
            }
        });
        for (auto it = _applied.begin(); it != _applied.end();) {
            if (!live.contains(it->first)) {
                it = _applied.erase(it);
            }
            else {
                ++it;
            }
        }
    }
} // namespace Mafia1Online::Features::Door
