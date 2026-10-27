#pragma once

#include "shared/features/combat/combat_protocol.h"
#include "shared/features/npc/npc_entity.h"

#include <networking/rpc/rpc.h>

#include <array>
#include <cstdint>
#include <string>

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
