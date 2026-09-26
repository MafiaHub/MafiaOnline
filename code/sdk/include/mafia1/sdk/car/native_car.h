#pragma once

#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_mesh.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <span>

namespace Mafia1Online::SDK::Car {
    // Verified against the exact Steam Game.exe named in retail_image.h.
    inline constexpr uintptr_t kDestructor         = 0x41BCB0;
    inline constexpr uintptr_t kExplosion          = 0x421D60;
    inline constexpr uintptr_t kSetParticlesActive = 0x47B440;
    inline constexpr uintptr_t kSetEngineOn        = 0x4CB5B0;
    inline constexpr uintptr_t kSetLinearVelocity  = 0x47B100;
    inline constexpr uintptr_t kSetSteer           = 0x4CB4D0;
    inline constexpr uintptr_t kSetGear            = 0x4CB070;
    inline constexpr uintptr_t kSetSpeedLimit      = 0x4CB6A0;
    // C_car::AI, void __thiscall(C_car*, unsigned frameMs), ret 4: runs the
    // vehicle physics (C_Vehicle::Tick) inside C_game::Tick's actor loop,
    // before C_car::Update and G_Camera::Tick.
    inline constexpr uintptr_t kCarAI                    = 0x41F780;
    inline constexpr uintptr_t kUpdateImportantVariables = 0x4CDE90;
    inline constexpr uintptr_t kDoWheelsCollision        = 0x4E6AF0;
    inline constexpr uintptr_t kEngineFreq               = 0x4EBE40;
    inline constexpr uintptr_t kOpenDoor           = 0x4CDA20;
    // reM Game_constants.h GAME_SPEED_LIMITER_*; SetSpeedLimit adds a margin.
    inline constexpr float kSpeedLimiterActive     = 16.0f;
    inline constexpr float kSpeedLimiterDisabled   = 1000.0f;
    inline constexpr float kSpeedLimiterThreshold  = 800.0f;
    inline constexpr uintptr_t kSetFuel            = 0x4CB6E0;
    inline constexpr uintptr_t kSetAngularVelocity = 0x47B1B0;
    inline constexpr uintptr_t kSetHorn            = 0x4A9C10;
    inline constexpr uintptr_t kDeformGlass         = 0x4D6670;
    inline constexpr uintptr_t kDeform              = 0x4D5610;
    inline constexpr uintptr_t kDamageVehicle       = 0x4D6720;
    inline constexpr uintptr_t kCarHit              = 0x423600;
    inline constexpr uintptr_t kVehicleStateSize    = 0x4CEB30;
    inline constexpr uintptr_t kSaveVehicleState    = 0x4CEE00;
    inline constexpr uintptr_t kDropOut             = 0x427010;
    inline constexpr uintptr_t kReset               = 0x422BE0;
    inline constexpr uintptr_t kSetTransparency     = 0x4233E0;
    inline constexpr uintptr_t kCollisionFilterBody = 0x426340;
    inline constexpr uintptr_t kInitDamage          = 0x4D54E0;
    inline constexpr uintptr_t kInitDeform          = 0x4D51F0;
    inline constexpr uintptr_t kSwitchSpecialProjectorTexture = 0x4D6580;

    enum class NativeDropOutType : int32_t {
        Wheel = 1,
        Bumper = 2,
        Light = 3,
        LicensePlate = 4,
        Mirror = 5,
        Wing = 6,
        Door = 7,
    };

    struct NativeDropOutBox {
        float weight;
        Player::Vector3 direction;
        float friction;
        NativeDropOutType type;
    };
    static_assert(sizeof(NativeDropOutBox) == 0x18);

    struct NativeDropOutWheel {
        float radius;
        float scale;
        float weight;
        Player::Vector3 linearVelocity;
        float lateralForceMaximum;
        float rollingResistance;
    };
    static_assert(sizeof(NativeDropOutWheel) == 0x20);

    struct NativeCar;

    template <typename T> struct NativeVector {
        void *allocatorProxy;
        T *begin;
        T *end;
        T *capacity;
        size_t Size() const {
            return begin && end >= begin ? static_cast<size_t>(end - begin) : 0;
        }
    };
    static_assert(sizeof(NativeVector<uint32_t>) == 0x10);

    // reM S_DOOR: DoDoors turns the door from its current toward its target
    // angle; OpenDoor sets the target as a fraction of the maximum angle.
    struct NativeDoor {
        std::byte _unused00[0x20];
        float currentAngle;
        float targetAngle;
        float maximumAngle;
    };
    static_assert(offsetof(NativeDoor, currentAngle) == 0x20);
    static_assert(offsetof(NativeDoor, targetAngle) == 0x24);
    static_assert(offsetof(NativeDoor, maximumAngle) == 0x28);

    // reM S_SEAT; a seat without a door has a null door.
    struct NativeSeatSlot {
        std::byte _unused00[0x20];
        NativeDoor *door;
    };
    static_assert(sizeof(NativeSeatSlot) == 0x24);

    // reM S_LIGHT.
    struct NativeLight {
        Scene::NativeFrame *frame;
        void *billboard;
        void *projector;
        void *projectorLight;
        uint16_t type;
        int16_t deformZoneIndex;
        uint32_t flags;
        float damage;
        std::byte _unused1C[4];
    };
    static_assert(sizeof(NativeLight) == 0x20);
    static_assert(offsetof(NativeLight, projector) == 0x08);
    static_assert(offsetof(NativeLight, damage) == 0x18);

    struct NativeDeformZone {
        Scene::NativeFrame *frame;
        Scene::NativeMeshObject *mesh;
        void *material;
        uint16_t type;
        uint16_t flags;
        std::byte _unused10[0x0c];
        float crackThreshold;
        float crackLevel;
        std::byte _unused24[0x10];
    };
    static_assert(sizeof(NativeDeformZone) == 0x34);
    static_assert(offsetof(NativeDeformZone, crackThreshold) == 0x1c);
    static_assert(offsetof(NativeDeformZone, crackLevel) == 0x20);

    struct NativeWheel {
        void *name;
        Scene::NativeFrame *frame;
        std::byte _unused08[0xb4 - 0x08];
        // Collision material under the wheel. C_car::Update at 0x4216ca kills
        // occupants on 31 (water) and 40 (fall volume).
        int32_t surfaceMaterial;
        std::byte _unusedB8[0x108 - 0xb8];
        float slipValue;
        std::byte _unused10C[0x120 - 0x10c];
        uint32_t flags;
        std::byte _unused124[0x188 - 0x124];
        // m_fCurrentDeformAngle drives the visible wheel wobble; retail's
        // vehicle save keeps it, while DAMAGED lasts only one tick.
        float deformAngle;
        float health;
        float maximumHealth;
    };
    static_assert(offsetof(NativeWheel, surfaceMaterial) == 0xb4);
    static_assert(offsetof(NativeWheel, flags) == 0x120);
    static_assert(offsetof(NativeWheel, slipValue) == 0x108);
    // reM E_vehicle_wheel_flags; the replay record keeps a wheel skidding
    // while it is in contact.
    inline constexpr uint32_t kWheelInContact = 0x00000008;
    inline constexpr uint32_t kWheelSkidding  = 0x00000010;
    inline constexpr uint32_t kWheelHasVisual = 0x01000000;
    static_assert(offsetof(NativeWheel, deformAngle) == 0x188);
    static_assert(offsetof(NativeWheel, health) == 0x18c);

    // reM tDynamicCollObject: the grid header precedes the world position the
    // collision grid indexes; the shape reads the vehicle's m_mWorld through a
    // pointer, so only the position must be refreshed before DynUpdate.
    struct NativeDynamicCollision {
        static constexpr uint32_t kDynamicTypeMask = 0x40;
        uint32_t packed;
        std::byte _header[0x0c];
        Player::Vector3 position;
        std::byte _shape[0x44 - 0x1c];
        Player::NativeActor *ownerActor;
    };
    static_assert(offsetof(NativeDynamicCollision, position) == 0x10);
    static_assert(offsetof(NativeDynamicCollision, ownerActor) == 0x44);
    static_assert(sizeof(NativeDynamicCollision) == 0x48);

    inline constexpr uintptr_t kCollision          = 0x647F48;
    inline constexpr uintptr_t kCollisionDynUpdate = 0x5C3AC0;

    // g_collision::DynUpdate re-files the object in the dynamic grid at its
    // current position. The vehicle kernel calls it at 0x4e0d46 each physics
    // tick with the body collision at vehicle+0x22c.
    inline void UpdateDynamicCollision(NativeDynamicCollision *collision) {
        using Call = void(__thiscall *)(void *, NativeDynamicCollision *);
        reinterpret_cast<Call>(kCollisionDynUpdate)(reinterpret_cast<void *>(kCollision), collision);
    }

    // C_car contains C_actor at +0 and C_Vehicle at +0x70. The retail
    // constructor explicitly passes car+0x70 to C_Vehicle::C_Vehicle.
    struct NativeVehicle {
        static constexpr uint32_t kReplicatedLightMask = 0x00000001 | 0x00000002 | 0x00000080 | 0x00000100 | 0x00000800 |
                                                         0x00001000 | 0x00008000 | 0x00010000;
        // VEHICLE_LIGHT_STATE_MASTER lights only VEHICLE_LIGHT_TYPE_SIREN lamps.
        static constexpr uint32_t kSirenLightMask = 0x00010000;

        Player::Vector3 LinearVelocity() const {
            return _linearVelocity;
        }
        Player::Vector3 AngularVelocity() const {
            return _angularVelocity;
        }
        Player::Vector3 WorldCenter() const {
            return _worldCenter;
        }
        uint32_t LightState() const {
            return _lightStateFlags & kReplicatedLightMask;
        }
        bool HornOn() const {
            return _hornOn;
        }
        float Fuel() const {
            return _fuel;
        }
        float FuelTankCapacity() const {
            return _fuelTankCapacity;
        }
        const NativeVector<NativeLight> &Lights() const { return _lights; }
        const NativeVector<NativeDeformZone> &OriginalDeformZones() const { return _originalDeformZones; }
        const NativeVector<NativeDeformZone> &DeformZones() const { return _deformZones; }
        NativeCar *Owner() const { return _owner; }
        // reM C_Vehicle::m_pFrame; C_car::Hit tests its null-frame reray
        // against this subtree at 0x423676.
        Scene::NativeFrame *ModelFrame() const { return _modelFrame; }
        // Pose part of C_Vehicle::Reset 0x4c3860 plus the tick's collision
        // refresh 0x4e0d1e-0x4e0d7d, leaving velocities alone. The frame must
        // be updated first; special-mode per-cell volumes stay with the tick.
        void SyncPhysicsPose() {
            const float *m = _modelFrame->WorldMatrix();
            for (int i = 0; i < 16; ++i) {
                _world[i] = m[i];
            }
            // Car model frames carry no scale, so the inverse is rigid.
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    _inverseWorld[r * 4 + c] = m[c * 4 + r];
                }
                _inverseWorld[r * 4 + 3] = 0.0f;
            }
            for (int c = 0; c < 3; ++c) {
                _inverseWorld[12 + c] = -(m[12] * m[c * 4] + m[13] * m[c * 4 + 1] + m[14] * m[c * 4 + 2]);
            }
            _inverseWorld[15] = 1.0f;
            for (int i = 0; i < 16; ++i) {
                _previousWorld[i] = _world[i];
                _previousInverseWorld[i] = _inverseWorld[i];
            }
            for (int i = 12; i < 15; ++i) {
                _previousWorld[i] = 0.0f;
                _previousInverseWorld[i] = 0.0f;
            }
            _right = {m[0], m[1], m[2]};
            _up = {m[4], m[5], m[6]};
            _forward = {m[8], m[9], m[10]};
            _position = {m[12], m[13], m[14]};
            _worldCenter = {_massCenter.x * m[0] + _massCenter.y * m[4] + _massCenter.z * m[8] + m[12],
                            _massCenter.x * m[1] + _massCenter.y * m[5] + _massCenter.z * m[9] + m[13],
                            _massCenter.x * m[2] + _massCenter.y * m[6] + _massCenter.z * m[10] + m[14]};
            if (_bodyDynamicCollisionEnabled && !_specialDynamicCollisionMode) {
                _bodyCollision.position = _position;
                UpdateDynamicCollision(&_bodyCollision);
            }
            if (_extraDynamicCollisionEnabled) {
                _extraCollision.position = _position;
                UpdateDynamicCollision(&_extraCollision);
            }
        }
        int VehicleStateSize() {
            using Call = int(__thiscall *)(NativeVehicle *);
            return reinterpret_cast<Call>(kVehicleStateSize)(this);
        }
        bool SaveVehicleState(void *output) {
            using Call = bool(__thiscall *)(NativeVehicle *, void *);
            return reinterpret_cast<Call>(kSaveVehicleState)(this, output);
        }
        uint32_t DeformLODCount(size_t zone) const {
            if (zone >= _originalDeformZones.Size() || zone >= _deformZones.Size()) {
                return 0;
            }
            const auto *original = _originalDeformZones.begin[zone].mesh;
            const auto *deformed = _deformZones.begin[zone].mesh;
            if (!original || !deformed || original->lodCount > 10 || deformed->lodCount > 10) {
                return 0;
            }
            return std::min({original->lodCount, deformed->lodCount, 2U});
        }
        bool ValidateDeformCheckpoint(size_t zone, int lod, std::span<const uint32_t> packed) const {
            if (zone >= _originalDeformZones.Size() || zone >= _deformZones.Size()) {
                return false;
            }
            const auto &original = _originalDeformZones.begin[zone];
            const auto &deformed = _deformZones.begin[zone];
            return Scene::ValidatePackedDeformCheckpoint(original.mesh, deformed.mesh, deformed.frame, lod, packed);
        }
        bool ApplyDeformCheckpoint(size_t zone, int lod, std::span<const uint32_t> packed) {
            if (!ValidateDeformCheckpoint(zone, lod, packed)) {
                return false;
            }
            const auto &original = _originalDeformZones.begin[zone];
            const auto &deformed = _deformZones.begin[zone];
            return Scene::ApplyPackedDeformCheckpoint(original.mesh, deformed.mesh, deformed.frame, lod, packed);
        }
        int WheelCount() const { return _wheelCount; }
        const NativeWheel *Wheel(int index) const { return index >= 0 && index < _wheelCount && _wheels ? _wheels[index] : nullptr; }
        NativeWheel *MutableWheel(int index) { return index >= 0 && index < _wheelCount && _wheels ? _wheels[index] : nullptr; }
        void SetLightDamage(int index, uint32_t flags, float damage) {
            auto &light = _lights.begin[index];
            const bool wasDestroyed = (light.flags & 1U) != 0;
            light.flags = (light.flags & ~1U) | (flags & 1U);
            light.damage = damage;
            // Retail Deform dims a broken lamp's road projector through
            // SwitchSpecialProjectorTexture (0x4d579d-0x4d57c6); flags alone
            // leave the beam lit.
            if ((flags & 1U) && !wasDestroyed && (light.flags & 0x80000000U) && light.projector && light.frame) {
                using Call = bool(__thiscall *)(NativeVehicle *, void *, uint32_t);
                reinterpret_cast<Call>(kSwitchSpecialProjectorTexture)(this, light.projector,
                                                                      light.frame->LocalPosition().x < 0.0f ? 1U : 2U);
            }
        }
        void SetZoneDamage(int index, uint16_t flags, float crackLevel, bool playEffects) {
            auto &zone = _deformZones.begin[index];
            if ((zone.flags & 3U) == (flags & 3U) && zone.crackLevel == crackLevel) {
                return;
            }
            const bool wasBroken = (zone.flags & 1U) != 0;
            const bool broken = (flags & 1U) != 0;
            zone.crackLevel = crackLevel;
            if (zone.type == 3 && zone.mesh) {
                // DeformGlass performs the live break callback. A checkpoint
                // restores only the material, without replaying old shards or
                // sound. A repaired pane gets the original zone's material.
                if (!wasBroken && playEffects && zone.frame &&
                    crackLevel < zone.crackThreshold) {
                    const Player::Vector3 position = zone.frame->WorldPosition();
                    using Call = void(__thiscall *)(NativeVehicle *, int, const Player::Vector3 *);
                    reinterpret_cast<Call>(kDeformGlass)(this, index, &position);
                }
                else if (auto *level = zone.mesh->GetLOD(0)) {
                    if (auto *group = level->GetFGroup(0)) {
                        void *material = zone.material;
                        if (!broken && crackLevel >= zone.crackThreshold &&
                            static_cast<size_t>(index) < _originalDeformZones.Size()) {
                            material = _originalDeformZones.begin[index].material;
                        }
                        group->SetMaterial(material);
                    }
                }
            }
            zone.flags = static_cast<uint16_t>((zone.flags & ~3U) | (flags & 3U));
            if (broken && !wasBroken && zone.type != 1 && zone.frame) {
                zone.frame->SetOn(false);
                zone.frame->Update();
            }
            else if (!broken && wasBroken && zone.frame) {
                zone.frame->SetOn(true);
                zone.frame->Update();
            }
        }
        // Flat tyre (0x80000000), destroyed (0x40000000) and broken (0x400).
        static constexpr uint32_t kPersistentWheelFlags = 0xC0000400U;
        void SetWheelDamage(int index, uint32_t flags, float health, float deformAngle) {
            auto *wheel = _wheels[index];
            const bool wasDestroyed = (wheel->flags & 0x40000000U) != 0;
            const bool destroyed = (flags & 0x40000000U) != 0;
            wheel->flags = (wheel->flags & ~kPersistentWheelFlags) | (flags & kPersistentWheelFlags);
            wheel->health = health;
            wheel->deformAngle = deformAngle;
            if (destroyed && !wasDestroyed && wheel->frame) {
                wheel->frame->SetOn(false);
                wheel->frame->Update();
            }
            else if (!destroyed && wasDestroyed && wheel->frame) {
                wheel->frame->SetOn(true);
                wheel->frame->Update();
            }
        }
        void SetMechanicalDamage(float engineHealth, float engineDamagePower, bool engineDestroyed, float gearboxHealth) {
            _engineHealth = engineHealth;
            _engineDamagePower = engineDamagePower;
            _engineDestroyed = engineDestroyed;
            _gearboxHealth = gearboxHealth;
        }
        void ClearDamageFlags() { _damageFlags = 0; }
        float EngineDamagePower() const { return _engineDamagePower; }
        // reM C_Vehicle::DamageVehicle: power scales with health above a floor.
        float EngineDamagePowerFor(float engineHealth) const {
            if (!(_engineHealthMaximum > 0.0f)) {
                return 1.0f;
            }
            return std::clamp(engineHealth / _engineHealthMaximum, 0.0f, 1.0f) * (1.0f - _minimumEngineDamagePower) + _minimumEngineDamagePower;
        }
        bool EngineDestroyed() const { return _engineDestroyed; }
        float EngineHealth() const { return _engineHealth; }
        float GearboxHealth() const { return _gearboxHealth; }
        void RepairDamage() {
            using Call = void(__thiscall *)(NativeVehicle *);
            reinterpret_cast<Call>(kInitDamage)(this);
        }
        void RepairDeform() {
            using Call = void(__thiscall *)(NativeVehicle *, bool);
            reinterpret_cast<Call>(kInitDeform)(this, false);
        }
        float SteeringInput() const {
            return _steeringInput;
        }
        void ApplySteeringInput(float mappedAngle) {
            // SetSteer only writes this value after its local-control
            // linearity mapping. Replaying the mapped value through SetSteer
            // would apply that mapping a second time.
            _steeringInput = mappedAngle;
        }
        float VisualSteeringAngle() const {
            return _visualSteeringAngle;
        }
        int Gear() const {
            return _gear;
        }
        // Resolved pedal values, not raw input: an automatic gearbox swaps the
        // pedals while reversing. DoLights and the tick derive brake light,
        // revs and wheel lock from these.
        struct DriverInputs {
            float power;
            float brake;
            float handbrake;
            float clutch;
        };
        DriverInputs Inputs() const {
            const float handbrake = _handbrakeTorque > 0.0f ? _handbrakeOutput / _handbrakeTorque : 0.0f;
            return {_powerLeft, _brakePowerTarget, handbrake, _clutch};
        }
        void ApplyInputs(const DriverInputs &inputs) {
            // SetPower zeroes power while the engine is off.
            _powerLeft        = _engineOn ? inputs.power : 0.0f;
            _powerRight       = 0.0f;
            _brakePowerTarget = inputs.brake;
            _handbrakeOutput  = inputs.handbrake * _handbrakeTorque;
            _handbrakeTime    = inputs.handbrake * _handbrakeMaximumTime;
            _clutch           = inputs.clutch;
        }
        float DoorTarget(int seat) const {
            if (seat < 0 || static_cast<size_t>(seat) >= _seats.Size()) {
                return -1.0f;
            }
            const auto *door = _seats.begin[seat].door;
            return door && door->maximumAngle > 0.0f ? door->targetAngle / door->maximumAngle : -1.0f;
        }
        // C_Vehicle::OpenDoor; DoDoors then swings the door and plays its sound.
        void OpenDoor(int seat, float fraction) {
            using Call = void(__thiscall *)(NativeVehicle *, int, float, bool);
            reinterpret_cast<Call>(kOpenDoor)(this, seat, fraction, false);
        }
        size_t SeatSlotCount() const {
            return _seats.Size();
        }
        bool SpeedLimited() const {
            return _speedLimit < kSpeedLimiterThreshold;
        }
        void SetSpeedLimited(bool limited) {
            using Call = bool(__thiscall *)(NativeVehicle *, float);
            reinterpret_cast<Call>(kSetSpeedLimit)(this, limited ? kSpeedLimiterActive : kSpeedLimiterDisabled);
        }
        // Retail has no player siren control; police AI writes m_bSirenOn and
        // the siren light bar follows the master light bit (EnableLights).
        bool SirenOn() const {
            return _sirenOn;
        }
        void SetSirenOn(bool on) {
            _sirenOn = on;
        }
        // The physics-free step retail uses for replay playback
        // (C_car::UpdateReplayPlayback 0x421b00): the pose comes from the
        // frame, then the wheels and engine sound are refreshed from it.
        void UpdateImportantVariables() {
            using Call = void(__thiscall *)(NativeVehicle *);
            reinterpret_cast<Call>(kUpdateImportantVariables)(this);
        }
        void SetMotion(const Player::Vector3 &linear, const Player::Vector3 &angular) {
            _linearVelocity  = linear;
            _angularVelocity = angular;
        }
        // PHYSICS_ACTIVE keeps C_Vehicle::Update running DynUpdate of the body
        // collision; only C_Vehicle::Tick clears it.
        void KeepPhysicsActive() {
            _stateFlags |= 1u;
        }
        void SetSteering(float mappedAngle) {
            _steeringInput  = mappedAngle;
            _steeringTarget = mappedAngle;
        }
        void SetEngineRotations(float rotations) {
            _engineRotations = rotations;
        }
        // S_vector __thiscall(C_Vehicle*, float dt, const S_vector& gravity,
        // bool deform), ret 0x10 with the hidden result pointer. Without
        // deform it only moves the suspension, contacts and wheel spin.
        void DoWheelsCollision(float dt) {
            const Player::Vector3 gravity {-_up.x, -_up.y, -_up.z};
            Player::Vector3 result {};
            using Call = Player::Vector3 *(__thiscall *)(NativeVehicle *, Player::Vector3 *, float, const Player::Vector3 *, bool);
            reinterpret_cast<Call>(kDoWheelsCollision)(this, &result, dt, &gravity, false);
        }
        // Engine sound pitch; retail calls it only from C_Vehicle::Tick.
        void EngineFreq(float dt) {
            using Call = void(__thiscall *)(NativeVehicle *, float);
            reinterpret_cast<Call>(kEngineFreq)(this, dt);
        }
        void DisableAutomaticGearbox() {
            _directControl = false;
        }
        // Requests a gear; MotorRot engages it on the next clutch step. It can
        // refuse, e.g. at the rev limit or with a destroyed gearbox.
        bool SetGear(int gear) {
            using Call = bool(__thiscall *)(NativeVehicle *, int);
            return reinterpret_cast<Call>(kSetGear)(this, gear);
        }
        int MaximumGear() const {
            return _maximumGear;
        }
        float EngineRotations() const {
            return _engineRotations;
        }
        bool EngineOn() const {
            return _engineOn;
        }
        bool EngineRunning() const {
            return _engineRunning;
        }
        void SetEngineOn(bool on, bool silent) {
            // A silent SetEngineOn clears m_bEngineDestroyed (reM
            // C_Vehicle.cpp:6371). Replicated engine commands must not
            // revive a destroyed engine; only damage state decides that.
            const bool destroyed = _engineDestroyed;
            using Call = bool(__thiscall *)(NativeVehicle *, bool, bool);
            reinterpret_cast<Call>(kSetEngineOn)(this, on, silent);
            _engineDestroyed = destroyed;
        }
        void SetLinearVelocity(const Player::Vector3 &velocity) {
            using Call = void(__thiscall *)(NativeVehicle *, const Player::Vector3 *);
            reinterpret_cast<Call>(kSetLinearVelocity)(this, &velocity);
        }
        void SetAngularVelocity(const Player::Vector3 &velocity) {
            using Call = void(__thiscall *)(NativeVehicle *, const Player::Vector3 *);
            reinterpret_cast<Call>(kSetAngularVelocity)(this, &velocity);
        }
        void SetHorn(bool on) {
            using Call = void(__thiscall *)(NativeVehicle *, bool);
            reinterpret_cast<Call>(kSetHorn)(this, on);
        }
        void ApplyLightState(uint32_t state) {
            const uint32_t merged = (_lightStateFlags & ~kReplicatedLightMask) | (state & kReplicatedLightMask);
            if (_lightStateFlags == merged) {
                return;
            }
            _lightStateFlags = merged;
            if (_worldUpdateCallback) {
                _worldUpdateCallback(_owner);
            }
        }
        bool SetSteer(float normalizedAngle) {
            using Call = bool(__thiscall *)(NativeVehicle *, float);
            return reinterpret_cast<Call>(kSetSteer)(this, normalizedAngle);
        }
        bool SetFuel(float fuel) {
            using Call = bool(__thiscall *)(NativeVehicle *, float);
            return reinterpret_cast<Call>(kSetFuel)(this, fuel);
        }

        std::byte _unused00[0xf8];
        uint32_t _damageFlags;
        NativeVector<NativeLight> _lights;
        std::byte _unused10C[0x11c - 0x10c];
        uint32_t _lightStateFlags;
        std::byte _unused120[0x1a4 - 0x120];
        uint32_t _stateFlags;
        std::byte _unused1A8[0x1ac - 0x1a8];
        float _minimumEngineDamagePower;
        float _engineDamagePower;
        float _engineHealth;
        float _engineHealthMaximum;
        std::byte _unused1BC[0x1ec - 0x1bc];
        float _gearboxHealth;
        float _gearboxHealthMaximum;
        std::byte _unused1F4[0x204 - 0x1f4];
        NativeVector<NativeDeformZone> _originalDeformZones;
        NativeVector<NativeDeformZone> _deformZones;
        std::byte _unused224[0x22c - 0x224];
        NativeDynamicCollision _bodyCollision;
        NativeDynamicCollision _extraCollision;
        std::byte _unused2BC[0x308 - 0x2bc];
        void(__thiscall *_worldUpdateCallback)(NativeCar *);
        std::byte _unused30C[0x320 - 0x30c];
        Player::Vector3 _position;
        std::byte _unused32C[0x34c - 0x32c];
        Scene::NativeFrame *_modelFrame;
        std::byte _unused350[0x398 - 0x350];
        Player::Vector3 _worldCenter;
        float _powerLeft;
        float _powerRight;
        std::byte _unused3AC[0x3b0 - 0x3ac];
        float _handbrakeTorque;
        float _handbrakeMaximumTime;
        float _handbrakeTime;
        std::byte _unused3BC[0x3f8 - 0x3bc];
        Player::Vector3 _massCenter;
        std::byte _unused404[0x420 - 0x404];
        Player::Vector3 _angularVelocity;
        std::byte _unused42C[0x430 - 0x42c];
        bool _hornOn;
        bool _sirenOn;
        std::byte _unused432[0x434 - 0x432];
        float _handbrakeOutput;
        std::byte _unused438[0x4a4 - 0x438];
        float _speedLimit;
        std::byte _unused4A8[0x4c4 - 0x4a8];
        int32_t _wheelCount;
        std::byte _unused4C8[0x4cc - 0x4c8];
        bool _directControl;
        std::byte _unused4CD[0x548 - 0x4cd];
        float _engineRotations;
        std::byte _unused54C[0x560 - 0x54c];
        int32_t _gear;
        std::byte _unused564[0x568 - 0x564];
        int32_t _maximumGear;
        std::byte _unused56C[0x59c - 0x56c];
        float _speed;
        std::byte _unused5A0[0x5b8 - 0x5a0];
        float _brakePowerTarget;
        std::byte _unused5BC[0x5e0 - 0x5bc];
        float _clutch;
        std::byte _unused5E4[0x5f0 - 0x5e4];
        bool _engineDestroyed;
        std::byte _unused5F1[0x610 - 0x5f1];
        float _currentSteeringAngle;
        std::byte _unused614[0x624 - 0x614];
        float _steeringInput;
        float _steeringTarget;
        std::byte _unused62C[0x638 - 0x62c];
        bool _engineRunning;
        std::byte _unused639[0xC2C - 0x639];
        bool _engineOn;
        std::byte _unusedC2D[0xc30 - 0xc2d];
        float _fuel;
        std::byte _unusedC34[0xc38 - 0xc34];
        NativeWheel **_wheels;
        Player::Vector3 _forward;
        Player::Vector3 _right;
        Player::Vector3 _up;
        std::byte _unusedC60[0xc64 - 0xc60];
        float _visualSteeringAngle;
        std::byte _unusedC68[0xc74 - 0xc68];
        NativeCar *_owner;
        std::byte _unusedC78[0xca0 - 0xc78];
        NativeVector<NativeSeatSlot> _seats;
        std::byte _unusedCB0[0xce8 - 0xcb0];
        float _world[16];
        float _inverseWorld[16];
        float _previousWorld[16];
        float _previousInverseWorld[16];
        std::byte _unusedDE8[0x1f90 - 0xde8];
        Player::Vector3 _linearVelocity;
        std::byte _unused1F9C[0x1fa0 - 0x1f9c];
        float _fuelTankCapacity;
        std::byte _unused1FA4[0x1faa - 0x1fa4];
        bool _specialDynamicCollisionMode;
        bool _bodyDynamicCollisionEnabled;
        bool _extraDynamicCollisionEnabled;
    };
    static_assert(offsetof(NativeVehicle, _lightStateFlags) == 0x11c);
    static_assert(offsetof(NativeVehicle, _damageFlags) == 0xf8);
    static_assert(offsetof(NativeVehicle, _lights) == 0xfc);
    static_assert(offsetof(NativeVehicle, _minimumEngineDamagePower) == 0x1ac);
    static_assert(offsetof(NativeVehicle, _engineDamagePower) == 0x1b0);
    static_assert(offsetof(NativeVehicle, _engineHealthMaximum) == 0x1b8);
    static_assert(offsetof(NativeVehicle, _engineHealth) == 0x1b4);
    static_assert(offsetof(NativeVehicle, _engineDestroyed) == 0x5f0);
    static_assert(offsetof(NativeVehicle, _gearboxHealth) == 0x1ec);
    static_assert(offsetof(NativeVehicle, _gearboxHealthMaximum) == 0x1f0);
    static_assert(offsetof(NativeVehicle, _deformZones) == 0x214);
    static_assert(offsetof(NativeVehicle, _bodyCollision) == 0x22c);
    static_assert(offsetof(NativeVehicle, _bodyCollision.position) == 0x23c);
    static_assert(offsetof(NativeVehicle, _extraCollision) == 0x274);
    static_assert(offsetof(NativeVehicle, _worldUpdateCallback) == 0x308);
    static_assert(offsetof(NativeVehicle, _position) == 0x320);
    static_assert(offsetof(NativeVehicle, _worldCenter) == 0x398);
    static_assert(offsetof(NativeVehicle, _massCenter) == 0x3f8);
    static_assert(offsetof(NativeVehicle, _forward) == 0xc3c);
    static_assert(offsetof(NativeVehicle, _right) == 0xc48);
    static_assert(offsetof(NativeVehicle, _up) == 0xc54);
    static_assert(offsetof(NativeVehicle, _world) == 0xce8);
    static_assert(offsetof(NativeVehicle, _inverseWorld) == 0xd28);
    static_assert(offsetof(NativeVehicle, _previousWorld) == 0xd68);
    static_assert(offsetof(NativeVehicle, _previousInverseWorld) == 0xda8);
    static_assert(offsetof(NativeVehicle, _specialDynamicCollisionMode) == 0x1faa);
    static_assert(offsetof(NativeVehicle, _bodyDynamicCollisionEnabled) == 0x1fab);
    static_assert(offsetof(NativeVehicle, _extraDynamicCollisionEnabled) == 0x1fac);
    static_assert(offsetof(NativeVehicle, _modelFrame) == 0x34c);
    static_assert(offsetof(NativeVehicle, _angularVelocity) == 0x420);
    static_assert(offsetof(NativeVehicle, _hornOn) == 0x430);
    static_assert(offsetof(NativeVehicle, _powerLeft) == 0x3a4);
    static_assert(offsetof(NativeVehicle, _sirenOn) == 0x431);
    static_assert(offsetof(NativeVehicle, _stateFlags) == 0x1a4);
    static_assert(offsetof(NativeVehicle, _speedLimit) == 0x4a4);
    static_assert(offsetof(NativeVehicle, _seats) == 0xca0);
    static_assert(offsetof(NativeVehicle, _powerRight) == 0x3a8);
    static_assert(offsetof(NativeVehicle, _handbrakeTorque) == 0x3b0);
    static_assert(offsetof(NativeVehicle, _handbrakeMaximumTime) == 0x3b4);
    static_assert(offsetof(NativeVehicle, _handbrakeTime) == 0x3b8);
    static_assert(offsetof(NativeVehicle, _handbrakeOutput) == 0x434);
    static_assert(offsetof(NativeVehicle, _directControl) == 0x4cc);
    static_assert(offsetof(NativeVehicle, _brakePowerTarget) == 0x5b8);
    static_assert(offsetof(NativeVehicle, _clutch) == 0x5e0);
    static_assert(offsetof(NativeVehicle, _wheelCount) == 0x4c4);
    static_assert(offsetof(NativeVehicle, _engineRotations) == 0x548);
    static_assert(offsetof(NativeVehicle, _gear) == 0x560);
    static_assert(offsetof(NativeVehicle, _maximumGear) == 0x568);
    static_assert(offsetof(NativeVehicle, _speed) == 0x59c);
    static_assert(offsetof(NativeVehicle, _currentSteeringAngle) == 0x610);
    static_assert(offsetof(NativeVehicle, _steeringInput) == 0x624);
    static_assert(offsetof(NativeVehicle, _steeringTarget) == 0x628);
    static_assert(offsetof(NativeVehicle, _engineRunning) == 0x638);
    static_assert(offsetof(NativeVehicle, _engineOn) == 0xC2C);
    static_assert(offsetof(NativeVehicle, _fuel) == 0xc30);
    static_assert(offsetof(NativeVehicle, _wheels) == 0xc38);
    static_assert(offsetof(NativeVehicle, _visualSteeringAngle) == 0xc64);
    static_assert(offsetof(NativeVehicle, _owner) == 0xc74);
    static_assert(offsetof(NativeVehicle, _linearVelocity) == 0x1f90);
    static_assert(offsetof(NativeVehicle, _fuelTankCapacity) == 0x1fa0);

    struct NativeCar: Player::NativeActor {
        void SetParticlesActive(bool active) {
            using Call = void(__thiscall *)(NativeCar *, bool);
            reinterpret_cast<Call>(kSetParticlesActive)(this, active);
        }
        bool EngineOn() const {
            return _vehicle.EngineOn();
        }
        bool EngineRunning() const {
            return _vehicle.EngineRunning();
        }
        void SetEngineOn(bool on, bool silent) {
            _vehicle.SetEngineOn(on, silent);
        }
        void SetOpacity(float opacity) {
            using Call = void(__thiscall *)(NativeCar *, float);
            // Despite its native name, SetTransparency takes an alpha
            // multiplier: 1 is opaque and 0 is invisible.
            reinterpret_cast<Call>(kSetTransparency)(this, opacity);
        }
        NativeVehicle &Vehicle() {
            return _vehicle;
        }
        const NativeVehicle &Vehicle() const {
            return _vehicle;
        }
        // reM C_car burn state: once the timer passes the duration, Update
        // requests CarExplosion. A new controller must inherit it.
        bool Burning() const { return _burning; }
        uint32_t BurnTimer() const { return _burnTimer; }
        uint32_t BurnDuration() const { return _burnDuration; }
        void SetBurnState(bool burning, uint32_t timer, uint32_t duration) {
            _burning = burning;
            _burnTimer = timer;
            _burnDuration = duration;
        }
        float BodyDamage() const { return _bodyDamage; }
        float BodyHealthMaximum() const { return _bodyHealthMaximum; }
        int FuelTankHealth() const { return _fuelTankHealth; }
        int FuelTankHealthMaximum() const { return _fuelTankHealthMaximum; }
        void SetBodyDamage(float damage, int fuelTankHealth) {
            _bodyDamage = damage;
            _fuelTankHealth = fuelTankHealth;
        }
        // C_car::Reset rebuilds the physics pose from the frame and clears
        // velocities, gear, steering and the engine. Retail repositions a car
        // this way after frame SetDir/Update, e.g. the fuel pump at 0x480452.
        void Reset(float speed, bool full) {
            using Call = void(__thiscall *)(NativeCar *, float, bool);
            reinterpret_cast<Call>(kReset)(this, speed, full);
        }
        bool DropOut(Scene::NativeFrame *source, void *parameters, NativeDropOutType type, int partIndex) {
            using Call = bool(__fastcall *)(NativeCar *, Scene::NativeFrame *, void *, int, int);
            return reinterpret_cast<Call>(kDropOut)(this, source, parameters, static_cast<int>(type), partIndex);
        }

        NativeVehicle _vehicle;
        std::byte _unusedAfterVehicle[0x20bc - 0x70 - sizeof(NativeVehicle)];
        float _bodyHealthMaximum;
        float _wheelDamageFactor;
        float _bodyDamage;
        std::byte _unused20C8[0x20e8 - 0x20c8];
        uint32_t _burnTimer;
        uint32_t _burnDuration;
        bool _burning;
        std::byte _unused20F1[0x2154 - 0x20f1];
        int32_t _fuelTankHealth;
        int32_t _fuelTankHealthMaximum;
    };
    static_assert(offsetof(NativeCar, _vehicle) == 0x70);
    static_assert(offsetof(NativeCar, _vehicle._engineRunning) == 0x6A8);
    static_assert(offsetof(NativeCar, _vehicle._engineOn) == 0xC9C);
    static_assert(offsetof(NativeCar, _bodyDamage) == 0x20c4);
    static_assert(offsetof(NativeCar, _bodyHealthMaximum) == 0x20bc);
    static_assert(offsetof(NativeCar, _burnTimer) == 0x20e8);
    static_assert(offsetof(NativeCar, _burning) == 0x20f0);
    static_assert(offsetof(NativeCar, _fuelTankHealth) == 0x2154);
    static_assert(offsetof(NativeCar, _fuelTankHealthMaximum) == 0x2158);
} // namespace Mafia1Online::SDK::Car
