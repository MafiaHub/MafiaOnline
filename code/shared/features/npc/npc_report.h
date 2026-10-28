#pragma once

#include "shared/features/combat/combat_protocol.h"
#include "shared/features/npc/npc_entity.h"

#include <networking/rpc/rpc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Mafia1Online::Shared::Npc {
    // Simulation host <-> server. A key names one NPC for the whole mission
    // generation and is never reused within it.

    // The host's native G_Inventory, item by item, in the Combat::State form.
    struct Inventory {
        uint32_t mask = 0;
        uint8_t selected = 0;
        std::array<Combat::WeaponAmmo, Combat::kWeaponSlots> ammo {};

        [[nodiscard]] bool Valid() const {
            return selected < Combat::kWeaponSlots && (selected == 0 || (mask & (1u << selected)) != 0);
        }
        [[nodiscard]] bool Same(const Inventory &other) const {
            if (mask != other.mask || selected != other.selected) {
                return false;
            }
            for (size_t slot = 0; slot < ammo.size(); ++slot) {
                if (ammo[slot].loaded != other.ammo[slot].loaded || ammo[slot].reserve != other.ammo[slot].reserve) {
                    return false;
                }
            }
            return true;
        }

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, mask);
            stream->Serialize(write, selected);
            for (uint8_t slot = 0; slot < Combat::kWeaponSlots; ++slot) {
                if ((mask & (1u << slot)) == 0) {
                    if (!write) {
                        ammo[slot] = {};
                    }
                    continue;
                }
                stream->Serialize(write, ammo[slot].loaded);
                stream->Serialize(write, ammo[slot].reserve);
            }
        }
    };

    struct Spawned {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcSpawned");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        Entities::NpcKind kind = Entities::NpcKind::Gangster;
        std::string model;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        float health = 0.0f;
        Inventory inventory;
        // The reported walker (Pedestrian::Spawned key) the retail game
        // turned into this NPC, or 0.
        uint32_t walkerKey = 0;
        // The network car a traffic driver was thrown out of (Corovacka in
        // Do_ThrowCocotFromCar), or 0.
        uint64_t carId = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto kindValue = static_cast<uint8_t>(kind);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, kindValue);
            stream->Serialize(write, model);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
            stream->Serialize(write, yaw);
            stream->Serialize(write, health);
            inventory.Serialize(stream, write);
            stream->Serialize(write, walkerKey);
            stream->Serialize(write, carId);
            if (!write) {
                kind = static_cast<Entities::NpcKind>(kindValue);
            }
        }
    };

    struct Retired {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcRetired");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
        }
    };

    // The native inventory changed: a shot, a reload, a pickup or a drop.
    struct InventoryChanged {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcInventory");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        Inventory inventory;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            inventory.Serialize(stream, write);
        }
    };

    // Retail C_human::DropOutItems ran: the items it would have put on the
    // ground, after the fatal hit's InventoryTruncate clamped their rounds.
    // The server takes them out of its inventory as world pickups.
    struct Dropped {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcDropped");
        static constexpr uint8_t kMaxItems = 8;

        struct Item {
            uint8_t weaponId = 0;
            uint16_t loaded = 0;
            uint16_t reserve = 0;
        };

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint8_t count = 0;
        std::array<Item, kMaxItems> items {};

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, count);
            if (count > kMaxItems) {
                count = 0;
                return;
            }
            for (uint8_t i = 0; i < count; ++i) {
                stream->Serialize(write, items[i].weaponId);
                stream->Serialize(write, items[i].loaded);
                stream->Serialize(write, items[i].reserve);
            }
        }
    };

    struct Pose {
        uint32_t key = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        // Ground speed in m/s, for the replicas' interpolation.
        float speed = 0.0f;
        int16_t locomotion = 1;
        float health = 0.0f;
        bool dead = false;
        bool aiming = false;
        bool crouching = false;
        // Where the neck and back point, relative to the position: the shoot
        // target while aiming, else straight ahead at neck height.
        float poseTargetX = 0.0f;
        float poseTargetY = 0.0f;
        float poseTargetZ = 0.0f;
    };

    // Newest poses of up to kMaxNpcs NPCs; an unknown key is ignored.
    struct Snapshot {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcSnapshot");
        static constexpr uint8_t kMaxNpcs = 16;

        uint64_t missionGeneration = 0;
        uint8_t count = 0;
        std::array<Pose, kMaxNpcs> npcs {};

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, count);
            if (count > kMaxNpcs) {
                count = 0;
                return;
            }
            for (uint8_t i = 0; i < count; ++i) {
                auto &pose = npcs[i];
                stream->Serialize(write, pose.key);
                stream->Serialize(write, pose.x);
                stream->Serialize(write, pose.y);
                stream->Serialize(write, pose.z);
                stream->Serialize(write, pose.yaw);
                stream->Serialize(write, pose.speed);
                stream->Serialize(write, pose.locomotion);
                stream->Serialize(write, pose.health);
                stream->Serialize(write, pose.dead);
                stream->Serialize(write, pose.aiming);
                stream->Serialize(write, pose.crouching);
                stream->Serialize(write, pose.poseTargetX);
                stream->Serialize(write, pose.poseTargetY);
                stream->Serialize(write, pose.poseTargetZ);
            }
        }
    };

    // One native shot an NPC fired: the C_game::NewShoot input and queued
    // pellets, which players replay without damage.
    struct Fire {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcFire");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint8_t weaponId = 0;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;
        float originX = 0.0f;
        float originY = 0.0f;
        float originZ = 0.0f;
        float nativeDirectionX = 0.0f;
        float nativeDirectionY = 0.0f;
        float nativeDirectionZ = 0.0f;
        uint8_t pelletCount = 0;
        std::array<float, Combat::kMaximumShotPellets * 2> scatterRandom {};
        std::array<Combat::PelletTrajectory, Combat::kMaximumShotPellets> pellets {};

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, weaponId);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
            stream->Serialize(write, originX);
            stream->Serialize(write, originY);
            stream->Serialize(write, originZ);
            stream->Serialize(write, nativeDirectionX);
            stream->Serialize(write, nativeDirectionY);
            stream->Serialize(write, nativeDirectionZ);
            stream->Serialize(write, pelletCount);
            for (auto &value : scatterRandom) {
                stream->Serialize(write, value);
            }
            for (auto &pellet : pellets) {
                stream->Serialize(write, pellet.x);
                stream->Serialize(write, pellet.y);
                stream->Serialize(write, pellet.z);
                stream->Serialize(write, pellet.staticHit);
                stream->Serialize(write, pellet.hitX);
                stream->Serialize(write, pellet.hitY);
                stream->Serialize(write, pellet.hitZ);
                stream->Serialize(write, pellet.normalX);
                stream->Serialize(write, pellet.normalY);
                stream->Serialize(write, pellet.normalZ);
                stream->Serialize(write, pellet.materialId);
            }
        }
    };

    // The NPC's native Do_Reload moved reserve rounds into its weapon;
    // players replay the reload animation. The rounds arrive as an
    // InventoryChanged.
    struct Reload {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcReload");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint8_t weaponId = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, weaponId);
        }
    };

    // The host's native C_human::Hit of an NPC on a player's replica, which
    // the host keeps protected. The server applies the damage.
    struct PlayerHit {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcPlayerHit");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t targetNetworkId = 0;
        uint64_t targetSpawnGeneration = 0;
        // Retail hit type: 0 bullet, 1 melee knockdown.
        uint8_t hitType = 0;
        // 1..6 skeleton part, 0 through the car the target sits in.
        uint8_t bodyPart = 0;
        // The Hit argument: weapon damage per pellet after range falloff,
        // before the body part scale.
        float damage = 0.0f;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, targetNetworkId);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, hitType);
            stream->Serialize(write, bodyPart);
            stream->Serialize(write, damage);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
        }
    };

    // One NPC bullet on a player's network car body, which the host's
    // C_car::Hit hook keeps from its own copy. The server forwards it to the
    // car's simulation controller as a Car::AuthoritativeHit. Bullets on an
    // occupant's frame reach that human instead and arrive as a PlayerHit.
    struct CarHit {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcCarHit");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t carId = 0;
        // The C_car::Hit argument: pellet damage after range falloff.
        float damage = 0.0f;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;
        float hitX = 0.0f;
        float hitY = 0.0f;
        float hitZ = 0.0f;
        float normalX = 0.0f;
        float normalY = 0.0f;
        float normalZ = 0.0f;
        // The hit point and direction in the car's frame, as the host saw it.
        float localHitX = 0.0f;
        float localHitY = 0.0f;
        float localHitZ = 0.0f;
        float localDirectionX = 0.0f;
        float localDirectionY = 0.0f;
        float localDirectionZ = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, carId);
            stream->Serialize(write, damage);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
            stream->Serialize(write, hitX);
            stream->Serialize(write, hitY);
            stream->Serialize(write, hitZ);
            stream->Serialize(write, normalX);
            stream->Serialize(write, normalY);
            stream->Serialize(write, normalZ);
            stream->Serialize(write, localHitX);
            stream->Serialize(write, localHitY);
            stream->Serialize(write, localHitZ);
            stream->Serialize(write, localDirectionX);
            stream->Serialize(write, localDirectionY);
            stream->Serialize(write, localDirectionZ);
        }
    };

    // Retail AI_ACTION_KICK_DRIVER (gangster fights, police arrests) threw a
    // player's replica out of a seat of that player's network car on the
    // host: Do_ThrowCocotFromCar -> intern_ThrowMeFromCar. The thrower never
    // takes the seat. The server frees it and every player replays the throw
    // with the NPC's replica, as it does a player's steal.
    struct Kick {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcKick");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t carId = 0;
        uint8_t seat = 0;
        uint64_t victimNetworkId = 0;
        uint64_t victimSpawnGeneration = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, carId);
            stream->Serialize(write, seat);
            stream->Serialize(write, victimNetworkId);
            stream->Serialize(write, victimSpawnGeneration);
        }
    };

    // Host -> server: a player's police crime record (C_shvestky S_shvestky)
    // after a police manager tick that changed it.
    struct PoliceRecord {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcPoliceRecord");

        uint64_t missionGeneration = 0;
        uint64_t playerNetworkId = 0;
        uint64_t playerSpawnGeneration = 0;
        // 0 none, 1 minor, 2 arrest, 3 lethal.
        uint8_t level = 0;
        // Retail crime bits since the record was last settled.
        uint16_t crimes = 0;
        uint8_t officers = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, playerNetworkId);
            stream->Serialize(write, playerSpawnGeneration);
            stream->Serialize(write, level);
            stream->Serialize(write, crimes);
            stream->Serialize(write, officers);
        }
    };

    // Server -> host: a script changed a player's police record.
    struct PoliceOrder {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcPoliceOrder");

        enum class Action : uint8_t {
            // Raise or lower the record to level; a higher level brings the
            // police the retail way.
            SetWanted = 1,
            // Settle the record and call off every officer after the player.
            Clear = 2,
        };

        uint64_t missionGeneration = 0;
        uint64_t playerNetworkId = 0;
        uint64_t playerSpawnGeneration = 0;
        Action action = Action::Clear;
        uint8_t level = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto actionValue = static_cast<uint8_t>(action);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, playerNetworkId);
            stream->Serialize(write, playerSpawnGeneration);
            stream->Serialize(write, actionValue);
            stream->Serialize(write, level);
            if (!write) {
                action = static_cast<Action>(actionValue);
            }
        }
    };

    // Server -> host: whether the police manager reacts to crimes at all.
    struct PoliceSwitch {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcPoliceSwitch");

        bool enabled = true;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, enabled);
        }
    };

    // Server -> host: every player a script put on police duty
    // (Police.setOfficer), sent whole on each change and to a newly attached
    // host. The host treats their replicas as retail treats its own
    // officers: no crime record, and the police behaviour class.
    struct PoliceOfficers {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcPoliceOfficers");
        static constexpr uint16_t kMaxOfficers = 256;

        std::vector<uint64_t> playerNetworkIds;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto count = static_cast<uint16_t>(std::min<size_t>(playerNetworkIds.size(), kMaxOfficers));
            stream->Serialize(write, count);
            if (count > kMaxOfficers) {
                playerNetworkIds.clear();
                return;
            }
            if (!write) {
                playerNetworkIds.assign(count, 0);
            }
            for (uint16_t i = 0; i < count; ++i) {
                stream->Serialize(write, playerNetworkIds[i]);
            }
        }
    };

    // Server -> host: send a police car the retail way after a wanted
    // player (C_shvestky::DalsiChalpiDoPartie), for Police.sendBackup.
    struct PoliceBackup {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcPoliceBackup");

        uint64_t missionGeneration = 0;
        uint32_t requestId = 0;
        uint64_t playerNetworkId = 0;
        uint64_t playerSpawnGeneration = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, requestId);
            stream->Serialize(write, playerNetworkId);
            stream->Serialize(write, playerSpawnGeneration);
        }
    };

    // Host -> server: whether the police car of a PoliceBackup set out.
    struct PoliceBackupSent {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcPoliceBackupSent");

        uint64_t missionGeneration = 0;
        uint32_t requestId = 0;
        bool sent = false;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, requestId);
            stream->Serialize(write, sent);
        }
    };

    // A police officer finished catching a player's replica: the retail
    // arrest (which would end a single-player game) or the on-the-spot
    // fine. The host keeps the retail game running; the server leaves the
    // outcome to its scripts.
    struct Caught {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcCaught");

        enum class Outcome : uint8_t {
            Arrest = 0,
            Fine = 1,
        };

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t targetNetworkId = 0;
        uint64_t targetSpawnGeneration = 0;
        Outcome outcome = Outcome::Arrest;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            auto outcomeValue = static_cast<uint8_t>(outcome);
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, targetNetworkId);
            stream->Serialize(write, targetSpawnGeneration);
            stream->Serialize(write, outcomeValue);
            if (!write) {
                outcome = static_cast<Outcome>(outcomeValue);
            }
        }
    };

    // A native hit the host applied to its NPC.
    struct Damaged {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcDamaged");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        // Network id of the player or car that hit it, or 0.
        uint64_t attackerId = 0;
        // Retail C_human hit type: 1 hard knockdown, 8 car impact, ...
        uint8_t hitType = 0;
        uint8_t bodyPart = 0;
        float damage = 0.0f;
        float healthBefore = 0.0f;
        float healthAfter = 0.0f;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, attackerId);
            stream->Serialize(write, hitType);
            stream->Serialize(write, bodyPart);
            stream->Serialize(write, damage);
            stream->Serialize(write, healthBefore);
            stream->Serialize(write, healthAfter);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
        }
    };

    // Server -> host: a player's hit on the NPC, which the player's own
    // client detected on its replica and the server validated like a hit on
    // a player. The host applies it as the native Hit by that player's
    // replica, or by the player's car.
    struct Hit {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcHit");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        // The player, or for a car impact the car.
        uint64_t attackerId = 0;
        // 0 bullet, 1 melee knockdown, 4 car impact.
        uint8_t hitType = 0;
        uint8_t bodyPart = 0;
        // Before the body part scale, which the native Hit applies.
        float damage = 0.0f;
        float directionX = 0.0f;
        float directionY = 0.0f;
        float directionZ = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, attackerId);
            stream->Serialize(write, hitType);
            stream->Serialize(write, bodyPart);
            stream->Serialize(write, damage);
            stream->Serialize(write, directionX);
            stream->Serialize(write, directionY);
            stream->Serialize(write, directionZ);
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
        }
    };

    // Host -> server: one of its NPCs sat down in, or left, a seat of a car
    // the host drives (C_human +0x98 used car, +0xac seat). seat kNoSeat
    // means on foot; animated means it plays the native enter or exit.
    struct Seat {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcSeat");
        static constexpr uint8_t kNoSeat = 0xff;

        uint64_t missionGeneration = 0;
        uint32_t key = 0;
        uint64_t carId = 0;
        uint8_t seat = kNoSeat;
        bool animated = false;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
            stream->Serialize(write, carId);
            stream->Serialize(write, seat);
            stream->Serialize(write, animated);
        }
    };

    // Server -> host: retire this NPC now (Npc.remove).
    struct Remove {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::NpcRemove");

        uint64_t missionGeneration = 0;
        uint32_t key = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, key);
        }
    };
} // namespace Mafia1Online::Shared::Npc
