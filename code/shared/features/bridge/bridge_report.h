#pragma once

#include <networking/rpc/rpc.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace Mafia1Online::Shared::Bridge {
    // Drawbridges (scene2.bin type 20, C_bridge): a left and a right one
    // (frame name starting with 'L' or not), each turning two leaves. Every
    // client loads them in the same order; an index is a position in that
    // order and nameHash (FNV-1a of the frame name) proves it matches.
    inline constexpr uint8_t kMaxBridges = 8;
    // The steamers the right bridge moves (S_bridge_runtime entries).
    inline constexpr uint8_t kSteamerCount = 2;
    inline constexpr uint8_t kRouteCount = 4;
    inline constexpr uint8_t kSegmentCount = 6;
    // E_bridge_phase: closed, signaling, preparing to open, opening, open,
    // closing.
    inline constexpr uint8_t kPhaseCount = 6;
    // Bounds a report must keep to: the retail open angle is 50 degrees and
    // the longest phase timer the closed duration, 120 s.
    inline constexpr float kMaxAngle = 3.1415927f;
    inline constexpr int32_t kMaxPhaseTimerMs = 3600000;

    inline uint32_t NameHash(const char *name) {
        uint32_t hash = 2166136261u;
        for (; name && *name; ++name) {
            hash = (hash ^ static_cast<uint8_t>(*name)) * 16777619u;
        }
        return hash;
    }

    struct Vector {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, x);
            stream->Serialize(write, y);
            stream->Serialize(write, z);
        }
    };

    // What AI decides and the leaves' pose follows from: the phase (+0x144),
    // its timer (+0x150) and the leaf angle (+0x148).
    struct State {
        uint8_t index = 0;
        uint32_t nameHash = 0;
        uint8_t phase = 0;
        int32_t phaseTimerMs = 0;
        float angle = 0.0f;
        // The bridge frame, for scripts.
        Vector position;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, index);
            stream->Serialize(write, nameHash);
            stream->Serialize(write, phase);
            stream->Serialize(write, phaseTimerMs);
            stream->Serialize(write, angle);
            position.Serialize(stream, write);
        }
    };

    // A steamer on its route: segment point plus progress within it.
    struct Steamer {
        uint8_t route = 0;
        uint8_t point = 0;
        float progress = 0.0f;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, route);
            stream->Serialize(write, point);
            stream->Serialize(write, progress);
        }
    };

    inline bool Valid(const State &state) {
        return state.index < kMaxBridges && state.phase < kPhaseCount && std::isfinite(state.angle) && std::fabs(state.angle) <= kMaxAngle &&
               state.phaseTimerMs >= -kMaxPhaseTimerMs && state.phaseTimerMs <= kMaxPhaseTimerMs && std::isfinite(state.position.x) && std::isfinite(state.position.y) &&
               std::isfinite(state.position.z);
    }

    inline bool Valid(const Steamer &steamer) {
        return steamer.route < kRouteCount && steamer.point < kSegmentCount && std::isfinite(steamer.progress) && steamer.progress >= 0.0f && steamer.progress <= 1.0f;
    }

    // Simulation host -> server at kSnapshotHz, and at once (reliable) when a
    // bridge changes phase; server -> every player in the mission. hostTimeMs
    // dates it on the host's clock for the players' interpolation.
    struct Snapshot {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::BridgeSnapshot");
        static constexpr int kSnapshotHz = 10;

        uint64_t missionGeneration = 0;
        // The host's process id (Simulation::HostSession) and its clock.
        uint32_t hostSession = 0;
        uint32_t hostTimeMs = 0;
        uint8_t count = 0;
        std::array<State, kMaxBridges> bridges {};
        // 0 without a right bridge, else kSteamerCount.
        uint8_t steamerCount = 0;
        std::array<Steamer, kSteamerCount> steamers {};

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, hostSession);
            stream->Serialize(write, hostTimeMs);
            stream->Serialize(write, count);
            if (count > kMaxBridges) {
                count = 0;
                return;
            }
            for (uint8_t i = 0; i < count; ++i) {
                bridges[i].Serialize(stream, write);
            }
            stream->Serialize(write, steamerCount);
            if (steamerCount != 0 && steamerCount != kSteamerCount) {
                steamerCount = 0;
                return;
            }
            for (uint8_t i = 0; i < steamerCount; ++i) {
                steamers[i].Serialize(stream, write);
            }
        }
    };
} // namespace Mafia1Online::Shared::Bridge
