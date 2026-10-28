#pragma once

#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/traffic/native_traffic_car.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::World {
    // Lip-synced speech (reM core/Dabing.cpp). A slot plays one line: the
    // sound "sounds\%08d.wav" linked to the model's neck and, while its
    // "tables\dat\%08d.dat" lip-sync samples last, the morphs of the model's
    // "base". Without a sound (no sound driver) the line still lasts as long
    // as its lip-sync data, so DabingIsSpeaking times a dialogue either way.
    // int DabingGetFreeChanel(): the highest free slot, else the fallback.
    inline constexpr uintptr_t kDabingGetFreeChannel = 0x5f4ca0;
    // void __fastcall DabingSetModel(int slot, I3D_model *model).
    inline constexpr uintptr_t kDabingSetModel = 0x5f5210;
    // void __fastcall DabingSpeak(int slot, unsigned speechId,
    // bool subtitles, unsigned mode), ret 8; mode 0 is the spot sound
    // (2.5 to 30 m) every C_pumper line uses.
    inline constexpr uintptr_t kDabingSpeak = 0x5f5570;

    // The slot the line plays in, as Start and ChooseNextAction take one.
    inline int DabingSpeak(Scene::NativeFrame *model, uint32_t speechId) {
        const int slot = reinterpret_cast<int(__cdecl *)()>(kDabingGetFreeChannel)();
        reinterpret_cast<void(__fastcall *)(int, Scene::NativeFrame *)>(kDabingSetModel)(slot, model);
        reinterpret_cast<void(__fastcall *)(int, uint32_t, bool, uint32_t)>(kDabingSpeak)(slot, speechId, false, 0);
        return slot;
    }

    // The gas station attendant (scene2.bin type 25, reM C_pumper, vtable
    // 0x624990, 0x2ac bytes). Every Free Ride scene has three: "pumpar",
    // "pumpar1" and "pumpar2" for pumps 0 to 2, each with its route between
    // the web nodes 666+2n and 667+2n, its pump "_pumpa%i", its hose model
    // "_pumpa_hadice%i" and its camera "_pumpa_camera%i".
    //
    // C_actor::Tick (0x4064f0) runs AI (vtable +0x34, 0x47ea30) unless the
    // actor's flags (+0x00c) have 5 set, then Update (+0x38, 0x47f560),
    // which places the frame at the actor's position and direction (+0x24,
    // +0x30), grounds it and moves its two dynamic collisions.
    //
    // AI runs the state machine below: in the idle and smoking states it
    // counts the action timer (+0x25c against +0x260), in the animated ones
    // it waits for the active animation's end (GetActiveAnimTime + 250 ms),
    // in the dialogue ones for DabingIsSpeaking to end, then it calls
    // ChooseNextAction (0x47f950). Every state ends with SetAnim(0)
    // (0x47f8e0, which starts the animation +0xd8 when it differs from the
    // previous one +0xd4), C_animations_machine_de_luxe::Tick (+0xe0) and
    // the frame's Tick, whose "notify" child fires PumperAnimEventHandler
    // (0x4809c0): footsteps, the fuel loop sound (StartPumpFuel 0x480da0 on
    // event 42, EndPumpFuel 0x480dd0 on 43), the cigar and cigarette props
    // and their smoke. FUELING also ticks the hose model (+0x234).
    //
    // A service, retail: C_player::Do_AB_OwnerCar (0x594fa0) offers "use the
    // pump" when CanPump (0x4805d0) says AVAILABLE (fuel cap within 6 m of the
    // pump, car within 4 m, slower than 0.1, facing within 45 degrees of the
    // pump), then Start (0x480190) greets (speech 0x1b996f7), aligns the
    // car's fuel cap with the pump, switches its engine off, raises its
    // control override (C_car +0x21dc), locks the player's controls and
    // points the camera from "_pumpa_camera%i" at the attendant. The player
    // answers (0xe3f763, model C_game::m_pPlayer), the attendant walks his
    // route, fuels (animation 0x153; EndPumpFuel fills the tank and takes
    // 500 from the score), turns (turn1), says he is done (+0x84) and hears
    // the player (0x987165), walks back, and on reaching his start node the
    // WALKING branch of ChooseNextAction releases the car: 1 m forward,
    // Reset, camera SetCar, engine on, override down, controls unlocked,
    // PopMode.
    struct NativePumper {
        // reM E_pumper_state.
        enum class State : int32_t {
            Walking = 0,
            Idle = 1,
            PumpTurn = 2,
            PumpTransition = 3,
            BusyDialogueStart = 4,
            Fueling = 5,
            BusyDialogue = 6,
            RequestDialogue = 7,
            RequestReply = 8,
            BusyReply = 9,
            PumpReady = 10,
            IdleAnimation = 11,
            IdlePause = 12,
            SmokeLoop = 13,
            SmokeDrag = 14,
            SmokeLight = 15,
            SmokeDrop = 16,
            Panic = 17,
        };
        static constexpr int32_t kStateCount = 18;

        static constexpr uint32_t kActorType = 25;
        static constexpr uintptr_t kGameInit = 0x47caa0;
        static constexpr uintptr_t kGameDone = 0x47e6e0;
        // void __thiscall(C_pumper *, unsigned frameTime), ret 4.
        static constexpr uintptr_t kAI = 0x47ea30;
        // void __thiscall(C_pumper *).
        static constexpr uintptr_t kChooseNextAction = 0x47f950;
        // bool __thiscall(C_pumper *, C_car *), ret 4. It refuses only
        // CanPump's UNAVAILABLE (0x4801a3): a BLOCKED car (moving, off the
        // pump, chased) is still pulled onto the pump.
        static constexpr uintptr_t kStart = 0x480190;
        // int __thiscall(C_pumper *, C_car *), ret 4: 0 unavailable, 1
        // available, 2 blocked (it flashes the reason on screen).
        static constexpr uintptr_t kCanPump = 0x4805d0;
        static constexpr int32_t kPumpAvailable = 1;
        // void __thiscall(C_pumper *).
        static constexpr uintptr_t kEndPumpFuel = 0x480dd0;
        // void __thiscall(C_pumper *, unsigned channel, bool on), ret 8.
        static constexpr uintptr_t kPlaySound = 0x481770;
        // bool __thiscall(C_pumper *, C_actor *, const S_vector &,
        // const S_vector &, const S_vector &, float, I3D_frame *), ret 18h:
        // a car or a human ran into him; he panics.
        static constexpr uintptr_t kCollision = 0x47e940;
        // bool __thiscall(C_pumper *, E_hit_type, const S_vector &,
        // const S_vector &, const S_vector &, float, C_actor *, unsigned,
        // I3D_frame *), ret 20h: shot or struck; he panics.
        static constexpr uintptr_t kHit = 0x47e9c0;
        // I3D_model *__thiscall(C_pumper *): hides him and hands a copy of
        // his model to C_traffic_generator::AddFreePerson and NewPanic. Update
        // calls it once the pump "_pumpa_pumpa%i" is off (blown up).
        static constexpr uintptr_t kPanic = 0x480fd0;

        // Retail speech ids (C_pumper.cpp): the greeting, the player's two
        // answers; the attendant's "done" line is per actor (+0x84).
        static constexpr uint32_t kGreetingSpeech = 0x1b996f7;
        static constexpr uint32_t kRequestReplySpeech = 0xe3f763;
        static constexpr uint32_t kBusyReplySpeech = 0x987165;
        // anim_info id of the base idle (GetAnimIndex(1)).
        static constexpr uint32_t kIdleAnimationId = 1;
        static constexpr uint32_t kFuelSoundChannel = 3;
        static constexpr uint32_t kSmokingPropCount = 4;

        static constexpr size_t kRouteStartNodeOffset = 0x78;
        static constexpr size_t kRouteEndNodeOffset = 0x7c;
        static constexpr size_t kPumpIdOffset = 0x80;
        static constexpr size_t kBusySpeechOffset = 0x84;
        // m_iHandSmokeParticle, m_iNeckSmokeParticle, m_iMouthSmokeParticle.
        static constexpr size_t kSmokeParticlesOffset = 0xa0;
        static constexpr size_t kStateOffset = 0xd0;
        static constexpr size_t kAnimationOffset = 0xd8;
        static constexpr size_t kAnimationFiniteOffset = 0xdc;
        static constexpr size_t kAnimationTrackOffset = 0xdd;
        static constexpr size_t kNextNodeOffset = 0x20c;
        static constexpr size_t kCurrentNodeOffset = 0x210;
        // The Dabing slot the dialogue states wait on.
        static constexpr size_t kDialogueChannelOffset = 0x230;
        static constexpr size_t kHoseModelOffset = 0x234;
        static constexpr size_t kSmokingPropsOffset = 0x238;
        static constexpr size_t kCarOffset = 0x24c;
        static constexpr size_t kPumpingFuelOffset = 0x258;
        static constexpr size_t kActionElapsedOffset = 0x25c;
        static constexpr size_t kActionDurationOffset = 0x260;
        // C_web_node::m_uID (u16 at +0x0e, reM ai/C_web_node.h).
        static constexpr size_t kWebNodeIdOffset = 0x0e;
        // C_mission::m_pParticleManager, as Start reads it (0x480255).
        static constexpr size_t kMissionParticleManagerOffset = 0x6c;
        // void __thiscall C_ParticleManager::DestroyParticle(int), ret 4.
        static constexpr uintptr_t kDestroyParticle = 0x44a240;
        // I3D_model vtable +0x78: SetAnimTime(unsigned index, unsigned time),
        // as PUMP_READY rewinds the hose (0x47ff32).
        static constexpr size_t kModelSetAnimTimeSlot = 0x78;

        [[nodiscard]] int32_t CanPump(void *car) {
            return reinterpret_cast<int32_t(__thiscall *)(NativePumper *, void *)>(kCanPump)(this, car);
        }

        [[nodiscard]] Player::NativeActor &Actor() {
            return *reinterpret_cast<Player::NativeActor *>(this);
        }
        [[nodiscard]] State GetState() const {
            return Field<State>(kStateOffset);
        }
        void SetState(State state) {
            Field<State>(kStateOffset) = state;
        }
        [[nodiscard]] int32_t Animation() const {
            return Field<int32_t>(kAnimationOffset);
        }
        [[nodiscard]] bool AnimationFinite() const {
            return Field<bool>(kAnimationFiniteOffset);
        }
        [[nodiscard]] bool AnimationTrack() const {
            return Field<bool>(kAnimationTrackOffset);
        }
        // The animation SetAnim starts on AI's next tail: an anim_info index,
        // or one of E_pumper_special_animation (0x10000 to 0x10008).
        void SetAnimation(int32_t animation, bool finite, bool track) {
            Field<int32_t>(kAnimationOffset) = animation;
            Field<bool>(kAnimationFiniteOffset) = finite;
            Field<bool>(kAnimationTrackOffset) = track;
        }
        [[nodiscard]] uint32_t BusySpeech() const {
            return Field<uint32_t>(kBusySpeechOffset);
        }
        void SetDialogueChannel(int slot) {
            Field<int32_t>(kDialogueChannelOffset) = slot;
        }
        [[nodiscard]] void *Car() const {
            return Field<void *>(kCarOffset);
        }
        void SetCar(void *car) {
            Field<void *>(kCarOffset) = car;
        }
        void SetPumpingFuel(bool pumping) {
            Field<bool>(kPumpingFuelOffset) = pumping;
        }
        // The idle states choose again once the elapsed time passes the
        // duration.
        void SetActionTimer(uint32_t elapsed, uint32_t duration) {
            Field<uint32_t>(kActionElapsedOffset) = elapsed;
            Field<uint32_t>(kActionDurationOffset) = duration;
        }
        [[nodiscard]] void *NextNode() const {
            return Field<void *>(kNextNodeOffset);
        }
        [[nodiscard]] void *CurrentNode() const {
            return Field<void *>(kCurrentNodeOffset);
        }
        void SetNodes(void *current, void *next) {
            Field<void *>(kCurrentNodeOffset) = current;
            Field<void *>(kNextNodeOffset) = next;
        }
        [[nodiscard]] static uint16_t NodeId(const void *node) {
            return *reinterpret_cast<const uint16_t *>(static_cast<const std::byte *>(node) + kWebNodeIdOffset);
        }
        [[nodiscard]] int32_t RouteStartNode() const {
            return Field<int32_t>(kRouteStartNodeOffset);
        }
        [[nodiscard]] int32_t RouteEndNode() const {
            return Field<int32_t>(kRouteEndNodeOffset);
        }
        // reM IsPumping (0x481810): walking, or past idle up to PUMP_READY.
        [[nodiscard]] static bool Pumping(State state) {
            return state == State::Walking || (state > State::Idle && state <= State::PumpReady);
        }
        [[nodiscard]] Scene::NativeFrame *HoseModel() const {
            return Field<Scene::NativeFrame *>(kHoseModelOffset);
        }

        // Start's own first part (0x4801fc-0x48028c): the cigar and
        // cigarette props go and their smoke is destroyed. The particle ids
        // are cleared here as well, which retail leaves for GameDone to
        // destroy a second time.
        void PutOutSmoking() {
            auto **props = &Field<Scene::NativeFrame *>(kSmokingPropsOffset);
            for (uint32_t i = 0; i < kSmokingPropCount; ++i) {
                if (props[i]) {
                    props[i]->SetOn(false);
                }
            }
            auto *particles = &Field<int32_t>(kSmokeParticlesOffset);
            void *manager = *reinterpret_cast<void **>(reinterpret_cast<std::byte *>(Core::Mission::Get()) + kMissionParticleManagerOffset);
            for (int i = 0; i < 3; ++i) {
                if (particles[i] != -1) {
                    reinterpret_cast<void(__thiscall *)(void *, int)>(kDestroyParticle)(manager, particles[i]);
                    particles[i] = -1;
                }
            }
        }
        void StopFuelSound() {
            reinterpret_cast<void(__thiscall *)(NativePumper *, uint32_t, bool)>(kPlaySound)(this, kFuelSoundChannel, false);
        }
        // PUMP_READY's rewind of the hose model (0x47ff2d-0x47ff3f).
        void RewindHose() {
            if (auto *hose = HoseModel()) {
                using SetAnimTime = void(__stdcall *)(Scene::NativeFrame *, uint32_t, uint32_t);
                (*reinterpret_cast<SetAnimTime *>(reinterpret_cast<std::byte *>(hose->vtable) + kModelSetAnimTimeSlot))(hose, 0, 0);
                hose->Tick(0);
            }
        }
        // C_car::m_iControlOverrideCount (+0x21dc): while above 0, C_car::AI
        // holds the power at 0. Start raises it (0x480533), the release
        // lowers it (0x47fe8b).
        static constexpr size_t kCarControlOverrideOffset = 0x21dc;
        static void ReleaseControlOverride(void *car) {
            auto &count = *reinterpret_cast<int32_t *>(static_cast<std::byte *>(car) + kCarControlOverrideOffset);
            if (count > 0) {
                --count;
            }
        }
        [[nodiscard]] static int32_t IdleAnimation() {
            using Call = int32_t(__fastcall *)(uint32_t);
            return reinterpret_cast<Call>(Traffic::kGetAnimIndex)(kIdleAnimationId);
        }

      private:
        template <typename T>
        T &Field(size_t offset) const {
            return *reinterpret_cast<T *>(reinterpret_cast<std::byte *>(const_cast<NativePumper *>(this)) + offset);
        }
    };
} // namespace Mafia1Online::SDK::World
