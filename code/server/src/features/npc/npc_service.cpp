#include "npc_service.h"

#include "features/combat/combat_service.h"
#include "features/player/player_service.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/combat/damage_event.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/npc/npc_melee.h"
#include "shared/features/npc/npc_report.h"
#include "shared/features/npc/npc_seized.h"
#include "shared/features/player/player_entity.h"
#include "shared/net/sample_clock.h"

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

        // Retail's voice ranges for these humans: police 10-11 (C_traffic_
        // generator.cpp:94-95), mafianos 12-16 (C_traffic_element.cpp:93-94),
        // a thrown-out male driver 26-28 (C_human.cpp:993-995), and C_human's
        // default 12 (C_human.cpp:830) for the rest.
        uint8_t VoiceFor(Shared::Entities::NpcKind kind, uint64_t networkId) {
            switch (kind) {
            case Shared::Entities::NpcKind::Police: return static_cast<uint8_t>(10 + networkId % 2);
            case Shared::Entities::NpcKind::Gangster: return static_cast<uint8_t>(12 + networkId % 5);
            case Shared::Entities::NpcKind::Driver: return static_cast<uint8_t>(26 + networkId % 3);
            default: return NpcEntity::kDefaultVoice;
            }
        }

        void SetPose(NpcEntity &entity, float x, float y, float z, float yaw, float velocityX = 0.0f, float velocityZ = 0.0f) {
            entity.position = {x, y, z};
            entity.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            entity.velocity = {velocityX, 0.0f, velocityZ};
        }

        // Retail firearm item ids; the host only reports native shots.
        bool Firearm(uint8_t weaponId) {
            return weaponId >= 6 && weaponId <= 14;
        }

        // Fists, knuckleduster, knife and baseball bat, as for players.
        bool MeleeWeapon(uint8_t weaponId) {
            return weaponId == 0 || (weaponId >= 2 && weaponId <= 4);
        }

        // A death the host's Hit did not pick (Pose::deathAnimation 0).
        uint16_t DeathAnimation(uint64_t networkId) {
            return 131 + static_cast<uint16_t>(networkId % 6);
        }

        void SetInventory(Shared::Combat::State &state, const Shared::Npc::Inventory &inventory) {
            state.inventoryMask = inventory.mask;
            state.selectedWeapon = inventory.selected;
            state.ammo = inventory.ammo;
        }

        // A plain retail .i3d file name every client can open: the rule
        // Player.setModel applies, within the NPC entity's model limit.
        bool ValidModel(const std::string &model) {
            constexpr std::string_view kExtension = ".i3d";
            if (model.size() <= kExtension.size() || model.size() > NpcEntity::kMaxModelLength || !model.ends_with(kExtension)) {
                return false;
            }
            return std::all_of(model.begin(), model.end(), [](unsigned char c) {
                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            });
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
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), spawned.missionGeneration)) {
                return;
            }
            // The host created a scripted NPC the server already made the
            // entity of (Spawn).
            if (const auto pending = _npcs.find(spawned.key); pending != _npcs.end()) {
                auto &npc = pending->second;
                if (!npc.scripted || npc.confirmed || spawned.kind != Shared::Entities::NpcKind::Scripted || !Finite(spawned.x, spawned.y, spawned.z) ||
                    !std::isfinite(spawned.yaw) || !std::isfinite(spawned.health) || !spawned.inventory.Valid()) {
                    return;
                }
                npc.confirmed = true;
                npc.lastSeen = std::chrono::steady_clock::now();
                SetPose(*npc.entity, spawned.x, spawned.y, spawned.z, spawned.yaw);
                // The pose Spawn gave it carried clock 0; the host's own
                // reaches observers as a new sample, not a repeat.
                npc.entity->sampleClock = spawned.hostClockMs;
                npc.entity->deathClock = spawned.hostClockMs;
                npc.entity->poseSource = MafiaNet::ToPeerGuid(packet->guid);
                npc.entity->alive = spawned.health > 0.0f;
                npc.state.alive = npc.entity->alive;
                npc.state.health = std::max(spawned.health, 0.0f);
                SetInventory(npc.state, spawned.inventory);
                Publish(npc);
                if (_events.spawn) {
                    _events.spawn(*npc.entity);
                }
                return;
            }
            // A scripted NPC the server gave up on (its creation timed out,
            // a script removed it): the host's copy goes too.
            if (Shared::Npc::IsScriptedKey(spawned.key)) {
                Shared::Npc::Remove remove;
                remove.missionGeneration = spawned.missionGeneration;
                remove.key = spawned.key;
                Framework::CoreModules::GetNetworkPeer()->SendRPC(remove, MafiaNet::ToGuid(MafiaNet::ToPeerGuid(packet->guid)), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
                return;
            }
            if (_npcs.size() >= kMaxNpcs ||
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
                entity->SetCarAction(Shared::Entities::NpcCarAction::Throw, spawned.carId, 0);
            }
            entity->alive = spawned.health > 0.0f;
            entity->voice = VoiceFor(spawned.kind, entity->GetNetworkID());
            entity->streaming.range = kStreamRange;
            entity->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
            SetPose(*entity, spawned.x, spawned.y, spawned.z, spawned.yaw);
            entity->sampleClock = spawned.hostClockMs;
            entity->deathClock = spawned.hostClockMs;
            entity->poseSource = MafiaNet::ToPeerGuid(packet->guid);
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
                if (!npc || (npc->scripted && !npc->confirmed) || !Finite(pose.x, pose.y, pose.z) || !std::isfinite(pose.yaw) || !std::isfinite(pose.velocityX) || !std::isfinite(pose.velocityZ) || !std::isfinite(pose.health) ||
                    !Finite(pose.poseTargetX, pose.poseTargetY, pose.poseTargetZ)) {
                    continue;
                }
                auto &entity = *npc->entity;
                auto &state = npc->state;
                npc->lastSeen = now;
                // An unchanged pose keeps its clock, so a standing NPC or a
                // corpse is not sent again every snapshot.
                Shared::Net::RelayPose(entity, snapshot.hostClockMs, [&] {
                    const bool moving = state.alive && !pose.dead;
                    SetPose(entity, pose.x, pose.y, pose.z, pose.yaw, moving ? pose.velocityX : 0.0f, moving ? pose.velocityZ : 0.0f);
                    entity.locomotion = pose.locomotion;
                    entity.aiming = moving && pose.aiming;
                    entity.crouching = moving && pose.crouching;
                });
                entity.poseSource = MafiaNet::ToPeerGuid(packet->guid);
                if (!state.alive) {
                    continue;
                }
                const bool died = pose.dead || pose.health <= 0.0f;
                const glm::vec3 target(pose.poseTargetX, pose.poseTargetY, pose.poseTargetZ);
                const glm::vec3 previous(state.poseTargetOffsetX, state.poseTargetOffsetY, state.poseTargetOffsetZ);
                const bool aimOnly = !died && pose.aiming == state.aiming && pose.crouching == state.crouching && pose.health == state.health;
                if (aimOnly && glm::distance(target, previous) <= 0.05f) {
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
                    // The death the host played; DeathAnimation's otherwise.
                    if (Shared::Combat::NativeDeathAnimation(pose.deathAnimation)) {
                        state.deathAnimation = pose.deathAnimation;
                    }
                    entity.deathAnimation = state.deathAnimation;
                    entity.deathClock = pose.deathClock;
                    npc->diedAt = now;
                }
                if (aimOnly) {
                    // Only the aim target moved: unreliable, to the players
                    // that stream this NPC, instead of a whole State to all.
                    Combat::CombatService::RelayAim(entity, state, *_simulation->Host(), npc->aimRelay);
                    continue;
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
            event.sampleClock = fire.hostClockMs;
            Emit(*npc, event);
        });
        network->RegisterRPC<Shared::Npc::Melee>([this](const Shared::Npc::Melee &melee, MafiaNet::Packet *packet) {
            using Action = Shared::Combat::Action;
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), melee.missionGeneration) || !MeleeWeapon(melee.weaponId) ||
                (melee.action != Action::MeleeStart && melee.action != Action::Melee && melee.action != Action::MeleeCancel) ||
                (melee.meleeIndex != Shared::Combat::kHeavyMelee && (melee.meleeIndex < 0 || melee.meleeIndex > 3)) || melee.holdMs > Shared::Combat::kMaximumMeleeHoldMs ||
                !Finite(melee.directionX, melee.directionY, melee.directionZ)) {
                return;
            }
            auto *npc = FindKey(melee.key);
            if (!npc || !npc->state.alive || npc->state.selectedWeapon != melee.weaponId) {
                return;
            }
            // Retail AI presses and releases in one tick: each event needs its
            // own revision, or players drop the release as a repeat of the press.
            Publish(*npc);
            Shared::Combat::Event event;
            event.action = melee.action;
            event.weaponId = melee.weaponId;
            event.directionX = melee.directionX;
            event.directionY = melee.directionY;
            event.directionZ = melee.directionZ;
            event.meleeIndex = melee.meleeIndex;
            event.meleeHoldMs = melee.holdMs;
            event.sampleClock = melee.hostClockMs;
            event.targetNetworkId = melee.targetNetworkId;
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
            event.sampleClock = reload.hostClockMs;
            Emit(*npc, event);
        });
        network->RegisterRPC<Shared::Npc::Speech>([this](const Shared::Npc::Speech &speech, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), speech.missionGeneration) || !Shared::Npc::RelayedSpeechLine(speech.line)) {
                return;
            }
            // Each line cuts off the one before it on every copy: lines
            // closer together than this would only be heard as noise.
            auto *npc = FindKey(speech.key);
            const auto now = std::chrono::steady_clock::now();
            if (!npc || !npc->entity || now - npc->lastSpeech < std::chrono::milliseconds(250)) {
                return;
            }
            if (Say(npc->entity->GetNetworkID(), speech.line)) {
                npc->lastSpeech = now;
            }
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
                                 glm::vec3(hit.directionX, hit.directionY, hit.directionZ), npc->state.selectedWeapon, hit.hostClockMs);
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
            // The host plays the animation natively; everyone else replays it,
            // the officer's half on its replica too.
            auto relay = seized;
            relay.officerNetworkId = npc->state.networkId;
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
        network->RegisterRPC<Shared::Npc::TaskDone>([this](const Shared::Npc::TaskDone &done, MafiaNet::Packet *packet) {
            using Result = Shared::Npc::TaskDone::Result;
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), done.missionGeneration) || !Shared::Npc::KnownTaskType(done.type) ||
                (done.result != Result::Completed && done.result != Result::Failed && done.result != Result::Cancelled)) {
                return;
            }
            auto *npc = FindKey(done.key);
            if (!npc || !npc->scripted) {
                return;
            }
            if (npc->task.id == done.taskId) {
                npc->taskRunning = false;
            }
            ReleaseIfUnused(*npc);
            if (_events.taskDone) {
                _events.taskDone(*npc->entity, done.taskId, done.type, done.result);
            }
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
            // The host's NPCs went with it. Each retires as if the host had
            // retired it, so its car seat is freed and scripts holding a
            // handle (npcRetire) know to make another once a host is back.
            while (!_npcs.empty()) {
                Retire(_npcs.begin());
            }
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        for (auto it = _npcs.begin(); it != _npcs.end();) {
            auto current = it++;
            auto &npc = current->second;
            if (npc.scripted && !npc.confirmed) {
                // The host creates it once it has the mission loaded.
                if (now - npc.createdAt > kPendingTimeout) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Simulation host never created scripted NPC {}", npc.state.networkId);
                    Retire(current);
                }
                continue;
            }
            if (now - npc.lastSeen > kStaleAfter) {
                Retire(current);
                continue;
            }
            if (npc.scripted && !npc.state.alive && !npc.removeSent && now - npc.diedAt > kCorpseLifetime) {
                npc.removeSent = Remove(npc.state.networkId);
            }
            if (npc.state.alive) {
                Combat::CombatService::SettleAim(*npc.entity, npc.state, npc.aimRelay, now);
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
        ReleaseClaim(npc->second);
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
        for (auto &[key, npc] : _npcs) {
            ReleaseClaim(npc);
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

    void NpcService::RemoveOwnedBy(const std::string &owner) {
        if (owner.empty()) {
            return;
        }
        for (auto &[key, npc] : _npcs) {
            if (npc.scripted && npc.owner == owner && !npc.removeSent) {
                npc.removeSent = Remove(npc.state.networkId);
            }
        }
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

    NpcService::Record *NpcService::FindNetwork(uint64_t networkId) {
        const auto key = _keyByNetworkId.find(networkId);
        return key == _keyByNetworkId.end() ? nullptr : FindKey(key->second);
    }

    const NpcService::Record *NpcService::FindNetwork(uint64_t networkId) const {
        const auto key = _keyByNetworkId.find(networkId);
        if (key == _keyByNetworkId.end()) {
            return nullptr;
        }
        const auto npc = _npcs.find(key->second);
        return npc == _npcs.end() ? nullptr : &npc->second;
    }

    Shared::Entities::NpcEntity *NpcService::Spawn(const SpawnRequest &request) {
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        const size_t scripted = static_cast<size_t>(std::count_if(_npcs.begin(), _npcs.end(), [](const auto &entry) { return entry.second.scripted; }));
        if (!host || _missionGeneration == 0 || _npcs.size() >= kMaxNpcs || scripted >= kMaxScripted || !ValidModel(request.model) ||
            !Finite(request.position.x, request.position.y, request.position.z) || !std::isfinite(request.heading) || !std::isfinite(request.health) ||
            request.health <= 0.0f || !Shared::Npc::KnownBehaviour(request.behaviour) || (request.weaponId != 0 && !Combat::CombatService::Weapon(request.weaponId))) {
            return nullptr;
        }
        // Server keys never repeat within a mission generation.
        uint32_t key = 0;
        do {
            key = Shared::Npc::kScriptedKeyBit | (_nextScriptedKey++ & ~Shared::Npc::kScriptedKeyBit);
        } while (_npcs.contains(key) || key == Shared::Npc::kScriptedKeyBit);

        auto *entity = Framework::CoreModules::GetReplication()->CreateEntity<NpcEntity>();
        entity->model = request.model;
        entity->missionGeneration = _missionGeneration;
        entity->kind = Shared::Entities::NpcKind::Scripted;
        entity->sourcePedestrianId = request.walkerId;
        entity->alive = true;
        entity->voice = request.voice && *request.voice < NpcEntity::kVoiceCount ? *request.voice : VoiceFor(entity->kind, entity->GetNetworkID());
        entity->streaming.range = kStreamRange;
        entity->SetVirtualWorld(request.virtualWorld);
        SetPose(*entity, request.position.x, request.position.y, request.position.z, request.heading);
        auto &npc = _npcs[key];
        npc.entity = entity;
        npc.scripted = true;
        npc.behaviour = request.behaviour;
        npc.owner = request.owner;
        npc.createdAt = std::chrono::steady_clock::now();
        npc.lastSeen = npc.createdAt;
        auto &state = npc.state;
        state.networkId = entity->GetNetworkID();
        state.missionGeneration = _missionGeneration;
        state.spawnGeneration = NpcEntity::kSpawnGeneration;
        state.spawned = true;
        state.alive = true;
        state.health = request.health;
        state.deathAnimation = DeathAnimation(state.networkId);
        _keyByNetworkId[state.networkId] = key;

        Shared::Npc::Create create;
        create.missionGeneration = _missionGeneration;
        create.key = key;
        create.model = request.model;
        create.x = request.position.x;
        create.y = request.position.y;
        create.z = request.position.z;
        create.yaw = request.heading;
        create.health = request.health;
        create.behaviour = request.behaviour;
        create.weaponId = request.weaponId;
        create.loaded = request.loaded;
        create.reserve = request.reserve;
        create.walkerKey = request.walkerKey;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(create, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Scripted NPC {} ({}, {}) requested", state.networkId, request.model, Shared::Npc::BehaviourName(request.behaviour));
        return entity;
    }

    uint32_t NpcService::GiveTask(uint64_t networkId, Shared::Npc::Task task) {
        using Type = Shared::Npc::TaskType;
        auto *npc = FindNetwork(networkId);
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        if (!npc || !npc->scripted || !npc->state.alive || !host || !Shared::Npc::KnownTaskType(task.type) || !Finite(task.x, task.y, task.z) ||
            !std::isfinite(task.distance)) {
            return 0;
        }
        // A player or car goes by network id; an NPC by key, since the host
        // binds none of its own as network objects.
        if (task.type == Type::Follow || task.type == Type::Attack) {
            if (const auto target = _keyByNetworkId.find(task.targetId); target != _keyByNetworkId.end()) {
                if (target->first == networkId) {
                    return 0;
                }
                task.targetKey = target->second;
                task.targetId = 0;
            }
            else {
                auto *player = _players->FindByNetworkId(task.targetId);
                if (!player || !player->spawned || player->missionGeneration != _missionGeneration) {
                    return 0;
                }
            }
        }
        if (task.type == Type::EnterVehicle && task.seat != 0 && _vehicles.playerDriven && _vehicles.playerDriven(task.targetId)) {
            // A passenger seat of a car a player drives: the player keeps it
            // and its physics; the NPC rides along.
            const uint64_t previous = npc->claimedCar;
            npc->claimedCar = 0;
            ReleaseCar(previous);
        }
        else if (task.type == Type::EnterVehicle) {
            // The host drives the car while a scripted NPC boards or sits in
            // it (NpcHost's seat rule: only cars it drives).
            if (npc->claimedCar != task.targetId) {
                const uint64_t previous = npc->claimedCar;
                bool shared = false;
                for (const auto &[key, other] : _npcs) {
                    shared = shared || (&other != npc && other.claimedCar == task.targetId);
                }
                if (!shared && (!_vehicles.claim || !_vehicles.claim(task.targetId))) {
                    return 0;
                }
                npc->claimedCar = 0;
                ReleaseCar(previous);
                npc->claimedCar = task.targetId;
            }
        }
        task.missionGeneration = _missionGeneration;
        task.key = _keyByNetworkId.at(networkId);
        task.taskId = _nextTaskId++;
        npc->task = {task.taskId, task.type};
        npc->taskRunning = task.type != Type::Stop;
        // The host stops a script clip when the task begins.
        if (!npc->entity->animation.empty()) {
            npc->entity->animation.clear();
            npc->entity->animationLoop = false;
            ++npc->entity->animationRevision;
        }
        ReleaseIfUnused(*npc);
        Framework::CoreModules::GetNetworkPeer()->SendRPC(task, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return task.taskId;
    }

    bool NpcService::SendOrder(uint64_t networkId, Shared::Npc::Order order) {
        auto *npc = FindNetwork(networkId);
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        if (!npc || !npc->scripted || !npc->state.alive || !host) {
            return false;
        }
        order.missionGeneration = _missionGeneration;
        order.key = _keyByNetworkId.at(networkId);
        Framework::CoreModules::GetNetworkPeer()->SendRPC(order, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    bool NpcService::Say(uint64_t networkId, uint16_t line) {
        auto *npc = FindNetwork(networkId);
        if (!npc || !npc->state.alive || line < NpcEntity::kMinSpeechLine || line > NpcEntity::kMaxSpeechLine) {
            return false;
        }
        npc->entity->speechLine = line;
        ++npc->entity->speechRevision;
        return true;
    }

    bool NpcService::SetAnimation(uint64_t networkId, const std::string &name, bool loop) {
        auto *npc = FindNetwork(networkId);
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        if (!npc || !npc->scripted || !npc->state.alive || !host || (!name.empty() && !Shared::Npc::AnimationNameValid(name))) {
            return false;
        }
        // Do_PlayAnim replaces the seated pose and Do_StopAnim leaves the
        // human in the standing idle state: a clip in a car seat (a taxi
        // fare) would break the seat and its later exit.
        if (!name.empty() && _vehicles.seatedCar && _vehicles.seatedCar(networkId) != 0) {
            return false;
        }
        Shared::Npc::Animation animation;
        animation.missionGeneration = _missionGeneration;
        animation.key = _keyByNetworkId.at(networkId);
        animation.name = name;
        animation.loop = loop;
        if (!name.empty() && npc->taskRunning) {
            npc->taskRunning = false;
            ReleaseIfUnused(*npc);
        }
        npc->entity->animation = name;
        npc->entity->animationLoop = !name.empty() && loop;
        ++npc->entity->animationRevision;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(animation, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    bool NpcService::IsScripted(uint64_t networkId) const {
        const auto *npc = FindNetwork(networkId);
        return npc && npc->scripted;
    }

    std::optional<Shared::Npc::Behaviour> NpcService::GetBehaviour(uint64_t networkId) const {
        const auto *npc = FindNetwork(networkId);
        return npc && npc->scripted ? std::optional<Shared::Npc::Behaviour>(npc->behaviour) : std::nullopt;
    }

    std::optional<NpcService::CurrentTask> NpcService::GetTask(uint64_t networkId) const {
        const auto *npc = FindNetwork(networkId);
        return npc && npc->scripted && npc->taskRunning ? std::optional<CurrentTask>(npc->task) : std::nullopt;
    }

    void NpcService::OnSeatChanged(uint64_t networkId) {
        if (auto *npc = FindNetwork(networkId)) {
            ReleaseIfUnused(*npc);
        }
    }

    void NpcService::ReleaseIfUnused(Record &npc) {
        if (!npc.claimedCar) {
            return;
        }
        const bool boarding = npc.taskRunning && npc.task.type == Shared::Npc::TaskType::EnterVehicle;
        const bool seated = _vehicles.seatedCar && _vehicles.seatedCar(npc.state.networkId) == npc.claimedCar;
        if (!boarding && !seated) {
            ReleaseClaim(npc);
        }
    }

    void NpcService::ReleaseClaim(Record &npc) {
        const uint64_t carId = npc.claimedCar;
        npc.claimedCar = 0;
        ReleaseCar(carId);
    }

    void NpcService::ReleaseCar(uint64_t carId) {
        if (!carId) {
            return;
        }
        for (const auto &[key, other] : _npcs) {
            if (other.claimedCar == carId) {
                return;
            }
        }
        if (_vehicles.release) {
            _vehicles.release(carId);
        }
    }
} // namespace Mafia1Online::Features::Npc
