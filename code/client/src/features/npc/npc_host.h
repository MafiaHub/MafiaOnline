#pragma once

#include "scripted_npc_host.h"

#include "shared/features/npc/npc_entity.h"
#include "shared/features/npc/npc_report.h"

#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/police/native_police_action.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Mafia1Online::SDK::Player {
    struct NativeHuman;
    struct NativeGameItem;
    enum class NativeHitType : int32_t;
}
namespace Mafia1Online::SDK::Police {
    struct NativePoliceManager;
}
namespace Mafia1Online::SDK::Seat {
    struct NativeCar;
}
namespace Mafia1Online::SDK::World {
    struct NativeItemVector;
}
namespace Mafia1Online::Features::Combat {
    struct NativeShotData;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Npc {
    // Simulation host: the retail C_entity humans that join the world while
    // it runs, of every kind. A spawn hook per kind hands each one to Track
    // (gangster_spawn.cpp). Each runs its native AI here and is reported to
    // the server, which streams it to players. The police manager retires one
    // once no player is within 150 of it, instead of the free camera. The host
    // never binds them as network objects, so their own hits stay native.
    class NpcHost final {
      public:
        static constexpr std::chrono::milliseconds kSnapshotInterval {100};
        // A grabbed player's replica keeps the place state 10 gave it this
        // long, or until the animations start (OnSeized holds it on).
        static constexpr std::chrono::seconds kArrestAlignHold {4};
        // An officer keeps a new target at least this long before another
        // attacker can take it over.
        static constexpr std::chrono::seconds kRedirectHold {5};
        // Officers on foot are checked for a closer wanted player this
        // often; one moves to a player within kRetargetReach and closer than
        // kRetargetCloser times its offender's distance.
        static constexpr std::chrono::seconds kRetargetInterval {1};
        static constexpr float kRetargetReach = 40.0f;
        static constexpr float kRetargetCloser = 0.5f;

        // Host only.
        bool Install();
        void Uninstall();
        void RegisterRPC(World::WorldService &world);
        void Update(World::WorldService &world);
        // The native NPCs are gone (mission closing); the server drops the
        // reported ones when the generation changes or they go stale.
        void Reset();

        // Starts reporting a C_entity the retail game just created from the
        // reported walker with this key (0 for none). carId: the network car
        // a traffic driver was just thrown out of (0 for none). key: the
        // server's key of a scripted NPC, else 0 for the next of the host's.
        void Track(SDK::Player::NativeActor *actor, Shared::Entities::NpcKind kind, const std::string &model, uint32_t walkerKey, uint64_t carId = 0, uint32_t key = 0);
        [[nodiscard]] SDK::Player::NativeActor *FindByKey(uint32_t key) const;
        // Stops reporting one of them now (Retired), before its removal.
        void Release(SDK::Player::NativeActor *actor) {
            Retire(actor);
        }
        // A Retired for a key the host never tracked (a scripted NPC it could
        // not create).
        void ReportRetired(uint32_t key) const;
        // C_human::NewScream on a tracked NPC: its line goes to the server
        // for every player's copy (Shared::Npc::Speech).
        void OnScream(SDK::Player::NativeHuman *human, uint32_t screamId);
        // A C_entity the retail game made without a spawn hook of its own
        // (a mafia car's crew, a police car's crew, a thrown traffic driver):
        // tracked with the model its frame was opened from.
        void TrackIfNew(SDK::Player::NativeActor *actor, Shared::Entities::NpcKind kind, uint64_t carId = 0);
        // A player's replica the police keep a crime record of.
        [[nodiscard]] bool IsPolicePerson(const void *actor) const {
            return _policePersons.contains(const_cast<void *>(actor));
        }
        [[nodiscard]] bool Owns(const void *actor) const {
            return _npcs.contains(const_cast<void *>(actor));
        }
        // One a server script spawned (Npc.spawn, Taxi.board).
        [[nodiscard]] bool IsScripted(const void *actor) const {
            const auto found = _npcs.find(const_cast<void *>(actor));
            return found != _npcs.end() && Shared::Npc::IsScriptedKey(found->second.key);
        }
        void ForEachTracked(const std::function<void(SDK::Player::NativeActor *, Shared::Entities::NpcKind)> &visit) const;
        // Retires a retail-made actor (a police or mafia car, or one of its
        // crew) on the next native retirement pass, wherever the players
        // are: C_shvestky::TickDoRiciActors then releases it the retail way
        // (crime records' officer lists, radar, car hunts, actor lists, the
        // police pool). A tracked NPC goes as a server Remove makes it go.
        // The caller checked no case runs through it (crew_car_retire.h).
        void RequestRetirement(SDK::Player::NativeActor *actor);
        // Before the host removes a player's replica: nothing native may keep
        // pointing at it (crime record, police actions, car hunts).
        void ForgetHuman(SDK::Player::NativeActor *human);
        // Reports one of them's seat again on the next update, whatever was
        // reported last (a throw the server took but the native refused).
        void ResendSeat(const void *actor);
        // C_human::Use_Actor on one of them finished (seat_hooks.cpp); seat
        // is the one it held before the call.
        void OnUseCar(SDK::Player::NativeActor *actor, SDK::Seat::NativeCar *car, int action, int seat);
        // A crew retail seated itself (Krles: Intern_UseCar) and that was
        // tracked just now: its seat goes to the server at once, and its
        // exit too if the AI already sent it out.
        void ReportCrewSeat(SDK::Player::NativeActor *actor, SDK::Seat::NativeCar *car);
        // ai_logs::GetMaxAngryCharacter on one of them: the angriest log entry
        // retail picked, replaced by the player that hurt this NPC most
        // lately, else the nearest player it is at odds with.
        void *PickTarget(SDK::Player::NativeActor *npc, void *logs, void *retail);
        // Takes back the anger PickTarget lent a player's log entry.
        void RestoreAnger(void *npc);
        void OnNativeDestroyed(void *actor);
        void OnShot(SDK::Player::NativeHuman &human, const Combat::NativeShotData &shot);
        // C_human::Do_Shoot started, released or dropped a melee attack of
        // one of them (combat_hooks.cpp).
        void OnMelee(SDK::Player::NativeHuman &human, Shared::Combat::Action action, int8_t meleeIndex, uint16_t holdMs);
        // Retail DropOutItems on one of them; the server creates the pickups.
        void OnNativeDrop(SDK::Player::NativeHuman &human, const SDK::World::NativeItemVector &items);
        // A native Hit by one of them on a player's protected replica.
        void OnHitPlayer(World::WorldService &world, SDK::Player::NativeHuman &target, SDK::Player::NativeHitType type, const SDK::Player::Vector3 &direction, float damage,
                         SDK::Player::NativeActor *attacker, uint32_t bodyPart);
        // A native C_car::Hit by one of their bullets on a player's network
        // car body, with the impact in the car's frame.
        void OnHitCar(uint64_t carId, const SDK::Player::Vector3 &direction, const SDK::Player::Vector3 &position, const SDK::Player::Vector3 &normal, float damage,
                      SDK::Player::NativeActor *attacker, const SDK::Player::Vector3 &localHit, const SDK::Player::Vector3 &localDirection);
        // C_entity::Hit on one of them finished (death_hooks.cpp). A police
        // officer hit by a player it is not hunting turns on that player:
        // retail never moves an officer to another offender by itself.
        void OnAttacked(World::WorldService &world, SDK::Player::NativeActor &npc, SDK::Player::NativeActor &attacker);
        // An officer's police action ticked in this state (police_host.cpp).
        void OnPoliceActionTick(SDK::Player::NativeActor *officer, uint32_t state);
        // After a native Hit on one of them; deathAnimation is the death
        // that Hit picked (C_human::GetAnimIDForDeath), 0 for none.
        void OnHit(World::WorldService &world, SDK::Player::NativeHuman &npc, SDK::Player::NativeHitType type, const SDK::Player::Vector3 &direction, float damage,
                   SDK::Player::NativeActor *attacker, uint32_t bodyPart, float healthBefore, uint16_t deathAnimation);
        // A player's car the server checked ran into a human this host
        // runs (one of its NPCs, or a player's replica) at `speed`: what the
        // driven car's own collision would have told the police here
        // (C_shvestky::CarCollision's human crash, and a run-over counted on
        // a kill), as the host's copy of the car runs no physics.
        void ReportCarContact(SDK::Player::NativeActor *car, SDK::Player::NativeActor *victim, float speed, bool killed);
        // Retail threw a player's replica out of its network car seat.
        void OnKick(SDK::Player::NativeActor *thrower, uint64_t carId, uint8_t seat, uint64_t victimId, uint64_t victimSpawnGeneration);
        // A police officer grabbed a player's replica for an arrest or a fine
        // (police_host.cpp), the moment retail settles its own player.
        void OnGrab(SDK::Player::NativeActor *officer, SDK::Player::NativeActor *target);
        // The arrest or fine animations start on a player's replica
        // (police_host.cpp).
        void OnSeized(SDK::Player::NativeActor *officer, SDK::Player::NativeActor *target, Shared::Npc::Caught::Outcome outcome);
        // A police officer finished an arrest or a fine on a player's replica
        // (police_host.cpp).
        void OnCaught(SDK::Player::NativeActor *officer, SDK::Player::NativeActor *target, Shared::Npc::Caught::Outcome outcome);
        // Around C_shvestky::Tick (police_host.cpp).
        void BeforePoliceTick(SDK::Police::NativePoliceManager &police);
        // evaluated: the tick ran its per-person evaluation (its timer
        // passed the period, 0x49aa95), where retail's own player block sits.
        void AfterPoliceTick(SDK::Police::NativePoliceManager &police, bool evaluated);
        // The police manager's retirement pass (hook body).
        char TickRetirement(SDK::Police::NativePoliceManager *police, char(__thiscall *original)(SDK::Police::NativePoliceManager *, bool));

      private:
        struct Tracked {
            uint32_t key = 0;
            Shared::Entities::NpcKind kind = Shared::Entities::NpcKind::Gangster;
            SDK::Player::Vector3 lastPosition {};
            // Seen in C_game's temporary actors; leaving them later is a
            // retirement (FreeTempEnemy returns a pool officer to its pool).
            bool temporary = false;
            // The seat last reported, carId 0 for on foot.
            uint64_t seatCarId = 0;
            uint8_t seat = Shared::Npc::Seat::kNoSeat;
            // Report the seat on the next pass even when it looks unchanged.
            bool forceSeatReport = false;
            // Damage each player (network id) did to it, and when last.
            struct Grudge {
                float damage = 0.0f;
                std::chrono::steady_clock::time_point last;
            };
            std::unordered_map<uint64_t, Grudge> grudges;
            uint64_t target = 0;
            std::chrono::steady_clock::time_point targetSince;
            // The inventory last reported to the server.
            Shared::Npc::Inventory inventory;
            bool removeRequested = false;
            // The death its fatal Hit played, for every replica to play.
            uint16_t deathAnimation = 0;
            // When it died on the sample clock its poses carry: the fatal
            // Hit's moment, or the first snapshot that found it dead.
            std::optional<uint16_t> deathClock;
            // Last time a hit turned this officer on another player.
            std::chrono::steady_clock::time_point lastRedirect;
            // Its police action's state and when it last ticked: states 10
            // to 12 run an arrest or fine past the grab, which nothing may
            // interrupt.
            uint32_t policeState = 0;
            std::chrono::steady_clock::time_point policeTickAt;
            // The replica it grabbed, while the arrest or fine runs.
            SDK::Player::NativeActor *grabbed = nullptr;
            // The offender and mode of its police board case as last seen,
            // for the log of each case's start, mode changes and end.
            SDK::Player::NativeActor *caseOffender = nullptr;
            uint32_t caseMode = 0;
            // The anger PickTarget lent a player's log entry (actor, its own
            // value, the value written), restored before the next pick.
            struct LentAnger {
                SDK::Player::NativeActor *actor = nullptr;
                float original = 0.0f;
                float written = 0.0f;
            };
            LentAnger anger;
        };

        void SendSnapshots(float seconds);
        // Retail-made C_entities joining, and pool officers going back.
        void ScanTemporaryActors(World::WorldService &world);
        // Seat changes in the cars the host drives.
        void SendSeats(World::WorldService &world);
        // from: the seat it left, for the server to play the exit from.
        void ReportSeat(Tracked &tracked, uint64_t carId, uint8_t seat, bool animated, uint64_t fromCarId, uint8_t fromSeat);
        void Retire(void *actor);
        // The player (network id) that should be this officer's offender.
        uint64_t TopGrudge(Tracked &tracked) const;
        // Inventory changes and the reloads they reveal.
        void SendInventory(Tracked &tracked, const SDK::Player::NativeHuman &human, bool dead);
        bool NearObserver(const SDK::Player::Vector3 &position) const;
        // Retail's police block for its own player, run for every player.
        void RunPlayerPolicePass(SDK::Police::NativePoliceManager &police);
        // In an arrest or fine (police action states 10 to 12, ticked
        // within the last second).
        static bool Arresting(const Tracked &tracked, std::chrono::steady_clock::time_point now) {
            return tracked.policeState >= SDK::Police::NativePoliceAction::kStateArrestWalk && tracked.policeState <= SDK::Police::NativePoliceAction::kStateDone &&
                   now - tracked.policeTickAt < std::chrono::seconds(1);
        }
        // Keeps the records of the replicas being arrested or fined settled.
        void QuietGrabbed(SDK::Police::NativePoliceManager &police);
        // Police off: every player's crime bits and pending level go.
        void ClearPlayerCrimes(SDK::Police::NativePoliceManager &police);
        // Moves an officer from its case to a new one on this player, with
        // its place in the players' crime records (m_Nasrany).
        void MoveOfficer(SDK::Police::NativePoliceManager &police, SDK::Player::NativeActor *officer, Tracked &tracked, SDK::Player::NativeActor *from,
                         SDK::Player::NativeActor *to, uint32_t mode);
        // Officers on foot after one player move to a wanted player much
        // closer to them.
        void RetargetOfficers(SDK::Police::NativePoliceManager &police);
        void LogPoliceCases(SDK::Police::NativePoliceManager &police);
        void LogPoliceCaseEnd(SDK::Player::NativeActor *officer, Tracked &tracked, SDK::Police::NativePoliceManager *police, bool despawned, bool newOffender);
        [[nodiscard]] uint64_t CaseTargetId(SDK::Player::NativeActor *offender) const;
        // Turns the police manager on for the loaded mission and keeps every
        // spawned player's replica in its crime records, except the police
        // officers', which it treats as retail treats its own.
        void UpdatePolice(World::WorldService &world);
        // A replica just turned officer or back: every NPC rates it afresh.
        void ForgetContacts(SDK::Player::NativeActor *replica);
        // Script backup orders (Police.sendBackup), each answered.
        void SendBackups(SDK::Police::NativePoliceManager &police);
        // Any spawned player's replica, officer or not.
        [[nodiscard]] bool IsPlayerReplica(const void *actor) const {
            return _policePersons.contains(const_cast<void *>(actor)) || _policeOfficers.contains(const_cast<void *>(actor));
        }

        World::WorldService *_world = nullptr;
        ScriptedNpcHost _scripted {*this};
        uint64_t _missionGeneration = 0;
        uint32_t _nextKey = 1;
        std::unordered_map<void *, Tracked> _npcs;
        // Retail-made actors RequestRetirement put in the retirement list,
        // which the next pass retires whatever their distance; compared
        // only, and dropped once they left the list.
        std::unordered_set<void *> _forcedRetirement;
        // Player replicas the police manager keeps a crime record for.
        std::unordered_set<void *> _policePersons;
        struct ReportedRecord {
            uint8_t level = 0;
            uint16_t crimes = 0;
            uint8_t officers = 0;
        };
        std::unordered_map<void *, ReportedRecord> _reportedRecords;
        // A script order for a player whose replica is not yet in the police
        // records (just spawned) waits this long for it.
        static constexpr std::chrono::seconds kPoliceOrderWait {3};
        struct PendingOrder {
            Shared::Npc::PoliceOrder order;
            std::chrono::steady_clock::time_point received;
        };
        std::vector<PendingOrder> _policeOrders;
        bool _policeEnabled = true;
        // Players on police duty (network ids, from the server) and the
        // replicas of theirs that are out of the crime records and carry
        // the police behaviour class.
        std::unordered_set<uint64_t> _officerIds;
        std::unordered_set<void *> _policeOfficers;
        // A police car the server asked for; the suspect's replica may not
        // be recorded yet (just spawned) and is waited for like an order.
        // C_shvestky::DalsiChalpiDoPartie looks for its road node about this
        // far from the suspect; retail's own reinforcements use 200.
        static constexpr float kBackupRadius = 140.0f;
        struct PendingBackup {
            Shared::Npc::PoliceBackup order;
            std::chrono::steady_clock::time_point received;
        };
        std::vector<PendingBackup> _backups;
        // Spawned players' positions, refreshed every Update.
        std::vector<SDK::Player::Vector3> _observers;
        std::chrono::steady_clock::time_point _lastSnapshot;
        std::chrono::steady_clock::time_point _lastRetarget;
    };

    // Native inventory in the reported form.
    Shared::Npc::Inventory ReadInventory(const SDK::Player::NativeHuman &human);
} // namespace Mafia1Online::Features::Npc
