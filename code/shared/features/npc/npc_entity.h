#pragma once

#include "shared/net/quantize.h"

#include <networking/replication/network_entity.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Entities {
    // What made a simulation host NPC; every kind is a retail C_entity.
    enum class NpcKind : uint8_t {
        // A gangster pedestrian someone hit (CreateMafianos).
        Gangster = 1,
        // A police pedestrian the police manager recruited against a
        // player's crime (FizlosNamakatDoAkce).
        Police = 2,
        // A traffic driver turned into a C_entity: thrown out of its car by a
        // player (Do_ThrowCocotFromCar) or a mafia car's crew (Krles).
        Driver = 3,
        // A C_entity a server script spawned (Npc.spawn) and gives tasks.
        Scripted = 4,
    };

    inline bool KnownNpcKind(NpcKind kind) {
        return kind == NpcKind::Gangster || kind == NpcKind::Police || kind == NpcKind::Driver || kind == NpcKind::Scripted;
    }

    // A door action an NPC took at a car (NpcEntity::carAction).
    enum class NpcCarAction : uint8_t {
        None = 0,
        // A stolen traffic car's driver thrown out (Do_ThrowCocotFromCar).
        Throw = 1,
        // C_human::Use_Actor's animated exit and entry.
        Exit = 2,
        Enter = 3,
    };

    inline const char *NpcKindName(NpcKind kind) {
        switch (kind) {
        case NpcKind::Gangster: return "gangster";
        case NpcKind::Police: return "police";
        case NpcKind::Driver: return "driver";
        case NpcKind::Scripted: return "scripted";
        }
        return "unknown";
    }

    // An armed human the simulation host runs with the retail AI. The server
    // owns it and streams it to nearby players. Its inventory, aim, health
    // and death travel as a Combat::State under this network id, and its
    // shots and reloads as Combat::Events, the same as a player's.
    class NpcEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::Npc";
        static constexpr size_t kMaxModelLength = 63;
        static constexpr size_t kMaxAnimationLength = 59;
        // An NPC has one life; its combat records use this spawn generation.
        static constexpr uint64_t kSpawnGeneration = 1;

        std::string model;
        uint64_t missionGeneration = 0;
        NpcKind kind = NpcKind::Gangster;
        // The walker this NPC replaced, or 0. A player who sees that walker
        // swaps it for this NPC where it stands on that player's screen.
        uint64_t sourcePedestrianId = 0;
        // Server only: the car a stolen traffic car's driver was thrown out
        // of, or 0.
        uint64_t thrownFromCarId = 0;
        // Its last door action at a car, and that car and seat.
        NpcCarAction carAction = NpcCarAction::None;
        uint64_t carActionCarId = 0;
        uint8_t carActionSeat = 0;
        // Server only: when that action started on the host.
        std::chrono::steady_clock::time_point carActionAt {};
        // How long before this copy was constructed the action started, in
        // ms (construction is written per destination); kCarActionAgeUnknown
        // for none. A player constructing it during the action replays it.
        static constexpr uint16_t kCarActionAgeUnknown = 0xffff;
        uint16_t carActionAgeMs = kCarActionAgeUnknown;
        // Death is final: the corpse stays until the host retires it.
        bool alive = true;
        // The death the host's NPC played (Combat::NativeDeathAnimation),
        // with alive, so a copy that learns of the death here plays it.
        uint16_t deathAnimation = 0;
        // When it died on the host's sample clock (sampleClock's): a copy
        // plays the death once it draws the body at that moment.
        uint16_t deathClock = 0;
        // The host's native locomotion state (C_human m_iAnimState), which
        // rides with the pose like a player's.
        int16_t locomotion = 1;
        // Its stance (C_human m_bIsAiming, m_bIsCrouching), with the pose
        // for the same reason: a replica raises the gun or ducks where the
        // host's body did. The Combat::State keeps them too.
        bool aiming = false;
        bool crouching = false;
        // The weapon in its hand, with the pose too: a replica draws or
        // holsters where the host's body did.
        uint8_t weapon = 0;
        // The clip a script plays on it (npc.playAnimation), empty for none.
        // Each play or stop counts the revision up, so a replayed one-shot
        // clip starts again on every player's copy.
        std::string animation;
        bool animationLoop = false;
        uint16_t animationRevision = 0;
        // When the host applied revision animationDated, on its sample clock
        // (Npc::AnimationPlayed): a copy starts that clip once it draws the
        // body at that moment, where the host's started it.
        uint16_t animationClock = 0;
        uint16_t animationDated = 0;
        // Retail's voice for it (C_human m_uVoice, +0x680): which of the 40
        // sounds of each voices.def line it says. Chosen once at spawn, so
        // one NPC always speaks with the same voice.
        static constexpr uint8_t kVoiceCount   = 40;
        static constexpr uint8_t kDefaultVoice = 12;
        uint8_t voice = kDefaultVoice;
        // npc.say: the line (a C_human::NewScream id, voices.def group + 2)
        // and a count each say bumps; every copy that streams the NPC says
        // it on a new count, and a copy constructed later does not.
        static constexpr uint16_t kMinSpeechLine = 2;
        static constexpr uint16_t kMaxSpeechLine = 1023;
        uint16_t speechLine = 0;
        uint16_t speechRevision = 0;

        // The host's sample clock for this pose (Shared::Net).
        uint16_t sampleClock = 0;
        // Server only: the simulation host whose snapshots move it.
        MafiaNet::PeerGuid poseSource = MafiaNet::UNASSIGNED_PEER_GUID;

        MafiaNet::PeerGuid GetTransformSource() const override {
            return poseSource;
        }

        // Server: the host reports an action as it starts.
        void SetCarAction(NpcCarAction action, uint64_t carId, uint8_t seat) {
            carAction = action;
            carActionCarId = carId;
            carActionSeat = seat;
            carActionAt = std::chrono::steady_clock::now();
        }

        // A walker's pose plus the locomotion, the stance's two bits and the
        // weapon in hand.
        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            auto *stream = fields.PlainStream();
            if (!stream) {
                NetworkEntity::SerializeTransform(fields);
                fields.Field(sampleClock);
                fields.Field(locomotion);
                fields.Field(aiming);
                fields.Field(crouching);
                fields.Field(weapon);
                return;
            }
            const bool write = fields.Writing();
            if (!Net::SerializeWorldPosition(stream, write, position) || !Net::SerializeHeading(stream, write, rotation, 10) ||
                !Net::SerializePlanarVelocity(stream, write, velocity) || !stream->Serialize(write, sampleClock) || !stream->Serialize(write, locomotion) ||
                !stream->Serialize(write, aiming) || !stream->Serialize(write, crouching) || !stream->Serialize(write, weapon)) {
                fields.Fail();
            }
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            auto kindValue = static_cast<uint8_t>(kind);
            auto actionValue = static_cast<uint8_t>(carAction);
            fields.Field(model);
            fields.Field(missionGeneration);
            fields.Field(kindValue);
            fields.Field(sourcePedestrianId);
            if (fields.Writing()) {
                const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - carActionAt).count();
                carActionAgeMs = carAction != NpcCarAction::None ? static_cast<uint16_t>(std::clamp<long long>(age, 0, kCarActionAgeUnknown)) : kCarActionAgeUnknown;
            }
            fields.Field(actionValue);
            fields.Field(carActionCarId);
            fields.Field(carActionSeat);
            fields.Field(carActionAgeMs);
            fields.Field(alive);
            fields.Field(deathAnimation);
            fields.Field(deathClock);
            SerializeAnimation(fields);
            fields.Field(voice);
            fields.Field(speechLine);
            fields.Field(speechRevision);
            if (!fields.Writing()) {
                kind = static_cast<NpcKind>(kindValue);
                carAction = actionValue <= static_cast<uint8_t>(NpcCarAction::Enter) ? static_cast<NpcCarAction>(actionValue) : NpcCarAction::None;
                if (voice >= kVoiceCount) {
                    voice = kDefaultVoice;
                }
                if (model.size() > kMaxModelLength) {
                    model.clear();
                }
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(alive);
            fields.Field(deathAnimation);
            fields.Field(deathClock);
            SerializeAnimation(fields);
            fields.Field(speechLine);
            fields.Field(speechRevision);
        }

      private:
        void SerializeAnimation(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(animation);
            fields.Field(animationLoop);
            fields.Field(animationRevision);
            fields.Field(animationClock);
            fields.Field(animationDated);
            if (!fields.Writing() && animation.size() > kMaxAnimationLength) {
                animation.clear();
            }
        }
    };
} // namespace Mafia1Online::Shared::Entities
