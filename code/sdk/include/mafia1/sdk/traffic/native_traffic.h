#pragma once

#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <cmath>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Traffic {
    // Retail pedestrian traffic: C_traffic_generator actors (mission actor
    // type 18, 0xb8 bytes) each own a pool of 0x200-byte C_traffic_element
    // walkers created in GameInit (0x44e500). Offsets below are the ones the
    // retail code itself uses; see docs/npc_multiplayer_audit.md.
    inline constexpr uint32_t kGeneratorActorType = 18;

    // void __thiscall C_traffic_generator::GameInit(): scales the configured
    // maximum by kPedestrianDensity (0x44e52e-0x44e554), then allocates one
    // walker per model instance count and creates every model.
    inline constexpr uintptr_t kGeneratorGameInit = 0x44E500;
    // float pedestrian density from the game setup, read by GameInit.
    inline constexpr uintptr_t kPedestrianDensity = 0x632638;
    // void __thiscall C_traffic_generator::AI(unsigned frameMs), ret 4.
    inline constexpr uintptr_t kGeneratorAI = 0x44EDB0;
    // static void __fastcall TickElements(unsigned frameMs); its only caller
    // is C_game::Tick at 0x5a5631, after the primary actors tick.
    inline constexpr uintptr_t kTickElements = 0x44EF70;
    // void __thiscall C_traffic_element::Tick(unsigned frameMs), ret 4.
    inline constexpr uintptr_t kElementTick = 0x452CD0;
    // bool __thiscall ActivateFree(bool pedestrianNodesOnly, bool lookAhead),
    // ret 8: activates at the model's own pose and registers the animation
    // machine, dynamic collision, use object, shadow and generator count.
    // It rerolls health, walk style and voice.
    inline constexpr uintptr_t kElementActivateFree = 0x456FA0;
    // void __thiscall SetAnim(): plays the target animation (+0x164) once it
    // differs from the current one (+0x160); id 6 is the walk of the walk
    // style (+0x1b4), any other id an anim_info entry (0x459450-0x45954d).
    inline constexpr uintptr_t kElementSetAnim = 0x459450;
    // void __thiscall UpdateModel(): model pose, sector, inverse matrix and
    // dynamic collision from the element's position and direction.
    inline constexpr uintptr_t kElementUpdateModel = 0x4564F0;
    // void __thiscall Deactivate(): unregisters everything ActivateFree
    // registered; a generator-owned element returns to its pool.
    inline constexpr uintptr_t kElementDeactivate = 0x4587C0;
    // void __thiscall Hit(E_hit_type, const S_vector &, unsigned, C_actor *,
    // float), ret 14h: damage, reaction and death.
    inline constexpr uintptr_t kElementHit = 0x4588D0;
    // bool __thiscall C_don_ocas::Collision(C_actor *, const S_vector &,
    // const S_vector &, const S_vector &, float, I3D_frame *), ret 18h: an
    // actor hit a walker's collision. The vtable entry (0x623f70) is
    // 0x44ca50; IDA's function start at 0x44ca4a is a mis-decode.
    inline constexpr uintptr_t kWalkerCollision = 0x44CA50;
    // Hit's bullet blood (0x458dba-0x458e30): C_blood_manager::AddBlood
    // (0x412140, thiscall, ret 14h) on C_game +0x2d90 with the walker's
    // position 1.0 up and the shot direction scaled by 3.0, while the
    // mission's actor detail (+0x9c) is at least 1.
    inline constexpr uintptr_t kBloodManagerAddBlood = 0x412140;
    inline constexpr uintptr_t kGameBloodManager = 0x2D90;
    inline constexpr uintptr_t kMissionActorDetail = 0x9C;
    inline constexpr float kBloodHeight = 1.0f;
    inline constexpr float kBloodDirectionScale = 3.0f;
    // static void __fastcall ElementNewScream(int voice, int screamId,
    // S_vector &position, float minRange, float maxRange, float curve,
    // float minVolume), ret 14h. Returns at once while screams are off
    // (0x63d728), without a sound driver, past the global cooldown or out
    // of the camera's range.
    inline constexpr uintptr_t kElementNewScream = 0x4510B0;
    // Walker types at +0x15a. Hit (0x4588f0) hands a gangster (4) to
    // CreateMafianos (0x459b90), which replaces it with an armed C_entity.
    inline constexpr uint8_t kStandardPedestrian = 0;
    inline constexpr uint8_t kGangsterPedestrian = 4;
    // static void __fastcall FizlosNamakatDoAkce(C_human *offender,
    // unsigned mode, std::vector<C_actor *> &police, bool announce,
    // float radius, bool directional), ret 10h: turns walkers into police.
    // It returns at once while traffic is hidden, takes only active walkers
    // of type 1 or 3 (0x44fe5e-0x44fe70) in range and sight of the offender,
    // and makes each a C_entity at the walker's position from a duplicate of
    // its model, which it adds to the retirement list (AddDoRiciActor).
    inline constexpr uintptr_t kRecruitPolice = 0x44FDE0;
    inline constexpr uint8_t kPolicePedestrian = 1;
    inline constexpr uint8_t kPoliceSecondaryPedestrian = 3;
    // void __thiscall SetAlpha(float), ret 4: the walker's fade, applied to
    // its transparency object (+0xb8). Tick fades +0xbc in and out.
    inline constexpr uintptr_t kElementSetAlpha = 0x456EA0;
    // void __thiscall DropUmbrella(): hands the umbrella model to a physics
    // prop and clears +0x4c; nothing without an umbrella.
    inline constexpr uintptr_t kElementDropUmbrella = 0x459A50;
    // Activate's rain umbrella (0x458618-0x458779): "9dest.i3d" linked to
    // r_hand, rotated -pi/2 about X, at a local offset, holding
    // "destnik01.i3d" as the machine's extended animation.
    inline constexpr const char *kUmbrellaModel = "9dest.i3d";
    inline constexpr const char *kUmbrellaAnimation = "destnik01.i3d";
    inline constexpr Player::Vector3 kUmbrellaPosition {0.093f, -0.013f, 0.346f};
    inline constexpr float kUmbrellaAnimationWeight = 0.9f;
    // g_AnimationSetCache and its Open(name, I3D_animation_set **out, 0, 0),
    // thiscall, ret 10h.
    inline constexpr uintptr_t kAnimationSetCache = 0x647D80;
    inline constexpr uintptr_t kAnimationSetCacheOpen = 0x4104F0;
    // C_animations_machine_de_luxe::SetExtendedAnim(set, weight, fade, finite),
    // thiscall, ret 10h.
    inline constexpr uintptr_t kAnimationMachineSetExtendedAnim = 0x410060;
    // int __thiscall C_animations_machine_de_luxe::Tick(unsigned frameMs),
    // ret 4; returns early for a closed machine or one without a model.
    inline constexpr uintptr_t kAnimationMachineTick = 0x40F1B0;
    // static void __fastcall SetVisible(bool): stores the flag below and
    // switches every active element's model on or off.
    inline constexpr uintptr_t kGeneratorSetVisible = 0x44E960;
    // g_bTrafficVisible. GameInit sets it; AI (0x44edb0) and TickElements
    // (0x44ef70) do nothing while it is clear.
    inline constexpr uintptr_t kTrafficVisible = 0x63D730;
    // g_vTrafficElements (0x63d764): std::vector<C_traffic_element *> of
    // active walkers. VC6 keeps the allocator first, so TickElements loads
    // _First from 0x63d768 (0x44efd9) and _Last from 0x63d76c (0x44efe3).
    // AddElement/DelElement reorder it, so an index is not an identity.
    inline constexpr uintptr_t kTrafficElementsBegin = 0x63D768;
    inline constexpr uintptr_t kTrafficElementsEnd   = 0x63D76C;
    // Set by GameInit; the next AI spawns a whole batch around the camera,
    // starting 5 units out (Activate(true, ...)), then clears it (0x44ede1).
    inline constexpr uintptr_t kInitialBatch = 0x63D74C;
    // g_iTrafficScreamCooldownMs, counted down at the end of every AI call.
    inline constexpr uintptr_t kScreamCooldown = 0x63D73C;
    // g_iTrafficCollisionCooldown (reM), counted down by TickElements
    // (0x44ef83-0x44ef9e) before the walkers tick.
    inline constexpr uintptr_t kCollisionCooldown = 0x63D738;
    // C_actor::SetActState(E_act_state), thiscall, ret 4. State 0 is active;
    // only active (primary) actors run AI.
    inline constexpr uintptr_t kActorSetActState = 0x406DA0;
    inline constexpr int32_t kActStateActive = 0;

    struct NativeTrafficElement {
        [[nodiscard]] bool IsActive() const { return _active; }
        [[nodiscard]] const Player::Vector3 &Position() const { return _position; }
        [[nodiscard]] int32_t CurrentAnimation() const { return _currentAnimation; }
        [[nodiscard]] bool AnimationFlag() const { return _animationFlag; }
        [[nodiscard]] int32_t WalkStyle() const { return _walkStyle; }
        void SetPose(const Player::Vector3 &position, const Player::Vector3 &direction) {
            _position  = position;
            _direction = direction;
            using Call = void(__thiscall *)(NativeTrafficElement *);
            reinterpret_cast<Call>(kElementUpdateModel)(this);
        }
        void SetWalkStyle(int32_t style) { _walkStyle = style; }
        // Plays animation through retail SetAnim, which ignores the
        // animation already current.
        void PlayAnimation(int32_t animation, bool flag) {
            _targetAnimation = animation;
            _animationFlag   = flag;
            using Call = void(__thiscall *)(NativeTrafficElement *);
            reinterpret_cast<Call>(kElementSetAnim)(this);
        }
        // Starts animation again even when it is already current.
        void RestartAnimation(int32_t animation, bool flag) {
            _currentAnimation = -1;
            PlayAnimation(animation, flag);
        }
        // True when the clip ended this tick (bit 0 of the machine's result).
        // Element Tick then returns before ticking the model, so the clip's
        // last pose holds; a dead walker settles there as a corpse.
        bool TickAnimation(uint32_t frameMs) {
            if (!_animationMachine) {
                return false;
            }
            using Call = int(__thiscall *)(void *, uint32_t);
            return (reinterpret_cast<Call>(kAnimationMachineTick)(_animationMachine, frameMs) & 1) != 0;
        }
        // Pedestrian nodes only: an element whose allowed node types were
        // never set (Activate sets them) finds no node otherwise.
        bool ActivateFree() {
            using Call = bool(__thiscall *)(NativeTrafficElement *, bool, bool);
            return reinterpret_cast<Call>(kElementActivateFree)(this, true, false);
        }
        void Deactivate() {
            using Call = void(__thiscall *)(NativeTrafficElement *);
            reinterpret_cast<Call>(kElementDeactivate)(this);
        }
        [[nodiscard]] Scene::NativeFrame *Model() const { return _model; }
        // Hit (0x4588d0) subtracts damage here and clamps a dead walker to 0;
        // the walker stays active as a corpse until it retires.
        [[nodiscard]] bool IsDead() const { return !(_health > 0.0f); }
        [[nodiscard]] float Health() const { return _health; }
        [[nodiscard]] bool HasUmbrella() const { return _umbrellaModel != nullptr; }
        [[nodiscard]] float Alpha() const { return _alpha; }
        [[nodiscard]] uint8_t PedestrianType() const { return _pedestrianType; }
        void SetPedestrianType(uint8_t type) { _pedestrianType = type; }
        void SetAlpha(float alpha) {
            _alpha = alpha;
            using Call = void(__thiscall *)(NativeTrafficElement *, float);
            reinterpret_cast<Call>(kElementSetAlpha)(this, alpha);
        }
        void DropUmbrella() {
            using Call = void(__thiscall *)(NativeTrafficElement *);
            reinterpret_cast<Call>(kElementDropUmbrella)(this);
        }
        // Retail Activate's umbrella for an element ActivateFree activated;
        // Deactivate releases it from +0x4c like any other.
        bool AttachUmbrella() {
            auto *hand = _model->FindChildFrame("r_hand");
            if (_umbrellaModel || !hand || !_animationMachine) {
                return false;
            }
            auto *umbrella = Scene::GetDriver()->CreateModel();
            if (!umbrella) {
                return false;
            }
            if (!Scene::GetModelCache()->OpenModel(umbrella, kUmbrellaModel)) {
                umbrella->Release();
                return false;
            }
            umbrella->LinkTo(hand);
            const float half = -0.78539819f;
            umbrella->SetLocalRotation(std::cos(half), std::sin(half), 0.0f, 0.0f);
            umbrella->SetLocalPosition(kUmbrellaPosition);
            umbrella->Update();
            _umbrellaModel = umbrella;
            void *animation = nullptr;
            using Open = int(__thiscall *)(void *, const char *, void **, int, int);
            reinterpret_cast<Open>(kAnimationSetCacheOpen)(reinterpret_cast<void *>(kAnimationSetCache), kUmbrellaAnimation, &animation, 0, 0);
            if (animation) {
                using SetExtendedAnim = void(__thiscall *)(void *, void *, float, float, bool);
                reinterpret_cast<SetExtendedAnim>(kAnimationMachineSetExtendedAnim)(_animationMachine, animation, kUmbrellaAnimationWeight, 0.0f, false);
                using Release = int(__stdcall *)(void *);
                reinterpret_cast<Release>((*static_cast<void ***>(animation))[0])(animation);
            }
            return true;
        }
        // TickElements writes the squared XZ distance from the camera here;
        // Tick reads it for fading, retirement and corpse removal.
        void SetObserverDistanceSq(float distanceSq) { _observerDistanceSq = distanceSq; }
        // I3D_model::GetFileName (0x47adc0) reads the model's file name at
        // +0x154; CreateModel renamed the frame itself to "p_traff_elem".
        [[nodiscard]] const char *ModelFileName() const {
            const char *name = *reinterpret_cast<const char *const *>(reinterpret_cast<const std::byte *>(_model) + 0x154);
            return name ? name : "";
        }
        // Tick treats a model whose render stamp (+0x13c) equals the
        // mission's (0x45345f-0x453474) as drawn this pass: it keeps the
        // skeleton ticking and fades instead of popping out.
        void MarkDrawn(uint32_t missionRenderStamp) {
            *reinterpret_cast<uint32_t *>(reinterpret_cast<std::byte *>(_model) + 0x13c) = missionRenderStamp;
        }
        void Tick(uint32_t frameMs) {
            using Call = void(__thiscall *)(NativeTrafficElement *, uint32_t);
            reinterpret_cast<Call>(kElementTick)(this, frameMs);
        }

        std::byte _unused00[0x08];
        void *_animationMachine;
        bool _active;
        std::byte _unused0d[3];
        Player::Vector3 _position;
        Player::Vector3 _direction;
        std::byte _unused28[0x40 - 0x28];
        Scene::NativeFrame *_model;
        Scene::NativeFrame *_headModel;
        std::byte _unused48[0x4c - 0x48];
        Scene::NativeFrame *_umbrellaModel;
        std::byte _unused50[0x54 - 0x50];
        float _observerDistanceSq;
        std::byte _unused58[0xbc - 0x58];
        float _alpha;
        std::byte _unusedc0[0x15a - 0xc0];
        uint8_t _pedestrianType;
        std::byte _unused15b[0x15c - 0x15b];
        float _health;
        int32_t _currentAnimation;
        int32_t _targetAnimation;
        std::byte _unused168[0x16c - 0x168];
        bool _animationFlag;
        std::byte _unused16d[0x1b4 - 0x16d];
        int32_t _walkStyle;
        std::byte _unused1b8[0x200 - 0x1b8];
    };
    static_assert(sizeof(NativeTrafficElement) == 0x200);
    static_assert(offsetof(NativeTrafficElement, _animationMachine) == 0x08);
    static_assert(offsetof(NativeTrafficElement, _active) == 0x0c);
    static_assert(offsetof(NativeTrafficElement, _direction) == 0x1c);
    static_assert(offsetof(NativeTrafficElement, _currentAnimation) == 0x160);
    static_assert(offsetof(NativeTrafficElement, _targetAnimation) == 0x164);
    static_assert(offsetof(NativeTrafficElement, _animationFlag) == 0x16c);
    static_assert(offsetof(NativeTrafficElement, _walkStyle) == 0x1b4);
    static_assert(offsetof(NativeTrafficElement, _position) == 0x10);
    static_assert(offsetof(NativeTrafficElement, _model) == 0x40);
    static_assert(offsetof(NativeTrafficElement, _headModel) == 0x44);
    static_assert(offsetof(NativeTrafficElement, _umbrellaModel) == 0x4c);
    static_assert(offsetof(NativeTrafficElement, _observerDistanceSq) == 0x54);
    static_assert(offsetof(NativeTrafficElement, _alpha) == 0xbc);
    static_assert(offsetof(NativeTrafficElement, _pedestrianType) == 0x15a);
    static_assert(offsetof(NativeTrafficElement, _health) == 0x15c);

    struct NativeTrafficGenerator {
        [[nodiscard]] Player::NativeActor &Actor() { return _actor; }
        // m_bEnabled: true selects by nearest distance; false forces the
        // generator inside its spawn radius (C_game::Tick 0x5a59ea-0x5a5a1c).
        [[nodiscard]] bool SelectsByDistance() const { return _enabled; }
        [[nodiscard]] float SpawnRadius() const { return _spawnRadius; }
        [[nodiscard]] bool GenerationActive() const { return _generationActive; }
        void SetGenerationActive(bool active) { _generationActive = active; }
        [[nodiscard]] int32_t SpawnedCount() const { return _spawnedCount; }
        [[nodiscard]] int32_t MaxSpawned() const { return _maxSpawned; }
        [[nodiscard]] float DespawnRadius() const { return _despawnRadius; }
        [[nodiscard]] int32_t PoolSize() const { return _elementCount; }
        [[nodiscard]] bool OwnsElement(const NativeTrafficElement *element) const {
            return element >= _elements && element < _elements + _elementCount;
        }
        [[nodiscard]] NativeTrafficElement *Element(int32_t index) const {
            return _elements + index;
        }
        [[nodiscard]] Player::Vector3 WorldPosition() const { return _actor.Frame()->WorldPosition(); }
        void Activate() {
            using Call = void(__thiscall *)(Player::NativeActor *, int32_t);
            reinterpret_cast<Call>(kActorSetActState)(&_actor, kActStateActive);
        }

        Player::NativeActor _actor;
        bool _enabled;
        std::byte _unused71[3];
        float _spawnRadius;
        bool _generationActive;
        std::byte _unused79[3];
        NativeTrafficElement *_elements;
        int32_t _spawnedCount;
        int32_t _elementCount;
        float _despawnRadius;
        float _innerSpawnRadius;
        float _outerSpawnRadius;
        std::byte _unused94[0x9c - 0x94];
        int32_t _modelCount;
        // Walkers GameInit creates per model; their sum is the pool size.
        uint32_t *_modelInstanceCounts;
        // AI adds the frame time and tries one spawn at 113 ms (0x44ee7a).
        uint32_t _spawnTimerMs;
        int32_t _maxSpawned;
        std::byte _unusedac[0xb8 - 0xac];
    };
    static_assert(sizeof(NativeTrafficGenerator) == 0xb8);
    static_assert(offsetof(NativeTrafficGenerator, _enabled) == 0x70);
    static_assert(offsetof(NativeTrafficGenerator, _spawnRadius) == 0x74);
    static_assert(offsetof(NativeTrafficGenerator, _generationActive) == 0x78);
    static_assert(offsetof(NativeTrafficGenerator, _elements) == 0x7c);
    static_assert(offsetof(NativeTrafficGenerator, _spawnedCount) == 0x80);
    static_assert(offsetof(NativeTrafficGenerator, _elementCount) == 0x84);
    static_assert(offsetof(NativeTrafficGenerator, _despawnRadius) == 0x88);
    static_assert(offsetof(NativeTrafficGenerator, _innerSpawnRadius) == 0x8c);
    static_assert(offsetof(NativeTrafficGenerator, _outerSpawnRadius) == 0x90);
    static_assert(offsetof(NativeTrafficGenerator, _modelCount) == 0x9c);
    static_assert(offsetof(NativeTrafficGenerator, _modelInstanceCounts) == 0xa0);
    static_assert(offsetof(NativeTrafficGenerator, _spawnTimerMs) == 0xa4);
    static_assert(offsetof(NativeTrafficGenerator, _maxSpawned) == 0xa8);

    inline bool IsVisible() {
        return *reinterpret_cast<const bool *>(kTrafficVisible);
    }

    inline void SetVisible(bool visible) {
        using Call = void(__fastcall *)(bool);
        reinterpret_cast<Call>(kGeneratorSetVisible)(visible);
    }

    // What the end of every generator AI does (0x44ef5f); a player runs no
    // generator AI, so it counts the cooldown down itself.
    inline void CountDownScreamCooldown(uint32_t frameMs) {
        auto &cooldown = *reinterpret_cast<int32_t *>(kScreamCooldown);
        if (cooldown > 0) {
            cooldown -= static_cast<int32_t>(frameMs);
        }
    }

    inline void CountDownCollisionCooldown(uint32_t frameMs) {
        auto &cooldown = *reinterpret_cast<int32_t *>(kCollisionCooldown);
        if (cooldown > 0) {
            cooldown = frameMs > static_cast<uint32_t>(cooldown) ? 0 : cooldown - static_cast<int32_t>(frameMs);
        }
    }

    // A bullet's blood on a walker, as Hit draws it; cosmetic only.
    inline void AddWalkerBlood(void *mission, void *game, const NativeTrafficElement &element, const Player::Vector3 &shotDirection) {
        if (*reinterpret_cast<const int32_t *>(static_cast<const std::byte *>(mission) + kMissionActorDetail) < 1) {
            return;
        }
        const float length = std::sqrt(shotDirection.x * shotDirection.x + shotDirection.y * shotDirection.y + shotDirection.z * shotDirection.z);
        if (!(length > 0.0001f)) {
            return;
        }
        Player::Vector3 position = element.Position();
        position.y += kBloodHeight;
        Player::Vector3 direction {shotDirection.x / length * kBloodDirectionScale, shotDirection.y / length * kBloodDirectionScale, shotDirection.z / length * kBloodDirectionScale};
        using Call = void(__thiscall *)(void *, Player::Vector3 *, Player::Vector3 *, float, int, bool);
        reinterpret_cast<Call>(kBloodManagerAddBlood)(static_cast<std::byte *>(game) + kGameBloodManager, &position, &direction, 1.0f, 0, false);
    }

    inline void PlayScream(int32_t voice, int32_t screamId, Player::Vector3 position, float minimumRange, float maximumRange, float curve, float minimumVolume) {
        using Call = void(__fastcall *)(int32_t, int32_t, Player::Vector3 *, float, float, float, float);
        reinterpret_cast<Call>(kElementNewScream)(voice, screamId, &position, minimumRange, maximumRange, curve, minimumVolume);
    }

    inline NativeTrafficElement **ElementsBegin() {
        return *reinterpret_cast<NativeTrafficElement ***>(kTrafficElementsBegin);
    }

    inline NativeTrafficElement **ElementsEnd() {
        return *reinterpret_cast<NativeTrafficElement ***>(kTrafficElementsEnd);
    }
} // namespace Mafia1Online::SDK::Traffic
