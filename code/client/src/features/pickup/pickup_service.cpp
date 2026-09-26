#include <utils/safe_win32.h>

#include "pickup_service.h"

#include "features/combat/combat_service.h"
#include "features/world/world_service.h"
#include "shared/features/pickup/pickup_request.h"
#include "shared/features/pickup/weapon_pickup_entity.h"
#include "shared/features/player/player_entity.h"

#include <MinHook.h>
#include <core_modules.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/world/native_using_object.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <cmath>
#include <unordered_set>

namespace Mafia1Online::Features::Pickup {
    namespace {
        using NativeItemVector = SDK::World::NativeItemVector;
        using FindNearCall  = void(__thiscall *)(void *, const SDK::Player::Vector3 *, const SDK::Player::Vector3 *, NativeItemVector *, void *);
        using DropOutCall   = bool(__thiscall *)(SDK::Player::NativeHuman *, NativeItemVector *);
        FindNearCall gFindNearOriginal = nullptr;
        DropOutCall gDropOutOriginal   = nullptr;
        PickupService *gService        = nullptr;

        constexpr float kUseReachSquared = 0.5f;
        constexpr float kUseHeight       = 0.4f;

        void __fastcall FindNearObjectsHook(void *registry, void *, const SDK::Player::Vector3 *position, const SDK::Player::Vector3 *direction,
                                            NativeItemVector *items, void *frame) {
            gFindNearOriginal(registry, position, direction, items, frame);
            if (gService && items) {
                gService->OnNearObjects(*items);
            }
        }

        // Retail creates a C_drop_in_weapon for every dropped weapon. In a mod
        // mission the server owns world items, so the local drop creates none;
        // returning true keeps Do_WeaponDrop's weapon change.
        bool __fastcall DropOutItemsHook(SDK::Player::NativeHuman *human, void *, NativeItemVector *items) {
            if (gService && gService->SuppressNativeDrop()) {
                if (human && items) {
                    gService->OnNativeDrop(*human, *items);
                }
                return items && items->begin != items->end;
            }
            return gDropOutOriginal(human, items);
        }
    } // namespace

    PickupService::PickupService() = default;
    PickupService::~PickupService() = default;

    bool PickupService::Install(World::WorldService &world, Combat::CombatService &combat) {
        _world   = &world;
        _combat  = &combat;
        gService = this;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::World::kFindNearObjects), reinterpret_cast<void *>(&FindNearObjectsHook), reinterpret_cast<void **>(&gFindNearOriginal)) != MH_OK
            || MH_CreateHook(reinterpret_cast<void *>(SDK::World::kDropOutItems), reinterpret_cast<void *>(&DropOutItemsHook), reinterpret_cast<void **>(&gDropOutOriginal)) != MH_OK
            || MH_EnableHook(reinterpret_cast<void *>(SDK::World::kFindNearObjects)) != MH_OK
            || MH_EnableHook(reinterpret_cast<void *>(SDK::World::kDropOutItems)) != MH_OK) {
            Shutdown();
            return false;
        }
        return true;
    }

    void PickupService::Shutdown() {
        for (uintptr_t address : {SDK::World::kFindNearObjects, SDK::World::kDropOutItems}) {
            MH_DisableHook(reinterpret_cast<void *>(address));
            MH_RemoveHook(reinterpret_cast<void *>(address));
        }
        gService = nullptr;
        OnMissionClosing();
    }

    bool PickupService::SuppressNativeDrop() const {
        return _world && _world->LoadedMissionGeneration() != 0;
    }

    void PickupService::OnNativeDrop(SDK::Player::NativeHuman &human, const SDK::World::NativeItemVector &items) {
        if (!_combat || &human.Actor() != SDK::Player::CurrentPlayer() || !items.begin) {
            return;
        }
        for (auto *item = items.begin; item != items.end; ++item) {
            if (item->itemId >= 2 && item->itemId <= 15) {
                _combat->OnNativeDrop(human, static_cast<uint8_t>(item->itemId));
            }
        }
    }

    void PickupService::Remove(uint64_t id) {
        const auto it = _items.find(id);
        if (it == _items.end()) {
            return;
        }
        if (auto *mission = SDK::Core::Mission::Get(); mission && mission->Game()) {
            // FRAME_BOUND: DelObject invalidates and releases the frame; the
            // record stays ours and is freed with the map entry.
            SDK::World::DelUsingObject(mission->Game(), it->second.record.get());
        }
        _items.erase(it);
    }

    void PickupService::Create(World::WorldService &world, uint64_t id) {
        const auto *pickup = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::WeaponPickupEntity>(id);
        auto *mission      = SDK::Core::Mission::Get();
        auto *scene        = mission ? mission->GetScene() : nullptr;
        if (!pickup || !scene || !mission->Game() || pickup->missionGeneration != world.LoadedMissionGeneration()) {
            return;
        }
        auto *frame = SDK::Scene::GetDriver()->CreateModel();
        if (!frame) {
            return;
        }
        if (!SDK::Scene::GetModelCache()->OpenModel(frame, SDK::World::ItemModelName(pickup->weaponId))) {
            frame->Release();
            return;
        }
        const SDK::Player::Vector3 position {pickup->position.x, pickup->position.y, pickup->position.z};
        frame->SetName("mp_pickup");
        frame->SetWorldPosition(position);
        frame->SetDirection({std::sin(pickup->yaw), 0.0f, std::cos(pickup->yaw)}, 0.0f);
        frame->LinkTo(scene->PrimarySector());
        scene->SetFrameSectorPos(frame, position);
        frame->Update();

        auto record = std::make_unique<SDK::World::NativeUsingObject>();
        *record = {};
        record->position        = {position.x, position.y + kUseHeight, position.z};
        record->distanceSquared = kUseReachSquared;
        record->flags           = SDK::World::kUsingObjectFrameBound;
        record->frame           = frame;
        record->item.itemId     = pickup->weaponId;
        record->item.ammoLoaded = pickup->loaded;
        record->item.ammoHidden = pickup->reserve;
        record->item.usingObject = record.get();
        SDK::World::AddUsingObject(mission->Game(), record.get());
        _items[id] = {std::move(record), pickup->loaded, pickup->reserve};
    }

    void PickupService::Update(World::WorldService &world) {
        _world = &world;
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || !world.IsReady()) {
            return;
        }
        std::unordered_set<uint64_t> seen;
        replication->ForEach<Shared::Entities::WeaponPickupEntity>([&](Shared::Entities::WeaponPickupEntity *pickup) {
            if (pickup->missionGeneration != world.LoadedMissionGeneration()) {
                return;
            }
            const uint64_t id = pickup->GetNetworkID();
            seen.insert(id);
            auto it = _items.find(id);
            if (it == _items.end()) {
                Create(world, id);
                return;
            }
            if (it->second.loaded != pickup->loaded || it->second.reserve != pickup->reserve) {
                it->second.record->item.ammoLoaded = pickup->loaded;
                it->second.record->item.ammoHidden = pickup->reserve;
                it->second.loaded  = pickup->loaded;
                it->second.reserve = pickup->reserve;
            }
        });
        for (auto it = _items.begin(); it != _items.end();) {
            const uint64_t id = it->first;
            ++it;
            if (!seen.contains(id)) {
                Remove(id);
            }
        }
    }

    bool PickupService::OnNearObjects(SDK::World::NativeItemVector &items) {
        if (_choiceFilter && _choiceFilter(items)) return true;
        if (!items.begin || items.begin == items.end) {
            return false;
        }
        uint64_t nearest = 0;
        for (const auto &[id, item] : _items) {
            if (items.begin->usingObject == item.record.get()) {
                nearest = id;
                break;
            }
        }
        if (nearest == 0) {
            // A native object is nearer; hide ours so the retail pickup menu
            // never takes a replicated weapon locally.
            auto *out = items.begin;
            for (auto *in = items.begin; in != items.end; ++in) {
                bool ours = false;
                for (const auto &[id, item] : _items) {
                    ours = ours || in->usingObject == item.record.get();
                }
                if (!ours) {
                    *out++ = *in;
                }
            }
            items.end = out;
            return false;
        }
        // Empty the list: Do_AB_OwnerNULL then returns before the menu or any
        // local inventory change, and the server moves the weapon.
        items.end = items.begin;
        Shared::Entities::PlayerEntity *local = nullptr;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            const uint64_t myGuid = static_cast<uint64_t>(replication->GetMyGUID());
            replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
                if (!local && player->controllerGuid == myGuid && player->spawned && player->alive) {
                    local = player;
                }
            });
        }
        const uint64_t now = GetTickCount64();
        if (local && (nearest != _lastRequestId || now - _lastRequestAt > 500)) {
            Shared::Pickup::PickupRequest request;
            request.pickupId          = nearest;
            request.networkId         = local->GetNetworkID();
            request.missionGeneration = local->missionGeneration;
            request.spawnGeneration   = local->spawnGeneration;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(request);
            _lastRequestId = nearest;
            _lastRequestAt = now;
        }
        return true;
    }

    void PickupService::OnMissionClosing() {
        for (auto it = _items.begin(); it != _items.end();) {
            const uint64_t id = it->first;
            ++it;
            Remove(id);
        }
    }

    void PickupService::Reset() {
        OnMissionClosing();
        _lastRequestId = 0;
        _lastRequestAt = 0;
    }
} // namespace Mafia1Online::Features::Pickup
