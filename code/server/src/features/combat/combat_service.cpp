#include "combat_service.h"

#include "features/car/car_service.h"
#include "features/player/player_service.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/combat/weapon_inventory.h"

#include <core_modules.h>
#include <networking/network_peer.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace Mafia1Online::Features::Combat {
    namespace {
        using Clock = std::chrono::steady_clock;

        struct WeaponRule {
            uint8_t capacity;
            uint16_t intervalMs;
            float reach;
            float damage;
        };

        // Firearm capacity, range and base damage come from the stock Steam
        // tables/predmety.def S_item records. Melee damage is the retail
        // constant C_human::AI applies per selected item (0x570df7-0x570e42);
        // cadence intervals remain multiplayer rules.
        constexpr std::array<WeaponRule, Shared::Combat::kWeaponSlots> kRules {{
            {0, 300, 2.0f, 4.0f},
            {},
            {0, 300, 2.0f, 7.0f},
            {0, 300, 2.0f, 6.0f},
            {0, 300, 2.0f, 10.0f},
            {0, 850, 0.0f, 0.0f},
            {6, 300, 30.0f, 30.0f},
            {6, 450, 100.0f, 75.0f},
            {6, 300, 70.0f, 40.0f},
            {7, 190, 90.0f, 55.0f},
            {50, 90, 120.0f, 50.0f},
            {8, 800, 70.0f, 250.0f},
            {2, 850, 40.0f, 250.0f},
            {5, 900, 400.0f, 80.0f},
            {5, 1100, 1500.0f, 100.0f},
            {0, 850, 0.0f, 0.0f},
        }};

        bool ValidWeapon(uint8_t weaponId) {
            return weaponId >= 2 && weaponId <= 15;
        }

        // The supported retail predmety.def records all occupy native weapon
        // slots. Item 4 and firearms 10-14 are big: only one can sit in the
        // coat while another may be selected in the hand. The five remaining
        // native weapon slots hold small weapons. Match NativeInventory's
        // Reconcile layout before publishing a state to clients.
        bool RepresentableInventory(const Shared::Combat::State &state) {
            unsigned smallCount = 0;
            unsigned coatCount = 0;
            for (uint8_t id = 2; id <= 15; ++id) {
                if ((state.inventoryMask & (1u << id)) == 0 || id == state.selectedWeapon) {
                    continue;
                }
                const bool big = Shared::Combat::IsLargeWeapon(id);
                if (big) {
                    ++coatCount;
                }
                else {
                    ++smallCount;
                }
            }
            return coatCount <= Shared::Combat::kCoatWeaponSlots && smallCount <= Shared::Combat::kSmallWeaponSlots
                && std::popcount(state.inventoryMask) <= Shared::Combat::kHandWeaponSlots + Shared::Combat::kSmallWeaponSlots + Shared::Combat::kCoatWeaponSlots;
        }

        // Fists, knuckleduster, knife and baseball bat.
        bool Melee(uint8_t weaponId) {
            return weaponId == 0 || (weaponId >= 2 && weaponId <= 4);
        }

        glm::vec3 Facing(const Shared::Entities::PlayerEntity &player) {
            // Movement stores angleAxis(atan2(dir.x, dir.z), up).
            return player.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        }

        bool Throwable(uint8_t weaponId) {
            return weaponId == 5 || weaponId == 15;
        }

        bool Firearm(uint8_t weaponId) {
            return weaponId >= 6 && weaponId <= 14;
        }

        // Retail S_item total ammunition (magazine plus carried) for firearms.
        uint16_t TotalAmmo(uint8_t weaponId) {
            switch (weaponId) {
            case 6: case 7: case 8: return 30;
            case 9: return 35;
            case 10: return 200;
            case 11: return 32;
            case 12: case 13: case 14: return 30;
            default: return 0;
            }
        }

        bool Held(const Shared::Combat::State &state, uint8_t weaponId) {
            return weaponId < Shared::Combat::kWeaponSlots && (state.inventoryMask & (1u << weaponId)) != 0;
        }

        bool ValidDirection(const Shared::Combat::Intent &intent) {
            const float squared = intent.directionX * intent.directionX + intent.directionY * intent.directionY + intent.directionZ * intent.directionZ;
            return std::isfinite(squared) && squared > 0.8f && squared < 1.2f;
        }

        bool ValidNativeShot(const Shared::Combat::Intent &intent, const Shared::Entities::PlayerEntity &shooter, bool shooterSeated) {
            const glm::vec3 origin(intent.originX, intent.originY, intent.originZ);
            const glm::vec3 nativeDirection(intent.nativeDirectionX, intent.nativeDirectionY, intent.nativeDirectionZ);
            const float length = glm::length(nativeDirection);
            const bool shotgun = intent.weaponId == 11 || intent.weaponId == 12;
            const float retailRange = kRules[intent.weaponId].reach;
            const uint8_t expectedPellets = shotgun && !shooterSeated ? Shared::Combat::kMaximumShotPellets : 1;
            if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) || !std::isfinite(length)
                || std::abs(length - retailRange) > std::max(0.05f, retailRange * 0.01f)
                || glm::distance(origin, shooter.position) > 8.0f || intent.pelletCount != expectedPellets) {
                return false;
            }
            const glm::vec3 claimedDirection(intent.directionX, intent.directionY, intent.directionZ);
            if (glm::dot(nativeDirection / length, claimedDirection) < 0.999f) {
                return false;
            }
            for (size_t index = 0; index < intent.pelletCount; ++index) {
                const auto &pellet = intent.pellets[index];
                const glm::vec3 trajectory(pellet.x, pellet.y, pellet.z);
                const float travel = glm::length(trajectory);
                if (!std::isfinite(travel) || travel < 0.001f || travel > length * 1.2f || glm::dot(trajectory / travel, claimedDirection) < 0.75f) {
                    return false;
                }
                if (pellet.staticHit) {
                    const glm::vec3 hit(pellet.hitX, pellet.hitY, pellet.hitZ);
                    const glm::vec3 normal(pellet.normalX, pellet.normalY, pellet.normalZ);
                    if (!std::isfinite(hit.x) || !std::isfinite(hit.y) || !std::isfinite(hit.z) || !std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z)
                        || glm::distance(hit, origin + trajectory) > 0.1f || glm::length(normal) < 0.5f || glm::length(normal) > 1.5f) {
                        return false;
                    }
                }
            }
            if (intent.pelletCount > 1) {
                for (size_t index = 0; index < intent.pelletCount * 2; ++index) {
                    const float draw = intent.scatterRandom[index];
                    if (!std::isfinite(draw) || draw < 0.0f || draw > 1.0f) {
                        return false;
                    }
                }
            }
            return true;
        }
    } // namespace

    void CombatService::RegisterRPC(Player::PlayerService &players, Car::CarService &cars) {
        _players = &players;
        _cars    = &cars;
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Combat::Intent>([this](const Shared::Combat::Intent &intent, MafiaNet::Packet *packet) {
            OnIntent(intent, MafiaNet::ToPeerGuid(packet->guid));
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Combat::HitReport>([this](const Shared::Combat::HitReport &report, MafiaNet::Packet *packet) {
            OnHitReport(report, MafiaNet::ToPeerGuid(packet->guid));
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Combat::DetonationReport>([this](const Shared::Combat::DetonationReport &report, MafiaNet::Packet *packet) {
            OnDetonationReport(report, MafiaNet::ToPeerGuid(packet->guid));
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Combat::FatalFallReport>([this](const Shared::Combat::FatalFallReport &report, MafiaNet::Packet *packet) {
            OnFatalFallReport(report, MafiaNet::ToPeerGuid(packet->guid));
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Combat::VehicleImpact>([this](const Shared::Combat::VehicleImpact &report, MafiaNet::Packet *packet) {
            OnVehicleImpact(report, MafiaNet::ToPeerGuid(packet->guid));
        });
    }

    void CombatService::OnPlayerConnect(uint64_t networkId) {
        auto *player = _players->FindByNetworkId(networkId);
        if (!player) {
            return;
        }
        auto &session                   = _sessions[networkId];
        session.state.networkId         = networkId;
        session.state.missionGeneration = player->missionGeneration;
        session.state.spawnGeneration   = player->spawnGeneration;
        session.state.health            = player->health;
        session.state.spawned           = player->spawned;
        session.state.alive             = player->alive;

        const auto recipient = MafiaNet::ToGuid(_players->GuidForNetworkId(networkId));
        auto *network        = Framework::CoreModules::GetNetworkPeer();
        for (auto &[id, existing] : _sessions) {
            auto snapshot = existing.state;
            network->SendRPC(snapshot, recipient);
        }
        Publish(session);
    }

    void CombatService::OnPlayerDisconnect(uint64_t networkId) {
        auto it = _sessions.find(networkId);
        if (it == _sessions.end()) {
            return;
        }
        it->second.state.spawned = false;
        it->second.state.alive   = false;
        ++it->second.state.revision;
        Publish(it->second);
        _sessions.erase(it);
    }

    void CombatService::OnPlayerSpawn(uint64_t networkId) {
        auto *player = _players->FindByNetworkId(networkId);
        if (!player) {
            return;
        }
        auto &session                   = _sessions[networkId];
        session                         = {};
        session.state.networkId         = networkId;
        session.state.missionGeneration = player->missionGeneration;
        session.state.spawnGeneration   = player->spawnGeneration;
        session.state.health            = player->health;
        session.state.spawned           = player->spawned;
        session.state.alive             = player->alive;
        Publish(session);
    }

    void CombatService::OnPlayerDespawn(uint64_t networkId) {
        OnPlayerSpawn(networkId);
    }

    void CombatService::ResetForMission() {
        _sessions.clear();
        _throws.clear();
        _fires.clear();
        _vehicleImpacts.clear();
        _pendingVehicleImpacts.clear();
    }

    void CombatService::Update() {
        // The native contact RPC is emitted inside the car physics tick,
        // before that tick's unreliable movement update. Give its pose a
        // short time to arrive before validating the impact.
        const auto impactCutoff = Clock::now() - std::chrono::milliseconds(100);
        while (!_pendingVehicleImpacts.empty() && _pendingVehicleImpacts.front().receivedAt <= impactCutoff) {
            auto pending = _pendingVehicleImpacts.front();
            _pendingVehicleImpacts.pop_front();
            ApplyVehicleImpact(pending.report, pending.sender, pending.receivedAt);
        }
        UpdateThrows();
        _players->ForEach([this](Shared::Entities::PlayerEntity *player) {
            auto &session = _sessions[player->GetNetworkID()];
            if (session.pendingFallUntil != Clock::time_point {}) {
                const auto until         = session.pendingFallUntil;
                session.pendingFallUntil = {};
                if (player->spawned && player->alive && Clock::now() <= until) {
                    if (_players->HasRecentFatalFall(player->GetNetworkID())) {
                        SetHealth(player->GetNetworkID(), 0.0f, 0, std::nullopt, DamageCause::Fall);
                    }
                    else {
                        session.pendingFallUntil = until;
                    }
                }
            }
            auto &state = session.state;
            if (state.networkId == 0 || state.missionGeneration != player->missionGeneration || state.spawnGeneration != player->spawnGeneration) {
                OnPlayerSpawn(player->GetNetworkID());
                return;
            }
            if (state.health != player->health || state.alive != player->alive || state.spawned != player->spawned) {
                state.health  = player->health;
                state.alive   = player->alive;
                state.spawned = player->spawned;
                Publish(session);
            }
        });
    }

    CombatService::Session *CombatService::FindCurrent(uint64_t networkId) {
        auto it      = _sessions.find(networkId);
        auto *player = _players->FindByNetworkId(networkId);
        if (it == _sessions.end() || !player || it->second.state.missionGeneration != player->missionGeneration || it->second.state.spawnGeneration != player->spawnGeneration) {
            return nullptr;
        }
        return &it->second;
    }

    std::optional<CombatService::WeaponInfo> CombatService::Weapon(uint8_t weaponId) {
        // Stock predmety.def item ids.
        static constexpr std::array<const char *, 16> kNames {nullptr, nullptr, "Knuckle duster", "Knife", "Baseball bat", "Molotov cocktail", "Colt Detective Special",
                                                             "S&W Model 27 Magnum", "S&W Model 10 M&P", "Colt 1911", "Thompson 1928", "Pump-action shotgun",
                                                             "Sawed-off shotgun", "US Rifle M1903 Springfield", "Mosin-Nagant 1891/30", "Grenade"};
        if (!ValidWeapon(weaponId)) {
            return std::nullopt;
        }
        const char *kind = Melee(weaponId) ? "melee" : Throwable(weaponId) ? "throwable" : "firearm";
        return WeaponInfo {kNames[weaponId], kind, kRules[weaponId].capacity};
    }

    bool CombatService::GiveWeapon(uint64_t networkId, uint8_t weaponId, uint16_t loaded, uint16_t reserve) {
        auto *session = FindCurrent(networkId);
        if (!session || !session->state.spawned || !session->state.alive || !ValidWeapon(weaponId)) {
            return false;
        }
        auto state = session->state;
        // Retail inventory has the equipped hand item, five weapon slots and
        // one coat slot. Never publish more weapons than a native human can
        // carry, even while item-type placement is being audited.
        if (!Held(state, weaponId) && std::popcount(state.inventoryMask) >= Shared::Combat::kHandWeaponSlots + Shared::Combat::kSmallWeaponSlots + Shared::Combat::kCoatWeaponSlots) {
            return false;
        }
        const auto rule = kRules[weaponId];
        state.inventoryMask |= (1u << weaponId);
        state.ammo[weaponId].loaded  = Firearm(weaponId) ? std::min<uint16_t>(loaded, rule.capacity) : loaded;
        state.ammo[weaponId].reserve = reserve;
        if (state.selectedWeapon == 0) {
            state.selectedWeapon = weaponId;
        }
        if (!RepresentableInventory(state)) {
            return false;
        }
        session->state = state;
        Publish(*session);
        return true;
    }

    bool CombatService::ReplaceInventory(uint64_t networkId, const std::vector<InventoryEntry> &entries, std::optional<uint8_t> selected) {
        auto *session = FindCurrent(networkId);
        if (!session || !session->state.spawned || !session->state.alive || entries.size() > Shared::Combat::kHandWeaponSlots + Shared::Combat::kSmallWeaponSlots + Shared::Combat::kCoatWeaponSlots) {
            return false;
        }

        auto replacement = session->state;
        replacement.inventoryMask = 0;
        replacement.ammo.fill({});
        replacement.aiming = false;
        for (const auto &entry : entries) {
            if (!ValidWeapon(entry.weaponId) || Held(replacement, entry.weaponId)) {
                return false;
            }
            replacement.inventoryMask |= (1u << entry.weaponId);
            replacement.ammo[entry.weaponId].loaded = Firearm(entry.weaponId)
                ? std::min<uint16_t>(entry.loaded, kRules[entry.weaponId].capacity) : entry.loaded;
            replacement.ammo[entry.weaponId].reserve = entry.reserve;
        }

        const uint8_t nextSelected = selected.value_or(entries.empty() ? 0 : entries.front().weaponId);
        if (nextSelected != 0 && !Held(replacement, nextSelected)) {
            return false;
        }
        replacement.selectedWeapon = nextSelected;
        if (!RepresentableInventory(replacement)) {
            return false;
        }
        session->state = replacement;
        Publish(*session);
        return true;
    }

    bool CombatService::RemoveWeapon(uint64_t networkId, uint8_t weaponId) {
        auto *session = FindCurrent(networkId);
        if (!session || !session->state.spawned || !session->state.alive || !ValidWeapon(weaponId) || !Held(session->state, weaponId)) {
            return false;
        }
        auto &state = session->state;
        state.inventoryMask &= ~(1u << weaponId);
        state.ammo[weaponId] = {};
        if (state.selectedWeapon == weaponId) {
            state.selectedWeapon = 0;
            state.aiming = false;
        }
        Publish(*session);
        return true;
    }

    bool CombatService::SetWeaponAmmo(uint64_t networkId, uint8_t weaponId, uint16_t loaded, uint16_t reserve) {
        auto *session = FindCurrent(networkId);
        if (!session || !session->state.spawned || !session->state.alive || !ValidWeapon(weaponId) || !Held(session->state, weaponId)) {
            return false;
        }
        auto &ammo   = session->state.ammo[weaponId];
        ammo.loaded  = Firearm(weaponId) ? std::min<uint16_t>(loaded, kRules[weaponId].capacity) : loaded;
        ammo.reserve = reserve;
        Publish(*session);
        return true;
    }

    bool CombatService::SelectWeapon(uint64_t networkId, uint8_t weaponId) {
        auto *session = FindCurrent(networkId);
        if (!session || !session->state.spawned || !session->state.alive || (weaponId != 0 && !Held(session->state, weaponId))) {
            return false;
        }
        auto replacement = session->state;
        replacement.selectedWeapon = weaponId;
        if (!RepresentableInventory(replacement)) {
            return false;
        }
        const bool changed = session->state.selectedWeapon != weaponId;
        session->state = replacement;
        if (changed) {
            session->state.aiming = false;
        }
        Publish(*session);
        return true;
    }

    bool CombatService::SetHealth(uint64_t networkId, float health, uint64_t sourceId, std::optional<uint8_t> weaponId, DamageCause cause) {
        auto *before          = _players->FindByNetworkId(networkId);
        const float oldHealth = before ? before->health : 0.0f;
        if (before && before->alive && health <= 0.0f) {
            if (auto *session = FindCurrent(networkId)) {
                if (session->state.deathAnimation == 0) {
                    session->state.deathAnimation = 134 + static_cast<uint16_t>((networkId ^ before->spawnGeneration) % 3);
                }
            }
        }
        const bool dying = before && before->alive && health <= 0.0f;
        uint64_t droppedPickupId = 0;
        if (dying) {
            // Retail death drops only the selected weapon (Do_WeaponDrop).
            if (auto *session = FindCurrent(networkId); session && session->state.selectedWeapon != 0) {
                const uint8_t weaponId = session->state.selectedWeapon;
                droppedPickupId = DropWeapon(*session, *before, weaponId);
                session->state.inventoryMask &= ~(1u << weaponId);
                session->state.ammo[weaponId] = {};
                session->state.selectedWeapon = 0;
            }
        }
        if (!_players->SetHealth(networkId, health)) {
            return false;
        }
        auto *session = FindCurrent(networkId);
        auto *player  = _players->FindByNetworkId(networkId);
        if (session && player) {
            session->state.health = player->health;
            session->state.alive  = player->alive;
            Publish(*session);
            if (droppedPickupId != 0 && _droppedCallback) {
                _droppedCallback(droppedPickupId, networkId);
            }
            if (_transitionCallback && oldHealth != player->health) {
                _transitionCallback(networkId, sourceId, weaponId, cause, oldHealth, player->health, session->state.deathAnimation);
            }
        }
        return true;
    }

    uint64_t CombatService::DropWeapon(Session &session, Shared::Entities::PlayerEntity &player, uint8_t weaponId) {
        if (!_dropCallback || !ValidWeapon(weaponId) || !Held(session.state, weaponId)) {
            return 0;
        }
        const auto &ammo = session.state.ammo[weaponId];
        const glm::vec3 facing = Facing(player);
        const glm::vec3 position = player.position + glm::vec3(facing.x, 0.0f, facing.z) * 0.6f + glm::vec3(0.0f, 0.05f, 0.0f);
        return _dropCallback(player.GetNetworkID(), weaponId, Firearm(weaponId) || Throwable(weaponId) ? ammo.loaded : 1, ammo.reserve,
                             position, std::atan2(facing.x, facing.z));
    }

    uint64_t CombatService::DropWeapon(uint64_t networkId, uint8_t weaponId) {
        auto *session = FindCurrent(networkId);
        auto *player = _players->FindByNetworkId(networkId);
        if (!session || !player || !session->state.spawned || !session->state.alive || !ValidWeapon(weaponId) || !Held(session->state, weaponId)
            || _cars->SeatForPlayer(networkId, player->spawnGeneration).has_value()) {
            return 0;
        }
        const uint64_t pickupId = DropWeapon(*session, *player, weaponId);
        if (pickupId == 0) {
            return 0;
        }
        auto &state = session->state;
        state.inventoryMask &= ~(1u << weaponId);
        state.ammo[weaponId] = {};
        if (state.selectedWeapon == weaponId) {
            state.selectedWeapon = 0;
            state.aiming = false;
        }
        session->meleeStartedAt = {};
        Publish(*session);
        Shared::Combat::Intent action;
        action.networkId = networkId;
        action.missionGeneration = state.missionGeneration;
        action.spawnGeneration = state.spawnGeneration;
        action.action = Shared::Combat::Action::Drop;
        action.weaponId = weaponId;
        Emit(action, *session);
        if (_droppedCallback) {
            _droppedCallback(pickupId, networkId);
        }
        return pickupId;
    }

    bool CombatService::TakeWeapon(uint64_t networkId, uint8_t weaponId, uint16_t loaded, uint16_t reserve, uint16_t &leftLoaded, uint16_t &leftReserve) {
        auto *session = FindCurrent(networkId);
        leftLoaded  = loaded;
        leftReserve = reserve;
        if (!session || !session->state.spawned || !session->state.alive || !ValidWeapon(weaponId)) {
            return false;
        }
        auto &state = session->state;
        if (Held(state, weaponId)) {
            auto &ammo = state.ammo[weaponId];
            if (Firearm(weaponId)) {
                // G_Inventory::DobijNaboje: carried ammunition is capped at the
                // weapon's total; the magazine and reserve on the floor both
                // refill it.
                const uint16_t total = TotalAmmo(weaponId);
                const uint16_t carried = static_cast<uint16_t>(ammo.loaded + ammo.reserve);
                const uint16_t room = carried < total ? static_cast<uint16_t>(total - carried) : 0;
                const uint16_t offered = static_cast<uint16_t>(loaded + reserve);
                const uint16_t taken = std::min(room, offered);
                if (taken == 0) {
                    return false;
                }
                ammo.reserve = static_cast<uint16_t>(ammo.reserve + taken);
                const uint16_t remaining = static_cast<uint16_t>(offered - taken);
                leftLoaded  = std::min(loaded, remaining);
                leftReserve = static_cast<uint16_t>(remaining - leftLoaded);
            }
            else if (Throwable(weaponId)) {
                ammo.loaded = static_cast<uint16_t>(std::min<int>(ammo.loaded + loaded, 10));
                leftLoaded = leftReserve = 0;
            }
            else {
                return false;
            }
            Publish(*session);
            return true;
        }
        if (!GiveWeapon(networkId, weaponId, loaded, reserve)) {
            return false;
        }
        leftLoaded = leftReserve = 0;
        return true;
    }

    const Shared::Combat::State *CombatService::GetState(uint64_t networkId) const {
        const auto it = _sessions.find(networkId);
        return it == _sessions.end() ? nullptr : &it->second.state;
    }

    void CombatService::Publish(Session &session, MafiaNet::PeerGuid except) {
        ++session.state.revision;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(session.state, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::ToGuid(except));
    }

    void CombatService::Emit(const Shared::Combat::Intent &intent, const Session &session) {
        Shared::Combat::Event event;
        event.networkId         = intent.networkId;
        event.missionGeneration = intent.missionGeneration;
        event.spawnGeneration   = intent.spawnGeneration;
        event.revision          = session.state.revision;
        event.action            = intent.action;
        event.weaponId          = intent.weaponId;
        event.targetNetworkId   = intent.targetNetworkId;
        event.directionX        = intent.directionX;
        event.directionY        = intent.directionY;
        event.directionZ        = intent.directionZ;
        event.meleeIndex        = intent.meleeIndex;
        event.meleeHoldMs       = intent.meleeHoldMs;
        event.throwHoldMs       = intent.throwHoldMs;
        event.sequence          = intent.sequence;
        event.grenadeType       = intent.grenadeType;
        if (intent.action == Shared::Combat::Action::Throw) {
            event.originX          = intent.originX;
            event.originY          = intent.originY;
            event.originZ          = intent.originZ;
            event.nativeDirectionX = intent.nativeDirectionX;
            event.nativeDirectionY = intent.nativeDirectionY;
            event.nativeDirectionZ = intent.nativeDirectionZ;
        }
        if (intent.action == Shared::Combat::Action::Fire) {
            event.originX = intent.originX;
            event.originY = intent.originY;
            event.originZ = intent.originZ;
            event.nativeDirectionX = intent.nativeDirectionX;
            event.nativeDirectionY = intent.nativeDirectionY;
            event.nativeDirectionZ = intent.nativeDirectionZ;
            event.pelletCount = intent.pelletCount;
            event.scatterRandom = intent.scatterRandom;
            event.pellets = intent.pellets;
        }
        if (intent.action != Shared::Combat::Action::Aim) {
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(event);
        }
        if (_actionCallback) {
            _actionCallback(event);
        }
    }

    void CombatService::OnIntent(const Shared::Combat::Intent &intent, MafiaNet::PeerGuid sender) {
        auto *player  = _players->FindByGuid(sender);
        auto *session = FindCurrent(intent.networkId);
        if (!player || !session || player->GetNetworkID() != intent.networkId || !player->spawned || !player->alive || intent.missionGeneration != player->missionGeneration || intent.spawnGeneration != player->spawnGeneration || intent.weaponId >= Shared::Combat::kWeaponSlots
            || (session->hasSequence && static_cast<int32_t>(intent.sequence - session->lastSequence) <= 0)) {
            return;
        }
        auto &state = session->state;
        if (intent.action != Shared::Combat::Action::Equip && intent.action != Shared::Combat::Action::Aim && intent.action != Shared::Combat::Action::Crouch &&
            !((intent.action == Shared::Combat::Action::Melee || intent.action == Shared::Combat::Action::MeleeStart || intent.action == Shared::Combat::Action::MeleeCancel)
              && intent.weaponId == 0) && !Held(state, intent.weaponId)) {
            return;
        }

        const auto now = Clock::now();
        bool emitAimChange = true;
        uint64_t droppedPickupId = 0;
        switch (intent.action) {
        case Shared::Combat::Action::Equip:
            if (intent.weaponId != 0 && !Held(state, intent.weaponId)) {
                return;
            }
            {
                auto candidate = state;
                candidate.selectedWeapon = intent.weaponId;
                if (!RepresentableInventory(candidate)) {
                    return;
                }
            }
            state.selectedWeapon = intent.weaponId;
            state.aiming = false;
            session->meleeStartedAt = {};
            session->grenadeStartedAt = {};
            session->grenadeReleasedAt = {};
            break;
        case Shared::Combat::Action::Aim: {
            if (state.selectedWeapon != intent.weaponId) {
                return;
            }
            if (intent.aiming && !ValidDirection(intent)) {
                return;
            }
            const float poseLengthSquared = intent.poseTargetOffsetX * intent.poseTargetOffsetX + intent.poseTargetOffsetY * intent.poseTargetOffsetY + intent.poseTargetOffsetZ * intent.poseTargetOffsetZ;
            if (!std::isfinite(poseLengthSquared) || poseLengthSquared < 1.0f || poseLengthSquared > 625.0f) {
                return;
            }
            emitAimChange = intent.aiming || state.aiming != intent.aiming;
            state.aiming = intent.aiming;
            state.poseTargetOffsetX = intent.poseTargetOffsetX;
            state.poseTargetOffsetY = intent.poseTargetOffsetY;
            state.poseTargetOffsetZ = intent.poseTargetOffsetZ;
            if (intent.aiming) {
                state.aimDirectionX = intent.directionX;
                state.aimDirectionY = intent.directionY;
                state.aimDirectionZ = intent.directionZ;
            }
            break;
        }
        case Shared::Combat::Action::Crouch:
            state.crouching = intent.crouching;
            break;
        case Shared::Combat::Action::MeleeStart:
            if (!Melee(intent.weaponId) || state.selectedWeapon != intent.weaponId ||
                _cars->SeatForPlayer(intent.networkId, intent.spawnGeneration).has_value()) {
                return;
            }
            session->meleeStartedAt = now;
            session->meleeStartSequence = intent.sequence;
            session->meleeStartWeapon = intent.weaponId;
            break;
        case Shared::Combat::Action::MeleeCancel:
            if (session->meleeStartedAt == Clock::time_point {} || session->meleeStartWeapon != intent.weaponId ||
                state.selectedWeapon != intent.weaponId) {
                return;
            }
            session->meleeStartedAt = {};
            break;
        case Shared::Combat::Action::Melee: {
            if (!Melee(intent.weaponId) || state.selectedWeapon != intent.weaponId ||
                (intent.meleeIndex != Shared::Combat::kHeavyMelee && (intent.meleeIndex < 0 || intent.meleeIndex > 3)) ||
                intent.meleeHoldMs > Shared::Combat::kMaximumMeleeHoldMs ||
                session->meleeStartedAt == Clock::time_point {} || session->meleeStartWeapon != intent.weaponId ||
                static_cast<int32_t>(intent.sequence - session->meleeStartSequence) <= 0 ||
                now - session->meleeStartedAt > std::chrono::milliseconds(Shared::Combat::kMaximumMeleeHoldMs + Shared::Combat::kMeleeNetworkGraceMs) ||
                _cars->SeatForPlayer(intent.networkId, intent.spawnGeneration).has_value()) {
                return;
            }
            session->meleeStartedAt = {};
            if (session->lastMelee == Clock::time_point {} || now - session->lastMelee >= std::chrono::milliseconds(kRules[intent.weaponId].intervalMs)) {
                session->lastMelee = now;
                session->melee.push_back({intent.sequence, intent.weaponId, intent.meleeIndex == Shared::Combat::kHeavyMelee, now, false});
            }
            while (!session->melee.empty() && (session->melee.size() > 16 || now - session->melee.front().acceptedAt > std::chrono::seconds(3))) {
                session->melee.pop_front();
            }
            break;
        }
        case Shared::Combat::Action::Drop:
            session->meleeStartedAt = {};
            droppedPickupId = DropWeapon(*session, *player, intent.weaponId);
            if (droppedPickupId == 0) {
                return;
            }
            state.inventoryMask &= ~(1u << intent.weaponId);
            state.ammo[intent.weaponId] = {};
            if (state.selectedWeapon == intent.weaponId) {
                state.selectedWeapon = 0;
                state.aiming = false;
            }
            session->grenadeStartedAt = {};
            session->grenadeReleasedAt = {};
            break;
        case Shared::Combat::Action::ThrowStart:
            if (!Throwable(intent.weaponId) || state.selectedWeapon != intent.weaponId || state.ammo[intent.weaponId].loaded == 0
                || session->grenadeStartedAt != Clock::time_point {} || _cars->SeatForPlayer(intent.networkId, intent.spawnGeneration).has_value()) {
                return;
            }
            session->grenadeStartedAt = now;
            session->grenadeReleasedAt = {};
            session->grenadeStartSequence = intent.sequence;
            session->grenadeWeapon = intent.weaponId;
            break;
        case Shared::Combat::Action::ThrowCancel:
            if (session->grenadeStartedAt == Clock::time_point {} || session->grenadeReleasedAt != Clock::time_point {}
                || session->grenadeWeapon != intent.weaponId || state.selectedWeapon != intent.weaponId) {
                return;
            }
            session->grenadeStartedAt = {};
            break;
        case Shared::Combat::Action::ThrowRelease:
            if (session->grenadeStartedAt == Clock::time_point {} || session->grenadeReleasedAt != Clock::time_point {}
                || session->grenadeWeapon != intent.weaponId || state.selectedWeapon != intent.weaponId
                || static_cast<int32_t>(intent.sequence - session->grenadeStartSequence) <= 0
                || intent.throwHoldMs < Shared::Combat::kMinimumThrowHoldMs || intent.throwHoldMs > Shared::Combat::kMaximumThrowChargeMs
                || !ValidDirection(intent)
                || _cars->SeatForPlayer(intent.networkId, intent.spawnGeneration).has_value()) {
                return;
            }
            session->grenadeReleasedAt = now;
            session->grenadeReleaseSequence = intent.sequence;
            session->grenadeDirection = {intent.directionX, intent.directionY, intent.directionZ};
            break;
        case Shared::Combat::Action::Reload: {
            if (!Firearm(intent.weaponId) || state.selectedWeapon != intent.weaponId) {
                return;
            }
            auto &ammo = state.ammo[intent.weaponId];
            const auto next = Shared::Combat::ReloadWeaponAmmo(intent.weaponId, kRules[intent.weaponId].capacity, ammo.loaded, ammo.reserve);
            if (next.loaded == ammo.loaded && next.reserve == ammo.reserve) {
                return;
            }
            ammo.loaded = next.loaded;
            ammo.reserve = next.reserve;
            break;
        }
        case Shared::Combat::Action::Fire:
        case Shared::Combat::Action::Throw: {
            if (state.selectedWeapon != intent.weaponId || !ValidDirection(intent)
                || (intent.action == Shared::Combat::Action::Fire && !ValidNativeShot(intent, *player, _cars->SeatForPlayer(intent.networkId, intent.spawnGeneration).has_value()))) {
                return;
            }
            if (intent.action == Shared::Combat::Action::Throw) {
                // Retail NewGrenade types: the grenade (15) is type 0, any other
                // item the Molotov type 3. The spawn point is the hand plus one
                // unit; the impulse is charge x 0.015 x strength, capped at a
                // two second charge.
                const glm::vec3 origin(intent.originX, intent.originY, intent.originZ);
                const glm::vec3 impulse(intent.nativeDirectionX, intent.nativeDirectionY, intent.nativeDirectionZ);
                const float speed = glm::length(impulse);
                const uint8_t expectedType = intent.weaponId == 15 ? 0 : 3;
                if (!Throwable(intent.weaponId) || intent.grenadeType != expectedType || !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
                    !std::isfinite(origin.z) || glm::distance(origin, player->position + glm::vec3(0.0f, 1.2f, 0.0f)) > 3.5f || !std::isfinite(speed) ||
                    speed < 1.0f || speed > 45.0f || _cars->SeatForPlayer(intent.networkId, intent.spawnGeneration).has_value()
                    || session->grenadeReleasedAt == Clock::time_point {} || session->grenadeWeapon != intent.weaponId
                    || static_cast<int32_t>(intent.sequence - session->grenadeReleaseSequence) <= 0
                    || now - session->grenadeReleasedAt > std::chrono::seconds(3)
                    || glm::dot(session->grenadeDirection, glm::vec3(intent.directionX, intent.directionY, intent.directionZ)) < 0.75f) {
                    return;
                }
            }
            if (intent.action == Shared::Combat::Action::Fire && Throwable(intent.weaponId)) {
                return;
            }
            const auto interval = std::chrono::milliseconds(kRules[intent.weaponId].intervalMs);
            if (session->lastAction != Clock::time_point {} && now - session->lastAction < interval) {
                return;
            }
            auto &ammo = state.ammo[intent.weaponId];
            if (Firearm(intent.weaponId) || Throwable(intent.weaponId)) {
                if (ammo.loaded == 0) {
                    return;
                }
                --ammo.loaded;
            }
            session->lastAction = now;
            if (intent.action == Shared::Combat::Action::Throw) {
                session->grenadeStartedAt = {};
                session->grenadeReleasedAt = {};
                // Retail notify 41 removes the selected item after the throw.
                // Any remaining server-owned quantity stays in a pocket and
                // must be selected again for the next throw.
                state.selectedWeapon = 0;
                state.aiming = false;
                if (ammo.loaded == 0) {
                    state.inventoryMask &= ~(1u << intent.weaponId);
                }
                ThrowRecord record;
                record.throwerId         = intent.networkId;
                record.missionGeneration = intent.missionGeneration;
                record.sequence          = intent.sequence;
                record.grenadeType       = intent.grenadeType;
                record.origin            = glm::vec3(intent.originX, intent.originY, intent.originZ);
                record.speed             = glm::length(glm::vec3(intent.nativeDirectionX, intent.nativeDirectionY, intent.nativeDirectionZ));
                record.acceptedAt        = now;
                _throws.push_back(record);
                while (_throws.size() > 128) {
                    _throws.pop_front();
                }
            }
            if (intent.action == Shared::Combat::Action::Fire) {
                session->fires.push_back({intent, now, 0});
                while (!session->fires.empty() && (session->fires.size() > 64 || now - session->fires.front().acceptedAt > std::chrono::seconds(2))) {
                    session->fires.pop_front();
                }
            }
            break;
        }
        default: return;
        }
        session->lastSequence = intent.sequence;
        session->hasSequence  = true;
        // The native local player already owns its camera and aim pose.
        // Echoing every pose revision makes it reconcile stock state during
        // local movement, while observers still need the accepted pose.
        Publish(*session, intent.action == Shared::Combat::Action::Aim ? sender : MafiaNet::UNASSIGNED_PEER_GUID);
        if (emitAimChange) {
            Emit(intent, *session);
        }
        if (droppedPickupId != 0 && _droppedCallback) {
            _droppedCallback(droppedPickupId, intent.networkId);
        }
    }

    void CombatService::OnHitReport(const Shared::Combat::HitReport &report, MafiaNet::PeerGuid sender) {
        auto *shooter = _players->FindByGuid(sender);
        auto *target  = _players->FindByNetworkId(report.targetNetworkId);
        auto *session = FindCurrent(report.shooterNetworkId);
        if (!shooter || !target || !session || shooter->GetNetworkID() != report.shooterNetworkId || !shooter->spawned || !shooter->alive || !target->spawned || !target->alive || shooter->missionGeneration != report.missionGeneration
            || target->missionGeneration != report.missionGeneration || shooter->spawnGeneration != report.shooterSpawnGeneration || target->spawnGeneration != report.targetSpawnGeneration || report.shotSequence == 0) {
            return;
        }
        // A seated occupant is hit through the car and reports the in-car
        // marker instead of a skeleton body part.
        if (report.kind == Shared::Combat::HitReport::Kind::Melee) {
            OnMeleeHit(report, *shooter, *target, *session);
            return;
        }
        const bool targetSeated = _cars->SeatForPlayer(report.targetNetworkId, report.targetSpawnGeneration).has_value();
        const bool skeletonPart = report.bodyPart >= 1 && report.bodyPart <= 6;
        if (!skeletonPart && !(targetSeated && report.bodyPart == Shared::Combat::HitReport::kInCarBodyPart)) {
            return;
        }
        const auto fire = std::find_if(session->fires.begin(), session->fires.end(), [&](const Session::FireRecord &candidate) { return candidate.intent.sequence == report.shotSequence; });
        if (fire == session->fires.end() || fire->intent.missionGeneration != report.missionGeneration || fire->intent.spawnGeneration != report.shooterSpawnGeneration || !Firearm(fire->intent.weaponId)
            || Clock::now() - fire->acceptedAt > std::chrono::seconds(2) || report.pelletIndex >= fire->intent.pelletCount || (fire->consumedPellets & (1u << report.pelletIndex)) != 0) {
            return;
        }
        const float directionSquared = report.directionX * report.directionX + report.directionY * report.directionY + report.directionZ * report.directionZ;
        if (!std::isfinite(directionSquared) || directionSquared < 0.8f || directionSquared > 1.2f) {
            return;
        }
        const auto &pellet = fire->intent.pellets[report.pelletIndex];
        const glm::vec3 fireDirection = glm::normalize(glm::vec3(pellet.x, pellet.y, pellet.z));
        const glm::vec3 reportedDirection(report.directionX, report.directionY, report.directionZ);
        if (glm::dot(fireDirection, reportedDirection) < 0.995f) {
            return;
        }
        const glm::vec3 origin(fire->intent.originX, fire->intent.originY, fire->intent.originZ);
        const glm::vec3 hit(report.hitX, report.hitY, report.hitZ);
        const auto delta     = target->position - origin;
        const float distance = glm::length(delta);
        const glm::vec3 direction(report.directionX, report.directionY, report.directionZ);
        const float forward = glm::dot(delta, direction);
        const float lateral = glm::length(delta - forward * direction);
        const float pelletReach = glm::length(glm::vec3(pellet.x, pellet.y, pellet.z));
        const glm::vec3 hitDelta = hit - origin;
        const float hitDistance = glm::length(hitDelta);
        const float hitForward = glm::dot(hitDelta, fireDirection);
        const float hitLateral = glm::length(hitDelta - hitForward * fireDirection);
        const float nativeRange = glm::length(glm::vec3(fire->intent.nativeDirectionX, fire->intent.nativeDirectionY, fire->intent.nativeDirectionZ));
        if (!std::isfinite(distance) || !std::isfinite(hitDistance) || !std::isfinite(hitLateral) || distance > kRules[fire->intent.weaponId].reach + 4.0f
            || forward < -2.0f || forward > pelletReach + 4.0f || lateral > 4.0f || hitForward < 0.0f || hitForward > pelletReach + 1.0f
            || hitLateral > 1.5f || glm::distance(hit, target->position) > 4.5f || hitDistance >= nativeRange) {
            return;
        }
        fire->consumedPellets |= static_cast<uint16_t>(1u << report.pelletIndex);
        // Retail NewShoot splits a shotgun's item damage over its ten queued
        // bullets. TickShoot then reduces each bullet after one third of the
        // original (unclipped) weapon range. In-car Hit ignores body parts.
        float damage = kRules[fire->intent.weaponId].damage / fire->intent.pelletCount;
        if (hitDistance * 3.0f >= nativeRange) {
            damage *= std::max(0.0f, 1.0f - (hitDistance - nativeRange * 0.33000001f) / (nativeRange * 0.66000003f));
        }
        if (!targetSeated) {
            if (report.bodyPart <= 2) {
                damage *= 0.30000001f;
            }
            else if (report.bodyPart <= 4) {
                damage *= 0.5f;
            }
            else if (report.bodyPart == 6) {
                damage *= 1.5f;
            }
        }
        // The stock shot-type-10 flag belongs to both shotguns. On foot,
        // C_human::Hit forces a fatal result within three units of the shooter.
        // HitInCar returns before that branch and applies unscaled damage.
        const bool closeRangeShotgun = !targetSeated && (fire->intent.weaponId == 11 || fire->intent.weaponId == 12)
            && glm::dot(target->position - shooter->position, target->position - shooter->position) <= 9.0f;
        if (target->health <= damage || closeRangeShotgun) {
            if (auto *victim = FindCurrent(report.targetNetworkId)) {
                victim->state.deathAnimation = 131 + static_cast<uint16_t>((report.shotSequence + report.pelletIndex + report.bodyPart + report.targetNetworkId) % 6);
            }
        }
        const float appliedDamage = closeRangeShotgun ? target->health : damage;
        PublishDamage(report, appliedDamage, 0, fire->intent.weaponId);
    }

    void CombatService::OnVehicleImpact(const Shared::Combat::VehicleImpact &report, MafiaNet::PeerGuid sender) {
        if (_pendingVehicleImpacts.size() >= 256) {
            _pendingVehicleImpacts.pop_front();
        }
        _pendingVehicleImpacts.push_back({report, sender, Clock::now()});
    }

    void CombatService::ApplyVehicleImpact(const Shared::Combat::VehicleImpact &report, MafiaNet::PeerGuid sender,
                                           Clock::time_point receivedAt) {
        auto *car = _cars->Find(report.carId);
        auto *target = _players->FindByNetworkId(report.targetId);
        if (!car || !target || !target->spawned || !target->alive || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active ||
            car->simulationControllerGuid == 0 || car->simulationControllerGuid != static_cast<uint64_t>(sender) ||
            car->missionGeneration != report.missionGeneration || target->missionGeneration != report.missionGeneration ||
            target->spawnGeneration != report.targetSpawnGeneration ||
            _cars->SeatForPlayer(report.targetId, report.targetSpawnGeneration).has_value()) {
            return;
        }
        const glm::vec3 contact(report.contactX, report.contactY, report.contactZ);
        if (!std::isfinite(contact.x) || !std::isfinite(contact.y) || !std::isfinite(contact.z)) {
            return;
        }
        const auto impactSpeed = _cars->ValidateVehicleImpactGeometry(report.carId, report.missionGeneration,
                                                                       contact, target->position, receivedAt);
        if (!impactSpeed) {
            return;
        }
        const auto key = std::make_pair(report.carId, report.targetId);
        const auto now = Clock::now();
        const auto previous = _vehicleImpacts.find(key);
        if (previous != _vehicleImpacts.end() && now - previous->second < std::chrono::milliseconds(500)) {
            return;
        }
        if (_vehicleImpacts.size() >= 1024) {
            _vehicleImpacts.clear();
        }
        _vehicleImpacts[key] = now;
        const float damage = (*impactSpeed - 2.5f) * 4.2f; // C_human::Collision, car branch.
        uint64_t driverId = car->occupantIds[0];
        auto *driver = _players->FindByNetworkId(driverId);
        if (!driver || !driver->spawned || !driver->alive || driver->missionGeneration != report.missionGeneration ||
            driver->spawnGeneration != car->occupantGenerations[0]) {
            driverId = 0;
        }
        if (!SetHealth(report.targetId, target->health - damage, driverId, std::nullopt, DamageCause::Vehicle)) {
            return;
        }
        auto *victim = FindCurrent(report.targetId);
        if (!victim) {
            return;
        }
        Shared::Combat::DamageEvent event;
        event.targetNetworkId = report.targetId;
        event.missionGeneration = report.missionGeneration;
        event.targetSpawnGeneration = report.targetSpawnGeneration;
        event.targetRevision = victim->state.revision;
        event.bodyPart = 5;
        event.hitType = 4; // Retail car impact enters C_human::Hit as HIT_TYPE_SNIPER.
        event.damage = damage;
        event.directionX = car->velocity.x;
        event.directionY = car->velocity.y;
        event.directionZ = car->velocity.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(event);
    }

    void CombatService::PublishDamage(const Shared::Combat::HitReport &report, float damage, uint8_t hitType, std::optional<uint8_t> weaponId) {
        auto *target = _players->FindByNetworkId(report.targetNetworkId);
        const auto cause = hitType == 0 ? DamageCause::Firearm : hitType == 1 ? DamageCause::Melee : hitType == 2 ? DamageCause::Explosion : DamageCause::Fire;
        if (!target || !SetHealth(target->GetNetworkID(), target->health - damage, report.shooterNetworkId, weaponId, cause)) {
            return;
        }
        auto *victim = FindCurrent(report.targetNetworkId);
        if (!victim) {
            return;
        }
        Shared::Combat::DamageEvent event;
        event.targetNetworkId       = report.targetNetworkId;
        event.missionGeneration     = report.missionGeneration;
        event.targetSpawnGeneration = report.targetSpawnGeneration;
        event.targetRevision        = victim->state.revision;
        event.bodyPart              = report.bodyPart;
        event.hitType               = hitType;
        event.damage                = damage;
        event.directionX            = report.directionX;
        event.directionY            = report.directionY;
        event.directionZ            = report.directionZ;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(event);
    }

    void CombatService::OnMeleeHit(const Shared::Combat::HitReport &report, Shared::Entities::PlayerEntity &attacker,
                                   Shared::Entities::PlayerEntity &target, Session &session) {
        // C_human::AI sweeps the swing points after the attack animation's
        // strike note and hits torso (5) or head (6) with hit type 1.
        if (report.targetNetworkId == report.shooterNetworkId || (report.bodyPart != 5 && report.bodyPart != 6) ||
            _cars->SeatForPlayer(report.shooterNetworkId, report.shooterSpawnGeneration).has_value() ||
            _cars->SeatForPlayer(report.targetNetworkId, report.targetSpawnGeneration).has_value()) {
            return;
        }
        const auto now    = Clock::now();
        const auto record = std::find_if(session.melee.begin(), session.melee.end(),
                                         [&](const Session::MeleeRecord &candidate) { return candidate.sequence == report.shotSequence; });
        if (record == session.melee.end() || record->used || now - record->acceptedAt > std::chrono::milliseconds(1800)) {
            return;
        }
        // Retail reach is the animation itself; the item range is 2 units.
        const glm::vec3 offset = target.position - attacker.position;
        const glm::vec3 flat(offset.x, 0.0f, offset.z);
        const float distance   = glm::length(flat);
        const glm::vec3 hit(report.hitX, report.hitY, report.hitZ);
        if (!std::isfinite(distance) || distance > 2.6f || std::abs(offset.y) > 1.5f || !std::isfinite(hit.x) || !std::isfinite(hit.y) || !std::isfinite(hit.z) ||
            glm::length(glm::vec3(hit.x - target.position.x, 0.0f, hit.z - target.position.z)) > 1.6f ||
            hit.y < target.position.y - 0.5f || hit.y > target.position.y + 2.5f) {
            return;
        }
        const glm::vec3 facing = Facing(attacker);
        if (distance > 0.2f && glm::dot(glm::normalize(flat), facing) < -0.17f) {
            return;
        }
        record->used = true;
        float damage = kRules[record->weaponId].damage * (record->heavy ? 2.0f : 1.0f);
        // A heavy knife or bat blow from behind is lethal in retail when the
        // attacker faces the victim's way (1.2566 rad) and the victim is in
        // front of the attacker (0.9425 rad).
        if (record->heavy && (record->weaponId == 3 || record->weaponId == 4) && distance > 0.2f) {
            const glm::vec3 targetFacing = Facing(target);
            if (glm::dot(facing, targetFacing) > std::cos(1.2566f) && glm::dot(glm::normalize(flat), facing) > std::cos(0.9425f)) {
                damage = target.health + 1.0f;
            }
        }
        if (target.health <= damage) {
            if (auto *victim = FindCurrent(report.targetNetworkId)) {
                victim->state.deathAnimation = 131 + static_cast<uint16_t>((report.shotSequence + report.bodyPart + report.targetNetworkId) % 6);
            }
        }
        PublishDamage(report, damage, 1, record->weaponId);
    }

    std::optional<CombatService::CarImpactEvidence> CombatService::InspectCarImpact(const Shared::Car::HitReport &report, MafiaNet::PeerGuid sender) const {
        auto *shooter = _players->FindByGuid(sender);
        const auto session = _sessions.find(report.shooterId);
        if (!shooter || session == _sessions.end() || shooter->GetNetworkID() != report.shooterId || !shooter->spawned || !shooter->alive
            || shooter->missionGeneration != report.missionGeneration || shooter->spawnGeneration != report.shooterSpawnGeneration
            || report.carId == 0 || report.shotSequence == 0) {
            return std::nullopt;
        }
        const auto fire = std::find_if(session->second.fires.begin(), session->second.fires.end(), [&](const Session::FireRecord &candidate) {
            return candidate.intent.sequence == report.shotSequence;
        });
        if (fire == session->second.fires.end() || fire->intent.missionGeneration != report.missionGeneration
            || fire->intent.spawnGeneration != report.shooterSpawnGeneration || !Firearm(fire->intent.weaponId)
            || Clock::now() - fire->acceptedAt > std::chrono::seconds(2) || report.pelletIndex >= fire->intent.pelletCount
            || (fire->consumedPellets & (1u << report.pelletIndex)) != 0) {
            return std::nullopt;
        }
        const glm::vec3 reportedDirection(report.directionX, report.directionY, report.directionZ);
        const glm::vec3 hit(report.hitX, report.hitY, report.hitZ);
        const glm::vec3 normal(report.normalX, report.normalY, report.normalZ);
        const float directionSquared = glm::dot(reportedDirection, reportedDirection);
        const float normalLength = glm::length(normal);
        if (!std::isfinite(directionSquared) || directionSquared < 0.8f || directionSquared > 1.2f
            || !std::isfinite(hit.x) || !std::isfinite(hit.y) || !std::isfinite(hit.z)
            || !std::isfinite(normalLength) || normalLength < 0.5f || normalLength > 1.5f) {
            return std::nullopt;
        }
        const auto &pellet = fire->intent.pellets[report.pelletIndex];
        const glm::vec3 trajectory(pellet.x, pellet.y, pellet.z);
        const glm::vec3 fireDirection = glm::normalize(trajectory);
        const glm::vec3 origin(fire->intent.originX, fire->intent.originY, fire->intent.originZ);
        const glm::vec3 hitDelta = hit - origin;
        const float hitForward = glm::dot(hitDelta, fireDirection);
        const float hitLateral = glm::length(hitDelta - hitForward * fireDirection);
        const float hitDistance = glm::length(hitDelta);
        const float nativeRange = glm::length(glm::vec3(fire->intent.nativeDirectionX, fire->intent.nativeDirectionY, fire->intent.nativeDirectionZ));
        if (glm::dot(fireDirection, reportedDirection) < 0.995f || !std::isfinite(hitLateral) || !std::isfinite(hitDistance)
            || hitForward < 0.0f || hitForward > glm::length(trajectory) + 0.5f || hitLateral > 1.5f || hitDistance >= nativeRange) {
            return std::nullopt;
        }
        float damage = kRules[fire->intent.weaponId].damage / fire->intent.pelletCount;
        if (hitDistance * 3.0f >= nativeRange) {
            damage *= std::max(0.0f, 1.0f - (hitDistance - nativeRange * 0.33000001f) / (nativeRange * 0.66000003f));
        }
        return CarImpactEvidence {damage, origin, trajectory, fire->acceptedAt};
    }

    bool CombatService::ConsumeCarImpact(const Shared::Car::HitReport &report, MafiaNet::PeerGuid sender) {
        if (!InspectCarImpact(report, sender)) {
            return false;
        }
        auto &fires = _sessions.at(report.shooterId).fires;
        const auto fire = std::find_if(fires.begin(), fires.end(), [&](const Session::FireRecord &candidate) { return candidate.intent.sequence == report.shotSequence; });
        fire->consumedPellets |= static_cast<uint16_t>(1u << report.pelletIndex);
        return true;
    }

    void CombatService::OnDetonationReport(const Shared::Combat::DetonationReport &report, MafiaNet::PeerGuid sender) {
        auto *thrower = _players->FindByGuid(sender);
        if (!thrower || thrower->GetNetworkID() != report.networkId || thrower->missionGeneration != report.missionGeneration) {
            return;
        }
        const auto record = std::find_if(_throws.begin(), _throws.end(), [&](const ThrowRecord &candidate) {
            return candidate.throwerId == report.networkId && candidate.sequence == report.sequence && candidate.missionGeneration == report.missionGeneration;
        });
        const glm::vec3 position(report.x, report.y, report.z);
        if (record == _throws.end() || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
            return;
        }
        // The grenade fuse is 5000 ms; the Molotov bursts on first contact.
        // Its flight is bounded by the throw speed, 10 u/s^2 gravity and slack.
        const float elapsed = std::chrono::duration<float>(Clock::now() - record->acceptedAt).count();
        const bool grenade  = record->grenadeType == 0;
        const glm::vec3 offset = position - record->origin;
        const float horizontal = glm::length(glm::vec2(offset.x, offset.z));
        if ((grenade && (elapsed < 4.0f || elapsed > 7.5f)) || (!grenade && elapsed > 7.5f) ||
            horizontal > record->speed * elapsed + 3.0f || offset.y > record->speed * record->speed / 20.0f + 3.0f) {
            return;
        }
        const ThrowRecord accepted = *record;
        _throws.erase(record);
        if (grenade) {
            ApplyAreaDamage(accepted.throwerId, accepted.missionGeneration, position, 15.0f, true, 400.0f, 15);
        }
        else {
            const auto now = Clock::now();
            _fires.push_back({accepted.throwerId, uint8_t {5}, accepted.missionGeneration, position, now, {}});
        }
        Shared::Combat::Detonation detonation;
        detonation.throwerNetworkId  = accepted.throwerId;
        detonation.missionGeneration = accepted.missionGeneration;
        detonation.sequence          = accepted.sequence;
        detonation.grenadeType       = accepted.grenadeType;
        detonation.x                 = position.x;
        detonation.y                 = position.y;
        detonation.z                 = position.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(detonation);
    }

    void CombatService::ApplyAreaDamage(uint64_t sourceId, uint64_t missionGeneration, const glm::vec3 &position, float radius, bool explosion, float damage,
                                        std::optional<uint8_t> weaponId) {
        std::vector<Shared::Entities::PlayerEntity *> victims;
        _players->ForEach([&](Shared::Entities::PlayerEntity *player) {
            victims.push_back(player);
        });
        for (auto *player : victims) {
            // Retail explosions and fires never reach a seated human: HitInCar
            // applies only generic and car-impact damage.
            if (!player->spawned || !player->alive || player->missionGeneration != missionGeneration ||
                _cars->SeatForPlayer(player->GetNetworkID(), player->spawnGeneration).has_value()) {
                continue;
            }
            const glm::vec3 body = player->position + glm::vec3(0.0f, 1.0f, 0.0f);
            const float distance = glm::distance(body, position);
            float applied = 0.0f;
            if (explosion) {
                // ApplyExplosionDamageToActor: damage x max(0, 1 - d / radius).
                applied = distance < radius ? damage * (1.0f - distance / radius) : 0.0f;
            }
            else if (glm::dot(body - position, body - position) < radius * radius) {
                applied = damage;
            }
            if (applied <= 0.0f) {
                continue;
            }
            Shared::Combat::HitReport report;
            report.shooterNetworkId      = sourceId;
            report.targetNetworkId       = player->GetNetworkID();
            report.missionGeneration     = missionGeneration;
            report.targetSpawnGeneration = player->spawnGeneration;
            report.bodyPart              = 5;
            const glm::vec3 direction    = distance > 0.01f ? (body - position) / distance : glm::vec3(0.0f, 1.0f, 0.0f);
            report.directionX            = direction.x;
            report.directionY            = direction.y;
            report.directionZ            = direction.z;
            if (player->health <= applied) {
                if (auto *victim = FindCurrent(player->GetNetworkID())) {
                    victim->state.deathAnimation = 131 + static_cast<uint16_t>((player->GetNetworkID() + static_cast<uint64_t>(distance * 10.0f)) % 6);
                }
            }
            PublishDamage(report, applied, explosion ? 2 : 3, weaponId);
        }
    }

    void CombatService::UpdateThrows() {
        const auto now = Clock::now();
        // A throw never reported within its fuse and flight is dropped with no
        // damage; clients still remove their display copies.
        for (auto it = _throws.begin(); it != _throws.end();) {
            if (now - it->acceptedAt > std::chrono::milliseconds(8000)) {
                Shared::Combat::Detonation detonation;
                detonation.throwerNetworkId  = it->throwerId;
                detonation.missionGeneration = it->missionGeneration;
                detonation.sequence          = it->sequence;
                detonation.grenadeType       = it->grenadeType;
                detonation.visualOnly        = true;
                detonation.x                 = it->origin.x;
                detonation.y                 = it->origin.y;
                detonation.z                 = it->origin.z;
                Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(detonation);
                it = _throws.erase(it);
            }
            else {
                ++it;
            }
        }
        // C_fire::Tick deals its damage per second x the fire's remaining
        // seconds to everything within its radius, about twice a second; a
        // Molotov is 50 within 2.5 units for 5 s. The server ticks every 500 ms.
        constexpr auto kTick = std::chrono::milliseconds(500);
        for (auto it = _fires.begin(); it != _fires.end();) {
            const auto age = now - it->startedAt;
            if (age >= it->life) {
                it = _fires.erase(it);
                continue;
            }
            if (it->lastTick == Clock::time_point {} || now - it->lastTick >= kTick) {
                it->lastTick = now;
                const float remaining = std::chrono::duration<float>(it->life - age).count();
                ApplyAreaDamage(it->sourceId, it->missionGeneration, it->position, it->radius, false, it->damage * remaining, it->weaponId);
            }
            ++it;
        }
    }

    void CombatService::ApplyExplosion(uint64_t sourceId, uint64_t missionGeneration, const glm::vec3 &position, float radius, float damage) {
        ApplyAreaDamage(sourceId, missionGeneration, position, radius, true, damage, std::nullopt);
    }

    void CombatService::StartFire(uint64_t sourceId, uint64_t missionGeneration, const glm::vec3 &position, std::chrono::milliseconds life, float radius, float damage) {
        FireArea fire;
        fire.sourceId          = sourceId;
        fire.weaponId          = std::nullopt;
        fire.missionGeneration = missionGeneration;
        fire.position          = position;
        fire.startedAt         = Clock::now();
        fire.life              = life;
        fire.radius            = radius;
        fire.damage            = damage;
        _fires.push_back(fire);
    }

    void CombatService::OnFatalFallReport(const Shared::Combat::FatalFallReport &report, MafiaNet::PeerGuid sender) {
        auto *player  = _players->FindByGuid(sender);
        auto *session = FindCurrent(report.networkId);
        if (!player || !session || player->GetNetworkID() != report.networkId || !player->spawned || !player->alive || player->missionGeneration != report.missionGeneration || player->spawnGeneration != report.spawnGeneration) {
            return;
        }
        if (report.cause != Shared::Combat::FatalFallReport::Cause::Fall || _players->HasRecentFatalFall(report.networkId)) {
            SetHealth(report.networkId, 0.0f, 0, std::nullopt,
                      report.cause == Shared::Combat::FatalFallReport::Cause::Drowned ? DamageCause::Drowning : DamageCause::Fall);
        }
        else {
            session->pendingFallUntil = Clock::now() + std::chrono::seconds(2);
        }
    }
} // namespace Mafia1Online::Features::Combat
