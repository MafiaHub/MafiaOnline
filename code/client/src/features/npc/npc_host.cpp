#include <utils/safe_win32.h>

#include "npc_host.h"

#include "gangster_spawn.h"

#include "features/combat/combat_hooks.h"
#include "features/death/death_service.h"
#include "features/world/world_service.h"
#include "shared/features/player/player_entity.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_death.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/police/native_police_manager.h>
#include <mafia1/sdk/world/native_using_object.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <cmath>

namespace Mafia1Online::Features::Npc {
    namespace {
        using SDK::Player::NativeActor;
        using SDK::Player::NativeHuman;
        using SDK::Player::Vector3;

        using TickDoRiciActors = char(__thiscall *)(SDK::Police::NativePoliceManager *, bool);
        TickDoRiciActors gTickDoRiciOriginal = nullptr;
        NpcHost *gHost                       = nullptr;

        // Far beyond any map: from here the retirement pass sees every entity
        // left in its list as out of the keep distance.
        constexpr float kFarAway = 1.0e6f;

        NativeHuman &AsHuman(void *actor) {
            return *static_cast<NativeHuman *>(actor);
        }

        char __fastcall TickDoRiciHook(SDK::Police::NativePoliceManager *police, void *, bool force) {
            if (force) {
                return gTickDoRiciOriginal(police, force);
            }
            return gHost->TickRetirement(police, gTickDoRiciOriginal);
        }

        uint16_t Ammo(int32_t value) {
            return static_cast<uint16_t>(value < 0 ? 0 : value > 0xffff ? 0xffff : value);
        }

        Vector3 Unit(const Vector3 &direction, const Vector3 &fallback) {
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
            return length > 0.0001f ? Vector3 {direction.x / length, direction.y / length, direction.z / length} : fallback;
        }
    } // namespace

    Shared::Npc::Inventory ReadInventory(const NativeHuman &human) {
        Shared::Npc::Inventory inventory;
        human.Inventory().ForEachItem([&](const SDK::Player::NativeGameItem &item) {
            if (item.itemId < Shared::Combat::kWeaponSlots) {
                inventory.mask |= 1u << item.itemId;
                inventory.ammo[item.itemId] = {Ammo(item.ammoLoaded), Ammo(item.ammoReserve)};
            }
        });
        const auto selected = human.SelectedWeapon();
        inventory.selected = selected < Shared::Combat::kWeaponSlots ? static_cast<uint8_t>(selected) : 0;
        return inventory;
    }

    bool NpcHost::Install() {
        gHost = this;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Police::kTickDoRiciActors), reinterpret_cast<void *>(&TickDoRiciHook), reinterpret_cast<void **>(&gTickDoRiciOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(SDK::Police::kTickDoRiciActors)) != MH_OK || !InstallGangsterSpawn(*this)) {
            Uninstall();
            return false;
        }
        return true;
    }

    void NpcHost::Uninstall() {
        UninstallGangsterSpawn();
        MH_DisableHook(reinterpret_cast<void *>(SDK::Police::kTickDoRiciActors));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Police::kTickDoRiciActors));
        Reset();
        gHost = nullptr;
    }

    void NpcHost::RegisterRPC(World::WorldService &world) {
        _world = &world;
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Shared::Npc::Hit>([this](const Shared::Npc::Hit &hit, MafiaNet::Packet *) {
            if (hit.missionGeneration != _missionGeneration || !_world->IsReady() || !std::isfinite(hit.damage) || hit.bodyPart > 6) {
                return;
            }
            for (auto &[native, tracked] : _npcs) {
                if (tracked.key != hit.key) {
                    continue;
                }
                auto *attacker = static_cast<NativeActor *>(_world->NativeObjects().Resolve(_world->NativeObjects().FindByNetwork(hit.attackerId)));
                // Retail C_human::Collision enters a car impact as HIT_TYPE_SNIPER.
                const auto type = hit.hitType == 1 ? SDK::Player::NativeHitType::HardKnockdown :
                                  hit.hitType == 4 ? SDK::Player::NativeHitType::Sniper : SDK::Player::NativeHitType::Generic;
                const float before = AsHuman(native).Health();
                Death::ApplyAuthoritativeHit(AsHuman(native), type, {hit.directionX, hit.directionY, hit.directionZ}, {hit.x, hit.y, hit.z}, hit.damage, attacker, hit.bodyPart);
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("NPC {} hit by {} (type {}, part {}, damage {:.1f}): health {:.1f} -> {:.1f}", tracked.key, hit.attackerId,
                                                                           hit.hitType, hit.bodyPart, hit.damage, before, AsHuman(native).Health());
                return;
            }
        });
        network->RegisterRPC<Shared::Npc::Remove>([this](const Shared::Npc::Remove &remove, MafiaNet::Packet *) {
            if (remove.missionGeneration != _missionGeneration) {
                return;
            }
            for (auto &[actor, tracked] : _npcs) {
                if (tracked.key == remove.key) {
                    tracked.removeRequested = true;
                }
            }
        });
    }

    void NpcHost::Reset() {
        _npcs.clear();
        _observers.clear();
    }

    void NpcHost::Track(NativeActor *actor, Shared::Entities::NpcKind kind, const std::string &model) {
        if (!_missionGeneration) {
            return;
        }
        auto &human = AsHuman(actor);
        const auto position = SDK::Player::WorldPosition(actor);
        const auto direction = SDK::Player::WorldDirection(actor);
        Tracked tracked;
        tracked.key = _nextKey++;
        tracked.lastPosition = position;
        tracked.inventory = ReadInventory(human);
        _npcs[actor] = tracked;

        Shared::Npc::Spawned spawned;
        spawned.missionGeneration = _missionGeneration;
        spawned.key = tracked.key;
        spawned.kind = kind;
        spawned.model = model;
        spawned.x = position.x;
        spawned.y = position.y;
        spawned.z = position.z;
        spawned.yaw = std::atan2(direction.x, direction.z);
        spawned.health = human.Health();
        spawned.inventory = tracked.inventory;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(spawned);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("NPC {} ({}, {}) joined with weapon {}, health {:.0f}", tracked.key, Shared::Entities::NpcKindName(kind), model,
                                                                   tracked.inventory.selected, spawned.health);
    }

    void NpcHost::OnNativeDestroyed(void *actor) {
        const auto found = _npcs.find(actor);
        if (found == _npcs.end()) {
            return;
        }
        Shared::Npc::Retired retired;
        retired.missionGeneration = _missionGeneration;
        retired.key = found->second.key;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(retired);
        _npcs.erase(found);
    }

    void NpcHost::OnNativeDrop(NativeHuman &human, const SDK::World::NativeItemVector &items) {
        const auto found = _npcs.find(&human.Actor());
        if (found == _npcs.end() || !items.begin) {
            return;
        }
        Shared::Npc::Dropped dropped;
        dropped.missionGeneration = _missionGeneration;
        dropped.key = found->second.key;
        for (const auto *item = items.begin; item != items.end && dropped.count < Shared::Npc::Dropped::kMaxItems; ++item) {
            if (item->itemId != 0 && item->itemId < Shared::Combat::kWeaponSlots) {
                dropped.items[dropped.count++] = {static_cast<uint8_t>(item->itemId), Ammo(item->ammoLoaded), Ammo(item->ammoHidden)};
            }
        }
        if (dropped.count) {
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(dropped, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    bool NpcHost::NearObserver(const Vector3 &position) const {
        for (const auto &observer : _observers) {
            const float dx = observer.x - position.x;
            const float dz = observer.z - position.z;
            if (dx * dx + dz * dz < SDK::Police::kDoRiciKeepDistanceSq) {
                return true;
            }
        }
        return false;
    }

    char NpcHost::TickRetirement(SDK::Police::NativePoliceManager *police, TickDoRiciActors original) {
        // Every actor near a player leaves the list for this pass; the native
        // pass then runs from far away, so it retires exactly the rest with
        // its own sequence, and the kept actors return afterwards.
        auto **begin = police->RetirementBegin();
        const size_t count = police->RetirementCount();
        std::vector<NativeActor *> kept;
        size_t retiring = 0;
        for (size_t i = 0; i < count; ++i) {
            NativeActor *actor = begin[i];
            const auto tracked = _npcs.find(actor);
            const bool removeRequested = tracked != _npcs.end() && tracked->second.removeRequested;
            if (!removeRequested && NearObserver(actor->Frame()->WorldPosition())) {
                kept.push_back(actor);
            }
            else {
                begin[retiring++] = actor;
            }
        }
        if (retiring == 0) {
            return 0;
        }
        police->TruncateRetirement(retiring);
        auto *camera = SDK::Core::Mission::Get()->GetScene()->ActiveCamera();
        const auto cameraPosition = camera->WorldPosition();
        camera->SetWorldPosition({kFarAway, cameraPosition.y, kFarAway});
        camera->Update();
        const char result = original(police, false);
        camera->SetWorldPosition(cameraPosition);
        camera->Update();
        for (auto *actor : kept) {
            police->AddRetirement(actor);
        }
        return result;
    }

    void NpcHost::Update(World::WorldService &world) {
        const uint64_t generation = world.LoadedMissionGeneration();
        if (generation != _missionGeneration) {
            _npcs.clear();
            _nextKey = 1;
            _missionGeneration = generation;
        }
        _observers.clear();
        if (auto *replication = Framework::CoreModules::GetReplication(); replication && world.IsReady()) {
            replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
                if (player->spawned && player->missionGeneration == generation) {
                    _observers.push_back({player->position.x, player->position.y, player->position.z});
                }
            });
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastSnapshot < kSnapshotInterval || !world.IsReady()) {
            return;
        }
        const float seconds = _lastSnapshot == std::chrono::steady_clock::time_point {} ? 0.0f : std::chrono::duration<float>(now - _lastSnapshot).count();
        _lastSnapshot = now;
        SendSnapshots(seconds);
    }

    void NpcHost::SendInventory(Tracked &tracked, const NativeHuman &human, bool dead) {
        const auto inventory = ReadInventory(human);
        if (inventory.Same(tracked.inventory)) {
            return;
        }
        auto *network = Framework::CoreModules::GetNetworkPeer();
        // Do_Reload moves reserve rounds of the selected weapon into it.
        const uint8_t weapon = inventory.selected;
        if (!dead && weapon != 0 && weapon == tracked.inventory.selected && inventory.ammo[weapon].reserve < tracked.inventory.ammo[weapon].reserve) {
            Shared::Npc::Reload reload;
            reload.missionGeneration = _missionGeneration;
            reload.key = tracked.key;
            reload.weaponId = weapon;
            network->BroadcastRPC(reload, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        Shared::Npc::InventoryChanged changed;
        changed.missionGeneration = _missionGeneration;
        changed.key = tracked.key;
        changed.inventory = inventory;
        network->BroadcastRPC(changed, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        tracked.inventory = inventory;
    }

    void NpcHost::SendSnapshots(float seconds) {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        Shared::Npc::Snapshot snapshot;
        snapshot.missionGeneration = _missionGeneration;
        for (auto &[native, tracked] : _npcs) {
            auto *actor = static_cast<NativeActor *>(native);
            auto &human = AsHuman(native);
            const auto position = SDK::Player::WorldPosition(actor);
            const auto direction = SDK::Player::WorldDirection(actor);
            auto &pose = snapshot.npcs[snapshot.count++];
            pose.key = tracked.key;
            pose.x = position.x;
            pose.y = position.y;
            pose.z = position.z;
            pose.yaw = std::atan2(direction.x, direction.z);
            const float dx = position.x - tracked.lastPosition.x;
            const float dz = position.z - tracked.lastPosition.z;
            pose.speed = seconds > 0.0f ? std::sqrt(dx * dx + dz * dz) / seconds : 0.0f;
            tracked.lastPosition = position;
            pose.locomotion = static_cast<int16_t>(human.AnimationState());
            pose.health = human.Health();
            pose.dead = actor->IsDead() || human.Health() <= 0.0f;
            pose.aiming = human.IsAiming();
            pose.crouching = human.IsCrouching();
            Vector3 target {};
            if (pose.aiming) {
                target = human.ShootTarget();
            }
            else if (auto *neck = actor->Frame()->FindChildFrame("neck")) {
                // PoseSetPoseAimed bends the back and neck toward the target
                // from the neck; this one keeps them straight.
                const auto neckPosition = neck->WorldPosition();
                target = {neckPosition.x + 10.0f * direction.x, neckPosition.y + 10.0f * direction.y, neckPosition.z + 10.0f * direction.z};
            }
            else {
                target = {position.x + 10.0f * direction.x, position.y, position.z + 10.0f * direction.z};
            }
            pose.poseTargetX = target.x - position.x;
            pose.poseTargetY = target.y - position.y;
            pose.poseTargetZ = target.z - position.z;
            SendInventory(tracked, human, pose.dead);
            if (snapshot.count == Shared::Npc::Snapshot::kMaxNpcs) {
                network->BroadcastRPC(snapshot, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
                snapshot.count = 0;
            }
        }
        if (snapshot.count) {
            network->BroadcastRPC(snapshot, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
        }
    }

    void NpcHost::OnShot(NativeHuman &human, const Combat::NativeShotData &shot) {
        const auto found = _npcs.find(&human.Actor());
        if (found == _npcs.end()) {
            return;
        }
        const float length = std::sqrt(shot.directionX * shot.directionX + shot.directionY * shot.directionY + shot.directionZ * shot.directionZ);
        if (!std::isfinite(length) || length < 0.001f) {
            return;
        }
        Shared::Npc::Fire fire;
        fire.missionGeneration = _missionGeneration;
        fire.key = found->second.key;
        fire.weaponId = static_cast<uint8_t>(human.SelectedWeapon());
        fire.directionX = shot.directionX / length;
        fire.directionY = shot.directionY / length;
        fire.directionZ = shot.directionZ / length;
        fire.originX = shot.originX;
        fire.originY = shot.originY;
        fire.originZ = shot.originZ;
        fire.nativeDirectionX = shot.directionX;
        fire.nativeDirectionY = shot.directionY;
        fire.nativeDirectionZ = shot.directionZ;
        fire.pelletCount = shot.pelletCount;
        fire.scatterRandom = shot.scatterRandom;
        fire.pellets = shot.pellets;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(fire, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void NpcHost::OnHitPlayer(World::WorldService &world, NativeHuman &target, SDK::Player::NativeHitType type, const Vector3 &direction, float damage, NativeActor *attacker,
                              uint32_t bodyPart) {
        const auto found = _npcs.find(attacker);
        if (found == _npcs.end() || bodyPart > 6 || (type != SDK::Player::NativeHitType::Generic && type != SDK::Player::NativeHitType::HardKnockdown)) {
            return;
        }
        const auto victim = world.NativeObjects().FindByNative(&target.Actor());
        auto *replication = Framework::CoreModules::GetReplication();
        auto *player = victim.networkId && replication ? replication->GetEntity<Shared::Entities::PlayerEntity>(victim.networkId) : nullptr;
        if (!player) {
            return;
        }
        // The melee sweep passes the swing segment, not a unit vector.
        const auto unit = Unit(direction, {0.0f, 0.0f, 1.0f});
        Shared::Npc::PlayerHit hit;
        hit.missionGeneration = _missionGeneration;
        hit.key = found->second.key;
        hit.targetNetworkId = victim.networkId;
        hit.targetSpawnGeneration = player->spawnGeneration;
        hit.hitType = static_cast<uint8_t>(type);
        hit.bodyPart = static_cast<uint8_t>(bodyPart);
        hit.damage = damage;
        hit.directionX = unit.x;
        hit.directionY = unit.y;
        hit.directionZ = unit.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(hit, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void NpcHost::OnHit(World::WorldService &world, NativeHuman &npc, SDK::Player::NativeHitType type, const Vector3 &direction, float damage, NativeActor *attacker,
                        uint32_t bodyPart, float healthBefore) {
        const auto found = _npcs.find(&npc.Actor());
        if (found == _npcs.end() || healthBefore <= 0.0f) {
            return;
        }
        const auto unit = Unit(direction, {0.0f, 0.0f, 1.0f});
        Shared::Npc::Damaged damaged;
        damaged.missionGeneration = _missionGeneration;
        damaged.key = found->second.key;
        damaged.attackerId = attacker ? world.NativeObjects().FindByNative(attacker).networkId : 0;
        damaged.hitType = static_cast<uint8_t>(type);
        damaged.bodyPart = static_cast<uint8_t>(bodyPart);
        damaged.damage = damage;
        damaged.healthBefore = healthBefore;
        damaged.healthAfter = npc.Health();
        damaged.directionX = unit.x;
        damaged.directionY = unit.y;
        damaged.directionZ = unit.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(damaged, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }
} // namespace Mafia1Online::Features::Npc
