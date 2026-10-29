#pragma once

#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/player/native_inventory.h>
#include <mafia1/sdk/sound/native_sound.h>

#include <cstdint>

namespace Mafia1Online::SDK::Player {
    // The retail executable is accepted by exact SHA-256 before this header is used.
    inline constexpr uintptr_t kActorWorldPosition  = 0x407FE0;
    inline constexpr uintptr_t kActorWorldDirection = 0x408040;
    inline constexpr uintptr_t kPersonAnim          = 0x573E50;
    // C_program::PERSON_PLAYANIM dispatches C_human::Do_PlayAnim for human
    // actors. C_entity's cutscene mode skips its autonomous AI while its
    // C_human animation machine continues to tick.
    inline constexpr uintptr_t kHumanDoPlayAnimation = 0x585E80;
    // PERSON_STOPANIM: C_human::Do_StopAnim, __thiscall, plain ret. Back to
    // idle (work state 1, PersonAnim of the stop state) while alive.
    inline constexpr uintptr_t kHumanDoStopAnimation = 0x57D2E0;
    inline constexpr uintptr_t kHumanResetActions    = 0x58B870;
    // C_human::Do_Breath, __thiscall, plain ret: stands a living human that
    // is idle or walking (work state 1 to 3) and stops its move.
    inline constexpr uintptr_t kHumanDoBreath        = 0x57D310;
    inline constexpr uintptr_t kEntityHoldForCutscene = 0x52FF50;
    inline constexpr uintptr_t kHumanDeactivateCollisions = 0x575F60;
    inline constexpr uintptr_t kChangeWeaponModel   = 0x57EC20;
    // Retail HUMAN_CHANGEMODEL calls C_human::intern_ChangeModel. The
    // preserving variant keeps player/seat registration while reloading the
    // same model frame; the caller refreshes the weapon attachment afterward.
    inline constexpr uintptr_t kHumanChangeModel    = 0x587190;
    inline constexpr uintptr_t kHumanShooting       = 0x584620;
    inline constexpr uintptr_t kDoAimed             = 0x57F830;
    inline constexpr uintptr_t kDoCrouched          = 0x57F8A0;
    inline constexpr uintptr_t kPoseSetPoseAimed    = 0x579EA0;
    inline constexpr uintptr_t kHumanDoReload       = 0x585B40;
    // C_human::Do_Shoot: bool __thiscall(C_human*, bool pressed, const
    // S_vector* target), ret 8. C_player::AI calls it every frame with the
    // fire button state; a melee release starts the attack animation.
    inline constexpr uintptr_t kHumanDoShoot        = 0x583590;
    // human_notify_callback(I3D_frame*, message, note, arg), __stdcall.
    // Note 41 duplicates the held model and calls C_game::NewGrenade.
    inline constexpr uintptr_t kHumanNotify         = 0x56D130;
    // reM HUMAN_ANIM_THROW_GRENADE; Do_Shoot(false) selects this state only
    // after a throwable has charged for more than 400 ms.
    inline constexpr int32_t kHumanThrowGrenadeAnimation = 163;
    // reM E_human_car_throw_animation: Do_ThrowCocotFromCar (0x587d70) moves
    // the thrower to the door and starts its pull-out as state 306 or 307.
    // C_human::Update (0x572f8a) returns it to HUMAN_ANIM_STOP at track end.
    inline constexpr int32_t kHumanCarThrowAttackerFrontAnimation = 306;
    inline constexpr int32_t kHumanCarThrowAttackerBackAnimation  = 307;
    // reM C_human::Movement dispatches on m_iCanWork. Ordinary movement
    // changes idle (1) to locomotion (2); timed actions (5) and animation
    // sequences (6) own their tracks until the native state machine ends them.
    inline constexpr int32_t kHumanIdleWorkState = 1;
    inline constexpr int32_t kHumanLocomotionWorkState = 2;
    // Do_PlayAnim's track (0x585e80); C_human::Update returns it to idle
    // once the track ends.
    inline constexpr int32_t kHumanPlayAnimationWorkState = 4;
    inline constexpr int32_t kHumanTimedActionWorkState = 5;
    inline constexpr int32_t kHumanAnimationSequenceWorkState = 6;
    // bool __thiscall(C_human*), plain ret: probes the wall and ledge ahead of
    // m_vPosition/m_vDirection and starts climb animation 104 or 105.
    inline constexpr uintptr_t kHumanDoClimb        = 0x586400;
    // void __thiscall C_human::NewScream(unsigned screamId), retn 4 (reM
    // C_human.cpp:11991): plays voices.def group screamId - 2 in m_uVoice
    // (+0x680) as "%08lu.wav" on the human's own sound frame (+0x6cc). It
    // returns quietly when screams are disabled (+0x684), the sound frame is
    // missing, (C_game +0x3628 & m_uScreamMask +0x9a0) is 0, the group is
    // out of range or has no sound for this voice, or a line of equal or
    // higher priority (+0x6d0) is playing (an unsigned compare). Nothing
    // resets that priority when a line ends, so a group above 500 silences
    // the human until something lowers it (NativeHuman::Say does).
    inline constexpr uintptr_t kHumanNewScream = 0x58b470;
    // voices.def holds S_voice_definition {uint priority; uint sounds[40]}.
    inline constexpr uint32_t kHumanVoiceCount = 40;
    inline constexpr int32_t kClimbLowAnimation     = 104;
    inline constexpr int32_t kClimbHighAnimation    = 105;
    // C_human::Can_SpecialStroke: C_human* __fastcall(C_human*, bool), plain
    // ret. A found target receives Do_HardHead/Do_Castrate, which subtract
    // health directly and bypass C_human::Hit.
    inline constexpr uintptr_t kHumanCanSpecialStroke = 0x584010;
    inline constexpr uintptr_t kDefaultAnimEvents   = 0x6368D0;
    // C_human::Collision: bool __thiscall(C_human*, C_actor*, const S_vector&
    // position, const S_vector& direction, const S_vector& normal, float,
    // I3D_frame*), retn 18h (IDA 0x5784a0; C_player::Collision 0x5941c0
    // forwards its six arguments and returns with retn 18h). For a car
    // (actor type 4) it returns at once for a human in a car (+0x98); below
    // |direction| 3 it pushes the human out of the car's frame (only while
    // no hit track plays) and calls Do_SideJump(position, direction); from 3
    // it calls Hit(HIT_TYPE_SNIPER, (|v| - 2.5) * 4.2). Either way sound 184
    // at 5 and, for a dead human with the police active,
    // C_shvestky::NextNaSrackySoudruh(the car's driver).
    inline constexpr uintptr_t kHumanCollision = 0x5784A0;
    // C_player::Collision: the same, unless the police collision lock
    // (+0xada) is set and ai_police_nastenka::SemTrefenaLocklaKurva refuses.
    inline constexpr uintptr_t kPlayerCollision = 0x5941C0;
    // C_human::Do_SideJump(const S_vector& position, const S_vector&
    // direction), __thiscall, retn 8: unless boxed, carrying a body or
    // already dodging (animation id 322-329), sets the animation state (+0x74)
    // to the dodge away from the direction (GAME_ANIM_DODGE_* 322-329).
    inline constexpr uintptr_t kHumanDoSideJump = 0x587040;
    inline constexpr int32_t kHumanSideJumpFirstAnimation = 322;
    inline constexpr int32_t kHumanSideJumpLastAnimation  = 329;
    // C_human::Do_Holster, __thiscall, plain ret: puts the selected weapon
    // away (G_Inventory::Holster); what does not fit drops (DropOutItems,
    // which PickupService routes to the server).
    inline constexpr uintptr_t kHumanDoHolster = 0x585C60;

    struct NativeItemProperty {
        int32_t type;
        int32_t value;
    };
    static_assert(sizeof(NativeItemProperty) == 8);

    enum class LocomotionAnimation : int32_t {
        Idle              = 1,
        RunForward        = 5,
        WalkForward       = 6,
        WalkBackward      = 7,
        WalkLeft          = 11,
        WalkRight         = 12,
        RunLeft           = 13,
        RunRight          = 14,
        RunForwardLeft    = 15,
        RunForwardRight   = 16,
        WalkForwardLeft   = 17,
        WalkForwardRight  = 18,
        WalkBackwardLeft  = 19,
        WalkBackwardRight = 20,
    };

    // C_human extends C_actor. reM's inline GetHealth/SetHealth address the
    // health member of m_Properties at +0x640, not the initial properties.
    struct NativeHuman {
        [[nodiscard]] NativeActor &Actor() {
            return _actor;
        }
        [[nodiscard]] const NativeActor &Actor() const {
            return _actor;
        }
        // Fists and knuckleduster (0), cutting (6) and blunt (7) item types
        // run Do_Shoot's melee branch; 5 is the unarmed-with-item case.
        [[nodiscard]] bool HoldsMeleeItem() const {
            return _weaponItemType == 0 || _weaponItemType == 5 || _weaponItemType == 6 || _weaponItemType == 7;
        }
        [[nodiscard]] bool HoldsThrowable() const {
            return _weaponItemType == 4;
        }
        [[nodiscard]] bool GrenadeCharging() const {
            return _weaponActionActive;
        }
        [[nodiscard]] uint32_t GrenadeChargeStartTime() const {
            return static_cast<uint32_t>(_grenadeChargeStartTime);
        }
        void PrepareGrenadeRelease(uint32_t gameTime, uint16_t chargeMs, const Vector3 &direction) {
            _grenadeChargeStartTime = static_cast<int32_t>(gameTime - chargeMs);
            _shootTarget = direction;
        }
        [[nodiscard]] bool MeleeArmed() const {
            return _meleeActive;
        }
        struct MeleeSnapshot {
            int32_t index;
            int32_t animationState;
            int32_t canWork;
            int32_t weaponChangeTime;
            int32_t actionTimeRemaining;
            bool operator==(const MeleeSnapshot &) const = default;
        };
        [[nodiscard]] MeleeSnapshot Melee() const {
            return {_meleeAttackIndex, _animationState, _canWork, _weaponChangeTime, _actionTimeRemaining};
        }
        [[nodiscard]] int32_t MeleeAttackIndex() const {
            return _meleeAttackIndex;
        }
        [[nodiscard]] uint32_t MeleeAttackTime() const {
            return static_cast<uint32_t>(_meleeAttackTime);
        }
        bool DoShoot(bool pressed) {
            using Call = bool(__thiscall *)(NativeHuman *, bool, const Vector3 *);
            return reinterpret_cast<Call>(kHumanDoShoot)(this, pressed, nullptr);
        }
        // The release branch reads the press time and combo index. Preserve
        // the controlling player's measured hold when replaying that branch.
        void PrepareMeleeRelease(int32_t index, uint32_t gameTime, uint16_t holdMs) {
            _meleeActive      = true;
            _meleeAttackIndex = index < 0 ? 0 : index;
            _meleeAttackTime  = static_cast<int32_t>(gameTime - holdMs);
        }
        void CancelMeleeRelease() {
            _meleeActive = false;
        }
        // The attack C_don_ocas::Hit's walker backstab reads off its attacker
        // (0x44cd59-0x44cd78): the combo index (+0x228, -1 for the held heavy
        // blow) and the selected item (+0x4a0: knife 3 or bat 4). Swapped in
        // around one such call and back; the weapon model is not rebuilt.
        struct MeleeAttack {
            int32_t index;
            uint16_t itemId;
        };
        static constexpr int32_t kHeavyMeleeAttackIndex = -1;
        MeleeAttack ExchangeMeleeAttack(MeleeAttack attack) {
            const MeleeAttack previous {_meleeAttackIndex, _inventory.Selected().itemId};
            _meleeAttackIndex           = attack.index;
            _inventory.Selected().itemId = attack.itemId;
            return previous;
        }
        [[nodiscard]] float Health() const {
            return _health;
        }
        void SetHealth(float health) {
            _health = health;
        }
        // m_iRailwayFrameIndex: the railway frame a rider is linked to, 0
        // the body and n wagon n - 1 (NativeRailway::Vagon).
        [[nodiscard]] uint32_t RailwayFrameIndex() const {
            return _railwayFrameIndex;
        }
        // m_uVoice indexes each voices.def group's 40 sounds; NewScream does
        // not range-check it. C_human::SetVoice is an inline plain store.
        [[nodiscard]] uint32_t Voice() const {
            return _voice;
        }
        void SetVoice(uint32_t voice) {
            _voice = voice < kHumanVoiceCount ? voice : _voice;
        }
        // Says voices.def line `screamId` (the id NewScream takes, group + 2)
        // now, cutting off whatever line it was saying. True when the line
        // started: false when the human cannot speak (screams disabled, no
        // sound frame, scream mask off) or its voice has no sound for it.
        bool Say(uint32_t screamId) {
            if (_screamDisabled || _screamSound == nullptr) {
                return false;
            }
            // NewScream refuses any line while one of equal or higher
            // priority plays (an unsigned compare) and every line once the
            // priority is above 500. Stopping the playing line (SetOn(false,
            // true) sets I3D_sound's state to stopped, so IsPlaying(false)
            // reads false) and dropping the priority to 0 lets this one in.
            _screamSound->Stop();
            _screamPriority = 0;
            reinterpret_cast<void(__thiscall *)(NativeHuman *, uint32_t)>(kHumanNewScream)(this, screamId);
            // NewScream ends with SetOn(true, true) only when the wave opened.
            return _screamSound->IsPlaying();
        }
        [[nodiscard]] float ShotCount() const {
            return _shotCount;
        }
        [[nodiscard]] bool IsAiming() const {
            return _isAiming;
        }
        bool DoClimb() {
            return reinterpret_cast<bool(__thiscall *)(NativeHuman *)>(kHumanDoClimb)(this);
        }
        [[nodiscard]] int32_t AnimationState() const {
            return _animationState;
        }
        [[nodiscard]] bool IsPullingFromCar() const {
            return _animationState == kHumanCarThrowAttackerFrontAnimation || _animationState == kHumanCarThrowAttackerBackAnimation;
        }
        [[nodiscard]] bool IsClimbing() const {
            return _animationId == kClimbLowAnimation || _animationId == kClimbHighAnimation || _animationState == kClimbLowAnimation || _animationState == kClimbHighAnimation;
        }
        [[nodiscard]] bool FatalCollisionProcessed() const {
            return _fatalCollisionProcessed;
        }
        // Movement kills a human whose fall speed passes 50 (0x6234a0).
        [[nodiscard]] float FallSpeed() const {
            return _fallSpeed;
        }
        void SetFallSpeed(float speed) {
            _fallSpeed = speed;
        }
        [[nodiscard]] bool IsCrouching() const {
            return _isCrouching;
        }
        void SetAiming(bool aiming) {
            if (_isAiming != aiming) {
                reinterpret_cast<void(__thiscall *)(NativeHuman *, bool)>(kDoAimed)(this, aiming);
            }
        }
        void SetCrouching(bool crouching) {
            if (_isCrouching != crouching) {
                reinterpret_cast<void(__thiscall *)(NativeHuman *, bool)>(kDoCrouched)(this, crouching);
            }
        }
        bool AimPoseAt(Vector3 target) {
            return reinterpret_cast<bool(__thiscall *)(NativeHuman *, Vector3)>(kPoseSetPoseAimed)(this, target);
        }
        [[nodiscard]] const Vector3 &ShootTarget() const {
            return _shootTarget;
        }
        void SetShootTarget(const Vector3 &target) {
            _shootTarget = target;
        }
        [[nodiscard]] uint16_t SelectedWeapon() const {
            return _inventory.Selected().itemId;
        }
        [[nodiscard]] const NativeGameItem &SelectedItem() const {
            return _inventory.Selected();
        }
        [[nodiscard]] const NativeInventory &Inventory() const {
            return _inventory;
        }
        [[nodiscard]] uint32_t InventoryMask() const {
            return _inventory.ItemMask();
        }
        void SetSelectedAmmo(int32_t loaded, int32_t reserve) {
            auto &selected       = _inventory.Selected();
            selected.ammoLoaded  = loaded;
            selected.ammoReserve = reserve;
        }
        bool ApplyInventory(std::span<const NativeStockItem> stock, uint16_t selectedId) {
            const bool selectedChanged = _inventory.Selected().itemId != selectedId;
            if (!_inventory.Reconcile(stock, selectedId)) {
                return false;
            }
            if (selectedChanged) {
                reinterpret_cast<void(__thiscall *)(NativeHuman *)>(kChangeWeaponModel)(this);
            }
            return true;
        }
        void ApplySelectedWeapon(uint16_t itemId, int32_t loaded, int32_t reserve) {
            auto &selected             = _inventory.Selected();
            const bool changed         = selected.itemId != itemId;
            selected.itemId            = itemId;
            selected.ammoLoaded        = loaded;
            selected.ammoReserve       = reserve;
            if (changed) {
                selected.weaponEffectLatch = 0;
                selected.usingObject       = nullptr;
                reinterpret_cast<void(__thiscall *)(NativeHuman *)>(kChangeWeaponModel)(this);
            }
        }
        void ChangeModel(char *modelName) {
            using Call = void(__thiscall *)(NativeHuman *, char *, bool);
            reinterpret_cast<Call>(kHumanChangeModel)(this, modelName, true);
            reinterpret_cast<void(__thiscall *)(NativeHuman *)>(kChangeWeaponModel)(this);
        }
        struct ShotEventCursor {
            NativeItemProperty *events;
            int32_t index;
            int32_t elapsed;
        };
        [[nodiscard]] ShotEventCursor PrepareNetworkShot() {
            const ShotEventCursor previous {_animationEvents, _animationEventIndex, _animationEventElapsed};
            _animationEvents       = reinterpret_cast<NativeItemProperty *>(kDefaultAnimEvents);
            _animationEventIndex   = 0;
            _animationEventElapsed = 0;
            return previous;
        }
        void RestoreShotEventCursor(const ShotEventCursor &cursor) {
            _animationEvents       = cursor.events;
            _animationEventIndex   = cursor.index;
            _animationEventElapsed = cursor.elapsed;
        }
        void Reload() {
            reinterpret_cast<void(__thiscall *)(NativeHuman *)>(kHumanDoReload)(this);
        }
        bool ReplayReload() {
            const auto previous = PrepareNetworkShot();
            Reload();
            if (_animationEvents == reinterpret_cast<NativeItemProperty *>(kDefaultAnimEvents) && _animationEventIndex == 0) {
                RestoreShotEventCursor(previous);
                return false;
            }
            return true;
        }
        void Shoot() {
            reinterpret_cast<void(__thiscall *)(NativeHuman *)>(kHumanShooting)(this);
        }
        void SetLocomotionAnimation(LocomotionAnimation animation) {
            const auto state = static_cast<int32_t>(animation);
            if (_animationState == state) {
                return;
            }
            _animationState = state;
            using Call      = void(__thiscall *)(NativeHuman *, float, bool, bool);
            reinterpret_cast<Call>(kPersonAnim)(this, 0.0f, true, false);
        }
        void RefreshAnimation() {
            using Call = void(__thiscall *)(NativeHuman *, float, bool, bool);
            reinterpret_cast<Call>(kPersonAnim)(this, 0.0f, true, false);
        }
        [[nodiscard]] bool PlayAnimation(const char *filename, bool loop) {
            using Call = uint32_t(__thiscall *)(NativeHuman *, const char *, bool, bool);
            // The second bool is retail's force-load flag, not a loop flag.
            // The first bool selects finite playback; false loops the clip.
            return reinterpret_cast<Call>(kHumanDoPlayAnimation)(this, filename, !loop, false) != 0;
        }
        // As the police arrest plays its two halves (0x5171e4-0x51721c):
        // force-loaded, once. Returns the track's length in milliseconds.
        uint32_t PlayAnimationOnce(const char *filename) {
            using Call = uint32_t(__thiscall *)(NativeHuman *, const char *, bool, bool);
            return reinterpret_cast<Call>(kHumanDoPlayAnimation)(this, filename, true, true);
        }
        // As the retail PERSON_PLAYANIM script command (0x470393) plays a
        // clip: Do_PlayAnim(name, simple, 1), simple being finite playback,
        // with the displacement track force-loaded. Returns the clip's length
        // in milliseconds, 0 when it is missing.
        uint32_t PlayScriptAnimation(const char *filename, bool loop) {
            using Call = uint32_t(__thiscall *)(NativeHuman *, const char *, bool, bool);
            return reinterpret_cast<Call>(kHumanDoPlayAnimation)(this, filename, !loop, true);
        }
        void Breathe() {
            reinterpret_cast<void(__thiscall *)(NativeHuman *)>(kHumanDoBreath)(this);
        }
        // Retail's car contact on this human (kHumanCollision): the car's
        // velocity as the direction; null frame as C_Vehicle passes it for a
        // body contact. A player goes through C_player::Collision.
        bool CarCollision(NativeActor *car, const Vector3 &contact, const Vector3 &velocity) {
            using Call = bool(__thiscall *)(NativeHuman *, NativeActor *, const Vector3 &, const Vector3 &, const Vector3 &, float, void *);
            const Vector3 normal {-velocity.x, -velocity.y, -velocity.z};
            return reinterpret_cast<Call>(_actor.IsPlayer() ? kPlayerCollision : kHumanCollision)(this, car, contact, velocity, normal, 0.0f, nullptr);
        }
        void SideJump(const Vector3 &contact, const Vector3 &direction) {
            reinterpret_cast<void(__thiscall *)(NativeHuman *, const Vector3 &, const Vector3 &)>(kHumanDoSideJump)(this, contact, direction);
        }
        [[nodiscard]] bool IsSideJumping() const {
            return _animationState >= kHumanSideJumpFirstAnimation && _animationState <= kHumanSideJumpLastAnimation;
        }
        void Holster() {
            reinterpret_cast<void(__thiscall *)(NativeHuman *)>(kHumanDoHolster)(this);
        }
        // PERSON_STOPANIM.
        void StopAnimation() {
            reinterpret_cast<bool(__thiscall *)(NativeHuman *)>(kHumanDoStopAnimation)(this);
        }
        [[nodiscard]] bool IsPlayingAnimation() const {
            return _canWork == kHumanPlayAnimationWorkState;
        }
        void HoldEntityForCutscene() {
            using Call = void(__thiscall *)(NativeHuman *, bool);
            reinterpret_cast<Call>(kEntityHoldForCutscene)(this, false);
        }
        void DisableCollisions() {
            reinterpret_cast<void(__thiscall *)(NativeHuman *, bool)>(kHumanDeactivateCollisions)(this, false);
        }
        void ReturnToIdle() {
            reinterpret_cast<void(__thiscall *)(NativeHuman *)>(kHumanResetActions)(this);
            _canWork = kHumanIdleWorkState;
            SetLocomotionAnimation(LocomotionAnimation::Idle);
        }

        NativeActor _actor;
        int32_t _animationId;
        int32_t _animationState;
        std::byte _unused78[0x16c];
        bool _isCrouching;
        bool _isAiming;
        std::byte _unused1e6[0x1e8 - 0x1e6];
        int32_t _weaponItemType;
        bool _meleeActive;
        std::byte _unused1ed[0x1f0 - 0x1ed];
        NativeItemProperty *_animationEvents;
        int32_t _animationEventIndex;
        int32_t _animationEventElapsed;
        std::byte _unused1fc[0x4];
        Vector3 _shootTarget;
        std::byte _unused20c[0x218 - 0x20c];
        int32_t _grenadeChargeStartTime;
        bool _weaponActionActive;
        std::byte _unused21d[0x220 - 0x21d];
        int32_t _meleeAttackTime;
        std::byte _unused224[0x228 - 0x224];
        int32_t _meleeAttackIndex;
        std::byte _unused22c[0x22f - 0x22c];
        // Movement's one-shot water (material 31) latch; it zeroes health and
        // lets the human sink until the fall-speed death.
        bool _fatalCollisionProcessed;
        std::byte _unused230[0x234 - 0x230];
        int32_t _weaponChangeTime;
        std::byte _unused238[0x40c - 0x238];
        int32_t _canWork;
        std::byte _unused410[0x418 - 0x410];
        float _fallSpeed;
        std::byte _unused41c[0x44c - 0x41c];
        int32_t _actionTimeRemaining;
        std::byte _unused450[0x480 - 0x450];
        NativeInventory _inventory;
        std::byte _unused564[0x584 - 0x564];
        uint32_t _railwayFrameIndex;
        std::byte _unused588[0x640 - 0x588];
        float _strength;
        float _health;
        std::byte _unused648[0x680 - 0x648];
        uint32_t _voice;
        bool _screamDisabled;
        std::byte _unused685[0x6cc - 0x685];
        Sound::NativeSound *_screamSound;
        int32_t _screamPriority;
        std::byte _unused6d4[0x9a0 - 0x6d4];
        uint16_t _screamMask;
        std::byte _unused9a2[0x9ac - 0x9a2];
        float _shotCount;
    };
    static_assert(offsetof(NativeHuman, _animationId) == 0x70);
    static_assert(offsetof(NativeHuman, _animationState) == 0x74);
    static_assert(offsetof(NativeHuman, _shootTarget) == 0x200);
    static_assert(offsetof(NativeHuman, _isCrouching) == 0x1e4);
    static_assert(offsetof(NativeHuman, _isAiming) == 0x1e5);
    static_assert(offsetof(NativeHuman, _animationEvents) == 0x1f0);
    static_assert(offsetof(NativeHuman, _animationEventIndex) == 0x1f4);
    static_assert(offsetof(NativeHuman, _animationEventElapsed) == 0x1f8);
    static_assert(offsetof(NativeHuman, _weaponItemType) == 0x1e8);
    static_assert(offsetof(NativeHuman, _meleeActive) == 0x1ec);
    static_assert(offsetof(NativeHuman, _meleeAttackTime) == 0x220);
    static_assert(offsetof(NativeHuman, _grenadeChargeStartTime) == 0x218);
    static_assert(offsetof(NativeHuman, _weaponActionActive) == 0x21c);
    static_assert(offsetof(NativeHuman, _meleeAttackIndex) == 0x228);
    static_assert(offsetof(NativeHuman, _weaponChangeTime) == 0x234);
    static_assert(offsetof(NativeHuman, _canWork) == 0x40c);
    static_assert(offsetof(NativeHuman, _actionTimeRemaining) == 0x44c);
    static_assert(offsetof(NativeHuman, _fatalCollisionProcessed) == 0x22f);
    static_assert(offsetof(NativeHuman, _fallSpeed) == 0x418);
    static_assert(offsetof(NativeHuman, _inventory) == 0x480);
    static_assert(offsetof(NativeHuman, _health) == 0x644);
    static_assert(offsetof(NativeHuman, _railwayFrameIndex) == 0x584);
    static_assert(offsetof(NativeHuman, _voice) == 0x680);
    static_assert(offsetof(NativeHuman, _screamDisabled) == 0x684);
    static_assert(offsetof(NativeHuman, _screamSound) == 0x6cc);
    static_assert(offsetof(NativeHuman, _screamPriority) == 0x6d0);
    static_assert(offsetof(NativeHuman, _screamMask) == 0x9a0);
    static_assert(offsetof(NativeHuman, _shotCount) == 0x9ac);

    inline NativeActor *CurrentPlayer() {
        auto *mission = Core::Mission::Get();
        if (!mission) {
            return nullptr;
        }
        auto *game = mission->Game();
        return game ? game->Player() : nullptr;
    }

    inline bool IsPlayerActor(void *actor) {
        return actor && static_cast<NativeActor *>(actor)->IsPlayer();
    }

    inline Vector3 WorldPosition(void *actor) {
        return reinterpret_cast<Vector3(__thiscall *)(void *)>(kActorWorldPosition)(actor);
    }

    inline Vector3 WorldDirection(void *actor) {
        return reinterpret_cast<Vector3(__thiscall *)(void *)>(kActorWorldDirection)(actor);
    }
} // namespace Mafia1Online::SDK::Player
