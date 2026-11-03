#pragma once

#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Mafia1Online::SDK::Car {
    struct NativeCar;
}

namespace Mafia1Online::SDK::Traffic {
    // Retail car traffic: one C_traffic_car mission actor (type 12, 0x26c
    // bytes) owns a pool of 0x158-byte S_ELEMENTs, each a pool C_car driven by
    // a C_driver. Drivers and passengers are only I3D models linked to the car
    // frame; nothing in a pool car is a C_human. Offsets below are the ones
    // the retail code reads (reM actors/C_traffic_car.cpp, checked in IDA).
    inline constexpr uint32_t kTrafficCarActorType = 12;

    // void __thiscall GameInit(): sizes the active slots as m_iMaxActiveCarCount
    // (+0x21c) times the traffic density and creates every pool car, so the
    // pool grows with the slot count.
    inline constexpr uintptr_t kTrafficCarGameInit = 0x4B54A0;
    // float g_fTrafficCarDensity: the slot multiplier GameInit reads
    // (0x4b5a50-0x4b5a63). G_GarageClass::ApplyFreerideStartPos 0x60e980
    // copies the profile's freeride density into it; C_game::DoneIntern
    // resets it to 1.
    inline constexpr uintptr_t kTrafficCarDensity = 0x63D7FC;
    // void __thiscall AI(unsigned frameMs), ret 4. With +0x70 set it copies
    // the active camera into +0x23c/+0x248 and, every 350 ms (+0x260), spawns
    // with ActivateElems or frees a slot for a returned police car. Then it
    // ticks every driver and despawns by camera distance (+0x224).
    inline constexpr uintptr_t kTrafficCarAI = 0x4AFC40;
    // bool __thiscall ActivateElems(bool force, int element), ret 8: element
    // -1 picks a pool car by the spawn schedule and places it on a road out
    // of the camera's sight (GetNewPosInfo 0x4af1c0).
    inline constexpr uintptr_t kTrafficCarActivateElems = 0x4AB910;
    // void __thiscall DeactivateElem(int slot, bool keepTemporary), ret 8:
    // every despawn, death and replacement ends here; it clears the slot.
    inline constexpr uintptr_t kTrafficCarDeactivateElem = 0x4AB140;
    // static void __fastcall Scipni(bool disabled): sets 0x63d809 and shows or
    // hides every active car (C_game::SetTraficVisible 0x5a8470).
    inline constexpr uintptr_t kTrafficCarScipni = 0x4AAA00;
    // static C_car *__fastcall SebraliMiKaaaruSvine(C_car *poolCar): makes a
    // temporary C_car at the pool car's pose and velocity, flags the element
    // for deactivation (+0x152) and returns the new car.
    inline constexpr uintptr_t kTrafficCarReplaceStolen = 0x4BA300;
    // static void __fastcall Krles(C_actor *mafiaCar, C_actor *attacker): a
    // rammed mafia car becomes a temporary car with one or two armed C_entity
    // crew hunting the attacker (or its car's driver); 0x63d818 records them.
    inline constexpr uintptr_t kTrafficCarMafiaResponse = 0x4B95A0;
    // static void __fastcall CoDelasTyCuraku1(C_car *, C_actor *, float force),
    // ret 4: a mafia car rammed by the local player's car (IsActivePlayerInside,
    // +0x215c) or player answers with Krles above force 5.
    inline constexpr uintptr_t kTrafficCarRammed = 0x4B94D0;
    // static void __fastcall CoDelasTyCuraku(C_car *, C_actor *driver, float
    // force), ret 4: a police pool car rammed by a person's car books the
    // ram on that person (C_shvestky::StrcilJsemHoPoldumDoVyfuku 0x4a00a0).
    inline constexpr uintptr_t kTrafficCarPoliceRammed = 0x4B9460;
    inline constexpr float kMafiaResponseForce = 5.0f;
    // static void __fastcall DostalJsemRanuKachlou(C_car *, I3D_frame *): a
    // bullet hit the driver or passenger model; it plays the in-car death
    // animation and, for the driver, force-stops the car.
    inline constexpr uintptr_t kTrafficCarOccupantShot = 0x4BADD0;
    // static I3D_model *__fastcall Corovacka(C_car *): takes the driver (or
    // passenger) model out of a traffic car and frees seat 0.
    inline constexpr uintptr_t kTrafficCarRemoveDriver = 0x4B8480;

    // g_bTrafficCarsDisabled; AI and Update return at once while set.
    inline constexpr uintptr_t kTrafficCarsDisabled = 0x63D809;
    // g_bTrafficCarsActivationSuppressed: AI does not spawn while set.
    inline constexpr uintptr_t kTrafficCarActivationSuppressed = 0x63D808;
    // std::vector<S_ELEMENT *> g_TrafficCarElements (_First, _Last).
    inline constexpr uintptr_t kTrafficCarElementsBegin = 0x63D848;
    inline constexpr uintptr_t kTrafficCarElementsEnd = 0x63D84C;
    // std::vector<S_ELEMENT *> gSpawnedCars (_First, _Last): police cars
    // waiting to go back into traffic. ActivateElems takes the first inactive
    // one into a free slot before any new car (0x4ab980), keeping its pose;
    // AI frees a slot for one when the pool is full (reM C_traffic_car.cpp
    // AI, inside the m_bGameInitStarted spawn block).
    inline constexpr uintptr_t kSpawnedCarsBegin = 0x63D86C;
    inline constexpr uintptr_t kSpawnedCarsEnd = 0x63D870;
    // std::vector<S_traffic_car_pursuit_entry> (car, driver, passenger).
    inline constexpr uintptr_t kPursuitEntriesBegin = 0x63D818;
    inline constexpr uintptr_t kPursuitEntriesEnd = 0x63D81C;

    // C_car::m_uDirectorControl, written by AI as the driver's collision flags
    // or'ed with 0x10 (VEHICLE_RUNTIME_COLLISION_ACTIVE) within 8 of the
    // player (0x4b2deb-0x4b2dfa); C_car::AI merges it into the runtime flags.
    inline constexpr uintptr_t kCarDirectorControl = 0x20F8;
    inline constexpr uint32_t kDirectorCollisionActive = 0x10;
    inline constexpr float kDirectorControlDistanceSq = 64.0f;
    // C_actor::m_bIsVisible, read by AI for the full-pool deactivation.
    inline constexpr uintptr_t kActorVisible = 0x5F;

    // C_driver fields the pool's AI reads.
    struct NativeDriver {
        [[nodiscard]] int32_t BlockedPathIndex() const {
            return *reinterpret_cast<const int32_t *>(reinterpret_cast<const std::byte *>(this) + 0x224);
        }
        // TRAFFIC_CAR_PATH_CONTROL_ACTIVE (2): the driver is parking and gets
        // out; AI starts the exit animation from it.
        [[nodiscard]] bool Parking() const {
            return (*reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(this) + 0x1DC) & 2) != 0;
        }
    };

    struct NativeTrafficCarElement {
        [[nodiscard]] Car::NativeCar *Car() const { return _car; }
        [[nodiscard]] Scene::NativeFrame *CarModel() const { return _carModel; }
        [[nodiscard]] Scene::NativeFrame *DriverModel() const { return _driverModel; }
        [[nodiscard]] Scene::NativeFrame *PassengerModel() const { return _passengerModel; }
        [[nodiscard]] NativeDriver *Driver() const { return _driver; }

        bool _active;
        std::byte _unused01[3];
        Scene::NativeFrame *_carModel;
        Scene::NativeFrame *_savedCarModel;
        Car::NativeCar *_car;
        Car::NativeCar *_savedCar;
        NativeDriver *_driver;
        float _distanceFromCamera;
        Scene::NativeFrame *_driverModel;
        Scene::NativeFrame *_passengerModel;
        // C_animations_machine_de_luxe; its model pointer is at +0x08.
        std::byte _animationMachine[0x14c - 0x24];
        bool _animationActive;
        bool _driverRemoved;
        bool _driverEntering;
        bool _skipTrafficSpeedControl;
        bool _policeCar;
        bool _carReplacementActive;
        bool _pendingDeactivation;
        bool _usedThisSpawnCycle;
        bool _dynamicallyAllocated;
        bool _mafiaCar;
        std::byte _unused156[0x158 - 0x156];
    };
    static_assert(sizeof(NativeTrafficCarElement) == 0x158);
    static_assert(offsetof(NativeTrafficCarElement, _carModel) == 0x04);
    static_assert(offsetof(NativeTrafficCarElement, _car) == 0x0c);
    static_assert(offsetof(NativeTrafficCarElement, _driver) == 0x14);
    static_assert(offsetof(NativeTrafficCarElement, _driverModel) == 0x1c);
    static_assert(offsetof(NativeTrafficCarElement, _passengerModel) == 0x20);
    static_assert(offsetof(NativeTrafficCarElement, _animationActive) == 0x14c);
    static_assert(offsetof(NativeTrafficCarElement, _driverRemoved) == 0x14d);
    static_assert(offsetof(NativeTrafficCarElement, _driverEntering) == 0x14e);
    static_assert(offsetof(NativeTrafficCarElement, _policeCar) == 0x150);
    static_assert(offsetof(NativeTrafficCarElement, _carReplacementActive) == 0x151);
    static_assert(offsetof(NativeTrafficCarElement, _pendingDeactivation) == 0x152);
    static_assert(offsetof(NativeTrafficCarElement, _dynamicallyAllocated) == 0x154);
    static_assert(offsetof(NativeTrafficCarElement, _mafiaCar) == 0x155);

    struct NativeTrafficCar {
        [[nodiscard]] Player::NativeActor &Actor() { return _actor; }
        [[nodiscard]] int32_t SlotCount() const { return _elementSlotCount; }
        [[nodiscard]] int32_t ActiveCount() const { return _activeElementCount; }
        [[nodiscard]] bool HasPool() const { return _activeElements != nullptr; }
        // Null for a free slot.
        [[nodiscard]] NativeTrafficCarElement *Slot(int32_t index) const { return _activeElements[index]; }
        [[nodiscard]] float DespawnRadius() const { return _despawnRadius; }
        [[nodiscard]] float MaxSpawnRadius() const { return _maxSpawnRadius; }

        bool ActivateElems() {
            using Call = bool(__thiscall *)(NativeTrafficCar *, bool, int32_t);
            return reinterpret_cast<Call>(kTrafficCarActivateElems)(this, false, -1);
        }
        void DeactivateElem(int32_t slot) {
            using Call = void(__thiscall *)(NativeTrafficCar *, int32_t, bool);
            reinterpret_cast<Call>(kTrafficCarDeactivateElem)(this, slot, false);
        }

        Player::NativeActor _actor;
        // m_bGameInitStarted: AI reads the camera and spawns only while set;
        // nothing else reads it.
        bool _gameInitStarted;
        std::byte _unused71[3];
        NativeTrafficCarElement **_activeElements;
        std::byte _unused78[0x218 - 0x78];
        int32_t _activeElementCount;
        int32_t _maxActiveCarCount;
        int32_t _elementSlotCount;
        float _despawnRadius;
        float _minSpawnRadius;
        float _maxSpawnRadius;
        float _currentSpawnRadius;
        std::byte _unused234[0x23c - 0x234];
        Player::Vector3 _cameraPosition;
        Player::Vector3 _cameraDirection;
        std::byte _unused254[0x260 - 0x254];
        uint32_t _spawnUpdateTimer;
        std::byte _unused264[0x26c - 0x264];
    };
    static_assert(sizeof(NativeTrafficCar) == 0x26c);
    static_assert(offsetof(NativeTrafficCar, _gameInitStarted) == 0x70);
    static_assert(offsetof(NativeTrafficCar, _activeElements) == 0x74);
    static_assert(offsetof(NativeTrafficCar, _activeElementCount) == 0x218);
    static_assert(offsetof(NativeTrafficCar, _maxActiveCarCount) == 0x21c);
    static_assert(offsetof(NativeTrafficCar, _elementSlotCount) == 0x220);
    static_assert(offsetof(NativeTrafficCar, _despawnRadius) == 0x224);
    static_assert(offsetof(NativeTrafficCar, _maxSpawnRadius) == 0x22c);
    static_assert(offsetof(NativeTrafficCar, _currentSpawnRadius) == 0x230);
    static_assert(offsetof(NativeTrafficCar, _cameraPosition) == 0x23c);
    static_assert(offsetof(NativeTrafficCar, _cameraDirection) == 0x248);
    static_assert(offsetof(NativeTrafficCar, _spawnUpdateTimer) == 0x260);

    struct NativePursuitEntry {
        Car::NativeCar *car;
        Player::NativeActor *driver;
        Player::NativeActor *passenger;
    };
    static_assert(sizeof(NativePursuitEntry) == 0x0c);

    inline void SetCarsDisabled(bool disabled) {
        using Call = void(__fastcall *)(bool);
        reinterpret_cast<Call>(kTrafficCarScipni)(disabled);
    }

    inline NativeTrafficCarElement **CarElementsBegin() {
        return *reinterpret_cast<NativeTrafficCarElement ***>(kTrafficCarElementsBegin);
    }
    inline NativeTrafficCarElement **CarElementsEnd() {
        return *reinterpret_cast<NativeTrafficCarElement ***>(kTrafficCarElementsEnd);
    }
    inline bool HasReturningCar() {
        auto **first = *reinterpret_cast<NativeTrafficCarElement ***>(kSpawnedCarsBegin);
        auto **last = *reinterpret_cast<NativeTrafficCarElement ***>(kSpawnedCarsEnd);
        for (auto **element = first; first && element != last; ++element) {
            if (*element && !(*element)->_active) {
                return true;
            }
        }
        return false;
    }
    inline NativePursuitEntry *PursuitEntriesBegin() {
        return *reinterpret_cast<NativePursuitEntry **>(kPursuitEntriesBegin);
    }
    inline NativePursuitEntry *PursuitEntriesEnd() {
        return *reinterpret_cast<NativePursuitEntry **>(kPursuitEntriesEnd);
    }

    inline Car::NativeCar *ReplaceStolen(Car::NativeCar *poolCar) {
        using Call = Car::NativeCar *(__fastcall *)(Car::NativeCar *);
        return reinterpret_cast<Call>(kTrafficCarReplaceStolen)(poolCar);
    }

    inline void MafiaResponse(Car::NativeCar *mafiaCar, Player::NativeActor *attacker) {
        using Call = void(__fastcall *)(Car::NativeCar *, Player::NativeActor *);
        reinterpret_cast<Call>(kTrafficCarMafiaResponse)(mafiaCar, attacker);
    }

    // HulaaaaaMuf: static void __fastcall (C_actor *suspect,
    // std::vector<C_car *> &policeCars, int count, bool requireAhead,
    // C_car *rammedCar, float radiusSquared), ret 10h. Its only caller is
    // C_shvestky::VasekSelChlastatNaOslavu (0x49fbff). It turns traffic police
    // cars near the suspect into police cars (pool or new "Police auto%d") at
    // the same pose and flags each source element for deactivation.
    inline constexpr uintptr_t kTrafficCarPoliceCars = 0x4B8940;
    // A VC6 std::vector<C_car *>: allocator first, then _First and _Last.
    struct NativeCarVector {
        void *allocator;
        Car::NativeCar **first;
        Car::NativeCar **last;
        Car::NativeCar **end;
    };

    // C_car seats (m_Seats: _First +0xd14, _Last +0xd18, 0x24 bytes each;
    // NoTakMiTedaDajTuPleckuJaJuOdvezu 0x4baec0) and seat in-points
    // (m_InPoints: _First +0x21cc, _Last +0x21d0, 0xa0 bytes, local position
    // first), where GameInit places a pool car's occupant models.
    inline uint8_t CarSeatCount(const Car::NativeCar *car) {
        const auto *base = reinterpret_cast<const std::byte *>(car);
        const auto first = *reinterpret_cast<const uintptr_t *>(base + 0xD14);
        const auto last = *reinterpret_cast<const uintptr_t *>(base + 0xD18);
        const auto count = first && last > first ? (last - first) / 0x24 : 0;
        return static_cast<uint8_t>(count > 8 ? 8 : count);
    }
    inline bool CarInPoint(const Car::NativeCar *car, int seat, Player::Vector3 &position) {
        const auto *base = reinterpret_cast<const std::byte *>(car);
        const auto first = *reinterpret_cast<const uintptr_t *>(base + 0x21CC);
        const auto last = *reinterpret_cast<const uintptr_t *>(base + 0x21D0);
        if (!first || last <= first || static_cast<uintptr_t>(seat) >= (last - first) / 0xA0) {
            return false;
        }
        position = *reinterpret_cast<const Player::Vector3 *>(first + static_cast<uintptr_t>(seat) * 0xA0);
        return true;
    }

    // C_game::AddDriverMan(I3D_model *, C_car *, int stage, bool formula),
    // thiscall, ret 10h: plays AutoRidicVolant.I3D on the model, posed every
    // C_game::Tick from the car's steering. DelDriverMan(I3D_model *), ret 4,
    // stops it. A registered model must be removed before its car goes.
    inline constexpr uintptr_t kGameAddDriverMan = 0x5B07E0;
    inline constexpr uintptr_t kGameDelDriverMan = 0x5B0A80;
    inline void AddDriverMan(void *game, Scene::NativeFrame *model, Car::NativeCar *car) {
        using Call = void(__thiscall *)(void *, Scene::NativeFrame *, Car::NativeCar *, int32_t, bool);
        reinterpret_cast<Call>(kGameAddDriverMan)(game, model, car, 0, false);
    }
    inline void DelDriverMan(void *game, Scene::NativeFrame *model) {
        using Call = void(__thiscall *)(void *, Scene::NativeFrame *);
        reinterpret_cast<Call>(kGameDelDriverMan)(game, model);
    }

    // GetAnimIndex(unsigned id), ecx argument, plain ret: the anim_info index
    // (8-byte entries at 0x62e488, name first) or -1. 0xf8 is the traffic
    // passenger's seated pose (HUMAN_CAR_ANIM_PASSENGER_STAND).
    inline constexpr uintptr_t kGetAnimIndex = 0x408760;
    inline constexpr uintptr_t kAnimInfo = 0x62E488;
    inline constexpr uint32_t kPassengerStandAnimation = 0xF8;
    inline const char *AnimationName(uint32_t id) {
        using Call = int32_t(__fastcall *)(uint32_t);
        const int32_t index = reinterpret_cast<Call>(kGetAnimIndex)(id);
        return index < 0 ? nullptr : *reinterpret_cast<const char *const *>(kAnimInfo + static_cast<uintptr_t>(index) * 8);
    }
    // DostalJsemRanuKachlou's in-car death pose.
    inline constexpr const char *kOccupantDeathAnimation = "smrt auto ridic01.I3D";

    // static C_actor::DebustizeFrame / NoShadowFrame(I3D_frame *), ecx
    // argument: shadows off, as GameInit does for occupant models.
    inline constexpr uintptr_t kDebustizeFrame = 0x407CD0;
    inline constexpr uintptr_t kNoShadowFrame = 0x407D10;
    inline void DisableShadows(Scene::NativeFrame *frame) {
        using Call = void(__fastcall *)(Scene::NativeFrame *);
        reinterpret_cast<Call>(kDebustizeFrame)(frame);
        reinterpret_cast<Call>(kNoShadowFrame)(frame);
    }

    // I3D_model::m_pszFileName (+0x154, kept by Duplicate); the retail
    // model cache opened "Models\<name>", replicas load the bare name.
    inline std::string ModelName(Scene::NativeFrame *model) {
        if (!model || model->FrameType() != 9) {
            return {};
        }
        const char *name = *reinterpret_cast<const char *const *>(reinterpret_cast<const std::byte *>(model) + 0x154);
        std::string result = name ? name : "";
        if (const auto slash = result.find_last_of("\\/"); slash != std::string::npos) {
            result.erase(0, slash + 1);
        }
        return result;
    }

    // C_game temporary actors (vector at +0x120: _First +0x124, _Last
    // +0x128; AddTemporaryActor 0x5a77c0).
    inline Player::NativeActor *const *TemporaryActorsBegin(const void *game) {
        return *reinterpret_cast<Player::NativeActor *const *const *>(static_cast<const std::byte *>(game) + 0x124);
    }
    inline Player::NativeActor *const *TemporaryActorsEnd(const void *game) {
        return *reinterpret_cast<Player::NativeActor *const *const *>(static_cast<const std::byte *>(game) + 0x128);
    }

    // C_sledovacka (C_game +0x2b18) car hunts: m_Cars _First +0x14, _Last
    // +0x18, 0x44 bytes each with the hunting car at +0x00, the target's car
    // at +0x08 and the target at +0x0c. Nothing clears a removed human target
    // (C_game::InvalidateActor only calls VypicujMiToyotu for cars).
    // RemoveCar(C_car *), thiscall, ret 4, ends a hunt.
    inline constexpr uintptr_t kGameCarHunts = 0x2B18;
    inline constexpr uintptr_t kCarHuntRemoveCar = 0x4A1C30;
    struct NativeCarHunt {
        Car::NativeCar *car;
        void *driver;
        Player::NativeActor *targetCar;
        Player::NativeActor *target;
        std::byte _unused10[0x44 - 0x10];
    };
    static_assert(sizeof(NativeCarHunt) == 0x44);
    inline void EndHuntsOf(void *game, const Player::NativeActor *target) {
        auto *hunts = static_cast<std::byte *>(game) + kGameCarHunts;
        using Remove = void(__thiscall *)(void *, Car::NativeCar *);
        // RemoveCar erases the entry; restart after each.
        for (bool removed = true; removed;) {
            removed = false;
            auto *first = *reinterpret_cast<NativeCarHunt **>(hunts + 0x14);
            auto *last = *reinterpret_cast<NativeCarHunt **>(hunts + 0x18);
            for (auto *hunt = first; first && hunt != last; ++hunt) {
                if (hunt->target == target || hunt->targetCar == target) {
                    reinterpret_cast<Remove>(kCarHuntRemoveCar)(hunts, hunt->car);
                    removed = true;
                    break;
                }
            }
        }
    }

    // std::vector<S_ELEMENT *> g_TrafficCarElementsWithoutDrivers: parked
    // cars whose driver got out (AI 0x4b06e5) until a pedestrian takes one
    // (ChodDoRici 0x4b8626) or it despawns. The driver model stays in the
    // element, switched off, with +0x14d clear.
    inline constexpr uintptr_t kElementsWithoutDriversBegin = 0x63D838;
    inline constexpr uintptr_t kElementsWithoutDriversEnd = 0x63D83C;
    inline bool IsParkedWithoutDriver(const NativeTrafficCarElement *element) {
        auto *const *first = *reinterpret_cast<NativeTrafficCarElement *const *const *>(kElementsWithoutDriversBegin);
        auto *const *last = *reinterpret_cast<NativeTrafficCarElement *const *const *>(kElementsWithoutDriversEnd);
        for (auto *const *it = first; first && it != last; ++it) {
            if (*it == element) {
                return true;
            }
        }
        return false;
    }

    // C_human::GetAnimIDForUseCar(bool side, bool hasDoor, bool rearSeat,
    // bool doorOpen, bool exit, bool noDriverDoor): static, the first two in
    // ecx/edx, ret 10h. The traffic driver's door animations use it with the
    // driver seat's C_car::GetSeatProperty (0x41dc30, thiscall, ret 14h).
    inline constexpr uintptr_t kHumanCarAnimation = 0x57DFC0;
    inline constexpr uintptr_t kCarSeatProperty = 0x41DC30;
    // void __thiscall C_car::GetPlacementFromTCK(int seat, int animation,
    // S_vector &position, S_vector &direction), ret 10h: where an entry
    // animation starts, in world space (ChodDoRici 0x4b8810).
    inline constexpr uintptr_t kCarPlacementFromTck = 0x41C550;
    struct SeatProperty {
        bool side = false;
        bool hasDoor = false;
        bool rearSeat = false;
        bool doorOpen = false;
    };
    inline bool CarSeatProperty(Car::NativeCar *car, int seat, SeatProperty &out) {
        using Call = bool(__thiscall *)(Car::NativeCar *, int, bool *, bool *, bool *, bool *);
        return reinterpret_cast<Call>(kCarSeatProperty)(car, seat, &out.side, &out.hasDoor, &out.rearSeat, &out.doorOpen);
    }
    // C_Vehicle::FirstSeatHasDoor: m_Seats (C_car +0xd14) has a first seat
    // with a door (+0x20). AI's exit animation is the "no door" one when the
    // car has a seat but that seat no door.
    inline bool CarFirstSeatHasDoor(const Car::NativeCar *car) {
        const auto *base = reinterpret_cast<const std::byte *>(car);
        const auto first = *reinterpret_cast<const uintptr_t *>(base + 0xD14);
        const auto last = *reinterpret_cast<const uintptr_t *>(base + 0xD18);
        return first && last > first && *reinterpret_cast<void *const *>(first + 0x20) != nullptr;
    }
    // The driver's door animation into (exit false) or out of (exit true)
    // seat 0 as C_traffic_car plays it: AI with the no-door flag for the
    // exit (reM C_traffic_car::AI), ChodDoRici without it for the entry
    // (0x4b87fb).
    inline const char *DriverDoorAnimation(Car::NativeCar *car, bool exit) {
        SeatProperty seat;
        if (!CarSeatProperty(car, 0, seat)) {
            return nullptr;
        }
        // A seat exists once GetSeatProperty answers (C_car::HasSeat).
        const bool noDriverDoor = exit && !CarFirstSeatHasDoor(car);
        using Call = uint32_t(__fastcall *)(bool, bool, bool, bool, bool, bool);
        return AnimationName(reinterpret_cast<Call>(kHumanCarAnimation)(seat.side, seat.hasDoor, seat.rearSeat, seat.doorOpen, exit, noDriverDoor));
    }
    inline bool DriverEntryPlacement(Car::NativeCar *car, Player::Vector3 &position, Player::Vector3 &direction) {
        SeatProperty seat;
        if (!CarSeatProperty(car, 0, seat)) {
            return false;
        }
        using Animation = uint32_t(__fastcall *)(bool, bool, bool, bool, bool, bool);
        const uint32_t animation = reinterpret_cast<Animation>(kHumanCarAnimation)(seat.side, seat.hasDoor, seat.rearSeat, seat.doorOpen, false, false);
        using Call = void(__thiscall *)(Car::NativeCar *, int, uint32_t, Player::Vector3 *, Player::Vector3 *);
        reinterpret_cast<Call>(kCarPlacementFromTck)(car, 0, animation, &position, &direction);
        return true;
    }

    // C_irenka, a road block barrier (actor type 31; C_mission::CreateActor
    // 0x53f7d0 allocates 0x10c bytes for it). GameInit (0x4458c0) inserts its
    // dynamic collision from the frame's world matrix, so the frame is placed
    // first; GameDone (0x445b70) erases it. C_shvestky's road block spawn
    // (0x49d750) poses a deactivated pool barrier and activates it.
    inline constexpr uintptr_t kBarrierDeactivate = 0x445bb0;
    inline constexpr uintptr_t kBarrierActivate   = 0x445be0;
    // Unlinks and hides the frame and erases the collision.
    inline void DeactivateBarrier(Player::NativeActor *barrier) {
        using Call = void(__thiscall *)(Player::NativeActor *);
        reinterpret_cast<Call>(kBarrierDeactivate)(barrier);
    }
    // Links the frame into the sector at its position, shows it and inserts
    // the collision at its world matrix.
    inline void ActivateBarrier(Player::NativeActor *barrier) {
        using Call = void(__thiscall *)(Player::NativeActor *);
        reinterpret_cast<Call>(kBarrierActivate)(barrier);
    }
} // namespace Mafia1Online::SDK::Traffic
