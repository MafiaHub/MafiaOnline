#pragma once

#include <mafia1/sdk/player/native_actor.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}

namespace Mafia1Online::SDK::Rail {
    // reM S_rail_human (0x94): a tram passenger or driver. +0 state, +4 its
    // I3D_model, +0x14 the animation (an index into the ranims table).
    struct NativeRailHuman {
        // E_rail_human_state values the host's driver reset uses.
        static constexpr uint32_t kDriverState = 19;
        static constexpr uint32_t kDeadDriverState = 20;
        // ranims indices: the standing (7-11) and seated (12-13) deaths and
        // the driver's (23-24) are the ones that end on a pose to hold.
        static constexpr uint32_t kDriverAnimation = 14;
        [[nodiscard]] static constexpr bool IsDeathAnimation(uint32_t animation) {
            return (animation >= 7 && animation <= 13) || animation == 23 || animation == 24;
        }

        uint32_t state;
        Scene::NativeFrame *model;
        std::byte _unused08[0x0c];
        uint32_t animation;
    };
    static_assert(offsetof(NativeRailHuman, model) == 0x04);
    static_assert(offsetof(NativeRailHuman, animation) == 0x14);

    // reM ranims (0x6345c4): {name, uses its track, finite, id}, 27 entries.
    struct NativeRailHumanAnimation {
        const char *name;
        bool useTrack;
        bool finite;
        uint16_t padding;
        uint32_t id;
    };
    static_assert(sizeof(NativeRailHumanAnimation) == 0x0c);
    inline constexpr uintptr_t kRailHumanAnimations = 0x6345c4;
    inline constexpr uint32_t kRailHumanAnimationCount = 27;
    inline const NativeRailHumanAnimation *RailHumanAnimation(uint32_t index) {
        return index < kRailHumanAnimationCount ? reinterpret_cast<const NativeRailHumanAnimation *>(kRailHumanAnimations) + index : nullptr;
    }

    // A tram or metro train (scene2.bin type 8, reM C_railway, vtable
    // 0x624a70). C_actor::Tick (0x4064f0) runs AI (vtable +0x34, 0x488140)
    // or, for a network-controlled actor (+0x20), NetDirect (+0x3c, the
    // empty 0x4ab130), then Update (+0x38, 0x4894d0), which places the body
    // frame from C_actor +0x24/+0x30 and each wagon from the arrays at
    // +0xbc/+0xc0, and moves the dynamic collision with them.
    struct NativeRailway {
        enum class PassengerTrafficMode : int32_t {
            Active = 0,
            Deactivating = 1,
            Activating = 2,
            Inactive = 3,
        };
        // E_railway_state: a bullet on the driver (C_railway Hit, 0x490b40)
        // saves the state in +0xe0 and stops the railway in this one;
        // Update's deactivating branch (0x4899de) restores it.
        static constexpr uint32_t kDriverHitState = 11;

        static constexpr uint32_t kActorType = 8;
        static constexpr uintptr_t kAI = 0x488140;
        static constexpr uintptr_t kUpdate = 0x4894d0;
        // void __thiscall(C_railway*), plain ret: vtable +0x64 and +0x68.
        static constexpr uintptr_t kGameInit = 0x48a390;
        static constexpr uintptr_t kGameDone = 0x48cfa0;
        // void __thiscall(C_railway*), plain ret. Each walks the doorway
        // S_using_object entries (+0x274 outer, +0x270 inner) of the body and
        // every wagon through C_using_object Add/DelObject, which skip a
        // duplicate or a missing entry; their matrices follow the frames.
        static constexpr uintptr_t kAddOuterEntries = 0x495100;
        static constexpr uintptr_t kAddInnerEntries = 0x495160;
        static constexpr uintptr_t kRemoveInnerEntries = 0x495220;
        static constexpr uintptr_t kRemoveOuterEntries = 0x4951c0;
        // ++/-- (if above 0) the lock count +0x288 that holds a departure.
        // I3D_frame* __thiscall GetVagon(unsigned index), retn 4 (reM
        // C_railway.cpp:5654): 0 the body frame, n wagon n - 1's model, null
        // past the wagon count.
        static constexpr uintptr_t kGetVagon = 0x496e10;
        static constexpr uintptr_t kLock = 0x497980;
        static constexpr uintptr_t kUnlock = 0x497990;
        // AI returns at once while this C_game police freeze is set
        // (0x488140); SetTrafficVisible(false) sets it.
        static constexpr uintptr_t kPoliceFreezeFlag = 0x63d7ac;
        // AI's `jnz` (75 1E) to its near-camera branch after comparing the
        // camera's squared distance to the front point with 250000 (500 m,
        // 0x624050). Far from the camera there are no station stops, braking
        // or collisions, and Update hides the model.
        static constexpr uintptr_t kNearCameraBranch = 0x4881ee;

        // void __thiscall Place(int frameTime), retn 4 (reM C_railway.cpp:
        // 3481): moves the front (+0x90) frameTime * speed toward the node
        // m_BackPathNodes[0], taking the node's last forward link once it is
        // reached (within 0.1 m, or the angle to the rail direction +0x70 at
        // or past 90 degrees; m_ForwardPathNodes first), rebuilds the paths,
        // and sets the body (+0x24/+0x30) and each wagon midway between a
        // front and a back point FindBackPos finds a body length behind on
        // the back path, with SpringProcess's sway, which leaves them as
        // they were below the speed 1e-8 (MRG_ZERO, 0x62323c).
        static constexpr uintptr_t kPlace = 0x48f860;
        static constexpr size_t kRailDirectionOffset = 0x70;
        // std::vector<unsigned> m_BackPathNodes at +0x7c and m_ForwardPathNodes
        // at +0xa8 (VC6: allocator, first, last, end).
        static constexpr size_t kBackPathFirstOffset = 0x80;
        static constexpr size_t kBackPathLastOffset = 0x84;
        static constexpr size_t kForwardPathFirstOffset = 0xac;
        static constexpr size_t kForwardPathLastOffset = 0xb0;
        static constexpr size_t kFrontPositionOffset = 0x90;
        // SpringProcess's sway phase per part, the body first.
        static constexpr size_t kWagonSwayPhasesOffset = 0xb8;
        static constexpr size_t kMaxSpeedOffset = 0x1b4;
        static constexpr float kMinimumPlacedSpeed = 0.0001f;
        static constexpr size_t kWagonPositionsOffset = 0xbc;
        static constexpr size_t kWagonDirectionsOffset = 0xc0;
        static constexpr size_t kSpeedOffset = 0xd4;
        static constexpr size_t kStateOffset = 0xdc;
        static constexpr size_t kPreviousStateOffset = 0xe0;
        static constexpr size_t kWagonCountOffset = 0x1a8;
        // bool __thiscall C_railway::Hit(E_hit_type, const S_vector& a,
        // const S_vector& b, const S_vector& normal, float damage, C_actor*
        // attacker, unsigned flags, I3D_frame* frame), vtable Hit, eight
        // stack arguments (IDA 0x490b40, named Collide there). C_game::
        // TickShoot passes a bullet (HIT_TYPE_GENERIC 0) as a = its unit
        // direction and b = the hit point (reM C_game.cpp:2297-2305); Hit
        // casts b + a * 100 through `frame`'s hierarchy, kills the rail human
        // or driver it meets and breaks the windows on the way. `frame` is
        // the struck part's collision owner, which it looks up among the
        // parts' dynamic collisions (0x490e7f-0x490eb5).
        static constexpr uintptr_t kHit = 0x490b40;
        static constexpr uint32_t kGenericHit = 0;
        // m_pDynamicCollisions: one tDynamicCollObject (0x48 bytes) per
        // part, the body first and each wagon after it; the owner frame Hit
        // matches sits at +4 of each (0x490e9e-0x490eac).
        static constexpr size_t kDynamicCollisionsOffset = 0xe8;
        static constexpr size_t kDynamicCollisionStride = 0x48;
        static constexpr size_t kDynamicCollisionOwnerFrame = 0x04;
        // m_bMetro: isInStop (0x496db0) offers no doorways off a station.
        static constexpr size_t kMetroOffset = 0x1d8;
        // rail_doors at +0x1e8: TickSwingState (0x496900) moves the angle at
        // +0x40 toward 0 or pi/2; Update applies it (ApplyVagonSwingQuats).
        static constexpr size_t kDoorAngleOffset = 0x228;
        // m_uLockCount: Lock (0x497980) increments it; the schedule wait,
        // semaphore and following departures wait for 0.
        static constexpr size_t kLockCountOffset = 0x288;
        static constexpr size_t kPassengerTrafficModeOffset = 0x2a8;
        // m_RailHumans: a vector of S_rail_human* (first +0x254, last
        // +0x258) that AddPerson (0x4928e0) appends to; the driver, embedded
        // at +0x2b0, is in it too.
        static constexpr size_t kRailHumansFirstOffset = 0x254;
        static constexpr size_t kRailHumansLastOffset = 0x258;
        static constexpr size_t kDriverOffset = 0x2b0;

        [[nodiscard]] Player::NativeActor &Actor() {
            return *reinterpret_cast<Player::NativeActor *>(this);
        }
        [[nodiscard]] int32_t WagonCount() const {
            return Field<int32_t>(kWagonCountOffset);
        }
        // The frame a rider of `index` is linked to (C_human +0x584, as
        // Intern_UseRailway links it).
        [[nodiscard]] Scene::NativeFrame *Vagon(uint32_t index) {
            return reinterpret_cast<Scene::NativeFrame *(__thiscall *)(NativeRailway *, uint32_t)>(kGetVagon)(this, index);
        }
        // The collision owner frame of part 0..WagonCount, the `frame` a
        // bullet's Hit on that part carries; null past the parts.
        [[nodiscard]] Scene::NativeFrame *PartCollisionFrame(int32_t part) const {
            auto *collisions = Field<std::byte *>(kDynamicCollisionsOffset);
            if (!collisions || part < 0 || part > WagonCount()) {
                return nullptr;
            }
            return *reinterpret_cast<Scene::NativeFrame **>(collisions + part * kDynamicCollisionStride + kDynamicCollisionOwnerFrame);
        }
        // The part a Hit's frame belongs to, as Hit finds it; -1 for none
        // (a boarding passenger's own collision).
        [[nodiscard]] int32_t PartOfCollisionFrame(const Scene::NativeFrame *frame) const {
            for (int32_t part = 0; frame && part <= WagonCount(); ++part) {
                if (PartCollisionFrame(part) == frame) {
                    return part;
                }
            }
            return -1;
        }
        // Owned by the railway; WagonCount entries each.
        [[nodiscard]] Player::Vector3 *WagonPositions() const {
            return Field<Player::Vector3 *>(kWagonPositionsOffset);
        }
        [[nodiscard]] Player::Vector3 *WagonDirections() const {
            return Field<Player::Vector3 *>(kWagonDirectionsOffset);
        }
        [[nodiscard]] Player::Vector3 FrontPosition() const {
            return Field<Player::Vector3>(kFrontPositionOffset);
        }
        // The web path node the front moves toward; none before GameInit
        // built the back path.
        [[nodiscard]] bool HeadNode(uint32_t &node) const {
            const auto *first = Field<uint32_t *>(kBackPathFirstOffset);
            if (!first || first == Field<uint32_t *>(kBackPathLastOffset)) {
                return false;
            }
            node = *first;
            return true;
        }
        [[nodiscard]] float MaxSpeed() const {
            return Field<float>(kMaxSpeedOffset);
        }
        // SpringProcess's sway phase of each part (radians, the body first;
        // WagonCount() + 1 of them), which Place(0) leaves as they are.
        [[nodiscard]] float *SwayPhases() const {
            return Field<float *>(kWagonSwayPhasesOffset);
        }
        // Places the front at `front`, heading for `node`, and the body and
        // wagons behind it as Place does, without moving it on: the front
        // path's head and rail direction are this one, the forward path is
        // dropped (Place builds it again from the node's links). The parts
        // sway (SpringProcess) as at `swaySpeed`, which places a stopped
        // railway too. False without a back path to place on.
        bool PlaceFront(const Player::Vector3 &front, uint32_t node, const Player::Vector3 &nodePosition, float swaySpeed) {
            auto *first = Field<uint32_t *>(kBackPathFirstOffset);
            if (!first || first == Field<uint32_t *>(kBackPathLastOffset)) {
                return false;
            }
            *first = node;
            Field<uint32_t *>(kForwardPathLastOffset) = Field<uint32_t *>(kForwardPathFirstOffset);
            Field<Player::Vector3>(kFrontPositionOffset) = front;
            Field<Player::Vector3>(kRailDirectionOffset) = {nodePosition.x - front.x, nodePosition.y - front.y, nodePosition.z - front.z};
            const float speed = Speed();
            SetSpeed(std::max(swaySpeed, kMinimumPlacedSpeed));
            reinterpret_cast<void(__thiscall *)(NativeRailway *, int)>(kPlace)(this, 0);
            SetSpeed(speed);
            return true;
        }
        [[nodiscard]] bool IsMetro() const {
            return Field<uint8_t>(kMetroOffset) != 0;
        }
        [[nodiscard]] uint32_t LockCount() const {
            return Field<uint32_t>(kLockCountOffset);
        }
        [[nodiscard]] float Speed() const {
            return Field<float>(kSpeedOffset);
        }
        void SetSpeed(float speed) {
            Field<float>(kSpeedOffset) = speed;
        }
        [[nodiscard]] uint32_t State() const {
            return Field<uint32_t>(kStateOffset);
        }
        void SetState(uint32_t state) {
            Field<uint32_t>(kStateOffset) = state;
        }
        [[nodiscard]] float DoorAngle() const {
            return Field<float>(kDoorAngleOffset);
        }
        void SetDoorAngle(float angle) {
            Field<float>(kDoorAngleOffset) = angle;
        }
        void Lock() {
            reinterpret_cast<void(__thiscall *)(NativeRailway *)>(kLock)(this);
        }
        void Unlock() {
            reinterpret_cast<void(__thiscall *)(NativeRailway *)>(kUnlock)(this);
        }
        void AddOuterEntries() {
            reinterpret_cast<void(__thiscall *)(NativeRailway *)>(kAddOuterEntries)(this);
        }
        void AddInnerEntries() {
            reinterpret_cast<void(__thiscall *)(NativeRailway *)>(kAddInnerEntries)(this);
        }
        void RemoveEntries() {
            reinterpret_cast<void(__thiscall *)(NativeRailway *)>(kRemoveInnerEntries)(this);
            reinterpret_cast<void(__thiscall *)(NativeRailway *)>(kRemoveOuterEntries)(this);
        }
        [[nodiscard]] PassengerTrafficMode Mode() const {
            return Field<PassengerTrafficMode>(kPassengerTrafficModeOffset);
        }
        // The C_railway constructor (0x4865b0) and GameInit never write it.
        void SetMode(PassengerTrafficMode mode) {
            Field<PassengerTrafficMode>(kPassengerTrafficModeOffset) = mode;
        }
        // Update's out-of-range reset of a stopped driver (reM
        // C_railway.cpp:3673-3700): a dead driver back in the cab ("ridic"
        // dummy) with its driving animation, the saved state, and the
        // doorway use objects go.
        void ResumeFromDriverHit() {
            auto &driver = Driver();
            if (driver.model && driver.state == NativeRailHuman::kDeadDriverState) {
                if (auto *cab = Actor().Frame()->FindChildFrame("ridic")) {
                    const auto rotation = cab->LocalRotation();
                    driver.model->SetLocalPosition(cab->LocalPosition());
                    driver.model->SetLocalRotation(rotation[0], rotation[1], rotation[2], rotation[3]);
                    driver.model->Update();
                }
                driver.state = NativeRailHuman::kDriverState;
                driver.animation = NativeRailHuman::kDriverAnimation;
            }
            Field<uint32_t>(kStateOffset) = Field<uint32_t>(kPreviousStateOffset);
            RemoveEntries();
        }
        // The driver, embedded at +0x2b0 (and in the rail human list).
        [[nodiscard]] NativeRailHuman &Driver() const {
            return Field<NativeRailHuman>(kDriverOffset);
        }

        // The passengers aboard, the driver left out.
        template <typename F>
        void ForEachPassenger(F &&visit) const {
            auto *const *first = Field<NativeRailHuman **>(kRailHumansFirstOffset);
            auto *const *last = Field<NativeRailHuman **>(kRailHumansLastOffset);
            const auto *driver = reinterpret_cast<const NativeRailHuman *>(reinterpret_cast<const std::byte *>(this) + kDriverOffset);
            for (auto *const *human = first; first && human != last; ++human) {
                if (*human && *human != driver && (*human)->model) {
                    visit(**human);
                }
            }
        }

        void Deactivate() {
            SetMode(PassengerTrafficMode::Deactivating);
            reinterpret_cast<void(__thiscall *)(NativeRailway *, unsigned int)>(kUpdate)(this, 0);
            SetMode(PassengerTrafficMode::Inactive);
        }

      private:
        template <typename T>
        T &Field(size_t offset) const {
            return *reinterpret_cast<T *>(reinterpret_cast<std::byte *>(const_cast<NativeRailway *>(this)) + offset);
        }
    };
} // namespace Mafia1Online::SDK::Rail
