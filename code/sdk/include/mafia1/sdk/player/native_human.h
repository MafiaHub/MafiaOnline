#pragma once

#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/player/native_inventory.h>

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
    inline constexpr uintptr_t kHumanResetActions    = 0x58B870;
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
    // reM C_human::Movement dispatches on m_iCanWork. Ordinary movement
    // changes idle (1) to locomotion (2); timed actions (5) and animation
    // sequences (6) own their tracks until the native state machine ends them.
    inline constexpr int32_t kHumanIdleWorkState = 1;
    inline constexpr int32_t kHumanLocomotionWorkState = 2;
    inline constexpr int32_t kHumanTimedActionWorkState = 5;
    inline constexpr int32_t kHumanAnimationSequenceWorkState = 6;
    // bool __thiscall(C_human*), plain ret: probes the wall and ledge ahead of
    // m_vPosition/m_vDirection and starts climb animation 104 or 105.
    inline constexpr uintptr_t kHumanDoClimb        = 0x586400;
    inline constexpr int32_t kClimbLowAnimation     = 104;
    inline constexpr int32_t kClimbHighAnimation    = 105;
    // C_human::Can_SpecialStroke: C_human* __fastcall(C_human*, bool), plain
    // ret. A found target receives Do_HardHead/Do_Castrate, which subtract
    // health directly and bypass C_human::Hit.
    inline constexpr uintptr_t kHumanCanSpecialStroke = 0x584010;
    inline constexpr uintptr_t kDefaultAnimEvents   = 0x6368D0;

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
        [[nodiscard]] float Health() const {
            return _health;
        }
        void SetHealth(float health) {
            _health = health;
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
        std::byte _unused564[0xdc];
        float _strength;
        float _health;
        std::byte _unused648[0x364];
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
