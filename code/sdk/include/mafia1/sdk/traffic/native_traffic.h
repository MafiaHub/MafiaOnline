#pragma once

#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <cmath>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

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
    // bool __thiscall C_don_ocas::Hit(E_hit_type, const S_vector &direction,
    // const S_vector &position, const S_vector &normal, float damage,
    // C_actor *attacker, unsigned flags, I3D_frame *model), retn 20h: the
    // walker collision's vtable Hit (slot 0x7c, entry 0x623f74). It finds
    // the walker by model (GetElemFromFrame 0x450830) and ends its taxi
    // wave. For hit type 1 a held heavy blow (attacker +0x228 == -1) with a
    // knife or bat (+0x4a0 3 or 4), from within 1.2566 rad of the walker's
    // facing and behind it (0.9425 rad), becomes its health + 1; then
    // C_traffic_element::Hit(1, direction, torso 5, attacker, damage), the
    // hit particle and, for a beat cop (type 1 or 3), DalJsemPochuzkari
    // (0x44cd28-0x44d135). `this` is never read: its first instruction
    // loads the frame argument into ecx.
    inline constexpr uintptr_t kWalkerHit = 0x44CCF0;
    // C_game::m_pDonOcas, the one walker collision actor (C_game::Init
    // 0x5a1ac6).
    inline constexpr uintptr_t kGameDonOcas = 0x2A1C;
    // C_human::AI's melee sweep on a walker (0x571323-0x571377): hit type 1,
    // the swing segment as direction, the item damage times the attacker's
    // strength (+0x640), flags 0 and the collision's owner frame (the model).
    inline constexpr int32_t kHardKnockdownHit = 1;
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

    // Retail taxi hailing. TickElements' tail (0x44f052-0x44f07c) runs
    // GeneratePassengerForTaxi (0x44f7d0) on its own countdown (0x63d72c)
    // while the mission script enabled taxi passengers (0x63d72a,
    // EnableTaxiDriver 0x44fcf0) and C_game's player sits in a taxi
    // (0x63d729, PlayerInTaxi 0x44f740). It picks the last eligible walker in the active-element vector and
    // starts its wave; element Tick (0x45334c) then runs MavaNaTaxi
    // (0x451ff0) on a waving walker instead of walking it, while the police
    // manager is on and has no officer on C_game's player.
    inline constexpr uintptr_t kElementMavaNaTaxi      = 0x451FF0;
    // void __thiscall EndMavaniNaTaxi(): fades the wave out over 4 s when
    // the machine's last extended-animation note is the wave's, and clears
    // +0x1fe (0x452ca0-0x452cbb).
    inline constexpr uintptr_t kElementEndMavaniNaTaxi = 0x452CA0;
    // int __thiscall C_shvestky::PocetNasranych(C_human *), ret 4: officers
    // on the person. Hailing and waving stop while it is above 0.
    inline constexpr uintptr_t kPoliceOfficersOn = 0x49D460;
    // PlayerInTaxi's model names (0x632648-0x6326a8), compared without case
    // against C_car's name at +0x218c: the model file name without its
    // "NN.i3d" (C_car Init strips 6 characters).
    inline constexpr const char *kTaxiModelNames[] {"taxi", "hotrodp4", "hotrodp5", "tbirdold", "fordhot", "phantomtaxi", "hotrodp2", "fordapicktaxi", "blackdragon"};
    inline constexpr uintptr_t kCarName = 0x218C;
    inline bool IsTaxiCar(const void *car) {
        const char *name = static_cast<const char *>(car) + kCarName;
        for (const char *taxi : kTaxiModelNames) {
            if (_stricmp(name, taxi) == 0) {
                return true;
            }
        }
        return false;
    }
    // GeneratePassengerForTaxi: the countdown restarts at 500 + 524 * rnd(4)
    // ms (0x44f7f8); a hail or a second waving walker adds 4000 (0x44f921,
    // 0x44fc65). Retail's taxi_pas.dat passenger leaves a 20 s countdown.
    inline constexpr int32_t kTaxiTimerBaseMs = 500;
    inline constexpr int32_t kTaxiTimerStepMs = 524;
    inline constexpr int32_t kTaxiTimerVariants = 4;
    inline constexpr int32_t kTaxiRetryDelayMs = 4000;
    inline constexpr int32_t kTaxiPassengerCooldownMs = 20000;
    // The taxi's driver speed (C_Vehicle +0x59c) must not exceed 20; the
    // walker stands 20 to 90 away (squared 400 and 8100, exclusive), within
    // 0.6283 rad of the taxi's heading; at most one other walker may wave.
    inline constexpr float kTaxiHailMaxSpeed = 20.0f;
    inline constexpr float kTaxiHailMinDistanceSq = 400.0f;
    inline constexpr float kTaxiHailMaxDistanceSq = 8100.0f;
    inline constexpr float kTaxiHailAngle = 0.62831855f;
    inline constexpr int32_t kTaxiMaxOtherWavers = 1;
    // MavaNaTaxi boards the walker once the driver speed is below 0.5 within
    // 10 of it (squared 100, 0x4522ed-0x45232f).
    inline constexpr float kTaxiBoardMaxSpeed = 0.5f;
    inline constexpr float kTaxiBoardDistanceSq = 100.0f;
    // The walk animation id a hail requires (+0x160 == 6).
    inline constexpr int32_t kWalkAnimation = 6;
    // The wave: "Mavani0N HPT.i3d" at weight 0.9, no fade, looping, with
    // note 0xdca9 written to the machine's last extended-animation note
    // (+0x10e, a word) so EndMavaniNaTaxi recognises it (0x44fc73-0x44fcd5).
    inline constexpr const char *kTaxiWaveAnimations[] {"Mavani01 HPT.i3d", "Mavani02 HPT.i3d", "Mavani03 HPT.i3d"};
    inline constexpr float kTaxiWaveWeight = 0.9f;
    inline constexpr uint16_t kTaxiWaveNote = 0xDCA9;
    inline constexpr uintptr_t kAnimationMachineLastNote = 0x10E;

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

        // GeneratePassengerForTaxi's walker gates (0x44fae0-0x44fb71): panic
        // blocked +0x00, the owning generator +0x04, panicking +0x19e, a
        // followed car +0x1a8, waving +0x1fe, may hail +0x1ff.
        [[nodiscard]] bool PanicBlocked() const { return *reinterpret_cast<const bool *>(Raw() + 0x00); }
        [[nodiscard]] bool HasGenerator() const { return *reinterpret_cast<void *const *>(Raw() + 0x04) != nullptr; }
        [[nodiscard]] bool Panicking() const { return *reinterpret_cast<const bool *>(Raw() + 0x19e); }
        [[nodiscard]] bool FollowsCar() const { return *reinterpret_cast<void *const *>(Raw() + 0x1a8) != nullptr; }
        [[nodiscard]] bool TaxiWaving() const { return *reinterpret_cast<const bool *>(Raw() + 0x1fe); }
        [[nodiscard]] bool CanHailTaxi() const { return *reinterpret_cast<const bool *>(Raw() + 0x1ff); }
        // GeneratePassengerForTaxi's hail (0x44fc4d-0x44fce5): the wave flag
        // and the retail wave as the extended animation. variant 1 to 3.
        void StartTaxiWave(uint8_t variant) {
            *reinterpret_cast<bool *>(Raw() + 0x1fe) = true;
            ShowTaxiWave(variant);
        }
        // The wave animation alone, for a replica whose host walker waves.
        void ShowTaxiWave(uint8_t variant) {
            if (variant == 0 || variant > std::size(kTaxiWaveAnimations) || !_animationMachine) {
                return;
            }
            void *animation = nullptr;
            using Open = int(__thiscall *)(void *, const char *, void **, int, int);
            reinterpret_cast<Open>(kAnimationSetCacheOpen)(reinterpret_cast<void *>(kAnimationSetCache), kTaxiWaveAnimations[variant - 1], &animation, 0, 0);
            if (!animation) {
                return;
            }
            using SetExtendedAnim = void(__thiscall *)(void *, void *, float, float, bool);
            reinterpret_cast<SetExtendedAnim>(kAnimationMachineSetExtendedAnim)(_animationMachine, animation, kTaxiWaveWeight, 0.0f, false);
            *reinterpret_cast<uint16_t *>(static_cast<std::byte *>(_animationMachine) + kAnimationMachineLastNote) = kTaxiWaveNote;
            using Release = int(__stdcall *)(void *);
            reinterpret_cast<Release>((*static_cast<void ***>(animation))[0])(animation);
        }
        void EndTaxiWave() {
            using Call = void(__thiscall *)(NativeTrafficElement *);
            reinterpret_cast<Call>(kElementEndMavaniNaTaxi)(this);
        }
        [[nodiscard]] const std::byte *Raw() const { return reinterpret_cast<const std::byte *>(this); }
        [[nodiscard]] std::byte *Raw() { return reinterpret_cast<std::byte *>(this); }

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

    // A melee swing's hit on a walker, through the walker collision's own Hit
    // exactly as C_human::AI makes it.
    inline bool HitWalker(void *game, const Player::Vector3 &direction, const Player::Vector3 &position, const Player::Vector3 &normal, float damage,
                          Player::NativeActor *attacker, Scene::NativeFrame *model) {
        void *collision = *reinterpret_cast<void **>(static_cast<std::byte *>(game) + kGameDonOcas);
        using Call = bool(__thiscall *)(void *, int32_t, const Player::Vector3 &, const Player::Vector3 &, const Player::Vector3 &, float, Player::NativeActor *, uint32_t,
                                        Scene::NativeFrame *);
        return reinterpret_cast<Call>(kWalkerHit)(collision, kHardKnockdownHit, direction, position, normal, damage, attacker, 0, model);
    }

    inline NativeTrafficElement **ElementsBegin() {
        return *reinterpret_cast<NativeTrafficElement ***>(kTrafficElementsBegin);
    }

    inline NativeTrafficElement **ElementsEnd() {
        return *reinterpret_cast<NativeTrafficElement ***>(kTrafficElementsEnd);
    }
} // namespace Mafia1Online::SDK::Traffic
