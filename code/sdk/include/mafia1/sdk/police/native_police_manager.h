#pragma once

#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/seat/native_seat.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Police {
    // C_game +0x2da0 is the police manager C_shvestky (0x459d94-0x459d97).
    inline constexpr uintptr_t kGamePoliceManager = 0x2DA0;
    // char __thiscall C_shvestky::TickDoRiciActors(bool force), ret 4. Its
    // retirement pass reads the active camera's world position once and,
    // unless forced, retires an entity whose XZ distance from it is at least
    // 150 (22500 squared): it removes the temporary actor, and C_human::
    // GameDone reactivates the walker it came from at its last pose.
    // C_shvestky::Tick calls it unforced every m_uDoRiciPeriod. Gangsters
    // (CreateMafianos) and recruited police (FizlosNamakatDoAkce) both join
    // this list.
    inline constexpr uintptr_t kTickDoRiciActors = 0x49EA80;
    inline constexpr float kDoRiciKeepDistanceSq = 22500.0f;
    // The exception is a living entity whose behaviour (C_human +0x5fc) is
    // 4, DoesntReactOnWeapon, which FizlosNamakatDoAkce gives every recruited
    // officer: the pass ignores its distance and retires it exactly when its
    // case is over, the police reset bit (0x08) of C_entity's state priority
    // (+0xf48) set (0x49eb8f-0x49eba7). Only a seated one still keeps its
    // car near the camera (0x49ebad-0x49ebf9). The police state sets that
    // bit as it drops to its idle substate, which only breathes (SEARCH_FOOT
    // after an arrest or fine, a call-off, a case gone).
    inline constexpr uintptr_t kHumanBehaviour        = 0x5FC;
    inline constexpr uint32_t kBehaviourIgnoresWeapons = 4;
    inline bool RetiresByDistance(const Player::NativeActor *actor) {
        if (actor->GetType() != Player::NativeActor::Type::Entity || actor->IsDead() ||
            *reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(actor) + kHumanBehaviour) != kBehaviourIgnoresWeapons) {
            return true;
        }
        const auto *vehicle = static_cast<const Seat::NativeHuman *>(static_cast<const void *>(actor))->usedActorEnter;
        return vehicle && vehicle->GetType() == Player::NativeActor::Type::Car;
    }
    // void __thiscall C_shvestky::AddDoRiciActor(C_actor *), ret 4: appends
    // the actor unless the list already holds it.
    inline constexpr uintptr_t kAddDoRiciActor = 0x49E820;

    // char __thiscall C_shvestky::SetActive(bool active, int jailMenuText,
    // char eraMode), ret 0Ch. Only mission script (C_program::Process
    // 0x472409) calls it, and mod missions run no script, so the manager
    // stays off unless the mod calls it. Turning it on stores the flag at
    // +0x0, adds C_game's player (none on the simulation host) as a person
    // and fills the police car, barrier and roadblock officer pools.
    inline constexpr uintptr_t kSetActive = 0x49B860;
    // void __thiscall AddPerson(C_human *) / DelPerson(C_human *), ret 4: a
    // crime record per person (m_Persons). Both do nothing while inactive.
    // Tick dereferences every recorded person, so a person must leave
    // before its actor is destroyed.
    inline constexpr uintptr_t kAddPerson = 0x49C7E0;
    inline constexpr uintptr_t kDelPerson = 0x49CA50;
    // void __thiscall DobreMiPokuril_MuzeJitDom(C_human *, bool punished),
    // ret 8: clears the person's crimes, wanted level and dispatch state in
    // place (punished also counts an offense). The arrest grab calls it for a
    // player target only.
    inline constexpr uintptr_t kSettleRecord = 0x49F850;
    // int __thiscall GetFreeEnemy(), plain ret: free officers in the
    // roadblock pool (vector at +0x70). DalsiChalpiDoPartie (0x49ef12) and
    // VasekSelChlastatNaOslavu (0x49fba6), its only callers, send police
    // cars only while it is above 1.
    inline constexpr uintptr_t kGetFreeEnemy = 0x49E530;
    // void __thiscall GenerateRoadBlock(int, C_human *target), ret 8; only
    // Tick calls it (0x49b072).
    inline constexpr uintptr_t kGenerateRoadBlock = 0x49D980;

    // void __thiscall C_shvestky::Tick(unsigned frameMs), ret 4: retirement
    // pass, then (while active) every person's evaluation: detectors raise
    // the pending level (+0x3e), and a pending level above the level
    // (0x49b29f) dispatches or escalates the police and becomes the level.
    // The binary clears every pending level at the end (0x49b69c).
    inline constexpr uintptr_t kTick = 0x49A300;

    inline constexpr uintptr_t kAddOfficer = 0x49F5D0;

    // void __thiscall CarCollision(C_car *car, g_collision_header *collision,
    // float speed), retn 0Ch: C_car's collision callback calls it while the
    // police are active (reM C_car.cpp:5597). For the record whose driven car (+0x04)
    // is the car it reads only the collision's first byte (dynamic flag 0x40)
    // and its owner actor (+0x44): a human (types 2, 0x13, 0x1b) hit above
    // 2.8 ORs crime 0x20 in, raises the pending level to minor up to 8.3,
    // arrest above, lethal after more than five honks (+0x68), and counts
    // the hit in its 5 s window (0x49cd5a-0x49ce12). Nothing in the first
    // ten game ticks.
    inline constexpr uintptr_t kCarCollision = 0x49CC90;
    inline constexpr float kHumanCrashSpeed = 2.8f;
    // void __thiscall NextNaSrackySoudruh(C_human *), retn 4: while active,
    // counts a kill by car on the person's record (+0x68).
    inline constexpr uintptr_t kCountRunOver = 0x49D220;
    // void __thiscall DalJsemPochuzkariDoDrzky(C_human *), retn 4: crime
    // 0x2000 (a hit patrolman) on the person's record and an evaluation now.
    inline constexpr uintptr_t kPatrolmanHit = 0x4A0040;

    // A stand-in tDynamicCollObject for CarCollision: the dynamic flag and
    // the owner actor are all it reads.
    struct NativeCollisionStandIn {
        uint32_t packed = 0x40;
        std::byte _unused04[0x44 - 0x04] {};
        Player::NativeActor *ownerActor = nullptr;
    };
    static_assert(offsetof(NativeCollisionStandIn, ownerActor) == 0x44);

    // S_shvestky, one person's crime record (0x80 bytes, AddPerson).
    struct NativeCrimeRecord {
        // Levels at +0x3c / +0x3e.
        static constexpr uint16_t kLevelNone   = 0;
        static constexpr uint16_t kLevelMinor  = 1;
        static constexpr uint16_t kLevelArrest = 2;
        static constexpr uint16_t kLevelLethal = 3;

        [[nodiscard]] Player::NativeActor *Person() const { return _person; }
        // Crime bits since the record was last settled; Firing ORs in 0x2
        // (0x49cb80).
        [[nodiscard]] uint16_t Crimes() const { return _crimes; }
        void ClearCrimes() { _crimes = 0; _pendingCrimes = 0; }
        [[nodiscard]] uint16_t Level() const { return _level; }
        [[nodiscard]] uint16_t PendingLevel() const { return _pendingLevel; }
        void SetPendingLevel(uint16_t level) {
            _pendingLevel = level;
            // The crime line VasekSel prints shows only for C_game's player.
            _pendingMessage = -1;
        }
        // Officers assigned to the person (m_Nasrany, AddNasrany 0x49f5d0).
        [[nodiscard]] size_t OfficerCount() const {
            return _officersBegin ? static_cast<size_t>(_officersEnd - _officersBegin) : 0;
        }
        // int __thiscall S_shvestky::AddNasrany(C_actor *), ret 4: appends the
        // officer unless the list holds it already.
        void AddOfficer(Player::NativeActor *officer) {
            using Call = int(__thiscall *)(NativeCrimeRecord *, Player::NativeActor *);
            reinterpret_cast<Call>(kAddOfficer)(this, officer);
        }
        // Takes the officer off the list the way Tick drops a gone one
        // (0x49acee): the last entry moves into its place.
        void RemoveOfficer(const Player::NativeActor *officer) {
            for (auto **entry = _officersBegin; _officersBegin && entry != _officersEnd; ++entry) {
                if (*entry == officer) {
                    *entry = *(_officersEnd - 1);
                    --_officersEnd;
                    return;
                }
            }
        }

        // Crime bits Tick's player block raises the shown level for
        // (0x49b4d2, 0x49b4e8): a forced arrest, a hit patrolman.
        static constexpr uint16_t kCrimeForceArrest = 0x4000;
        static constexpr uint16_t kCrimePatrolmanHit = 0x2000;
        // m_iPartyStep: reinforcements already sent for the arrest and the
        // lethal display level (0x49b618, 0x49b656).
        static constexpr int32_t kPartyArrestDispatched = 1;
        static constexpr int32_t kPartyLethalDispatched = 2;

        // m_pDrivenCar: DriveCar (0x49cbb0) sets it when the person takes a
        // driver's seat; CarCollision finds the record by it (0x49cce4).
        [[nodiscard]] Player::NativeActor *DrivenCar() const { return _drivenCar; }

        Player::NativeActor *_person;
        Player::NativeActor *_drivenCar;
        uint16_t _crimes;
        uint16_t _pendingCrimes;
        std::byte _unused0c[0x3c - 0x0c];
        uint16_t _level;
        uint16_t _pendingLevel;
        std::byte _unused40[0x50 - 0x40];
        bool _maxWanted;
        // m_bHidden: the police lost sight of the person; road blocks and
        // VasekSel's chase skip a hidden one.
        bool _hidden;
        std::byte _unused52[0x54 - 0x52];
        Player::NativeActor *_recordedVehicle;
        bool _recordedOnFoot;
        std::byte _unused59[0x5c - 0x59];
        int32_t _partyStep;
        std::byte _unused60[0x64 - 0x60];
        int32_t _pendingMessage;
        std::byte _unused68[0x74 - 0x68];
        Player::NativeActor **_officersBegin;
        Player::NativeActor **_officersEnd;
    };
    static_assert(offsetof(NativeCrimeRecord, _drivenCar) == 0x04);
    static_assert(offsetof(NativeCrimeRecord, _crimes) == 0x08);
    static_assert(offsetof(NativeCrimeRecord, _level) == 0x3c);
    static_assert(offsetof(NativeCrimeRecord, _pendingLevel) == 0x3e);
    static_assert(offsetof(NativeCrimeRecord, _maxWanted) == 0x50);
    static_assert(offsetof(NativeCrimeRecord, _hidden) == 0x51);
    static_assert(offsetof(NativeCrimeRecord, _recordedVehicle) == 0x54);
    static_assert(offsetof(NativeCrimeRecord, _recordedOnFoot) == 0x58);
    static_assert(offsetof(NativeCrimeRecord, _partyStep) == 0x5c);
    static_assert(offsetof(NativeCrimeRecord, _pendingMessage) == 0x64);

    // Pool entries of m_TempCars (+0x4c: _First +0x50, _Last +0x54) and
    // m_TempIris (+0x5c: _First +0x60, _Last +0x64), 8 bytes each with the
    // free flag at +4 (MasVolnouPlecku 0x49e240, GetTempIris 0x49e560).
    struct NativePoolSlot {
        Player::NativeActor *actor;
        bool free;
        bool special;
        std::byte _unused06[2];
    };
    static_assert(sizeof(NativePoolSlot) == 8);

    // int __thiscall PocetNasranychZivych(C_human *), ret 4: officers on
    // the person still alive. bool __thiscall (ai_police_nastenka at
    // kPoliceBoard) OhlasPatraniPlukovniku 0x53d3d0 and
    // PanoveViditeZmrdaNeboSteTupci 0x53d220 (C_actor *), ret 4: an action
    // is out for the person; an officer sees it.
    inline constexpr uintptr_t kLivingOfficersOn = 0x49D4B0;
    inline constexpr uintptr_t kBoardHasAction = 0x53D3D0;
    inline constexpr uintptr_t kBoardSeesOffender = 0x53D220;
    // char __thiscall DalsiChalpiDoPartie(C_human *, unsigned mode, float
    // radius), ret 0Ch: a police car with two officers from the pools,
    // hunting the person from a road node about the radius away (ten tries
    // for a node farther than 0.66 of it, 0x49efc0-0x49f0c1).
    inline constexpr uintptr_t kSendReinforcements = 0x49EEE0;
    inline constexpr uint32_t kModeKillCar = 0;
    inline constexpr uint32_t kModeArrestCar = 4;
    inline constexpr float kReinforcementRadius = 200.0f;
    static_assert(offsetof(NativeCrimeRecord, _officersBegin) == 0x74);

    // std::vector<C_actor *> m_DoRiciActors at +0x7c: first +0x80, last
    // +0x84, end of storage +0x88 (AddDoRiciActor 0x49e820-0x49e85f).
    struct NativePoliceManager {
        // m_Persons, std::vector<S_shvestky *> at +0x3c (first +0x40, last
        // +0x44; AddPerson 0x49c7fb, Tick 0x49b295).
        [[nodiscard]] NativeCrimeRecord *FindRecord(const Player::NativeActor *person) const {
            for (auto **record = _personsBegin; record && record != _personsEnd; ++record) {
                if ((*record)->Person() == person) {
                    return *record;
                }
            }
            return nullptr;
        }
        void SetActive(bool active) {
            using Call = char(__thiscall *)(NativePoliceManager *, bool, int32_t, char);
            reinterpret_cast<Call>(kSetActive)(this, active, 0, 0);
        }
        [[nodiscard]] bool IsActive() const {
            return _active;
        }
        // m_uEvaluationTimer (+0x04): Tick adds its frame time and, once
        // past the period (+0x08), takes the period off and evaluates every
        // person (0x49aa7f-0x49aa9d), retail's player block included. Its
        // early returns (inactive, game flag 0x40, the first ten ticks)
        // come before and leave the timer alone.
        [[nodiscard]] uint32_t EvaluationTimer() const {
            return _evaluationTimer;
        }
        void AddPerson(Player::NativeActor *human) {
            using Call = void(__thiscall *)(NativePoliceManager *, Player::NativeActor *);
            reinterpret_cast<Call>(kAddPerson)(this, human);
        }
        void DelPerson(Player::NativeActor *human) {
            using Call = void(__thiscall *)(NativePoliceManager *, Player::NativeActor *);
            reinterpret_cast<Call>(kDelPerson)(this, human);
        }
        void SettleRecord(Player::NativeActor *human, bool punished) {
            using Call = void(__thiscall *)(NativePoliceManager *, Player::NativeActor *, bool);
            reinterpret_cast<Call>(kSettleRecord)(this, human, punished);
        }

        [[nodiscard]] Player::NativeActor **RetirementBegin() const { return _retirementBegin; }
        [[nodiscard]] Player::NativeActor **RetirementEnd() const { return _retirementEnd; }
        [[nodiscard]] size_t RetirementCount() const {
            return _retirementBegin ? static_cast<size_t>(_retirementEnd - _retirementBegin) : 0;
        }
        // Shortens the list to its first count entries without freeing
        // storage; the actors past it stay alive and are re-added with
        // AddRetirement.
        void TruncateRetirement(size_t count) {
            _retirementEnd = _retirementBegin + count;
        }
        void AddRetirement(Player::NativeActor *actor) {
            using Call = void(__thiscall *)(NativePoliceManager *, Player::NativeActor *);
            reinterpret_cast<Call>(kAddDoRiciActor)(this, actor);
        }

        [[nodiscard]] NativePoolSlot *CarPoolBegin() const { return _carPoolBegin; }
        [[nodiscard]] NativePoolSlot *CarPoolEnd() const { return _carPoolEnd; }
        [[nodiscard]] NativePoolSlot *BarrierPoolBegin() const { return _barrierPoolBegin; }
        [[nodiscard]] NativePoolSlot *BarrierPoolEnd() const { return _barrierPoolEnd; }

        [[nodiscard]] int32_t LivingOfficersOn(Player::NativeActor *person) {
            using Call = int32_t(__thiscall *)(NativePoliceManager *, Player::NativeActor *);
            return reinterpret_cast<Call>(kLivingOfficersOn)(this, person);
        }
        // False when the pool has fewer than two free officers (GetFreeEnemy),
        // the person has no record or no road node lies far enough away.
        bool SendReinforcements(Player::NativeActor *person, uint32_t mode, float radius = kReinforcementRadius) {
            using Call = char(__thiscall *)(NativePoliceManager *, Player::NativeActor *, uint32_t, float);
            return reinterpret_cast<Call>(kSendReinforcements)(this, person, mode, radius) != 0;
        }

        // Retail's police reaction to the person's car hitting `partner`
        // at `speed`, as the car's own collision callback reports it. The
        // record must name the car (DriveCar); it names it for this call
        // only, whatever seat replay left there (the caller checked the
        // driver sits at the car's wheel).
        void ReportCarCollision(Player::NativeActor *driver, Player::NativeActor *car, Player::NativeActor *partner, float speed) {
            auto *record = FindRecord(driver);
            if (!_active || !record || !car || !partner) {
                return;
            }
            auto *previous = record->_drivenCar;
            record->_drivenCar = car;
            NativeCollisionStandIn collision;
            collision.ownerActor = partner;
            using Call = void(__thiscall *)(NativePoliceManager *, Player::NativeActor *, NativeCollisionStandIn *, float);
            reinterpret_cast<Call>(kCarCollision)(this, car, &collision, speed);
            record->_drivenCar = previous;
        }
        void CountRunOver(Player::NativeActor *driver) {
            reinterpret_cast<void(__thiscall *)(NativePoliceManager *, Player::NativeActor *)>(kCountRunOver)(this, driver);
        }
        void ReportPatrolmanHit(Player::NativeActor *offender) {
            reinterpret_cast<void(__thiscall *)(NativePoliceManager *, Player::NativeActor *)>(kPatrolmanHit)(this, offender);
        }

        bool _active;
        std::byte _unused01[0x04 - 0x01];
        uint32_t _evaluationTimer;
        uint32_t _evaluationPeriod;
        std::byte _unused0c[0x40 - 0x0c];
        NativeCrimeRecord **_personsBegin;
        NativeCrimeRecord **_personsEnd;
        std::byte _unused48[0x50 - 0x48];
        NativePoolSlot *_carPoolBegin;
        NativePoolSlot *_carPoolEnd;
        std::byte _unused58[0x60 - 0x58];
        NativePoolSlot *_barrierPoolBegin;
        NativePoolSlot *_barrierPoolEnd;
        std::byte _unused68[0x80 - 0x68];
        Player::NativeActor **_retirementBegin;
        Player::NativeActor **_retirementEnd;
        Player::NativeActor **_retirementStorageEnd;
    };
    static_assert(offsetof(NativePoliceManager, _evaluationTimer) == 0x04);
    static_assert(offsetof(NativePoliceManager, _evaluationPeriod) == 0x08);
    static_assert(offsetof(NativePoliceManager, _personsBegin) == 0x40);
    static_assert(offsetof(NativePoliceManager, _carPoolBegin) == 0x50);
    static_assert(offsetof(NativePoliceManager, _barrierPoolBegin) == 0x60);
    static_assert(offsetof(NativePoliceManager, _retirementBegin) == 0x80);
    static_assert(offsetof(NativePoliceManager, _retirementEnd) == 0x84);
    static_assert(offsetof(NativePoliceManager, _retirementStorageEnd) == 0x88);

    inline bool BoardHasActionFor(Player::NativeActor *person) {
        using Call = bool(__thiscall *)(uintptr_t, Player::NativeActor *);
        return reinterpret_cast<Call>(kBoardHasAction)(0x646D10, person);
    }
    inline bool BoardSeesOffender(Player::NativeActor *person) {
        using Call = bool(__thiscall *)(uintptr_t, Player::NativeActor *);
        return reinterpret_cast<Call>(kBoardSeesOffender)(0x646D10, person);
    }

    inline NativePoliceManager *PoliceManager(Core::Game::NativeGame *game) {
        return reinterpret_cast<NativePoliceManager *>(reinterpret_cast<std::byte *>(game) + kGamePoliceManager);
    }
} // namespace Mafia1Online::SDK::Police
