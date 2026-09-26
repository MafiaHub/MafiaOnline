#include "combat_service.h"

#include "combat_hooks.h"
#include "shared/features/combat/detonation.h"
#include "features/world/world_service.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <mafia1/sdk/combat/native_grenade.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <mafia1/sdk/ui/native_indicators.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <tuple>

namespace Mafia1Online::Features::Combat {
    size_t CombatService::EventKeyHash::operator()(const EventKey &key) const {
        size_t value = std::hash<uint64_t> {}(key.networkId);
        for (const uint64_t part : {key.missionGeneration, key.spawnGeneration, static_cast<uint64_t>(key.revision)}) {
            value ^= std::hash<uint64_t> {}(part) + static_cast<size_t>(0x9e3779b9) + (value << 6) + (value >> 2);
        }
        return value;
    }

    void CombatService::RegisterRPC() {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Shared::Combat::State>([this](const Shared::Combat::State &state, MafiaNet::Packet *) {
            OnState(state);
        });
        network->RegisterRPC<Shared::Combat::Event>([this](const Shared::Combat::Event &event, MafiaNet::Packet *) {
            OnEvent(event);
        });
        network->RegisterRPC<Shared::Combat::Detonation>([this](const Shared::Combat::Detonation &detonation, MafiaNet::Packet *) {
            OnDetonation(detonation);
        });
    }

    void CombatService::Update(World::WorldService &world) {
        _world = &world;
        if (!world.IsReady()) {
            return;
        }
        for (auto it = _states.begin(); it != _states.end();) {
            if (it->second.missionGeneration != world.LoadedMissionGeneration()) {
                _applied.erase(it->first);
                it = _states.erase(it);
            }
            else {
                ++it;
            }
        }
        for (auto it = _events.begin(); it != _events.end();) {
            if (it->event.missionGeneration < world.LoadedMissionGeneration()) {
                it = _events.erase(it);
            }
            else {
                ++it;
            }
        }
        UpdateLocalAim(world);
        UpdateLocalCrouch(world);
        UpdateLocalActions(world);
        ApplyNativeStates(world);
        ReplayEvents(world);
        UpdateRemoteMelee(world);
        UpdateRemoteGrenades(world);
    }

    void CombatService::Reset() {
        _states.clear();
        _applied.clear();
        _events.clear();
        _seenEvents.clear();
        _seenOrder.clear();
        _localAim              = {};
        _localCrouch           = {};
        _localAction           = {};
        _world                 = nullptr;
        _sequence              = 0;
        _lastLocalFireSequence = 0;
        _localFires.clear();
        _localGrenades.clear();
        _remoteGrenades.clear();
        _remoteMelee.clear();
        _remoteGrenadeCharges.clear();
        _remoteGrenadeReleases.clear();
        _localGrenadeCharge = {};
        _localGrenadeNetworkId = 0;
        _localGrenadeReleasedAt = {};
        _submitThrowHoldMs = 0;
    }

    void CombatService::ApplyNativeStates(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        for (const auto &[networkId, state] : _states) {
            auto *player = replication->GetEntity<Shared::Entities::PlayerEntity>(networkId);
            if (!player || !player->spawned || !state.spawned || player->missionGeneration != state.missionGeneration || player->spawnGeneration != state.spawnGeneration) {
                continue;
            }
            const auto handle = world.NativeObjects().FindByNetwork(networkId);
            auto *actor       = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(handle));
            if (!actor || (actor->GetType() != SDK::Player::NativeActor::Type::Player && actor->GetType() != SDK::Player::NativeActor::Type::Entity)) {
                continue;
            }
            auto *human = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
            // DeathService applies a retail Hit after the server's death state
            // arrives. Keep the native human alive until that replay occurs.
            if (state.alive) {
                human->SetHealth(state.health);
            }
            auto &applied        = _applied[networkId];
            const bool newNative = applied.nativeGeneration != handle.generation || applied.missionGeneration != state.missionGeneration || applied.spawnGeneration != state.spawnGeneration;
            const bool localPending = SDK::Player::CurrentPlayer() == actor && _localAction.networkId == networkId && _localAction.missionGeneration == state.missionGeneration
                && _localAction.spawnGeneration == state.spawnGeneration && _localAction.nativeGeneration == handle.generation && _localAction.sentAt != std::chrono::steady_clock::time_point {};
            bool inventoryChanged = newNative || applied.inventoryMask != state.inventoryMask || applied.selectedWeapon != state.selectedWeapon;
            if (!inventoryChanged) {
                for (size_t slot = 0; slot < state.ammo.size(); ++slot) {
                    if (applied.ammo[slot].loaded != state.ammo[slot].loaded || applied.ammo[slot].reserve != state.ammo[slot].reserve) {
                        inventoryChanged = true;
                        break;
                    }
                }
            }
            const bool remoteThrowBeforeNotify = actor != SDK::Player::CurrentPlayer()
                && (_remoteGrenadeCharges.contains(networkId) || _remoteGrenadeReleases.contains(networkId)
                    || std::any_of(_events.begin(), _events.end(), [&](const PendingEvent &pending) {
                           return pending.event.networkId == networkId && pending.event.missionGeneration == state.missionGeneration
                               && pending.event.spawnGeneration == state.spawnGeneration
                               && (pending.event.action == Shared::Combat::Action::ThrowStart
                                   || pending.event.action == Shared::Combat::Action::ThrowRelease);
                       }));
            if (!localPending && !remoteThrowBeforeNotify && (inventoryChanged || human->InventoryMask() != state.inventoryMask || human->SelectedWeapon() != state.selectedWeapon)) {
                const auto previousSelected = human->SelectedItem();
                std::array<SDK::Player::NativeStockItem, Shared::Combat::kWeaponSlots> stock {};
                size_t count = 0;
                for (uint8_t itemId = 0; itemId < Shared::Combat::kWeaponSlots; ++itemId) {
                    if ((state.inventoryMask & (1u << itemId)) != 0) {
                        const auto &ammo = state.ammo[itemId];
                        stock[count++]   = {itemId, ammo.loaded, ammo.reserve};
                    }
                }
                if (human->ApplyInventory(std::span<const SDK::Player::NativeStockItem>(stock.data(), count), state.selectedWeapon)) {
                    const auto &selected = human->SelectedItem();
                    if (actor == SDK::Player::CurrentPlayer() && previousSelected.itemId == selected.itemId
                        && (previousSelected.ammoLoaded != selected.ammoLoaded || previousSelected.ammoReserve != selected.ammoReserve)) {
                        SDK::UI::NativeIndicators::Get().SetAmmo(static_cast<uint32_t>(std::max(0, selected.ammoLoaded)), static_cast<uint32_t>(std::max(0, selected.ammoReserve)));
                    }
                    applied.nativeGeneration  = handle.generation;
                    applied.missionGeneration = state.missionGeneration;
                    applied.spawnGeneration   = state.spawnGeneration;
                    applied.revision          = state.revision;
                    applied.inventoryMask     = state.inventoryMask;
                    applied.selectedWeapon    = state.selectedWeapon;
                    applied.ammo              = state.ammo;
                }
            }
            else if (!localPending) {
                // Pose and stance revisions do not change the stock inventory.
                // Keep action acknowledgement current without resetting native
                // item slots during ordinary local movement.
                applied.revision = state.revision;
            }
            if (SDK::Player::CurrentPlayer() == actor) {
                continue;
            }
            const auto *seat      = static_cast<SDK::Seat::NativeHuman *>(static_cast<void *>(actor));
            const bool inCar      = seat->usedActorEnter != nullptr;
            if (!state.alive) {
                continue;
            }
            const bool crouchChanged = human->IsCrouching() != state.crouching;
            if (crouchChanged) {
                human->SetCrouching(state.crouching);
            }
            const bool aimChanged = human->IsAiming() != state.aiming;
            if (aimChanged) {
                human->SetAiming(state.aiming);
            }
            if ((aimChanged || crouchChanged) && !inCar) {
                human->RefreshAnimation();
            }
            const auto position = SDK::Player::WorldPosition(actor);
            if (!inCar) {
                // Retail updates the neck and back from the camera even when
                // the player is not aiming or carrying a weapon.
                const auto now = std::chrono::steady_clock::now();
                if (newNative || !applied.poseInitialized) {
                    applied.poseTargetOffsetX = state.poseTargetOffsetX;
                    applied.poseTargetOffsetY = state.poseTargetOffsetY;
                    applied.poseTargetOffsetZ = state.poseTargetOffsetZ;
                    applied.poseInitialized = true;
                }
                else {
                    const float elapsed = std::chrono::duration<float>(now - applied.poseUpdatedAt).count();
                    const float blend = 1.0f - std::exp(-elapsed / 0.045f);
                    applied.poseTargetOffsetX += blend * (state.poseTargetOffsetX - applied.poseTargetOffsetX);
                    applied.poseTargetOffsetY += blend * (state.poseTargetOffsetY - applied.poseTargetOffsetY);
                    applied.poseTargetOffsetZ += blend * (state.poseTargetOffsetZ - applied.poseTargetOffsetZ);
                }
                applied.poseUpdatedAt = now;
                (void)human->AimPoseAt({position.x + applied.poseTargetOffsetX, position.y + applied.poseTargetOffsetY, position.z + applied.poseTargetOffsetZ});
            }
            else if (state.aiming) {
                const float squared = state.aimDirectionX * state.aimDirectionX + state.aimDirectionY * state.aimDirectionY + state.aimDirectionZ * state.aimDirectionZ;
                if (std::isfinite(squared) && squared > 0.8f && squared < 1.2f) {
                    // PoseSetPoseAimed refuses seated humans. C_human::Movement
                    // turns the window-shooting arm toward m_vShootTarget each
                    // frame, which retail AI_car_fire refreshes for the player.
                    human->SetShootTarget({position.x + 100.0f * state.aimDirectionX, position.y + 100.0f * state.aimDirectionY, position.z + 100.0f * state.aimDirectionZ});
                }
            }
        }
    }

    void CombatService::UpdateLocalAim(World::WorldService &world) {
        const auto *state = GetLocalState(world);
        if (!state || !state->spawned || !state->alive) {
            _localAim = {};
            return;
        }
        const auto handle = world.NativeObjects().FindByNetwork(state->networkId);
        auto *actor       = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(handle));
        if (!actor || actor != SDK::Player::CurrentPlayer()) {
            _localAim = {};
            return;
        }
        auto *human = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
        if (_localAim.networkId != state->networkId || _localAim.missionGeneration != state->missionGeneration || _localAim.spawnGeneration != state->spawnGeneration || _localAim.nativeGeneration != handle.generation) {
            _localAim                   = {};
            _localAim.networkId         = state->networkId;
            _localAim.missionGeneration = state->missionGeneration;
            _localAim.spawnGeneration   = state->spawnGeneration;
            _localAim.nativeGeneration  = handle.generation;
        }
        const bool aiming = human->IsAiming() && state->selectedWeapon != 0;
        float dx          = 0.0f;
        float dy          = 0.0f;
        float dz          = 0.0f;
        float poseX       = 0.0f;
        float poseY       = 0.0f;
        float poseZ       = 0.0f;
        const auto position = SDK::Player::WorldPosition(actor);
        if (aiming) {
            const auto &target  = human->ShootTarget();
            dx                  = target.x - position.x;
            dy                  = target.y - position.y;
            dz                  = target.z - position.z;
            float length        = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!std::isfinite(length) || length < 0.001f) {
                const auto forward = SDK::Player::WorldDirection(actor);
                dx                 = forward.x;
                dy                 = forward.y;
                dz                 = forward.z;
                length             = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            if (!std::isfinite(length) || length < 0.001f) {
                return;
            }
            dx /= length;
            dy /= length;
            dz /= length;
        }
        auto *camera = SDK::Core::Mission::Get()->GetScene()->ActiveCamera();
        if (!camera) {
            return;
        }
        const auto cameraPosition  = camera->WorldPosition();
        const auto cameraDirection = camera->WorldDirection();
        poseX = cameraPosition.x + 10.0f * cameraDirection.x - position.x;
        poseY = cameraPosition.y + 10.0f * cameraDirection.y - position.y;
        poseZ = cameraPosition.z + 10.0f * cameraDirection.z - position.z;
        if (!std::isfinite(poseX) || !std::isfinite(poseY) || !std::isfinite(poseZ)) {
            return;
        }
        const auto now              = std::chrono::steady_clock::now();
        const float delta           = (dx - _localAim.directionX) * (dx - _localAim.directionX) + (dy - _localAim.directionY) * (dy - _localAim.directionY) + (dz - _localAim.directionZ) * (dz - _localAim.directionZ);
        const float poseDelta       = (poseX - _localAim.poseTargetOffsetX) * (poseX - _localAim.poseTargetOffsetX) + (poseY - _localAim.poseTargetOffsetY) * (poseY - _localAim.poseTargetOffsetY)
            + (poseZ - _localAim.poseTargetOffsetZ) * (poseZ - _localAim.poseTargetOffsetZ);
        const bool transition       = _localAim.sent && aiming != _localAim.aiming;
        const bool firstPose        = !_localAim.sent;
        const bool changedDirection = _localAim.sent && ((aiming && delta > 0.0001f) || poseDelta > (aiming ? 0.0025f : 0.01f))
            && now - _localAim.lastSent >= std::chrono::milliseconds(aiming ? 50 : 75);
        if (!transition && !firstPose && !changedDirection) {
            return;
        }
        if (SubmitAim(state->networkId, state->selectedWeapon, aiming, dx, dy, dz, poseX, poseY, poseZ)) {
            _localAim.sent       = true;
            _localAim.aiming     = aiming;
            _localAim.directionX = dx;
            _localAim.directionY = dy;
            _localAim.directionZ = dz;
            _localAim.poseTargetOffsetX = poseX;
            _localAim.poseTargetOffsetY = poseY;
            _localAim.poseTargetOffsetZ = poseZ;
            _localAim.lastSent   = now;
        }
    }

    void CombatService::UpdateLocalCrouch(World::WorldService &world) {
        const auto *state = GetLocalState(world);
        if (!state || !state->spawned || !state->alive) {
            _localCrouch = {};
            return;
        }
        const auto handle = world.NativeObjects().FindByNetwork(state->networkId);
        auto *actor       = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(handle));
        if (!actor || actor != SDK::Player::CurrentPlayer()) {
            _localCrouch = {};
            return;
        }
        if (_localCrouch.networkId != state->networkId || _localCrouch.missionGeneration != state->missionGeneration || _localCrouch.spawnGeneration != state->spawnGeneration || _localCrouch.nativeGeneration != handle.generation) {
            _localCrouch                   = {};
            _localCrouch.networkId         = state->networkId;
            _localCrouch.missionGeneration = state->missionGeneration;
            _localCrouch.spawnGeneration   = state->spawnGeneration;
            _localCrouch.nativeGeneration  = handle.generation;
        }
        const bool crouching = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor))->IsCrouching();
        if ((!_localCrouch.sent && !crouching) || (_localCrouch.sent && _localCrouch.crouching == crouching)) {
            return;
        }
        if (Submit(state->networkId, Shared::Combat::Action::Crouch, state->selectedWeapon, 0, 0, 0.0f, 0.0f, 0.0f, false, crouching)) {
            _localCrouch.sent      = true;
            _localCrouch.crouching = crouching;
        }
    }

    void CombatService::UpdateLocalActions(World::WorldService &world) {
        const auto *state = GetLocalState(world);
        if (!state || !state->spawned || !state->alive) {
            _localAction = {};
            return;
        }
        const auto handle = world.NativeObjects().FindByNetwork(state->networkId);
        auto *actor       = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(handle));
        if (!actor || actor != SDK::Player::CurrentPlayer()) {
            _localAction = {};
            return;
        }
        auto *human = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
        if (_localAction.networkId != state->networkId || _localAction.missionGeneration != state->missionGeneration || _localAction.spawnGeneration != state->spawnGeneration || _localAction.nativeGeneration != handle.generation) {
            _localAction = {};
        }
        const auto now = std::chrono::steady_clock::now();
        bool continuingReload = false;
        if (_localAction.sentAt != std::chrono::steady_clock::time_point {}) {
            bool accepted = false;
            if (state->revision > _localAction.baseRevision) {
                switch (_localAction.action) {
                case Shared::Combat::Action::Equip: accepted = state->selectedWeapon == _localAction.weaponId; break;
                case Shared::Combat::Action::Drop: accepted = (state->inventoryMask & (1u << _localAction.weaponId)) == 0; break;
                case Shared::Combat::Action::Reload:
                    accepted = state->ammo[_localAction.weaponId].loaded == _localAction.loaded && state->ammo[_localAction.weaponId].reserve == _localAction.reserve;
                    break;
                case Shared::Combat::Action::Throw:
                    accepted = (state->inventoryMask & (1u << _localAction.weaponId)) == 0
                        || state->ammo[_localAction.weaponId].loaded < _localAction.loaded;
                    break;
                default: break;
                }
            }
            if (!accepted) {
                if (now - _localAction.sentAt >= std::chrono::milliseconds(900)) {
                    // A rejected intent has no newer inventory snapshot to
                    // trigger reconciliation. Force the server stock back
                    // into the native slots after the pending action expires.
                    if (auto applied = _applied.find(state->networkId); applied != _applied.end()) {
                        applied->second.nativeGeneration = 0;
                    }
                    _localAction = {};
                }
                return;
            }
            // The pump shotgun may have loaded another shell before the first
            // accepted state arrives. Submit that completed native step before
            // inventory reconciliation can rewind the animation's ammo.
            continuingReload = _localAction.action == Shared::Combat::Action::Reload && human->SelectedWeapon() == state->selectedWeapon
                && human->SelectedItem().ammoReserve >= 0 && human->SelectedItem().ammoReserve < state->ammo[state->selectedWeapon].reserve;
            _localAction = {};
            if (!continuingReload) {
                return;
            }
        }
        if (!continuingReload) {
            const auto applied = _applied.find(state->networkId);
            if (applied == _applied.end() || applied->second.nativeGeneration != handle.generation || applied->second.missionGeneration != state->missionGeneration || applied->second.spawnGeneration != state->spawnGeneration || applied->second.revision != state->revision) {
                return;
            }
        }
        Shared::Combat::Action action;
        uint8_t weaponId = 0;
        Shared::Combat::ReloadedAmmo expectedReload {};
        // Real drops arrive through DropOutItemsHook. A missing bit here can
        // also be a grenade's native consumption or a newly applied script
        // inventory, and must never create a world pickup.
        if (human->InventoryMask() != state->inventoryMask) {
            return;
        }
        if (human->SelectedWeapon() != state->selectedWeapon) {
            if (human->SelectedWeapon() >= Shared::Combat::kWeaponSlots) {
                return;
            }
            weaponId = static_cast<uint8_t>(human->SelectedWeapon());
            if (weaponId != 0 && (state->inventoryMask & (1u << weaponId)) == 0) {
                return;
            }
            action = Shared::Combat::Action::Equip;
        }
        else {
            weaponId = state->selectedWeapon;
            if (weaponId < 6 || weaponId > 14) {
                return;
            }
            const auto &nativeAmmo = human->SelectedItem();
            const auto &serverAmmo = state->ammo[weaponId];
            if (nativeAmmo.ammoLoaded < 0 || nativeAmmo.ammoReserve < 0 || nativeAmmo.ammoReserve >= serverAmmo.reserve) {
                return;
            }
            expectedReload = Shared::Combat::ReloadWeaponAmmo(weaponId, SDK::Player::StockMagazineCapacity(weaponId), serverAmmo.loaded, serverAmmo.reserve);
            if (expectedReload.loaded == serverAmmo.loaded && expectedReload.reserve == serverAmmo.reserve) {
                return;
            }
            action = Shared::Combat::Action::Reload;
        }
        if (Submit(state->networkId, action, weaponId, 0, 0, 0.0f, 0.0f, 0.0f)) {
            _localAction.networkId         = state->networkId;
            _localAction.missionGeneration = state->missionGeneration;
            _localAction.spawnGeneration   = state->spawnGeneration;
            _localAction.nativeGeneration  = handle.generation;
            _localAction.baseRevision      = state->revision;
            _localAction.action            = action;
            _localAction.weaponId          = weaponId;
            _localAction.loaded            = expectedReload.loaded;
            _localAction.reserve           = expectedReload.reserve;
            _localAction.sentAt            = now;
        }
    }

    void CombatService::ReplayEvents(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        // Deliver actions only after a matching or newer complete snapshot.
        // Sorting buffered revisions preserves local shot order after jitter.
        std::stable_sort(_events.begin(), _events.end(), [](const PendingEvent &a, const PendingEvent &b) {
            const auto &left  = a.event;
            const auto &right = b.event;
            return std::tie(left.networkId, left.missionGeneration, left.spawnGeneration, left.revision) < std::tie(right.networkId, right.missionGeneration, right.spawnGeneration, right.revision);
        });
        const auto now = std::chrono::steady_clock::now();
        for (auto it = _events.begin(); it != _events.end();) {
            const auto &event = it->event;
            if (now - it->received > std::chrono::seconds(2) || event.missionGeneration < world.LoadedMissionGeneration()) {
                it = _events.erase(it);
                continue;
            }
            const auto *state = GetState(event.networkId);
            if (!state || state->missionGeneration < event.missionGeneration || (state->missionGeneration == event.missionGeneration && state->spawnGeneration < event.spawnGeneration)
                || (state->missionGeneration == event.missionGeneration && state->spawnGeneration == event.spawnGeneration && state->revision < event.revision)) {
                ++it;
                continue;
            }
            const bool firearmAction = (event.action == Shared::Combat::Action::Fire || event.action == Shared::Combat::Action::Reload) && event.weaponId >= 6 && event.weaponId <= 14;
            const bool meleeAction   = (event.action == Shared::Combat::Action::Melee || event.action == Shared::Combat::Action::MeleeStart || event.action == Shared::Combat::Action::MeleeCancel)
                && (event.weaponId == 0 || (event.weaponId >= 2 && event.weaponId <= 4));
            const bool throwAction   = event.action == Shared::Combat::Action::Throw && (event.weaponId == 5 || event.weaponId == 15);
            const bool grenadePhase = (event.action == Shared::Combat::Action::ThrowStart || event.action == Shared::Combat::Action::ThrowRelease || event.action == Shared::Combat::Action::ThrowCancel)
                && (event.weaponId == 5 || event.weaponId == 15);
            if (throwAction) {
                // A remote throw spawns a disarmed native copy for display.
                // It is removed at the server's detonation; a copy the local
                // player threw is never created.
                const auto *local = GetLocalState(world);
                if ((!local || local->networkId != event.networkId) && !_remoteGrenades.contains({event.networkId, event.sequence})) {
                    if (auto *grenade = ReplayNativeThrow(event.grenadeType, {event.originX, event.originY, event.originZ},
                                                          {event.nativeDirectionX, event.nativeDirectionY, event.nativeDirectionZ})) {
                        _remoteGrenades[{event.networkId, event.sequence}] = grenade;
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info(
                            "Remote grenade display spawned: player {}, sequence {}, type {}, origin ({:.2f}, {:.2f}, {:.2f}), impulse ({:.2f}, {:.2f}, {:.2f})",
                            event.networkId, event.sequence, event.grenadeType, event.originX, event.originY, event.originZ,
                            event.nativeDirectionX, event.nativeDirectionY, event.nativeDirectionZ);
                    }
                    else {
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn(
                            "Remote grenade display failed to spawn: player {}, sequence {}, type {}",
                            event.networkId, event.sequence, event.grenadeType);
                    }
                }
                it = _events.erase(it);
                continue;
            }
            if (state->missionGeneration != event.missionGeneration || state->spawnGeneration != event.spawnGeneration || (!firearmAction && !meleeAction && !grenadePhase)) {
                it = _events.erase(it);
                continue;
            }
            auto *player = replication->GetEntity<Shared::Entities::PlayerEntity>(event.networkId);
            if (!player || player->missionGeneration < event.missionGeneration || (player->missionGeneration == event.missionGeneration && player->spawnGeneration < event.spawnGeneration)) {
                ++it;
                continue;
            }
            if (player->missionGeneration != event.missionGeneration || player->spawnGeneration != event.spawnGeneration) {
                it = _events.erase(it);
                continue;
            }
            const auto handle = world.NativeObjects().FindByNetwork(event.networkId);
            auto *actor       = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(handle));
            if (!actor) {
                ++it;
                continue;
            }
            if (actor->GetType() != SDK::Player::NativeActor::Type::Entity) {
                it = _events.erase(it);
                continue;
            }
            const float squared = event.directionX * event.directionX + event.directionY * event.directionY + event.directionZ * event.directionZ;
            if (event.action == Shared::Combat::Action::Fire && (!std::isfinite(squared) || squared < 0.8f || squared > 1.2f)) {
                it = _events.erase(it);
                continue;
            }
            auto *human           = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
            // The controlling client already performed the native action.
            // Replaying its accepted event runs a second swing or shot on the
            // local human and can enter an invalid melee animation path.
            if (actor == SDK::Player::CurrentPlayer()) {
                it = _events.erase(it);
                continue;
            }
            if (grenadePhase) {
                if (auto *mission = SDK::Core::Mission::Get(); mission && mission->Game()) {
                    const auto active = _remoteGrenadeCharges.find(event.networkId);
                    const bool started = active != _remoteGrenadeCharges.end() && active->second.missionGeneration == event.missionGeneration
                        && active->second.spawnGeneration == event.spawnGeneration && active->second.nativeGeneration == handle.generation
                        && active->second.weaponId == event.weaponId;
                    const auto &ammo = state->ammo[event.weaponId];
                    if (event.action == Shared::Combat::Action::ThrowStart) {
                        _remoteGrenadeReleases.erase(event.networkId);
                        human->ApplySelectedWeapon(event.weaponId, std::max(1, static_cast<int32_t>(ammo.loaded)), ammo.reserve);
                        if (!ReplayNativeGrenadeStart(*human)) {
                            ++it;
                            continue;
                        }
                        _remoteGrenadeCharges[event.networkId] = {event.missionGeneration, event.spawnGeneration, handle.generation, event.weaponId, now};
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Remote grenade windup started: player {}, weapon {}", event.networkId, event.weaponId);
                    }
                    else if (event.action == Shared::Combat::Action::ThrowRelease) {
                        if (!started) {
                            ++it;
                            continue;
                        }
                        const SDK::Player::Vector3 direction {event.directionX, event.directionY, event.directionZ};
                        if (!ReplayNativeGrenadeRelease(*human, event.throwHoldMs, mission->Game()->GameTime(), direction)) {
                            ++it;
                            continue;
                        }
                        _remoteGrenadeReleases[event.networkId] = {event.missionGeneration, event.spawnGeneration, handle.generation, event.weaponId, now};
                        _remoteGrenadeCharges.erase(active);
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info(
                            "Remote grenade throw animation released: player {}, weapon {}, charge {} ms",
                            event.networkId, event.weaponId, event.throwHoldMs);
                    }
                    else if (started) {
                        ReplayNativeGrenadeCancel(*human, mission->Game()->GameTime());
                        _remoteGrenadeCharges.erase(active);
                    }
                    if (event.action == Shared::Combat::Action::ThrowCancel) {
                        const auto &selectedAmmo = state->ammo[state->selectedWeapon];
                        human->ApplySelectedWeapon(state->selectedWeapon, selectedAmmo.loaded, selectedAmmo.reserve);
                    }
                }
                it = _events.erase(it);
                continue;
            }
            if (meleeAction) {
                if (auto *mission = SDK::Core::Mission::Get(); mission && mission->Game()) {
                    if (state->selectedWeapon != event.weaponId) {
                        _remoteMelee.erase(event.networkId);
                        it = _events.erase(it);
                        continue;
                    }
                    const auto &meleeAmmo = state->ammo[event.weaponId];
                    human->ApplySelectedWeapon(event.weaponId, meleeAmmo.loaded, meleeAmmo.reserve);
                    bool replayed = true;
                    if (event.action == Shared::Combat::Action::MeleeStart) {
                        replayed = ReplayNativeMeleeStart(*human);
                        if (replayed) {
                            _remoteMelee[event.networkId] = {event.missionGeneration, event.spawnGeneration, handle.generation, event.weaponId, now};
                        }
                    }
                    else if (event.action == Shared::Combat::Action::MeleeCancel) {
                        ReplayNativeMeleeCancel(*human);
                        _remoteMelee.erase(event.networkId);
                    }
                    else {
                        replayed = ReplayNativeMelee(*human, event.meleeIndex, event.meleeHoldMs, mission->Game()->GameTime());
                        _remoteMelee.erase(event.networkId);
                    }
                    if (!replayed) {
                        ++it;
                        continue;
                    }
                }
                it = _events.erase(it);
                continue;
            }
            const auto position   = SDK::Player::WorldPosition(actor);
            const auto &eventAmmo = state->ammo[event.weaponId];
            // A later snapshot can already contain several spent rounds, or
            // no reserve after a reload. Give the native action temporary
            // ammunition and then restore the latest server state.
            const bool fire = event.action == Shared::Combat::Action::Fire;
            // The state snapshot already contains the ammunition moved by a
            // reload. Give Do_Reload a one-round deficit and a reserve round
            // so it can enter the stock reload animation, then restore the
            // authoritative post-action amounts below.
            human->ApplySelectedWeapon(event.weaponId, fire ? std::max(1, static_cast<int32_t>(eventAmmo.loaded) + 1) : std::max(0, static_cast<int32_t>(eventAmmo.loaded) - 1),
                                       fire ? eventAmmo.reserve : std::max(1, static_cast<int32_t>(eventAmmo.reserve) + 1));
            if (fire) {
                human->SetShootTarget({position.x + 100.0f * event.directionX, position.y + 100.0f * event.directionY, position.z + 100.0f * event.directionZ});
            }
            const auto shotResult = fire ? ReplayNativeShot(*human, event) : NativeShotReplayResult::Deferred;
            const bool replayed = fire ? shotResult != NativeShotReplayResult::Deferred : human->ReplayReload();
            const auto &selectedAmmo = state->ammo[state->selectedWeapon];
            human->ApplySelectedWeapon(state->selectedWeapon, selectedAmmo.loaded, selectedAmmo.reserve);
            it = replayed ? _events.erase(it) : std::next(it);
        }
    }

    void CombatService::UpdateRemoteMelee(World::WorldService &world) {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = _remoteMelee.begin(); it != _remoteMelee.end();) {
            const auto *state = GetState(it->first);
            const auto handle = world.NativeObjects().FindByNetwork(it->first);
            auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(handle));
            if (!state || !actor || actor == SDK::Player::CurrentPlayer() || state->missionGeneration != it->second.missionGeneration
                || state->spawnGeneration != it->second.spawnGeneration || state->selectedWeapon != it->second.weaponId
                || handle.generation != it->second.nativeGeneration || now - it->second.startedAt > std::chrono::milliseconds(Shared::Combat::kMaximumMeleeHoldMs + Shared::Combat::kMeleeNetworkGraceMs)) {
                it = _remoteMelee.erase(it);
                continue;
            }
            auto *human = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
            if (!human->MeleeArmed()) {
                it = _remoteMelee.erase(it);
                continue;
            }
            ReplayNativeMeleeHold(*human);
            ++it;
        }
    }

    void CombatService::UpdateRemoteGrenades(World::WorldService &world) {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = _remoteGrenadeReleases.begin(); it != _remoteGrenadeReleases.end();) {
            const auto *state = GetState(it->first);
            const auto handle = world.NativeObjects().FindByNetwork(it->first);
            if (!state || !state->alive || state->missionGeneration != it->second.missionGeneration
                || state->spawnGeneration != it->second.spawnGeneration || handle.generation != it->second.nativeGeneration
                || now - it->second.startedAt > std::chrono::seconds(3)) {
                it = _remoteGrenadeReleases.erase(it);
            }
            else {
                ++it;
            }
        }
        for (auto it = _remoteGrenadeCharges.begin(); it != _remoteGrenadeCharges.end();) {
            const auto *state = GetState(it->first);
            const auto handle = world.NativeObjects().FindByNetwork(it->first);
            auto *actor = static_cast<SDK::Player::NativeActor *>(world.NativeObjects().Resolve(handle));
            if (!state || !state->alive || !actor || actor == SDK::Player::CurrentPlayer() || state->missionGeneration != it->second.missionGeneration
                || state->spawnGeneration != it->second.spawnGeneration
                || handle.generation != it->second.nativeGeneration) {
                it = _remoteGrenadeCharges.erase(it);
                continue;
            }
            auto *human = static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
            const bool releaseQueued = std::any_of(_events.begin(), _events.end(), [&](const PendingEvent &pending) {
                return pending.event.networkId == it->first && pending.event.missionGeneration == it->second.missionGeneration
                    && pending.event.spawnGeneration == it->second.spawnGeneration && pending.event.action == Shared::Combat::Action::ThrowRelease;
            });
            if (state->selectedWeapon != it->second.weaponId && !releaseQueued) {
                if (human->GrenadeCharging()) {
                    if (auto *mission = SDK::Core::Mission::Get(); mission && mission->Game()) {
                        ReplayNativeGrenadeCancel(*human, mission->Game()->GameTime());
                    }
                }
                it = _remoteGrenadeCharges.erase(it);
                continue;
            }
            if (!human->GrenadeCharging()) {
                it = _remoteGrenadeCharges.erase(it);
                continue;
            }
            ReplayNativeGrenadeHold(*human);
            ++it;
        }
    }

    void CombatService::OnNativeMeleeStart(SDK::Player::NativeHuman &human) {
        if (!_world || !_world->IsReady()) {
            return;
        }
        const auto *state = GetLocalState(*_world);
        if (!state || !state->spawned || !state->alive || &human.Actor() != SDK::Player::CurrentPlayer() || human.SelectedWeapon() != state->selectedWeapon) {
            return;
        }
        const auto direction = SDK::Player::WorldDirection(&human.Actor());
        Submit(state->networkId, Shared::Combat::Action::MeleeStart, state->selectedWeapon, 0, 0, direction.x, direction.y, direction.z);
    }

    void CombatService::OnNativeGrenadeStart(SDK::Player::NativeHuman &human) {
        if (!_world || !_world->IsReady() || &human.Actor() != SDK::Player::CurrentPlayer()) {
            return;
        }
        const auto *state = GetLocalState(*_world);
        if (!state || !state->alive || (state->selectedWeapon != 5 && state->selectedWeapon != 15)
            || human.SelectedWeapon() != state->selectedWeapon) {
            return;
        }
        const auto handle = _world->NativeObjects().FindByNetwork(state->networkId);
        if (_world->NativeObjects().Resolve(handle) != &human.Actor()) {
            return;
        }
        const auto direction = SDK::Player::WorldDirection(&human.Actor());
        if (Submit(state->networkId, Shared::Combat::Action::ThrowStart, state->selectedWeapon, 0, 0, direction.x, direction.y, direction.z)) {
            _localGrenadeCharge = {state->missionGeneration, state->spawnGeneration, handle.generation, state->selectedWeapon, std::chrono::steady_clock::now()};
            _localGrenadeNetworkId = state->networkId;
        }
    }

    void CombatService::OnNativeGrenadeRelease(SDK::Player::NativeHuman &human, uint16_t chargeMs, const SDK::Player::Vector3 &direction) {
        if (!_world || !_world->IsReady() || &human.Actor() != SDK::Player::CurrentPlayer()) {
            return;
        }
        const auto *state = GetLocalState(*_world);
        if (!state || !state->alive || state->networkId != _localGrenadeNetworkId || state->missionGeneration != _localGrenadeCharge.missionGeneration
            || state->spawnGeneration != _localGrenadeCharge.spawnGeneration || state->selectedWeapon != _localGrenadeCharge.weaponId) {
            _localGrenadeNetworkId = 0;
            return;
        }
        const auto handle = _world->NativeObjects().FindByNetwork(state->networkId);
        if (_world->NativeObjects().Resolve(handle) == &human.Actor() && handle.generation == _localGrenadeCharge.nativeGeneration
            && chargeMs >= Shared::Combat::kMinimumThrowHoldMs) {
            _submitThrowHoldMs = chargeMs;
            if (Submit(state->networkId, Shared::Combat::Action::ThrowRelease, state->selectedWeapon, 0, 0, direction.x, direction.y, direction.z)) {
                _localGrenadeReleasedAt = std::chrono::steady_clock::now();
            }
            _submitThrowHoldMs = 0;
        }
        _localGrenadeNetworkId = 0;
    }

    void CombatService::OnNativeGrenadeCancel(SDK::Player::NativeHuman &human) {
        if (!_world || !_world->IsReady() || &human.Actor() != SDK::Player::CurrentPlayer()) {
            return;
        }
        const auto *state = GetLocalState(*_world);
        if (state && state->networkId == _localGrenadeNetworkId && state->missionGeneration == _localGrenadeCharge.missionGeneration
            && state->spawnGeneration == _localGrenadeCharge.spawnGeneration) {
            const auto direction = SDK::Player::WorldDirection(&human.Actor());
            Submit(state->networkId, Shared::Combat::Action::ThrowCancel, _localGrenadeCharge.weaponId, 0, 0, direction.x, direction.y, direction.z);
        }
        _localGrenadeNetworkId = 0;
        _localGrenadeReleasedAt = {};
    }

    void CombatService::OnNativeMeleeCancel(SDK::Player::NativeHuman &human) {
        if (!_world || !_world->IsReady()) {
            return;
        }
        const auto *state = GetLocalState(*_world);
        if (!state || !state->spawned || !state->alive || &human.Actor() != SDK::Player::CurrentPlayer()) {
            return;
        }
        const auto direction = SDK::Player::WorldDirection(&human.Actor());
        Submit(state->networkId, Shared::Combat::Action::MeleeCancel, state->selectedWeapon, 0, 0, direction.x, direction.y, direction.z);
    }

    void CombatService::OnNativeMelee(SDK::Player::NativeHuman &human, int8_t index, uint16_t holdMs) {
        if (!_world || !_world->IsReady()) {
            return;
        }
        const auto *state = GetLocalState(*_world);
        if (!state || !state->spawned || !state->alive || &human.Actor() != SDK::Player::CurrentPlayer()) {
            return;
        }
        const auto direction = SDK::Player::WorldDirection(&human.Actor());
        _submitMeleeIndex = index;
        _submitMeleeHoldMs = holdMs;
        const bool sent = Submit(state->networkId, Shared::Combat::Action::Melee, state->selectedWeapon, 0, 0, direction.x, direction.y, direction.z);
        _submitMeleeIndex = 0;
        _submitMeleeHoldMs = 0;
        if (sent) {
            _lastLocalMeleeSequence = _sequence;
            _lastLocalMeleeAt       = std::chrono::steady_clock::now();
        }
    }

    void CombatService::OnNativeDrop(SDK::Player::NativeHuman &human, uint8_t weaponId) {
        if (!_world || !_world->IsReady() || weaponId < 2 || weaponId > 15 || &human.Actor() != SDK::Player::CurrentPlayer() || human.Health() <= 0.0f) {
            return;
        }
        const auto *state = GetLocalState(*_world);
        if (!state || !state->alive || (state->inventoryMask & (1u << weaponId)) == 0) {
            return;
        }
        const auto handle = _world->NativeObjects().FindByNetwork(state->networkId);
        if (_world->NativeObjects().Resolve(handle) != &human.Actor()) {
            return;
        }
        if (_localAction.sentAt != std::chrono::steady_clock::time_point {} && _localAction.action == Shared::Combat::Action::Drop && _localAction.weaponId == weaponId) {
            return;
        }
        if (Submit(state->networkId, Shared::Combat::Action::Drop, weaponId, 0, 0, 0.0f, 0.0f, 0.0f)) {
            _localAction.networkId         = state->networkId;
            _localAction.missionGeneration = state->missionGeneration;
            _localAction.spawnGeneration   = state->spawnGeneration;
            _localAction.nativeGeneration  = handle.generation;
            _localAction.baseRevision      = state->revision;
            _localAction.action            = Shared::Combat::Action::Drop;
            _localAction.weaponId          = weaponId;
            _localAction.sentAt            = std::chrono::steady_clock::now();
        }
    }

    bool CombatService::IsRemoteNetworkHuman(SDK::Player::NativeActor &actor) const {
        return _world && _world->IsReady() && &actor != SDK::Player::CurrentPlayer() && _world->NativeObjects().FindByNative(&actor).networkId != 0;
    }

    void CombatService::OnRemoteGrenadeNotify(SDK::Player::NativeActor &actor) {
        if (_world && _world->IsReady()) {
            const auto networkId = _world->NativeObjects().FindByNative(&actor).networkId;
            _remoteGrenadeReleases.erase(networkId);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Remote grenade hand release completed: player {}", networkId);
        }
    }

    bool CombatService::HasLocalThrowRelease() const {
        if (!_world || !_world->IsReady()) {
            return false;
        }
        const auto *state = GetLocalState(*_world);
        auto *player      = SDK::Player::CurrentPlayer();
        return state && state->alive && player && (state->selectedWeapon == 5 || state->selectedWeapon == 15)
            && _localGrenadeReleasedAt != std::chrono::steady_clock::time_point {}
            && std::chrono::steady_clock::now() - _localGrenadeReleasedAt <= std::chrono::seconds(3);
    }

    void CombatService::OnNativeThrow(SDK::Combat::NativeGrenade *grenade, int32_t type, const SDK::Player::Vector3 &position, const SDK::Player::Vector3 &impulse) {
        const auto *state = _world ? GetLocalState(*_world) : nullptr;
        const float speed = std::sqrt(impulse.x * impulse.x + impulse.y * impulse.y + impulse.z * impulse.z);
        if (!state || !(speed > 0.0001f)) {
            return;
        }
        const SubmitThrow submit {static_cast<uint8_t>(type), position.x, position.y, position.z, impulse.x, impulse.y, impulse.z};
        _submitThrow = &submit;
        const bool sent = Submit(state->networkId, Shared::Combat::Action::Throw, state->selectedWeapon, 0, 0, impulse.x / speed, impulse.y / speed, impulse.z / speed);
        _submitThrow = nullptr;
        if (sent) {
            _localGrenades[grenade] = {state->networkId, state->missionGeneration, state->spawnGeneration, _sequence};
            const auto handle = _world->NativeObjects().FindByNetwork(state->networkId);
            _localAction.networkId         = state->networkId;
            _localAction.missionGeneration = state->missionGeneration;
            _localAction.spawnGeneration   = state->spawnGeneration;
            _localAction.nativeGeneration  = handle.generation;
            _localAction.baseRevision      = state->revision;
            _localAction.action            = Shared::Combat::Action::Throw;
            _localAction.weaponId          = state->selectedWeapon;
            _localAction.loaded            = state->ammo[state->selectedWeapon].loaded;
            _localAction.sentAt            = std::chrono::steady_clock::now();
            _localGrenadeReleasedAt = {};
        }
    }

    void CombatService::OnNativeDetonation(SDK::Combat::NativeGrenade *grenade, const SDK::Player::Vector3 &position) {
        const auto local = _localGrenades.find(grenade);
        if (local == _localGrenades.end()) {
            return;
        }
        Shared::Combat::DetonationReport report;
        report.networkId         = local->second.networkId;
        report.missionGeneration = local->second.missionGeneration;
        report.spawnGeneration   = local->second.spawnGeneration;
        report.sequence          = local->second.sequence;
        report.x                 = position.x;
        report.y                 = position.y;
        report.z                 = position.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
        _localGrenades.erase(local);
    }

    void CombatService::OnGrenadeDestroyed(SDK::Combat::NativeGrenade *grenade) {
        _localGrenades.erase(grenade);
        for (auto it = _remoteGrenades.begin(); it != _remoteGrenades.end();) {
            if (it->second == grenade) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info(
                    "Remote grenade display destroyed: player {}, sequence {}", it->first.first, it->first.second);
                it = _remoteGrenades.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void CombatService::OnDetonation(const Shared::Combat::Detonation &detonation) {
        if (!_world || !_world->IsReady() || detonation.missionGeneration != _world->LoadedMissionGeneration()) {
            return;
        }
        const auto *local = GetLocalState(*_world);
        const bool mine   = local && local->networkId == detonation.throwerNetworkId;
        const auto copy   = _remoteGrenades.find({detonation.throwerNetworkId, detonation.sequence});
        if (copy != _remoteGrenades.end()) {
            auto *grenade = copy->second;
            _remoteGrenades.erase(copy);
            if (auto *mission = SDK::Core::Mission::Get(); mission && mission->Game()) {
                mission->Game()->RemoveTemporaryActor(&grenade->Actor());
            }
        }
        // The thrower's own client already played its native detonation.
        if (!mine && !detonation.visualOnly) {
            ReplayDetonation(detonation.grenadeType, {detonation.x, detonation.y, detonation.z});
        }
    }

    std::optional<uint32_t> CombatService::OpenLocalMelee() const {
        if (_lastLocalMeleeSequence == 0 || std::chrono::steady_clock::now() - _lastLocalMeleeAt > std::chrono::milliseconds(1700)) {
            return std::nullopt;
        }
        return _lastLocalMeleeSequence;
    }

    void CombatService::OnNativeShot(SDK::Player::NativeHuman &human, const NativeShotData &shot) {
        if (!_world || !_world->IsReady()) {
            return;
        }
        const auto *state = GetLocalState(*_world);
        const auto weaponId = human.SelectedWeapon();
        if (!state || !state->spawned || !state->alive || weaponId >= Shared::Combat::kWeaponSlots) {
            return;
        }
        const float length = std::sqrt(shot.directionX * shot.directionX + shot.directionY * shot.directionY + shot.directionZ * shot.directionZ);
        if (!std::isfinite(length) || length < 0.001f) {
            return;
        }
        if (Submit(state->networkId, Shared::Combat::Action::Fire, static_cast<uint8_t>(weaponId), 0, 0, shot.directionX / length, shot.directionY / length, shot.directionZ / length, false, false, 0.0f, 0.0f, 0.0f, &shot)) {
            _lastLocalFireSequence = _sequence;
            LocalFire fire;
            fire.sequence = _sequence;
            fire.missionGeneration = state->missionGeneration;
            fire.spawnGeneration = state->spawnGeneration;
            fire.originX = shot.originX;
            fire.originY = shot.originY;
            fire.originZ = shot.originZ;
            fire.pelletCount = shot.pelletCount;
            fire.pellets = shot.pellets;
            fire.sentAt = std::chrono::steady_clock::now();
            _localFires.push_back(fire);
            while (_localFires.size() > 64) {
                _localFires.pop_front();
            }
        }
    }

    std::optional<CombatService::LocalPelletMatch> CombatService::MatchLocalPellet(float directionX, float directionY, float directionZ, float hitX, float hitY, float hitZ) {
        if (!_world || !_world->IsReady()) {
            return std::nullopt;
        }
        const auto *state = GetLocalState(*_world);
        if (!state || !state->alive) {
            return std::nullopt;
        }
        const auto now = std::chrono::steady_clock::now();
        while (!_localFires.empty() && (now - _localFires.front().sentAt > std::chrono::seconds(2) || _localFires.front().missionGeneration != state->missionGeneration || _localFires.front().spawnGeneration != state->spawnGeneration)) {
            _localFires.pop_front();
        }
        const float suppliedLength = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);
        if (!std::isfinite(suppliedLength) || suppliedLength < 0.001f || !std::isfinite(hitX) || !std::isfinite(hitY) || !std::isfinite(hitZ)) {
            return std::nullopt;
        }
        float bestScore = 1000000.0f;
        LocalFire *bestFire = nullptr;
        uint8_t bestIndex = 0;
        for (auto &fire : _localFires) {
            for (uint8_t index = 0; index < fire.pelletCount; ++index) {
                if ((fire.reportedPellets & (1u << index)) != 0) {
                    continue;
                }
                const auto &pellet = fire.pellets[index];
                const float length = std::sqrt(pellet.x * pellet.x + pellet.y * pellet.y + pellet.z * pellet.z);
                if (!std::isfinite(length) || length < 0.001f) {
                    continue;
                }
                const float dx = pellet.x / length;
                const float dy = pellet.y / length;
                const float dz = pellet.z / length;
                const float alignment = (dx * directionX + dy * directionY + dz * directionZ) / suppliedLength;
                if (alignment < 0.995f) {
                    continue;
                }
                const float px = hitX - fire.originX;
                const float py = hitY - fire.originY;
                const float pz = hitZ - fire.originZ;
                const float along = px * dx + py * dy + pz * dz;
                if (along < -0.5f || along > length + 1.0f) {
                    continue;
                }
                const float lx = px - dx * along;
                const float ly = py - dy * along;
                const float lz = pz - dz * along;
                const float lateral = std::sqrt(lx * lx + ly * ly + lz * lz);
                if (lateral > 1.5f) {
                    continue;
                }
                const float score = (1.0f - alignment) * 100.0f + lateral;
                if (score < bestScore) {
                    bestScore = score;
                    bestFire = &fire;
                    bestIndex = index;
                }
            }
        }
        if (!bestFire) {
            return std::nullopt;
        }
        bestFire->reportedPellets |= static_cast<uint16_t>(1u << bestIndex);
        return LocalPelletMatch {bestFire->sequence, bestIndex};
    }

    void CombatService::OnState(const Shared::Combat::State &state) {
        if (state.networkId == 0 || state.selectedWeapon >= Shared::Combat::kWeaponSlots) {
            return;
        }
        auto it = _states.find(state.networkId);
        if (it != _states.end()) {
            const auto &old = it->second;
            if (state.missionGeneration < old.missionGeneration || (state.missionGeneration == old.missionGeneration && state.spawnGeneration < old.spawnGeneration)
                || (state.missionGeneration == old.missionGeneration && state.spawnGeneration == old.spawnGeneration && state.revision <= old.revision)) {
                return;
            }
        }
        _states[state.networkId] = state;
    }

    void CombatService::OnEvent(const Shared::Combat::Event &event) {
        if (event.networkId == 0 || event.missionGeneration == 0 || event.spawnGeneration == 0 || event.revision == 0) {
            return;
        }
        const EventKey key {event.networkId, event.missionGeneration, event.spawnGeneration, event.revision};
        if (!_seenEvents.insert(key).second) {
            return;
        }
        _seenOrder.push_back(key);
        if (_seenOrder.size() > 512) {
            _seenEvents.erase(_seenOrder.front());
            _seenOrder.pop_front();
        }
        _events.push_back({event, std::chrono::steady_clock::now()});
        if (_events.size() > 256) {
            _events.pop_front();
        }
    }

    const Shared::Combat::State *CombatService::GetState(uint64_t networkId) const {
        const auto it = _states.find(networkId);
        return it == _states.end() ? nullptr : &it->second;
    }

    const Shared::Combat::State *CombatService::GetLocalState(const World::WorldService &world) const {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || !world.IsReady()) {
            return nullptr;
        }
        const auto myGuid                   = static_cast<uint64_t>(replication->GetMyGUID());
        const Shared::Combat::State *result = nullptr;
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (player->controllerGuid == myGuid) {
                auto *state = GetState(player->GetNetworkID());
                if (state && state->missionGeneration == world.LoadedMissionGeneration() && state->spawnGeneration == player->spawnGeneration) {
                    result = state;
                }
            }
        });
        return result;
    }

    bool CombatService::SubmitAim(uint64_t networkId, uint8_t weaponId, bool aiming, float directionX, float directionY, float directionZ, float poseTargetOffsetX, float poseTargetOffsetY, float poseTargetOffsetZ) {
        return Submit(networkId, Shared::Combat::Action::Aim, weaponId, 0, 0, directionX, directionY, directionZ, aiming, false, poseTargetOffsetX, poseTargetOffsetY, poseTargetOffsetZ);
    }

    bool CombatService::Submit(uint64_t networkId, Shared::Combat::Action action, uint8_t weaponId, uint64_t targetNetworkId, uint64_t targetSpawnGeneration, float directionX, float directionY, float directionZ, bool aiming, bool crouching,
                               float poseTargetOffsetX, float poseTargetOffsetY, float poseTargetOffsetZ, const NativeShotData *shot) {
        const auto *state = GetState(networkId);
        if (!state || !state->spawned || !state->alive) {
            return false;
        }
        Shared::Combat::Intent intent;
        intent.networkId             = networkId;
        intent.missionGeneration     = state->missionGeneration;
        intent.spawnGeneration       = state->spawnGeneration;
        intent.sequence              = ++_sequence;
        intent.action                = action;
        intent.weaponId              = weaponId;
        intent.aiming                = aiming;
        intent.crouching             = crouching;
        intent.targetNetworkId       = targetNetworkId;
        intent.targetSpawnGeneration = targetSpawnGeneration;
        intent.directionX            = directionX;
        intent.directionY            = directionY;
        intent.directionZ            = directionZ;
        intent.poseTargetOffsetX      = poseTargetOffsetX;
        intent.poseTargetOffsetY      = poseTargetOffsetY;
        intent.poseTargetOffsetZ      = poseTargetOffsetZ;
        intent.meleeIndex             = _submitMeleeIndex;
        intent.meleeHoldMs            = _submitMeleeHoldMs;
        intent.throwHoldMs            = _submitThrowHoldMs;
        if (action == Shared::Combat::Action::Throw) {
            if (!_submitThrow) {
                return false;
            }
            intent.grenadeType      = _submitThrow->type;
            intent.originX          = _submitThrow->originX;
            intent.originY          = _submitThrow->originY;
            intent.originZ          = _submitThrow->originZ;
            intent.nativeDirectionX = _submitThrow->impulseX;
            intent.nativeDirectionY = _submitThrow->impulseY;
            intent.nativeDirectionZ = _submitThrow->impulseZ;
        }
        if (action == Shared::Combat::Action::Fire) {
            if (!shot || !shot->captured) {
                return false;
            }
            intent.originX = shot->originX;
            intent.originY = shot->originY;
            intent.originZ = shot->originZ;
            intent.nativeDirectionX = shot->directionX;
            intent.nativeDirectionY = shot->directionY;
            intent.nativeDirectionZ = shot->directionZ;
            intent.pelletCount = shot->pelletCount;
            intent.scatterRandom = shot->scatterRandom;
            intent.pellets = shot->pellets;
        }
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(intent);
        return true;
    }

    bool CombatService::PopEvent(Shared::Combat::Event &event) {
        if (_events.empty()) {
            return false;
        }
        event = _events.front().event;
        _events.pop_front();
        return true;
    }
} // namespace Mafia1Online::Features::Combat
