#include "npc_service.h"

#include "features/combat/combat_service.h"
#include "features/player/player_service.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/combat/damage_event.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/npc/npc_report.h"
#include "shared/features/npc/npc_seized.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

namespace Mafia1Online::Features::Npc {
    namespace {
        using Shared::Entities::NpcEntity;

        bool Finite(float x, float y, float z) {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
        }

        void SetPose(NpcEntity &entity, float x, float y, float z, float yaw, float speed) {
            entity.position = {x, y, z};
            entity.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            entity.velocity = {std::sin(yaw) * speed, 0.0f, std::cos(yaw) * speed};
        }

        // Retail firearm item ids; the host only reports native shots.
        bool Firearm(uint8_t weaponId) {
            return weaponId >= 6 && weaponId <= 14;
        }

        uint16_t DeathAnimation(uint64_t networkId) {
            return 131 + static_cast<uint16_t>(networkId % 6);
        }

        void SetInventory(Shared::Combat::State &state, const Shared::Npc::Inventory &inventory) {
            state.inventoryMask = inventory.mask;
            state.selectedWeapon = inventory.selected;
            state.ammo = inventory.ammo;
        }

        bool SameInventory(const Shared::Combat::State &state, const Shared::Npc::Inventory &inventory) {
            Shared::Npc::Inventory current;
            current.mask = state.inventoryMask;
            current.selected = state.selectedWeapon;
            current.ammo = state.ammo;
            return current.Same(inventory);
        }
    } // namespace

    void NpcService::RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Combat::CombatService &combat) {
        _simulation = &simulation;
        _players = &players;
        _combat = &combat;
        _combat->SetNpcTargets({
            [this](uint64_t networkId) -> std::optional<Combat::CombatService::NpcTarget> {
                const auto key = _keyByNetworkId.find(networkId);
                if (key == _keyByNetworkId.end()) {
                    return std::nullopt;
                }
                const auto &npc = _npcs.at(key->second);
                return Combat::CombatService::NpcTarget {npc.entity->position, npc.entity->rotation, npc.state.health, npc.state.missionGeneration, npc.state.spawnGeneration,
                                                         npc.state.alive};
            },
            [this](uint64_t networkId, uint64_t attackerId, uint8_t hitType, uint8_t bodyPart, float damage, const glm::vec3 &direction, const glm::vec3 &position, uint8_t) {
                const auto key = _keyByNetworkId.find(networkId);
                const auto host = _simulation->Host();
                if (key == _keyByNetworkId.end() || !host) {
                    return;
                }
                Shared::Npc::Hit hit;
                hit.missionGeneration = _missionGeneration;
                hit.key = key->second;
                hit.attackerId = attackerId;
                hit.hitType = hitType;
                hit.bodyPart = bodyPart;
                hit.damage = damage;
                hit.directionX = direction.x;
                hit.directionY = direction.y;
                hit.directionZ = direction.z;
                hit.x = position.x;
                hit.y = position.y;
                hit.z = position.z;
                Framework::CoreModules::GetNetworkPeer()->SendRPC(hit, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            },
        });
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Shared::Npc::Spawned>([this](const Shared::Npc::Spawned &spawned, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), spawned.missionGeneration) || _npcs.contains(spawned.key) || _npcs.size() >= kMaxNpcs ||
                spawned.model.empty() || spawned.model.size() > NpcEntity::kMaxModelLength || !Finite(spawned.x, spawned.y, spawned.z) || !std::isfinite(spawned.yaw) ||
                !std::isfinite(spawned.health) || !spawned.inventory.Valid() || !Shared::Entities::KnownNpcKind(spawned.kind)) {
                return;
            }
            auto *entity = Framework::CoreModules::GetReplication()->CreateEntity<NpcEntity>();
            entity->model = spawned.model;
            entity->missionGeneration = spawned.missionGeneration;
            entity->kind = spawned.kind;
            entity->sourcePedestrianId = spawned.walkerKey && _walkerId ? _walkerId(spawned.walkerKey) : 0;
            if (const auto *car = spawned.kind == Shared::Entities::NpcKind::Driver && spawned.carId
                                      ? Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::CarEntity>(spawned.carId)
                                      : nullptr;
                car && car->missionGeneration == spawned.missionGeneration) {
                entity->thrownFromCarId = spawned.carId;
                // The host reports the driver right after Do_ThrowCocotFromCar.
                entity->thrownAt = std::chrono::steady_clock::now();
            }
            entity->alive = spawned.health > 0.0f;
            entity->streaming.range = kStreamRange;
            entity->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
            SetPose(*entity, spawned.x, spawned.y, spawned.z, spawned.yaw, 0.0f);
            auto &npc = _npcs[spawned.key];
            npc.entity = entity;
            npc.lastSeen = std::chrono::steady_clock::now();
            auto &state = npc.state;
            state.networkId = entity->GetNetworkID();
            state.missionGeneration = spawned.missionGeneration;
            state.spawnGeneration = NpcEntity::kSpawnGeneration;
            state.spawned = true;
            state.alive = entity->alive;
            state.health = std::max(spawned.health, 0.0f);
            state.deathAnimation = DeathAnimation(state.networkId);
            SetInventory(state, spawned.inventory);
            _keyByNetworkId[state.networkId] = spawned.key;
            Publish(npc);
            if (_events.spawn) {
                _events.spawn(*entity);
            }
        });
        network->RegisterRPC<Shared::Npc::Snapshot>([this](const Shared::Npc::Snapshot &snapshot, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), snapshot.missionGeneration)) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            for (uint8_t i = 0; i < snapshot.count; ++i) {
                const auto &pose = snapshot.npcs[i];
                auto *npc = FindKey(pose.key);
                if (!npc || !Finite(pose.x, pose.y, pose.z) || !std::isfinite(pose.yaw) || !std::isfinite(pose.speed) || !std::isfinite(pose.health) ||
                    !Finite(pose.poseTargetX, pose.poseTargetY, pose.poseTargetZ)) {
                    continue;
                }
                auto &entity = *npc->entity;
                auto &state = npc->state;
                npc->lastSeen = now;
                SetPose(entity, pose.x, pose.y, pose.z, pose.yaw, state.alive && !pose.dead ? pose.speed : 0.0f);
                entity.locomotion = pose.locomotion;
                if (!state.alive) {
                    continue;
                }
                const bool died = pose.dead || pose.health <= 0.0f;
                const glm::vec3 target(pose.poseTargetX, pose.poseTargetY, pose.poseTargetZ);
                const glm::vec3 previous(state.poseTargetOffsetX, state.poseTargetOffsetY, state.poseTargetOffsetZ);
                if (!died && pose.aiming == state.aiming && pose.crouching == state.crouching && pose.health == state.health && glm::distance(target, previous) <= 0.05f) {
                    continue;
                }
                state.aiming = pose.aiming;
                state.crouching = pose.crouching;
                state.health = std::max(pose.health, 0.0f);
                state.poseTargetOffsetX = target.x;
                state.poseTargetOffsetY = target.y;
                state.poseTargetOffsetZ = target.z;
                const float length = glm::length(target);
                const glm::vec3 aim = pose.aiming && length > 0.001f ? target / length : glm::vec3(0.0f);
                state.aimDirectionX = aim.x;
                state.aimDirectionY = aim.y;
                state.aimDirectionZ = aim.z;
                if (died) {
                    state.alive = false;
                    state.health = 0.0f;
                    state.aiming = false;
                    entity.alive = false;
                }
                Publish(*npc);
                if (died && _events.death) {
                    _events.death(entity, npc->lastAttackerId);
                }
            }
        });
        network->RegisterRPC<Shared::Npc::InventoryChanged>([this](const Shared::Npc::InventoryChanged &changed, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), changed.missionGeneration) || !changed.inventory.Valid()) {
                return;
            }
            auto *npc = FindKey(changed.key);
            if (!npc || SameInventory(npc->state, changed.inventory)) {
                return;
            }
            SetInventory(npc->state, changed.inventory);
            Publish(*npc);
        });
        network->RegisterRPC<Shared::Npc::Dropped>([this](const Shared::Npc::Dropped &dropped, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), dropped.missionGeneration)) {
                return;
            }
            auto *npc = FindKey(dropped.key);
            if (!npc) {
                return;
            }
            auto &state = npc->state;
            const auto &entity = *npc->entity;
            const glm::vec3 facing = entity.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            const glm::vec3 position = entity.position + glm::vec3(facing.x, 0.0f, facing.z) * 0.6f + glm::vec3(0.0f, 0.05f, 0.0f);
            const float yaw = std::atan2(facing.x, facing.z);
            std::vector<uint64_t> pickups;
            for (uint8_t i = 0; i < dropped.count; ++i) {
                const auto &item = dropped.items[i];
                const auto weapon = Combat::CombatService::Weapon(item.weaponId);
                // Only what the server's record holds leaves it, with no more
                // rounds than it had.
                if (!weapon || item.weaponId >= Shared::Combat::kWeaponSlots || (state.inventoryMask & (1u << item.weaponId)) == 0) {
                    continue;
                }
                const auto &held = state.ammo[item.weaponId];
                const bool melee = std::strcmp(weapon->kind, "melee") == 0;
                const uint16_t loaded = melee ? 1 : std::min(item.loaded, held.loaded);
                const uint16_t reserve = std::min(item.reserve, held.reserve);
                state.inventoryMask &= ~(1u << item.weaponId);
                state.ammo[item.weaponId] = {};
                if (state.selectedWeapon == item.weaponId) {
                    state.selectedWeapon = 0;
                }
                if (_drops.drop) {
                    if (const uint64_t pickupId = _drops.drop(item.weaponId, loaded, reserve, position, yaw)) {
                        pickups.push_back(pickupId);
                    }
                }
            }
            Publish(*npc);
            if (_drops.dropped) {
                for (const uint64_t pickupId : pickups) {
                    _drops.dropped(pickupId, state.networkId);
                }
            }
        });
        network->RegisterRPC<Shared::Npc::Fire>([this](const Shared::Npc::Fire &fire, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), fire.missionGeneration) || !Firearm(fire.weaponId) || fire.pelletCount == 0 ||
                fire.pelletCount > Shared::Combat::kMaximumShotPellets || !Finite(fire.originX, fire.originY, fire.originZ) ||
                !Finite(fire.nativeDirectionX, fire.nativeDirectionY, fire.nativeDirectionZ)) {
                return;
            }
            const float squared = fire.directionX * fire.directionX + fire.directionY * fire.directionY + fire.directionZ * fire.directionZ;
            auto *npc = FindKey(fire.key);
            if (!std::isfinite(squared) || squared < 0.8f || squared > 1.2f || !npc || !npc->state.alive || npc->state.selectedWeapon != fire.weaponId) {
                return;
            }
            // The round leaves now; the host's next inventory report confirms it.
            auto &ammo = npc->state.ammo[fire.weaponId];
            ammo.loaded = ammo.loaded > 0 ? ammo.loaded - 1 : 0;
            Publish(*npc);
            Shared::Combat::Event event;
            event.action = Shared::Combat::Action::Fire;
            event.weaponId = fire.weaponId;
            event.directionX = fire.directionX;
            event.directionY = fire.directionY;
            event.directionZ = fire.directionZ;
            event.originX = fire.originX;
            event.originY = fire.originY;
            event.originZ = fire.originZ;
            event.nativeDirectionX = fire.nativeDirectionX;
            event.nativeDirectionY = fire.nativeDirectionY;
            event.nativeDirectionZ = fire.nativeDirectionZ;
            event.pelletCount = fire.pelletCount;
            event.scatterRandom = fire.scatterRandom;
            event.pellets = fire.pellets;
            Emit(*npc, event);
        });
        network->RegisterRPC<Shared::Npc::Reload>([this](const Shared::Npc::Reload &reload, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), reload.missionGeneration) || !Firearm(reload.weaponId)) {
                return;
            }
            auto *npc = FindKey(reload.key);
            if (!npc || !npc->state.alive || npc->state.selectedWeapon != reload.weaponId) {
                return;
            }
            Publish(*npc);
            Shared::Combat::Event event;
            event.action = Shared::Combat::Action::Reload;
            event.weaponId = reload.weaponId;
            Emit(*npc, event);
        });
        network->RegisterRPC<Shared::Npc::PlayerHit>([this](const Shared::Npc::PlayerHit &hit, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), hit.missionGeneration)) {
                return;
            }
            auto *npc = FindKey(hit.key);
            if (!npc || !npc->state.alive) {
                return;
            }
            _combat->ApplyNpcHit(npc->state.networkId, hit.targetNetworkId, hit.missionGeneration, hit.targetSpawnGeneration, hit.hitType, hit.bodyPart, hit.damage,
                                 glm::vec3(hit.directionX, hit.directionY, hit.directionZ), npc->state.selectedWeapon);
        });
        network->RegisterRPC<Shared::Npc::CarHit>([this](const Shared::Npc::CarHit &hit, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), hit.missionGeneration) || !_carHit || !std::isfinite(hit.damage) || hit.damage <= 0.0f ||
                !Finite(hit.directionX, hit.directionY, hit.directionZ) || !Finite(hit.hitX, hit.hitY, hit.hitZ) || !Finite(hit.normalX, hit.normalY, hit.normalZ) ||
                !Finite(hit.localHitX, hit.localHitY, hit.localHitZ) || !Finite(hit.localDirectionX, hit.localDirectionY, hit.localDirectionZ)) {
                return;
            }
            auto *npc = FindKey(hit.key);
            if (!npc || !npc->state.alive) {
                return;
            }
            _carHit(npc->state.networkId, hit);
        });
        network->RegisterRPC<Shared::Npc::Kick>([this](const Shared::Npc::Kick &kick, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), kick.missionGeneration) || !_kick) {
                return;
            }
            auto *npc = FindKey(kick.key);
            if (!npc || !npc->state.alive) {
                return;
            }
            _kick(npc->state.networkId, npc->entity->position, kick);
        });
        network->RegisterRPC<Shared::Npc::Seat>([this](const Shared::Npc::Seat &seat, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), seat.missionGeneration) || !_seat) {
                return;
            }
            // A corpse may stay seated; only a live NPC takes a new seat.
            auto *npc = FindKey(seat.key);
            if (!npc || (!npc->state.alive && seat.seat != Shared::Npc::Seat::kNoSeat)) {
                return;
            }
            _seat(npc->state.networkId, seat);
        });
        network->RegisterRPC<Shared::Npc::Caught>([this](const Shared::Npc::Caught &caught, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), caught.missionGeneration) ||
                (caught.outcome != Shared::Npc::Caught::Outcome::Arrest && caught.outcome != Shared::Npc::Caught::Outcome::Fine)) {
                return;
            }
            auto *npc = FindKey(caught.key);
            auto *player = _players->FindByNetworkId(caught.targetNetworkId);
            if (!npc || !npc->state.alive || npc->entity->kind != Shared::Entities::NpcKind::Police || !player || !player->spawned || !player->alive ||
                player->missionGeneration != _missionGeneration || player->spawnGeneration != caught.targetSpawnGeneration) {
                return;
            }
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Police NPC {} {} player {}", npc->state.networkId,
                                                                       caught.outcome == Shared::Npc::Caught::Outcome::Fine ? "fined" : "arrested", caught.targetNetworkId);
            if (_events.caught) {
                _events.caught(*npc->entity, caught.targetNetworkId, caught.outcome == Shared::Npc::Caught::Outcome::Fine);
            }
        });
        network->RegisterRPC<Shared::Npc::Seized>([this](const Shared::Npc::Seized &seized, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            if (!FromHost(sender, seized.missionGeneration) ||
                (seized.outcome != Shared::Npc::Caught::Outcome::Arrest && seized.outcome != Shared::Npc::Caught::Outcome::Fine)) {
                return;
            }
            auto *npc = FindKey(seized.key);
            auto *player = _players->FindByNetworkId(seized.targetNetworkId);
            if (!npc || !npc->state.alive || npc->entity->kind != Shared::Entities::NpcKind::Police || !player || !player->spawned || !player->alive ||
                player->missionGeneration != _missionGeneration || player->spawnGeneration != seized.targetSpawnGeneration) {
                return;
            }
            // The host plays the animation natively; everyone else replays it.
            auto relay = seized;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(relay, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::ToGuid(sender));
        });
        network->RegisterRPC<Shared::Npc::Damaged>([this](const Shared::Npc::Damaged &damaged, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), damaged.missionGeneration) || !std::isfinite(damaged.damage) || !std::isfinite(damaged.healthBefore) ||
                !std::isfinite(damaged.healthAfter) || !Finite(damaged.directionX, damaged.directionY, damaged.directionZ)) {
                return;
            }
            auto *npc = FindKey(damaged.key);
            if (!npc || !npc->state.alive) {
                return;
            }
            auto &state = npc->state;
            if (damaged.attackerId) {
                npc->lastAttackerId = damaged.attackerId;
            }
            if (_events.damage) {
                _events.damage(*npc->entity, damaged.attackerId, damaged.hitType, damaged.damage, damaged.healthBefore, damaged.healthAfter);
            }
            // A fatal hit plays as the death the next snapshot reports.
            if (damaged.healthAfter <= 0.0f) {
                return;
            }
            state.health = damaged.healthAfter;
            Publish(*npc);
            Shared::Combat::DamageEvent event;
            event.targetNetworkId = state.networkId;
            event.missionGeneration = state.missionGeneration;
            event.targetSpawnGeneration = state.spawnGeneration;
            event.targetRevision = state.revision;
            event.bodyPart = damaged.bodyPart;
            // DamageEvent's reaction kinds: 1 melee knockdown, 4 car impact
            // (native HIT_TYPE_SNIPER, 8), else a bullet.
            event.hitType = damaged.hitType == 1 ? 1 : damaged.hitType == 8 ? 4 : 0;
            event.damage = damaged.damage;
            event.directionX = damaged.directionX;
            event.directionY = damaged.directionY;
            event.directionZ = damaged.directionZ;
            // The host reports the car of a car impact as the attacker.
            event.sourceCarId = event.hitType == 4 ? damaged.attackerId : 0;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(event, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::ToGuid(*_simulation->Host()));
        });
        network->RegisterRPC<Shared::Npc::Retired>([this](const Shared::Npc::Retired &retired, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), retired.missionGeneration)) {
                return;
            }
            if (const auto npc = _npcs.find(retired.key); npc != _npcs.end()) {
                Retire(npc);
            }
        });
    }

    bool NpcService::FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const {
        return _simulation->Host() == sender && missionGeneration == _missionGeneration;
    }

    NpcService::Record *NpcService::FindKey(uint32_t key) {
        const auto npc = _npcs.find(key);
        return npc == _npcs.end() ? nullptr : &npc->second;
    }

    void NpcService::Publish(Record &npc) {
        ++npc.state.revision;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(npc.state, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::ToGuid(*_simulation->Host()));
    }

    void NpcService::Emit(const Record &npc, Shared::Combat::Event &event) {
        event.networkId = npc.state.networkId;
        event.missionGeneration = npc.state.missionGeneration;
        event.spawnGeneration = npc.state.spawnGeneration;
        event.revision = npc.state.revision;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(event, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::ToGuid(*_simulation->Host()));
    }

    void NpcService::SetMissionGeneration(uint64_t generation) {
        if (generation != _missionGeneration) {
            Clear();
            _missionGeneration = generation;
        }
    }

    void NpcService::Update() {
        if (!_simulation->Host()) {
            Clear();
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        for (auto it = _npcs.begin(); it != _npcs.end();) {
            auto current = it++;
            if (now - current->second.lastSeen > kStaleAfter) {
                Retire(current);
            }
        }
    }

    void NpcService::OnPlayerConnect(MafiaNet::PeerGuid guid) const {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        for (const auto &[key, npc] : _npcs) {
            auto state = npc.state;
            network->SendRPC(state, MafiaNet::ToGuid(guid), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void NpcService::Retire(Iterator npc) {
        auto *entity = npc->second.entity;
        if (_events.retire) {
            _events.retire(*entity);
        }
        _keyByNetworkId.erase(entity->GetNetworkID());
        _npcs.erase(npc);
        Framework::CoreModules::GetReplication()->DestroyEntity(entity);
    }

    void NpcService::Clear() {
        if (_npcs.empty()) {
            return;
        }
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            for (const auto &[key, npc] : _npcs) {
                replication->DestroyEntity(npc.entity);
            }
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Removed {} NPCs", _npcs.size());
        _npcs.clear();
        _keyByNetworkId.clear();
    }

    bool NpcService::Remove(uint64_t networkId) {
        const auto key = _keyByNetworkId.find(networkId);
        const auto host = _simulation->Host();
        if (key == _keyByNetworkId.end() || !host) {
            return false;
        }
        Shared::Npc::Remove remove;
        remove.missionGeneration = _missionGeneration;
        remove.key = key->second;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(remove, MafiaNet::ToGuid(*host));
        return true;
    }

    Shared::Entities::NpcEntity *NpcService::Find(uint64_t networkId) const {
        const auto key = _keyByNetworkId.find(networkId);
        if (key == _keyByNetworkId.end()) {
            return nullptr;
        }
        const auto npc = _npcs.find(key->second);
        return npc == _npcs.end() ? nullptr : npc->second.entity;
    }

    const Shared::Combat::State *NpcService::GetState(uint64_t networkId) const {
        const auto key = _keyByNetworkId.find(networkId);
        if (key == _keyByNetworkId.end()) {
            return nullptr;
        }
        const auto npc = _npcs.find(key->second);
        return npc == _npcs.end() ? nullptr : &npc->second.state;
    }

    void NpcService::ForEach(const std::function<void(Shared::Entities::NpcEntity &)> &visit) const {
        for (const auto &[key, npc] : _npcs) {
            visit(*npc.entity);
        }
    }
} // namespace Mafia1Online::Features::Npc
