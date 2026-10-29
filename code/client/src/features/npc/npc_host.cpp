#include <utils/safe_win32.h>

#include "npc_host.h"

#include "gangster_spawn.h"
#include "police_host.h"

#include "features/combat/combat_hooks.h"
#include "shared/net/sample_clock.h"
#include "features/death/death_service.h"
#include "core/application.h"
#include "features/world/world_service.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/combat/vehicle_impact.h"
#include "shared/features/npc/npc_melee.h"
#include "shared/features/npc/npc_seized.h"
#include "shared/features/player/player_entity.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_death.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/police/native_police_action.h>
#include <mafia1/sdk/police/native_police_manager.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <mafia1/sdk/traffic/native_traffic_car.h>
#include <mafia1/sdk/world/native_using_object.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Npc {
    namespace {
        using SDK::Player::NativeActor;
        using SDK::Player::NativeHuman;
        using SDK::Player::Vector3;

        using TickDoRiciActors = char(__thiscall *)(SDK::Police::NativePoliceManager *, bool);
        TickDoRiciActors gTickDoRiciOriginal = nullptr;
        NpcHost *gHost                       = nullptr;

        // ai_character_log *__thiscall ai_logs::GetMaxAngryCharacter(), plain
        // ret: the entry with the highest anger (+0x4c) whose contact count
        // (+0x04) is not 666 (dead) and whose contact start (+0x18) is set.
        // Its callers are C_entity::StateSwitcher (0x529200, 0x52924a), which
        // stores the result's actor as the fight target (+0x1280), and
        // C_human::Can_Hit_FilterHumanType (0x58e2c9). Both pass the NPC's
        // own ai_logs, which sits at C_entity +0xe88.
        constexpr uintptr_t kGetMaxAngryCharacter = 0x4F7850;
        constexpr uintptr_t kEntityLogs = 0xE88;
        using GetMaxAngry = void *(__thiscall *)(void *);
        GetMaxAngry gGetMaxAngryOriginal = nullptr;

        // ai_character_log, 0x54 bytes; ai_logs keeps them in a vector with
        // _First at +0x24 and _Last at +0x28.
        struct NativeCharacterLog {
            NativeActor *actor;
            int32_t contacts;
            // 0 friend, 1 enemy, 2 neutral, 3 unknown.
            int32_t attitude;
            std::byte _unused0c[0x18 - 0x0c];
            uint32_t contactStart;
            std::byte _unused1c[0x4c - 0x1c];
            float anger;
            float fear;
        };
        static_assert(sizeof(NativeCharacterLog) == 0x54);
        constexpr int32_t kDeadContacts = 666;
        constexpr int32_t kEnemyAttitude = 1;
        // A player's damage counts as a grudge this long after its last hit.
        constexpr auto kGrudgeMemory = std::chrono::seconds(30);
        // A new target must beat the current one by this much damage.
        constexpr float kGrudgeMargin = 1.25f;
        // ...and closer by this much, after a switch at least this long ago.
        constexpr float kCloserMargin = 0.8f;
        constexpr auto kTargetHold = std::chrono::seconds(3);

        void *__fastcall GetMaxAngryHook(void *logs, void *) {
            auto *npc = reinterpret_cast<NativeActor *>(static_cast<std::byte *>(logs) - kEntityLogs);
            const bool owned = gHost && gHost->Owns(npc);
            if (owned) {
                // Retail's own pick sees the logs as retail left them.
                gHost->RestoreAnger(npc);
            }
            void *retail = gGetMaxAngryOriginal(logs);
            return owned ? gHost->PickTarget(npc, logs, retail) : retail;
        }

        // C_human::NewScream: void __thiscall(C_human*, unsigned screamId), retn 4.
        using NewScream = void(__thiscall *)(NativeHuman *, uint32_t);
        NewScream gNewScreamOriginal = nullptr;

        void __fastcall NewScreamHook(NativeHuman *human, void *, uint32_t screamId) {
            gNewScreamOriginal(human, screamId);
            if (gHost) {
                gHost->OnScream(human, screamId);
            }
        }

        bool IsTemporaryActor(const void *actor) {
            auto *game = SDK::Core::Mission::Get()->Game();
            for (auto *const *it = SDK::Traffic::TemporaryActorsBegin(game); it && it != SDK::Traffic::TemporaryActorsEnd(game); ++it) {
                if (*it == actor) {
                    return true;
                }
            }
            return false;
        }

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
            MH_EnableHook(reinterpret_cast<void *>(SDK::Police::kTickDoRiciActors)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(kGetMaxAngryCharacter), reinterpret_cast<void *>(&GetMaxAngryHook), reinterpret_cast<void **>(&gGetMaxAngryOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(kGetMaxAngryCharacter)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(SDK::Player::kHumanNewScream), reinterpret_cast<void *>(&NewScreamHook), reinterpret_cast<void **>(&gNewScreamOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(SDK::Player::kHumanNewScream)) != MH_OK || !InstallGangsterSpawn(*this) || !InstallPolice(*this)) {
            Uninstall();
            return false;
        }
        return true;
    }

    void NpcHost::Uninstall() {
        UninstallPolice();
        UninstallGangsterSpawn();
        MH_DisableHook(reinterpret_cast<void *>(SDK::Player::kHumanNewScream));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Player::kHumanNewScream));
        MH_DisableHook(reinterpret_cast<void *>(kGetMaxAngryCharacter));
        MH_RemoveHook(reinterpret_cast<void *>(kGetMaxAngryCharacter));
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
                const Vector3 direction {hit.directionX, hit.directionY, hit.directionZ};
                const Vector3 contact {hit.x, hit.y, hit.z};
                const float speed = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
                auto *car = attacker && attacker->GetType() == NativeActor::Type::Car ? attacker : nullptr;
                // The server's alive flag trails the host's: an NPC already
                // dead here is a corpse, which the police do not count again.
                const bool aliveBefore = !static_cast<NativeActor *>(native)->IsDead() && AsHuman(native).Health() > 0.0f;
                if (hit.hitType == Shared::Npc::Hit::kCarBump) {
                    // C_human::Collision's slow branch, as the player's car
                    // would have run it here: out of the car's frame and a
                    // side jump; a seated NPC returns at once. Without this
                    // host's copy of the car only the side jump plays. Not on
                    // a corpse: Collision counts a dead human as run over
                    // again (NextNaSrackySoudruh, 0x578a3b).
                    if (!aliveBefore || !std::isfinite(speed) || speed >= Shared::Combat::kVehicleImpactMinSpeed || !std::isfinite(contact.x) ||
                        !std::isfinite(contact.y) || !std::isfinite(contact.z)) {
                        return;
                    }
                    if (car) {
                        AsHuman(native).CarCollision(car, contact, direction);
                    }
                    else if (!static_cast<const SDK::Seat::NativeHuman *>(native)->usedActorEnter) {
                        AsHuman(native).SideJump(contact, direction);
                    }
                    ReportCarContact(car, static_cast<NativeActor *>(native), speed, false);
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("NPC {} bumped by car {} at {:.1f} m/s: animation {}", tracked.key, hit.attackerId, speed,
                                                                               AsHuman(native).AnimationState());
                    return;
                }
                // Retail C_human::Collision enters a car impact as HIT_TYPE_SNIPER.
                const auto type = hit.hitType == Shared::Npc::Hit::kMeleeKnockdown ? SDK::Player::NativeHitType::HardKnockdown :
                                  hit.hitType == Shared::Npc::Hit::kCarImpact ? SDK::Player::NativeHitType::Sniper : SDK::Player::NativeHitType::Generic;
                const float before = AsHuman(native).Health();
                Death::ApplyAuthoritativeHit(AsHuman(native), type, direction, contact, hit.damage, attacker, hit.bodyPart);
                // C_human::AI follows a swing's Hit on a human with the
                // attacker's boxing crime while the police are on
                // (0x5714cd-0x5714e6); the player's own sweep ran on its
                // client, whose police are off.
                if (hit.hitType == Shared::Npc::Hit::kMeleeKnockdown && attacker && attacker->GetType() != NativeActor::Type::Car) {
                    const Combat::ValidatedMeleeScope validated;
                    if (auto *police = SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game()); police->IsActive()) {
                        police->ReportBoxing(attacker);
                    }
                }
                if (hit.hitType == Shared::Npc::Hit::kCarImpact && aliveBefore) {
                    const bool killed = static_cast<NativeActor *>(native)->IsDead() || AsHuman(native).Health() <= 0.0f;
                    ReportCarContact(car, static_cast<NativeActor *>(native), speed, killed);
                }
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("NPC {} hit by {} (type {}, part {}, damage {:.1f}): health {:.1f} -> {:.1f}", tracked.key, hit.attackerId,
                                                                           hit.hitType, hit.bodyPart, hit.damage, before, AsHuman(native).Health());
                return;
            }
        });
        network->RegisterRPC<Shared::Npc::PoliceOrder>([this](const Shared::Npc::PoliceOrder &order, MafiaNet::Packet *) {
            if (order.missionGeneration == _missionGeneration && _policeOrders.size() < 64) {
                _policeOrders.push_back({order, std::chrono::steady_clock::now()});
            }
        });
        network->RegisterRPC<Shared::Npc::PoliceSwitch>([this](const Shared::Npc::PoliceSwitch &change, MafiaNet::Packet *) {
            _policeEnabled = change.enabled;
        });
        network->RegisterRPC<Shared::Npc::PoliceOfficers>([this](const Shared::Npc::PoliceOfficers &officers, MafiaNet::Packet *) {
            _officerIds = {officers.playerNetworkIds.begin(), officers.playerNetworkIds.end()};
        });
        network->RegisterRPC<Shared::Npc::PoliceBackup>([this](const Shared::Npc::PoliceBackup &backup, MafiaNet::Packet *) {
            if (backup.missionGeneration == _missionGeneration && _backups.size() < 16) {
                _backups.push_back({backup, std::chrono::steady_clock::now()});
                return;
            }
            // Every request is answered: the server holds each open until
            // the reply, and refuses new ones once too many are.
            Shared::Npc::PoliceBackupSent reply;
            reply.missionGeneration = backup.missionGeneration;
            reply.requestId = backup.requestId;
            reply.sent = false;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(reply, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        });
        network->RegisterRPC<Shared::Npc::Remove>([this](const Shared::Npc::Remove &remove, MafiaNet::Packet *) {
            // A scripted NPC is in no retirement list; it goes in order with
            // its other messages, whatever the mission the host has loaded.
            if (Shared::Npc::IsScriptedKey(remove.key)) {
                _scripted.RequestRemove(remove.missionGeneration, remove.key);
                return;
            }
            if (remove.missionGeneration != _missionGeneration) {
                return;
            }
            for (auto &[actor, tracked] : _npcs) {
                if (tracked.key == remove.key) {
                    tracked.removeRequested = true;
                }
            }
        });
        _scripted.RegisterRPC(world);
    }

    void NpcHost::Reset() {
        // Every caller (mission closing, connection closed, shutdown) runs
        // before the player replicas go, while C_game is alive: their crime
        // records leave first, since C_shvestky::Tick 0x49a300 dereferences
        // every recorded person (0x49a5bb, 0x49aaca). DelPerson does nothing
        // while the manager is off.
        if (auto *game = SDK::Core::Mission::Get()->Game()) {
            auto *police = SDK::Police::PoliceManager(game);
            for (void *native : _policePersons) {
                police->DelPerson(static_cast<NativeActor *>(native));
            }
        }
        _npcs.clear();
        _observers.clear();
        _policePersons.clear();
        _policeOfficers.clear();
        _reportedRecords.clear();
        _policeOrders.clear();
        _backups.clear();
        _scripted.Reset();
    }

    void NpcHost::Track(NativeActor *actor, Shared::Entities::NpcKind kind, const std::string &model, uint32_t walkerKey, uint64_t carId, uint32_t key) {
        if (!_missionGeneration) {
            return;
        }
        auto &human = AsHuman(actor);
        const auto position = SDK::Player::WorldPosition(actor);
        const auto direction = SDK::Player::WorldDirection(actor);
        Tracked tracked;
        tracked.key = key ? key : _nextKey++;
        tracked.kind = kind;
        tracked.temporary = IsTemporaryActor(actor);
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
        spawned.walkerKey = walkerKey;
        spawned.carId = carId;
        spawned.hostClockMs = Shared::Net::SampleClock(MafiaNet::GetTime());
        // Ahead of the walker's Retired, which the reporter sends on its next
        // pass, so the server links the two before the walker goes.
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(spawned);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("NPC {} ({}, {}) joined with weapon {}, health {:.0f}", tracked.key, Shared::Entities::NpcKindName(kind), model,
                                                                   tracked.inventory.selected, spawned.health);
    }

    void NpcHost::OnNativeDestroyed(void *actor) {
        // C_shvestky::Tick reads every recorded person; the record goes
        // before the actor. Mission close cleared the set beforehand.
        _reportedRecords.erase(actor);
        _policeOfficers.erase(actor);
        if (_policePersons.erase(actor)) {
            SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game())->DelPerson(static_cast<NativeActor *>(actor));
        }
        Retire(actor);
    }

    void NpcHost::Retire(void *actor) {
        const auto found = _npcs.find(actor);
        if (found == _npcs.end()) {
            return;
        }
        const uint32_t key = found->second.key;
        _npcs.erase(found);
        ReportRetired(key);
        if (Shared::Npc::IsScriptedKey(key)) {
            _scripted.Forget(key);
        }
    }

    void NpcHost::ReportRetired(uint32_t key) const {
        Shared::Npc::Retired retired;
        retired.missionGeneration = _missionGeneration;
        retired.key = key;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(retired);
    }

    NativeActor *NpcHost::FindByKey(uint32_t key) const {
        for (const auto &[actor, tracked] : _npcs) {
            if (tracked.key == key) {
                return static_cast<NativeActor *>(actor);
            }
        }
        return nullptr;
    }

    void NpcHost::TrackIfNew(NativeActor *actor, Shared::Entities::NpcKind kind, uint64_t carId) {
        if (!actor || Owns(actor) || actor->GetType() != NativeActor::Type::Entity) {
            return;
        }
        const std::string model = SDK::Traffic::ModelName(actor->Frame());
        if (!model.empty()) {
            Track(actor, kind, model, 0, carId);
        }
    }

    void NpcHost::ForEachTracked(const std::function<void(NativeActor *, Shared::Entities::NpcKind)> &visit) const {
        for (const auto &[actor, tracked] : _npcs) {
            visit(static_cast<NativeActor *>(actor), tracked.kind);
        }
    }

    void NpcHost::ScanTemporaryActors(World::WorldService &world) {
        auto *game = SDK::Core::Mission::Get()->Game();
        std::unordered_set<void *> present;
        for (auto *const *it = SDK::Traffic::TemporaryActorsBegin(game); it && it != SDK::Traffic::TemporaryActorsEnd(game); ++it) {
            NativeActor *actor = *it;
            if (!actor || actor->GetType() != NativeActor::Type::Entity) {
                continue;
            }
            present.insert(actor);
            // A player's replica is a bound network object; anything else is
            // the retail game's: a police car's officer, a mafia car's crew,
            // a traffic driver thrown out of its car.
            if (Owns(actor) || actor->_actorState != 0 || actor->IsDead() || world.NativeObjects().FindByNative(actor).networkId != 0) {
                continue;
            }
            const bool police = SDK::Police::BehaviourClass(actor) == SDK::Police::kPoliceBehaviourClass;
            TrackIfNew(actor, police ? Shared::Entities::NpcKind::Police : Shared::Entities::NpcKind::Gangster);
        }
        std::vector<void *> left;
        for (auto &[actor, tracked] : _npcs) {
            if (present.contains(actor)) {
                tracked.temporary = true;
            }
            else if (tracked.temporary) {
                // FreeTempEnemy takes a pool officer out of every actor list
                // and deactivates it for reuse; removal goes the same way.
                left.push_back(actor);
            }
        }
        for (void *actor : left) {
            Retire(actor);
        }
    }

    void NpcHost::SendSeats(World::WorldService &world) {
        for (auto &[native, tracked] : _npcs) {
            auto *human = static_cast<SDK::Seat::NativeHuman *>(native);
            NativeActor *seated = human->usedActorEnter && human->usedActorEnter->GetType() == NativeActor::Type::Car ? human->usedActorEnter : nullptr;
            // Use_Actor reserves the seat for its door animation (+0x9c) and
            // links it (+0x98) when the animation ends; the exit runs with +0x98
            // already clear and the car in +0x9c.
            NativeActor *door = human->usedActorLeave && human->usedActorLeave->GetType() == NativeActor::Type::Car ? human->usedActorLeave : nullptr;
            NativeActor *car = seated ? seated : human->HasPendingEntry(static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(door))) ? door : nullptr;
            uint64_t carId = 0;
            uint8_t seat = Shared::Npc::Seat::kNoSeat;
            if (car && human->seatId >= 0 && human->seatId < static_cast<int>(Shared::Entities::CarEntity::kMaxSeats)) {
                carId = world.NativeObjects().FindByNative(car).networkId;
                if (carId == 0) {
                    // The car is not a network car yet (its report is on the
                    // way): report the seat once it is.
                    continue;
                }
                seat = static_cast<uint8_t>(human->seatId);
            }
            if (carId == tracked.seatCarId && seat == tracked.seat && !tracked.forceSeatReport) {
                continue;
            }
            tracked.forceSeatReport = false;
            // Intern_UseCar / Intern_FromCar are instant; Use_Actor animates.
            ReportSeat(tracked, carId, seat, carId ? !seated : door != nullptr, tracked.seatCarId, tracked.seat);
        }
    }

    void NpcHost::ReportSeat(Tracked &tracked, uint64_t carId, uint8_t seat, bool animated, uint64_t fromCarId, uint8_t fromSeat) {
        Shared::Npc::Seat report;
        report.missionGeneration = _missionGeneration;
        report.key = tracked.key;
        report.carId = carId;
        report.seat = seat;
        report.animated = animated;
        report.fromCarId = fromCarId;
        report.fromSeat = fromCarId ? fromSeat : Shared::Npc::Seat::kNoSeat;
        tracked.seatCarId = carId;
        tracked.seat = seat;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("NPC {} seat: car {} seat {} ({})", tracked.key, carId, seat, animated ? "animated" : "instant");
    }

    void NpcHost::OnUseCar(NativeActor *actor, SDK::Seat::NativeCar *car, int action, int seat) {
        const auto found = _npcs.find(actor);
        if (found == _npcs.end() || !_world || action != static_cast<int>(SDK::Seat::UseAction::Exit) || seat < 0 ||
            seat >= static_cast<int>(Shared::Entities::CarEntity::kMaxSeats)) {
            return;
        }
        // Use_Actor's exit (0x582180) frees the seat and plays the door
        // animation with the car in +0x9c. A refused door climbs across
        // (Do_ClimbInCarLR) or pops it out (ForceExitCar): SendSeats reports
        // those.
        auto *human = static_cast<SDK::Seat::NativeHuman *>(static_cast<void *>(actor));
        if (human->usedActorEnter || human->usedActorLeave != static_cast<NativeActor *>(car) || human->HasPendingEntry(car)) {
            return;
        }
        const uint64_t carId = _world->NativeObjects().FindByNative(car).networkId;
        if (carId == 0) {
            return;
        }
        // Now, not on the next SendSeats: an NPC tracked as it left (a
        // mafia crew right after Krles) never shows that pass a seat.
        ReportSeat(found->second, 0, Shared::Npc::Seat::kNoSeat, true, carId, static_cast<uint8_t>(seat));
    }

    void NpcHost::ReportCrewSeat(NativeActor *actor, SDK::Seat::NativeCar *car) {
        const auto found = _npcs.find(actor);
        if (found == _npcs.end() || !_world || !car) {
            return;
        }
        auto *human = static_cast<SDK::Seat::NativeHuman *>(static_cast<void *>(actor));
        const uint64_t carId = _world->NativeObjects().FindByNative(car).networkId;
        if (carId == 0 || human->seatId < 0 || human->seatId >= static_cast<int>(Shared::Entities::CarEntity::kMaxSeats)) {
            return;
        }
        const auto seat = static_cast<uint8_t>(human->seatId);
        // Use_Actor's exit keeps +0xac and leaves the car in +0x9c.
        const bool leaving = !human->usedActorEnter && human->usedActorLeave == static_cast<NativeActor *>(car) && !human->HasPendingEntry(car);
        if (!human->IsSeatedIn(car, seat) && !leaving) {
            return;
        }
        ReportSeat(found->second, carId, seat, false, 0, Shared::Npc::Seat::kNoSeat);
        if (leaving) {
            ReportSeat(found->second, 0, Shared::Npc::Seat::kNoSeat, true, carId, seat);
        }
    }

    void NpcHost::ResendSeat(const void *actor) {
        if (const auto found = _npcs.find(const_cast<void *>(actor)); found != _npcs.end()) {
            found->second.seatCarId = 0;
            found->second.seat = Shared::Npc::Seat::kNoSeat;
            found->second.forceSeatReport = true;
        }
    }

    void NpcHost::ForgetHuman(NativeActor *human) {
        if (!human || !SDK::Core::Mission::Get()->Game()) {
            return;
        }
        // C_shvestky::Tick reads every recorded person.
        _reportedRecords.erase(human);
        _policeOfficers.erase(human);
        if (_policePersons.erase(human)) {
            SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game())->DelPerson(human);
        }
        SDK::Police::ForgetOffender(human);
        SDK::Traffic::EndHuntsOf(SDK::Core::Mission::Get()->Game(), human);
    }

    uint64_t NpcHost::TopGrudge(Tracked &tracked) const {
        const auto now = std::chrono::steady_clock::now();
        uint64_t best = 0;
        float most = 0.0f;
        for (const auto &[player, grudge] : tracked.grudges) {
            if (now - grudge.last <= kGrudgeMemory && grudge.damage > most) {
                most = grudge.damage;
                best = player;
            }
        }
        return best;
    }

    void *NpcHost::PickTarget(NativeActor *npc, void *logs, void *retail) {
        auto &tracked = _npcs.at(npc);
        auto *first = *reinterpret_cast<NativeCharacterLog **>(static_cast<std::byte *>(logs) + 0x24);
        auto *last = *reinterpret_cast<NativeCharacterLog **>(static_cast<std::byte *>(logs) + 0x28);
        if (!first || !_world) {
            return retail;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto position = npc->Frame()->WorldPosition();
        NativeCharacterLog *current = nullptr;
        NativeCharacterLog *byDamage = nullptr;
        NativeCharacterLog *byDistance = nullptr;
        float mostDamage = 0.0f;
        float nearestSq = std::numeric_limits<float>::max();
        float currentDamage = 0.0f;
        float currentSq = std::numeric_limits<float>::max();
        for (auto *entry = first; entry < last; ++entry) {
            // The same entries retail could pick, and only players it is at
            // odds with.
            if (!entry->actor || entry->contacts == kDeadContacts || entry->contactStart == 0 || entry->attitude != kEnemyAttitude ||
                !IsPlayerReplica(entry->actor) || entry->actor->IsDead()) {
                continue;
            }
            const uint64_t player = _world->NativeObjects().FindByNative(entry->actor).networkId;
            const auto grudge = tracked.grudges.find(player);
            const float damage = grudge != tracked.grudges.end() && now - grudge->second.last <= kGrudgeMemory ? grudge->second.damage : 0.0f;
            const auto at = entry->actor->Frame()->WorldPosition();
            const float dx = at.x - position.x;
            const float dy = at.y - position.y;
            const float dz = at.z - position.z;
            const float distanceSq = dx * dx + dy * dy + dz * dz;
            if (damage > mostDamage) {
                mostDamage = damage;
                byDamage = entry;
            }
            if (distanceSq < nearestSq) {
                nearestSq = distanceSq;
                byDistance = entry;
            }
            if (player == tracked.target) {
                current = entry;
                currentDamage = damage;
                currentSq = distanceSq;
            }
        }
        NativeCharacterLog *chosen = byDamage ? byDamage : byDistance;
        // Retail's own enemy stands unless it is a player too (then the
        // player that hurt it most, else the nearest, goes first) or a
        // player hurt this NPC lately: a cop or rival it fights is its own.
        auto *winner = static_cast<NativeCharacterLog *>(retail);
        const bool retailPlayer = winner && winner->actor && IsPlayerReplica(winner->actor);
        if (!chosen || (!retailPlayer && !byDamage)) {
            return retail;
        }
        // Every target change resets the fight modes: keep the current one
        // unless another hurt it clearly more, or is clearly closer after a
        // hold.
        if (current && chosen != current) {
            const bool hold = now - tracked.targetSince < kTargetHold;
            const bool damageWins = byDamage == chosen && mostDamage > currentDamage * kGrudgeMargin;
            const bool distanceWins = !byDamage && !hold && nearestSq < currentSq * kCloserMargin * kCloserMargin;
            if (!damageWins && !distanceWins) {
                chosen = current;
            }
        }
        const uint64_t target = _world->NativeObjects().FindByNative(chosen->actor).networkId;
        if (target != tracked.target) {
            tracked.target = target;
            tracked.targetSince = now;
        }
        // StateSwitcher 0x5291d0 weighs the returned entry's anger (+0x4c)
        // against the fear of GetMaxFearCharacter right after this call,
        // so the player stands in with the anger of retail's own choice. The
        // write lasts until this NPC's next pick or the host's next Update.
        if (winner && winner != chosen && winner->anger > chosen->anger) {
            tracked.anger = {chosen->actor, chosen->anger, winner->anger};
            chosen->anger = winner->anger;
        }
        return chosen;
    }

    void NpcHost::RestoreAnger(void *npc) {
        const auto found = _npcs.find(npc);
        if (found == _npcs.end() || !found->second.anger.actor) {
            return;
        }
        auto &anger = found->second.anger;
        auto *logs = static_cast<std::byte *>(npc) + kEntityLogs;
        auto *first = *reinterpret_cast<NativeCharacterLog **>(logs + 0x24);
        auto *last = *reinterpret_cast<NativeCharacterLog **>(logs + 0x28);
        // By actor: the log vector may have grown since. Anger retail has
        // changed since stays as retail made it.
        for (auto *entry = first; first && entry < last; ++entry) {
            if (entry->actor == anger.actor && entry->anger == anger.written) {
                entry->anger = anger.original;
                break;
            }
        }
        anger = {};
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
        // its own sequence, and the kept actors return afterwards. A
        // recruited officer stays in it: the native pass ignores its distance
        // and retires it once its case is over, and its walker walks on.
        auto **begin = police->RetirementBegin();
        const size_t count = police->RetirementCount();
        std::vector<NativeActor *> kept;
        size_t retiring = 0;
        for (size_t i = 0; i < count; ++i) {
            NativeActor *actor = begin[i];
            const auto tracked = _npcs.find(actor);
            const bool removeRequested = tracked != _npcs.end() && tracked->second.removeRequested;
            if (!removeRequested && SDK::Police::RetiresByDistance(actor) && NearObserver(actor->Frame()->WorldPosition())) {
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
            _policePersons.clear();
            _policeOfficers.clear();
            _reportedRecords.clear();
            _policeOrders.clear();
            _backups.clear();
            _nextKey = 1;
            _missionGeneration = generation;
        }
        _observers.clear();
        if (world.IsReady()) {
            for (auto &[native, tracked] : _npcs) {
                if (tracked.anger.actor) {
                    RestoreAnger(native);
                }
            }
        }
        if (auto *replication = Framework::CoreModules::GetReplication(); replication && world.IsReady()) {
            replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
                if (player->spawned && player->missionGeneration == generation) {
                    _observers.push_back({player->position.x, player->position.y, player->position.z});
                }
            });
        }
        _scripted.Update(world, generation);
        if (world.IsReady()) {
            UpdatePolice(world);
            ScanTemporaryActors(world);
            SendSeats(world);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastSnapshot < kSnapshotInterval || !world.IsReady()) {
            return;
        }
        const float seconds = _lastSnapshot == std::chrono::steady_clock::time_point {} ? 0.0f : std::chrono::duration<float>(now - _lastSnapshot).count();
        _lastSnapshot = now;
        SendSnapshots(seconds);
    }

    void NpcHost::UpdatePolice(World::WorldService &world) {
        auto *police = SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game());
        if (!police->IsActive()) {
            // Retail turns it on from the mission script, which mod missions
            // do not run. Without a native player it records nobody yet.
            police->SetActive(true);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Police manager on for mission generation {}", _missionGeneration);
        }
        // Every spawned player's replica, and whether its player is on
        // police duty.
        std::unordered_map<void *, bool> current;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
                if (!player->spawned || player->missionGeneration != _missionGeneration) {
                    return;
                }
                if (void *native = world.NativeObjects().Resolve(world.NativeObjects().FindByNetwork(player->GetNetworkID()))) {
                    current.emplace(native, _officerIds.contains(player->GetNetworkID()));
                }
            });
        }
        for (auto it = _policePersons.begin(); it != _policePersons.end();) {
            const auto found = current.find(*it);
            if (found != current.end() && !found->second) {
                ++it;
                continue;
            }
            auto *person = static_cast<NativeActor *>(*it);
            police->DelPerson(person);
            _reportedRecords.erase(*it);
            if (found != current.end()) {
                // On duty. Retail keeps no record of its officers: only
                // C_game's player (SetActive 0x49b860) and those a mission
                // script adds (POLICEMANAGER_ADD) become persons, and every
                // detector, case and car hunt starts from a person.
                SDK::Police::ForgetOffender(person);
                SDK::Traffic::EndHuntsOf(SDK::Core::Mission::Get()->Game(), person);
            }
            it = _policePersons.erase(it);
        }
        std::erase_if(_policeOfficers, [&](void *native) {
            const auto found = current.find(native);
            return found == current.end() || !found->second;
        });
        for (const auto &[native, officer] : current) {
            auto *actor = static_cast<NativeActor *>(native);
            // ai_logs::Event rates a type 2 player as class 0 but a type 27
            // replica by this field: NPCs see every player as retail sees
            // its own, and an officer as it sees its police walkers
            // (DoesntReactOnWeapon, class 2 from C_entity::GameInit 0x5074d0).
            const uint32_t behaviourClass = officer ? SDK::Police::kPoliceBehaviourClass : SDK::Police::kPlayerBehaviourClass;
            if (SDK::Police::BehaviourClass(actor) != behaviourClass) {
                SDK::Police::SetBehaviourClass(actor, behaviourClass);
                ForgetContacts(actor);
            }
            if (officer) {
                _policeOfficers.insert(native);
            }
            else if (_policePersons.insert(native).second) {
                police->AddPerson(actor);
            }
        }
        RetargetOfficers(*police);
    }

    void NpcHost::ForgetContacts(NativeActor *replica) {
        for (auto &[native, tracked] : _npcs) {
            auto *npc = static_cast<NativeActor *>(native);
            if (npc->GetType() == NativeActor::Type::Entity) {
                SDK::Police::ForgetContact(npc, replica);
            }
        }
    }

    void NpcHost::OnAttacked(World::WorldService &, NativeActor &npc, NativeActor &attacker) {
        // With the police off no officer opens a new case; C_entity::Hit's
        // own escalation of a case already aimed at the attacker still runs.
        const auto found = _npcs.find(&npc);
        if (!_policeEnabled || found == _npcs.end() || npc.IsDead() || SDK::Police::BehaviourClass(&npc) != SDK::Police::kPoliceBehaviourClass) {
            return;
        }
        // C_entity::Hit blames a car's driver the same way.
        NativeActor *offender = &attacker;
        if (attacker.GetType() == NativeActor::Type::Car) {
            offender = static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(&attacker))->GetOwner(0);
        }
        if (!offender || !_policePersons.contains(offender)) {
            return;
        }
        // The officer goes after the player that hurt it most lately, the
        // way PickTarget aims every other NPC; police actions ignore it.
        if (_world) {
            if (const uint64_t top = TopGrudge(found->second)) {
                auto *native = static_cast<NativeActor *>(_world->NativeObjects().Resolve(_world->NativeObjects().FindByNetwork(top)));
                if (native && _policePersons.contains(native) && !native->IsDead()) {
                    offender = native;
                }
            }
        }
        const auto *action = SDK::Police::OfficerBoardAction(&npc);
        const auto now = std::chrono::steady_clock::now();
        if ((action && action->Offender() == offender) || now - found->second.lastRedirect < kRedirectHold || Arresting(found->second, now)) {
            return;
        }
        // An assault on an officer is the lethal crime (assaultOnPolice): it
        // hunts its attacker to kill, as C_entity::Hit escalates its own case.
        MoveOfficer(*SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game()), &npc, found->second, action ? action->Offender() : nullptr, offender,
                    SDK::Police::kBoardModeKill);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("NPC {} turned on player {}", found->second.key, _world ? _world->NativeObjects().FindByNative(offender).networkId : 0);
    }

    void NpcHost::OnPoliceActionTick(NativeActor *officer, uint32_t state) {
        if (const auto found = _npcs.find(officer); found != _npcs.end()) {
            found->second.policeState = state;
            found->second.policeTickAt = std::chrono::steady_clock::now();
        }
    }

    void NpcHost::MoveOfficer(SDK::Police::NativePoliceManager &police, NativeActor *officer, Tracked &tracked, NativeActor *from, NativeActor *to, uint32_t mode) {
        tracked.lastRedirect = std::chrono::steady_clock::now();
        if (auto *record = from ? police.FindRecord(from) : nullptr) {
            record->RemoveOfficer(officer);
        }
        SDK::Police::RedirectOfficer(officer, to, mode);
        // VasekSelChlastatNaOslavu books every officer it sends on the
        // person's record: hidden checks and reinforcements count them.
        if (auto *record = police.FindRecord(to)) {
            record->AddOfficer(officer);
        }
    }

    void NpcHost::RetargetOfficers(SDK::Police::NativePoliceManager &police) {
        // Retail has one player, so an officer only ever chases the person
        // its case is about. With several, one on foot after a player far
        // away moves to a wanted player much closer to it, in the mode that
        // player's own record would send it (VasekSel's mapping).
        const auto now = std::chrono::steady_clock::now();
        if (!_policeEnabled || now - _lastRetarget < kRetargetInterval) {
            return;
        }
        _lastRetarget = now;
        struct Wanted {
            NativeActor *person;
            SDK::Player::Vector3 position;
            uint32_t mode;
        };
        std::vector<Wanted> wanted;
        for (void *native : _policePersons) {
            auto *person = static_cast<NativeActor *>(native);
            const auto *record = police.FindRecord(person);
            if (!record || person->IsDead()) {
                continue;
            }
            const uint16_t level = record->Level();
            if (level == SDK::Police::NativeCrimeRecord::kLevelNone) {
                continue;
            }
            const uint32_t mode = level >= SDK::Police::NativeCrimeRecord::kLevelLethal ? SDK::Police::kBoardModeKill
                                  : level == SDK::Police::NativeCrimeRecord::kLevelArrest ? SDK::Police::kBoardModeArrest
                                                                                          : SDK::Police::kBoardModePursue;
            wanted.push_back({person, person->Frame()->WorldPosition(), mode});
        }
        if (wanted.size() < 2) {
            return;
        }
        const auto distanceSq = [](const SDK::Player::Vector3 &a, const SDK::Player::Vector3 &b) {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            const float dz = a.z - b.z;
            return dx * dx + dy * dy + dz * dz;
        };
        for (auto &[native, tracked] : _npcs) {
            auto *officer = static_cast<NativeActor *>(native);
            const auto *seat = static_cast<const SDK::Seat::NativeHuman *>(native);
            if (tracked.kind != Shared::Entities::NpcKind::Police || officer->IsDead() || seat->usedActorEnter || seat->usedActorLeave ||
                SDK::Police::BehaviourClass(officer) != SDK::Police::kPoliceBehaviourClass || now - tracked.lastRedirect < kRedirectHold ||
                Arresting(tracked, now)) {
                continue;
            }
            const auto *action = SDK::Police::OfficerBoardAction(officer);
            if (!action || !action->OnFootCase() || !_policePersons.contains(action->Offender())) {
                continue;
            }
            // A player who hurt it lately keeps it (OnAttacked).
            if (const uint64_t top = TopGrudge(tracked); top && _world && _world->NativeObjects().FindByNative(action->Offender()).networkId == top) {
                continue;
            }
            const auto position = officer->Frame()->WorldPosition();
            const float currentSq = distanceSq(position, action->Offender()->Frame()->WorldPosition());
            const Wanted *best = nullptr;
            float bestSq = kRetargetReach * kRetargetReach;
            for (const auto &candidate : wanted) {
                const float candidateSq = distanceSq(position, candidate.position);
                if (candidate.person != action->Offender() && candidateSq < bestSq && candidateSq < currentSq * kRetargetCloser * kRetargetCloser) {
                    bestSq = candidateSq;
                    best = &candidate;
                }
            }
            if (!best) {
                continue;
            }
            NativeActor *from = action->Offender();
            MoveOfficer(police, officer, tracked, from, best->person, best->mode);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("NPC {} moved from player {} to the closer player {}", tracked.key,
                                                                       _world ? _world->NativeObjects().FindByNative(from).networkId : 0,
                                                                       _world ? _world->NativeObjects().FindByNative(best->person).networkId : 0);
        }
    }

    void NpcHost::BeforePoliceTick(SDK::Police::NativePoliceManager &police) {
        auto *replication = Framework::CoreModules::GetReplication();
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(_policeOrders, [&](const PendingOrder &pending) {
            const auto &order = pending.order;
            auto *player = replication && _world ? replication->GetEntity<Shared::Entities::PlayerEntity>(order.playerNetworkId) : nullptr;
            if (!player || player->spawnGeneration != order.playerSpawnGeneration) {
                // Gone, or a later life: the order was for one that ended.
                return true;
            }
            auto *native = static_cast<NativeActor *>(_world->NativeObjects().Resolve(_world->NativeObjects().FindByNetwork(order.playerNetworkId)));
            auto *record = native && _policePersons.contains(native) ? police.FindRecord(native) : nullptr;
            if (!record) {
                // Just spawned: UpdatePolice records the replica on a later
                // frame. The server already told the script it was sent.
                return now - pending.received > kPoliceOrderWait;
            }
            const uint16_t level = std::min<uint16_t>(order.level, SDK::Police::NativeCrimeRecord::kLevelLethal);
            if (order.action == Shared::Npc::PoliceOrder::Action::Clear || level < record->Level()) {
                police.SettleRecord(native, false);
                SDK::Police::CallOffOfficers(native);
            }
            // Tick compares the pending level with the level (0x49b29f) and
            // sends or escalates the police from it. The server sends no
            // raise while the police are off.
            if (order.action == Shared::Npc::PoliceOrder::Action::SetWanted && level > record->Level()) {
                record->SetPendingLevel(level);
            }
            return true;
        });
        SendBackups(police);
        QuietGrabbed(police);
        if (_policeEnabled) {
            return;
        }
        // Crimes the detectors found since the last evaluation raise nothing.
        ClearPlayerCrimes(police);
    }

    void NpcHost::SendBackups(SDK::Police::NativePoliceManager &police) {
        auto *replication = Framework::CoreModules::GetReplication();
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(_backups, [&](const PendingBackup &pending) {
            const auto &order = pending.order;
            auto *player = replication && _world ? replication->GetEntity<Shared::Entities::PlayerEntity>(order.playerNetworkId) : nullptr;
            const bool sameLife = player && player->spawnGeneration == order.playerSpawnGeneration;
            auto *native = sameLife ? static_cast<NativeActor *>(_world->NativeObjects().Resolve(_world->NativeObjects().FindByNetwork(order.playerNetworkId))) : nullptr;
            const auto *record = native && _policePersons.contains(native) ? police.FindRecord(native) : nullptr;
            if (!record && sameLife && now - pending.received <= kPoliceOrderWait) {
                return false;
            }
            // The level picks the hunt as VasekSelChlastatNaOslavu does:
            // lethal kill, arrest arrest, minor pursue (car modes).
            bool sent = false;
            if (record && _policeEnabled && !native->IsDead() && record->Level() != SDK::Police::NativeCrimeRecord::kLevelNone) {
                const uint16_t level = record->Level();
                const uint32_t mode = level >= SDK::Police::NativeCrimeRecord::kLevelLethal ? SDK::Police::kBoardModeKill
                                      : level == SDK::Police::NativeCrimeRecord::kLevelArrest ? SDK::Police::kBoardModeArrest
                                                                                              : SDK::Police::kBoardModePursue;
                sent = police.SendReinforcements(native, mode, kBackupRadius);
            }
            Shared::Npc::PoliceBackupSent reply;
            reply.missionGeneration = _missionGeneration;
            reply.requestId = order.requestId;
            reply.sent = sent;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(reply, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Police backup {} after player {}: {}", order.requestId, order.playerNetworkId, sent ? "sent" : "refused");
            return true;
        });
    }

    void NpcHost::QuietGrabbed(SDK::Police::NativePoliceManager &police) {
        // Retail's grab drops its player's weapon, settles the record and
        // stops new dispatches for it until the catch ends (0x515d49-
        // 0x515d68, VasekSelChlastatNaOslavu's m_bCountPlayerOffenses test).
        // A replica keeps the weapon its client holds, so weaponSeen would
        // raise the record again and PanoveZmenaZatkneteHoZmrda would send
        // GO_ARREST to the grabbing officer, whose car case only winds down
        // at the grab (HosiKoncime keeps a car case's members): a new police
        // action replaces the arrest mid-animation.
        const auto now = std::chrono::steady_clock::now();
        for (auto &[native, tracked] : _npcs) {
            if (!tracked.grabbed) {
                continue;
            }
            if (!Arresting(tracked, now) || static_cast<NativeActor *>(native)->IsDead() || !_policePersons.contains(tracked.grabbed)) {
                tracked.grabbed = nullptr;
                continue;
            }
            if (auto *record = police.FindRecord(tracked.grabbed)) {
                record->ClearCrimes();
                record->SetPendingLevel(SDK::Police::NativeCrimeRecord::kLevelNone);
            }
        }
    }

    void NpcHost::ReportCarContact(NativeActor *car, NativeActor *victim, float speed, bool killed) {
        auto *game = SDK::Core::Mission::Get()->Game();
        auto *police = game ? SDK::Police::PoliceManager(game) : nullptr;
        if (!police || !police->IsActive() || !car || car->GetType() != NativeActor::Type::Car || !victim || !std::isfinite(speed)) {
            return;
        }
        // Only a player's replica at the wheel has a record here.
        auto *driver = static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(car))->GetOwner(0);
        if (!driver || !_policePersons.contains(driver)) {
            return;
        }
        police->ReportCarCollision(driver, car, victim, speed);
        // C_human::Collision 0x5784a0 counts the run-over once the human is
        // dead (C_shvestky::NextNaSrackySoudruh); here once, for the contact
        // that killed it.
        if (killed) {
            police->CountRunOver(driver);
        }
    }

    void NpcHost::OnGrab(NativeActor *officer, NativeActor *target) {
        const auto found = _npcs.find(officer);
        if (found == _npcs.end() || !_policePersons.contains(target)) {
            return;
        }
        found->second.grabbed = target;
        found->second.policeState = SDK::Police::NativePoliceAction::kStateArrestWalk;
        found->second.policeTickAt = std::chrono::steady_clock::now();
        // Unpunished, as OnCaught explains.
        SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game())->SettleRecord(target, false);
        // State 10 places the replica in front of the officer (SetPos and
        // SetDir on both) and state 11 starts both halves from there; the
        // player's pose stream would pull it back in between.
        if (_world) {
            if (const auto victim = _world->NativeObjects().FindByNative(target); victim.networkId) {
                static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance())->Players().HoldAligned(victim.networkId, kArrestAlignHold);
            }
        }
    }

    void NpcHost::ClearPlayerCrimes(SDK::Police::NativePoliceManager &police) {
        for (void *native : _policePersons) {
            if (auto *record = police.FindRecord(static_cast<NativeActor *>(native))) {
                record->ClearCrimes();
                record->SetPendingLevel(SDK::Police::NativeCrimeRecord::kLevelNone);
            }
        }
    }

    void NpcHost::RunPlayerPolicePass(SDK::Police::NativePoliceManager &police) {
        using Record = SDK::Police::NativeCrimeRecord;
        // C_shvestky::Tick's block for C_game::m_pPlayer alone (0x49b4c8-
        // 0x49b68a), for every player: whether the police lost sight of them,
        // and the reinforcement cars at the highest levels. Its HUD writes
        // stay out; players' clients draw their own badge.
        for (void *native : _policePersons) {
            auto *person = static_cast<NativeActor *>(native);
            auto *record = police.FindRecord(person);
            if (!record || person->IsDead()) {
                continue;
            }
            int32_t display = record->Level();
            if ((record->Crimes() & Record::kCrimeForceArrest) && record->_maxWanted && display < Record::kLevelArrest) {
                display = Record::kLevelArrest;
            }
            if ((record->Crimes() & Record::kCrimePatrolmanHit) && display < Record::kLevelLethal) {
                display = Record::kLevelLethal;
            }
            if (display <= Record::kLevelNone) {
                continue;
            }
            if (record->_maxWanted) {
                display += Record::kLevelLethal;
            }
            if (police.LivingOfficersOn(person)) {
                record->_hidden = SDK::Police::BoardHasActionFor(person) ? !SDK::Police::BoardSeesOffender(person) : false;
            }
            else if (auto *vehicle = static_cast<SDK::Seat::NativeHuman *>(native)->usedActorEnter) {
                record->_hidden = record->_recordedVehicle != vehicle;
            }
            else {
                record->_hidden = !record->_recordedOnFoot;
            }
            if (display == Record::kLevelArrest + Record::kLevelLethal && record->_partyStep < Record::kPartyArrestDispatched) {
                record->_partyStep = Record::kPartyArrestDispatched;
                police.SendReinforcements(person, SDK::Police::kModeArrestCar);
            }
            else if (display == Record::kLevelLethal + Record::kLevelLethal && record->_partyStep < Record::kPartyLethalDispatched) {
                record->_partyStep = Record::kPartyLethalDispatched;
                police.SendReinforcements(person, SDK::Police::kModeKillCar);
                police.SendReinforcements(person, SDK::Police::kModeKillCar);
            }
        }
    }

    void NpcHost::AfterPoliceTick(SDK::Police::NativePoliceManager &police, bool evaluated) {
        QuietGrabbed(police);
        if (!_policeEnabled) {
            // The evaluation's own detectors (armed, speeding, red lights)
            // set crime bits again every period; with the police off they
            // stay out of the record, so no playerCrime repeats.
            ClearPlayerCrimes(police);
        }
        else if (evaluated) {
            RunPlayerPolicePass(police);
        }
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || !_world) {
            return;
        }
        for (void *native : _policePersons) {
            const auto *record = police.FindRecord(static_cast<NativeActor *>(native));
            if (!record) {
                continue;
            }
            const ReportedRecord current {static_cast<uint8_t>(record->Level()), record->Crimes(), static_cast<uint8_t>(std::min<size_t>(record->OfficerCount(), 0xff))};
            auto &reported = _reportedRecords[native];
            if (reported.level == current.level && reported.crimes == current.crimes && reported.officers == current.officers) {
                continue;
            }
            reported = current;
            const auto handle = _world->NativeObjects().FindByNative(native);
            auto *player = handle.networkId ? replication->GetEntity<Shared::Entities::PlayerEntity>(handle.networkId) : nullptr;
            if (!player) {
                continue;
            }
            Shared::Npc::PoliceRecord report;
            report.missionGeneration = _missionGeneration;
            report.playerNetworkId = handle.networkId;
            report.playerSpawnGeneration = player->spawnGeneration;
            report.level = current.level;
            report.crimes = current.crimes;
            report.officers = current.officers;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void NpcHost::OnKick(NativeActor *thrower, uint64_t carId, uint8_t seat, uint64_t victimId, uint64_t victimSpawnGeneration) {
        const auto found = _npcs.find(thrower);
        if (found == _npcs.end()) {
            return;
        }
        Shared::Npc::Kick kick;
        kick.missionGeneration = _missionGeneration;
        kick.key = found->second.key;
        kick.carId = carId;
        kick.seat = seat;
        kick.victimNetworkId = victimId;
        kick.victimSpawnGeneration = victimSpawnGeneration;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(kick, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("NPC {} threw player {} out of car {} seat {}", kick.key, victimId, carId, seat);
    }

    void NpcHost::OnSeized(NativeActor *officer, NativeActor *target, Shared::Npc::Caught::Outcome outcome) {
        const auto found = _npcs.find(officer);
        if (found == _npcs.end() || !_world) {
            return;
        }
        const auto victim = _world->NativeObjects().FindByNative(target);
        auto *replication = Framework::CoreModules::GetReplication();
        auto *player = victim.networkId && replication ? replication->GetEntity<Shared::Entities::PlayerEntity>(victim.networkId) : nullptr;
        if (!player) {
            return;
        }
        // The replica plays its half natively right after this; the pose
        // stream stays off it until the track ends.
        static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance())->Players().HoldSeized(victim.networkId);
        Shared::Npc::Seized seized;
        seized.missionGeneration = _missionGeneration;
        seized.key = found->second.key;
        seized.targetNetworkId = victim.networkId;
        seized.targetSpawnGeneration = player->spawnGeneration;
        seized.outcome = outcome;
        // Both stand where state 10 put them (C_entity::actionTick's
        // AI_POLICE_ARREST_WALK); players start both halves from there.
        const auto pose = [](NativeActor *actor) {
            const auto position = SDK::Player::WorldPosition(actor);
            const auto direction = SDK::Player::WorldDirection(actor);
            return Shared::Npc::Seized::Pose {position.x, position.y, position.z, std::atan2(direction.x, direction.z)};
        };
        seized.officer = pose(officer);
        seized.target = pose(target);
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(seized, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void NpcHost::OnCaught(NativeActor *officer, NativeActor *target, Shared::Npc::Caught::Outcome outcome) {
        const auto found = _npcs.find(officer);
        if (found == _npcs.end() || !target || !_world) {
            return;
        }
        const auto victim = _world->NativeObjects().FindByNative(target);
        auto *replication = Framework::CoreModules::GetReplication();
        auto *player = victim.networkId && replication ? replication->GetEntity<Shared::Entities::PlayerEntity>(victim.networkId) : nullptr;
        if (!player) {
            return;
        }
        // The grab settles a native player's record (C_entity.cpp arrest
        // grab); a replica's is settled at the grab too (OnGrab) and again
        // here, once the catch is complete. Unpunished: a punished settle
        // counts an offense for everyone but C_game's player, so repeated
        // catches would escalate on their own.
        SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game())->SettleRecord(target, false);
        Shared::Npc::Caught caught;
        caught.missionGeneration = _missionGeneration;
        caught.key = found->second.key;
        caught.targetNetworkId = victim.networkId;
        caught.targetSpawnGeneration = player->spawnGeneration;
        caught.outcome = outcome;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(caught, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("NPC {} {} player {}", caught.key, outcome == Shared::Npc::Caught::Outcome::Arrest ? "arrested" : "fined",
                                                                   victim.networkId);
    }

    void NpcHost::OnScream(NativeHuman *human, uint32_t screamId) {
        const auto tracked = _npcs.find(human);
        if (tracked == _npcs.end() || screamId > 0xffff || !Shared::Npc::RelayedSpeechLine(static_cast<uint16_t>(screamId))) {
            return;
        }
        Shared::Npc::Speech speech;
        speech.missionGeneration = _missionGeneration;
        speech.key = tracked->second.key;
        speech.line = static_cast<uint16_t>(screamId);
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(speech, MafiaNet::Priority::Medium, MafiaNet::Reliability::Reliable);
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
        snapshot.hostClockMs = Shared::Net::SampleClock(MafiaNet::GetTime());
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
        // The server takes a shot only with the weapon it records in hand: a
        // weapon drawn since the last snapshot goes first (same ordered
        // channel), or the first shot after the draw would be dropped.
        SendInventory(found->second, human, false);
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

    void NpcHost::OnMelee(NativeHuman &human, Shared::Combat::Action action, int8_t meleeIndex, uint16_t holdMs) {
        const auto found = _npcs.find(&human.Actor());
        if (found == _npcs.end()) {
            return;
        }
        // As for a shot: the weapon in hand reaches the server first.
        SendInventory(found->second, human, false);
        const auto direction = Unit(SDK::Player::WorldDirection(&human.Actor()), {0.0f, 0.0f, 1.0f});
        Shared::Npc::Melee melee;
        melee.missionGeneration = _missionGeneration;
        melee.key = found->second.key;
        melee.action = action;
        melee.weaponId = static_cast<uint8_t>(human.SelectedWeapon());
        melee.meleeIndex = meleeIndex;
        melee.holdMs = holdMs;
        melee.directionX = direction.x;
        melee.directionY = direction.y;
        melee.directionZ = direction.z;
        melee.hostClockMs = Shared::Net::SampleClock(MafiaNet::GetTime());
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(melee, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
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
        hit.hostClockMs = Shared::Net::SampleClock(MafiaNet::GetTime());
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(hit, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("NPC {} hit player {} (type {}, part {}, damage {:.1f})", hit.key, hit.targetNetworkId, hit.hitType, hit.bodyPart,
                                                                    damage);
    }

    void NpcHost::OnHitCar(uint64_t carId, const Vector3 &direction, const Vector3 &position, const Vector3 &normal, float damage, NativeActor *attacker, const Vector3 &localHit,
                           const Vector3 &localDirection) {
        const auto found = _npcs.find(attacker);
        if (found == _npcs.end() || !std::isfinite(damage) || damage <= 0.0f) {
            return;
        }
        Shared::Npc::CarHit hit;
        hit.missionGeneration = _missionGeneration;
        hit.key = found->second.key;
        hit.carId = carId;
        hit.damage = damage;
        hit.directionX = direction.x;
        hit.directionY = direction.y;
        hit.directionZ = direction.z;
        hit.hitX = position.x;
        hit.hitY = position.y;
        hit.hitZ = position.z;
        hit.normalX = normal.x;
        hit.normalY = normal.y;
        hit.normalZ = normal.z;
        hit.localHitX = localHit.x;
        hit.localHitY = localHit.y;
        hit.localHitZ = localHit.z;
        hit.localDirectionX = localDirection.x;
        hit.localDirectionY = localDirection.y;
        hit.localDirectionZ = localDirection.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(hit, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("NPC {} shot car {} (damage {:.1f})", hit.key, carId, damage);
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
        // A car impact blames the car's driver, as C_entity::Hit does.
        NativeActor *blamed = attacker && attacker->GetType() == NativeActor::Type::Car ? static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(attacker))->GetOwner(0) : attacker;
        damaged.attackerId = attacker ? world.NativeObjects().FindByNative(attacker).networkId : 0;
        if (blamed && IsPlayerReplica(blamed) && std::isfinite(damage) && damage > 0.0f) {
            if (const uint64_t player = world.NativeObjects().FindByNative(blamed).networkId) {
                auto &grudge = found->second.grudges[player];
                grudge.damage += damage;
                grudge.last = std::chrono::steady_clock::now();
            }
        }
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
