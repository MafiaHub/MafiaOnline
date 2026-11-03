#pragma once

#include <networking/rpc/rpc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mafia1Online::Shared::World::Props {
    // Two kinds of world props, both from the mission's scene:
    //
    // Breakables are static scene models whose collision has the crash
    // material (41): phone booths (PHbox01), the hydrant (dopravni9B, whose
    // crash model carries the ';X1H;' hydrant piece), trash bins (popel01,
    // kos01), barrels and the like. SpawnCrashActorFromFrame (0x431710)
    // turns one off and throws C_crash_obj pieces in its place. They are
    // named by their scene frame, which every client loads the same. The
    // stock freeride scene2.bin places 1,044 of them; the city cache holds
    // one barrel (left to retail) and 154 traffic signs (dopravni1B-8B).
    //
    // Traffic signs break through HandleSignalMarkCrash (0x5b7be0) instead:
    // it closes the sign's cache placement and turns off the static tree
    // primitive the car or shot struck. They are named by the cache.bin
    // segment and placement and the primitive's index in the tree.klz
    // arrays, which every client loads the same.
    //
    // Physical props are scene2.bin type 35 actors (C_box_new): boxes,
    // railings, mail boxes, cans. Every client loads them in the same order;
    // an index is a position in that order and nameHash (FNV-1a of the
    // frame name) proves it matches. Each client simulates them itself; a
    // prop this client's player (or, on the simulation host, its traffic and
    // NPCs) sent to sleep somewhere new reports the pose, and the server's
    // newest pose is everyone's.
    inline constexpr size_t kMaxBroken = 2048;
    // Breaks one player can have standing until a repair; the simulation
    // host is held only to kMaxBroken.
    inline constexpr size_t kMaxBrokenPerPlayer = 256;
    inline constexpr uint16_t kMaxPhysical = 1024;
    inline constexpr size_t kMaxNameLength = 63;
    // A player reports props it broke or moved this close to it; the
    // simulation host, which drives the traffic everywhere, anywhere.
    inline constexpr float kReportReach = 250.0f;
    // A breakable is found at the reported place within this distance.
    inline constexpr float kBreakablePlaceTolerance = 2.0f;
    // One report moves a physical prop at most this far from its last pose.
    inline constexpr float kMaxPropTravel = 60.0f;
    // |cos| between a pose's direction and up: SetDir3 (0x60fc90) builds the
    // frame's rotation from both, and parallel ones give a singular matrix
    // UpdateImportantVariables (0x460220) inverts.
    inline constexpr float kMaxPoseSkew = 0.05f;
    // SpawnCrashActorFromFrame's arguments as the natives pass them: a car
    // its velocity, force 2 and the hit point 3 m back along it
    // (Collision_Filter_Body 0x426340); a bullet its 5-unit direction, the
    // segment start this frame's flight began at and its damage (TickShoot
    // 0x5a8e70); an explosion a 5-unit impulse from its centre up to 0.3 of
    // its radius away and its scaled damage (NewExplosion 0x5aae10); a melee
    // hit the weapon's damage. CreateCrashActors casts its ray from origin
    // along impulse and throws the pieces at force * 0.5 / mass.
    inline constexpr float kMaxBreakImpulse = 100.0f;
    inline constexpr float kMaxBreakForce = 1000.0f;
    inline constexpr float kMaxBreakOrigin = 40.0f;
    // Signs broken until a repair, everyone together; they count towards
    // kMaxBrokenPerPlayer with the breakables.
    inline constexpr size_t kMaxSigns = 1024;
    // HandleSignalMarkCrash takes the nearest sign within 10 m across and
    // 5 m up of the hit (0x5b7d42, 0x5b7dac).
    inline constexpr float kSignReach = 10.0f;
    inline constexpr float kSignHeight = 5.0f;
    // The static tree arrays a struck primitive can lie in
    // (SDK::World::TreePrimitiveKind); 0: none of them.
    inline constexpr uint8_t kSignPrimitiveKinds = 7;
    // The C_crash_obj pieces a break throws (CreateCrashActors, one per
    // visual of the crash model) are physics bodies each client runs. The
    // client whose break stood reports where they came to rest, the first
    // kMaxPieces of them in the order they were thrown, at most
    // kMaxPieceTravel from the prop.
    inline constexpr size_t kMaxPieces = 16;
    inline constexpr float kMaxPieceTravel = 50.0f;

    inline uint32_t NameHash(std::string_view name) {
        uint32_t hash = 2166136261u;
        for (const char c : name) {
            hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
        }
        return hash;
    }

    // Scene frame names: printable ASCII; the stock scene has names with
    // spaces ("xbarel 27").
    inline bool ValidName(std::string_view name) {
        if (name.empty() || name.size() > kMaxNameLength) {
            return false;
        }
        for (const char c : name) {
            if (c < 0x20 || c > 0x7e) {
                return false;
            }
        }
        return true;
    }

    // The wire format of MafiaNet's std::string (a 16-bit length and the
    // bytes, byte aligned), read only up to kMaxNameLength: its own reader
    // resizes the string to the sender's length before it knows the stream
    // holds that much.
    inline bool SerializeName(MafiaNet::BitStream *stream, bool write, std::string &name) {
        uint16_t length = static_cast<uint16_t>(std::min(name.size(), kMaxNameLength));
        if (write) {
            stream->Write(length);
            stream->WriteAlignedBytes(reinterpret_cast<const unsigned char *>(name.data()), length);
            return true;
        }
        name.clear();
        if (!stream->Read(length) || length > kMaxNameLength) {
            return false;
        }
        if (length == 0) {
            stream->AlignReadToByteBoundary();
            return true;
        }
        name.resize(length);
        if (!stream->ReadAlignedBytes(reinterpret_cast<unsigned char *>(name.data()), length)) {
            name.clear();
            return false;
        }
        return true;
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
        [[nodiscard]] bool Finite() const {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
        }
        [[nodiscard]] float Length() const {
            return std::sqrt(x * x + y * y + z * z);
        }
    };
    inline Vector operator-(const Vector &a, const Vector &b) {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    inline Vector operator+(const Vector &a, const Vector &b) {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    inline Vector operator*(const Vector &v, float scale) {
        return {v.x * scale, v.y * scale, v.z * scale};
    }
    inline float Dot(const Vector &a, const Vector &b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    inline float Distance(const Vector &a, const Vector &b) {
        return (a - b).Length();
    }

    // What C_PhThing::MoveCount places a prop's frame from: its position
    // (C_actor +0x24), direction (+0x30) and up (C_PhThing +0xac).
    struct Pose {
        uint16_t index = 0;
        uint32_t nameHash = 0;
        Vector position;
        Vector direction;
        Vector up;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, index);
            stream->Serialize(write, nameHash);
            position.Serialize(stream, write);
            direction.Serialize(stream, write);
            up.Serialize(stream, write);
        }
        [[nodiscard]] bool Valid() const {
            const auto unit = [](const Vector &v) {
                const float length = v.Length();
                return std::isfinite(length) && length > 0.5f && length < 1.5f;
            };
            if (index >= kMaxPhysical || !position.Finite() || !unit(direction) || !unit(up)) {
                return false;
            }
            return std::abs(Dot(direction, up)) / (direction.Length() * up.Length()) < kMaxPoseSkew;
        }
        // Unit direction and the unit up square to it, for SetDir3. Only
        // for a Valid pose.
        [[nodiscard]] Pose Orthonormalized() const {
            Pose pose = *this;
            pose.direction = direction * (1.0f / direction.Length());
            const auto up2 = up - pose.direction * Dot(up, pose.direction);
            pose.up = up2 * (1.0f / up2.Length());
            return pose;
        }
    };

    // Resting poses of a break's pieces; index: the order they were thrown.
    inline bool SerializePieces(MafiaNet::BitStream *stream, bool write, std::vector<Pose> &pieces) {
        uint8_t count = static_cast<uint8_t>(std::min(pieces.size(), kMaxPieces));
        if (!stream->Serialize(write, count)) {
            return false;
        }
        if (!write) {
            if (count > kMaxPieces) {
                pieces.clear();
                return false;
            }
            pieces.resize(count);
        }
        for (uint8_t i = 0; i < count; ++i) {
            pieces[i].Serialize(stream, write);
        }
        return true;
    }
    inline bool ValidPieces(const std::vector<Pose> &pieces, const Vector &place) {
        if (pieces.empty() || pieces.size() > kMaxPieces) {
            return false;
        }
        for (const auto &piece : pieces) {
            if (!piece.Valid() || piece.index >= kMaxPieces || !(Distance(piece.position, place) <= kMaxPieceTravel)) {
                return false;
            }
        }
        return true;
    }

    // The arguments of SpawnCrashActorFromFrame: the struck frame (empty:
    // the model itself), the ray origin and sweep CreateCrashActors finds
    // the struck part with, and the force the pieces fly off with. position
    // is the model's, to find it again. ParseCrashConfigFromFrameName
    // (0x431a00) reads the crash settings from the struck frame's
    // properties, so a replay strikes the same frame.
    struct Break {
        std::string name;
        std::string frame;
        Vector position;
        Vector origin;
        Vector impulse;
        float force = 0.0f;
        // Where its pieces came to rest, once the breaker reported them.
        std::vector<Pose> pieces;

        bool Serialize(MafiaNet::BitStream *stream, bool write) {
            if (!SerializeName(stream, write, name) || !SerializeName(stream, write, frame)) {
                return false;
            }
            position.Serialize(stream, write);
            origin.Serialize(stream, write);
            impulse.Serialize(stream, write);
            return stream->Serialize(write, force) && SerializePieces(stream, write, pieces);
        }
        [[nodiscard]] bool Valid() const {
            return ValidName(name) && (frame.empty() || ValidName(frame)) && position.Finite() && origin.Finite() && impulse.Finite() && std::isfinite(force);
        }
        // Within what the natives pass (kMaxBreak*); the ray keeps its line.
        void Clamp() {
            force = std::clamp(force, 0.0f, kMaxBreakForce);
            if (const float length = impulse.Length(); length > kMaxBreakImpulse) {
                impulse = impulse * (kMaxBreakImpulse / length);
            }
            const auto offset = origin - position;
            if (const float distance = offset.Length(); distance > kMaxBreakOrigin) {
                origin = position + offset * (kMaxBreakOrigin / distance);
            }
        }
    };

    // A traffic sign of the city cache: its cache.bin segment and placement,
    // the placement's position to find it again, and what struck it: the
    // static primitive (kind 0: one outside the tree, nothing to turn off),
    // the hit point and the impulse (a car's velocity, a shot's 10-unit
    // direction) its pieces fly off with, at force |impulse|.
    struct SignBreak {
        uint16_t segment = 0;
        uint16_t placement = 0;
        uint8_t primitiveKind = 0;
        uint32_t primitiveIndex = 0;
        Vector position;
        Vector hit;
        Vector impulse;
        // Where its pieces came to rest, once the breaker reported them.
        std::vector<Pose> pieces;

        bool Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, segment);
            stream->Serialize(write, placement);
            stream->Serialize(write, primitiveKind);
            stream->Serialize(write, primitiveIndex);
            position.Serialize(stream, write);
            hit.Serialize(stream, write);
            impulse.Serialize(stream, write);
            return SerializePieces(stream, write, pieces);
        }
        [[nodiscard]] uint32_t Key() const {
            return static_cast<uint32_t>(segment) << 16 | placement;
        }
        // Struck where HandleSignalMarkCrash finds this sign.
        [[nodiscard]] bool Valid() const {
            if (primitiveKind >= kSignPrimitiveKinds || !position.Finite() || !hit.Finite() || !impulse.Finite()) {
                return false;
            }
            const auto offset = hit - position;
            return offset.x * offset.x + offset.z * offset.z < kSignReach * kSignReach && std::abs(offset.y) < kSignHeight;
        }
        void Clamp() {
            if (const float length = impulse.Length(); length > kMaxBreakImpulse) {
                impulse = impulse * (kMaxBreakImpulse / length);
            }
        }
    };

    // Client -> server: a breakable broke here. Server -> every client: it
    // broke; the others break theirs the same way.
    struct BreakReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PropBreak");

        uint64_t missionGeneration = 0;
        Break prop;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            prop.Serialize(stream, write);
        }
    };

    // Client -> server: a traffic sign broke here. Server -> every client:
    // it broke; the others break theirs the same way.
    struct SignBreakReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PropSignBreak");

        uint64_t missionGeneration = 0;
        SignBreak sign;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            sign.Serialize(stream, write);
        }
    };

    // Client -> server: where the pieces of a break this client reported
    // came to rest (a breakable by name, or a sign by SignBreak::Key).
    // Server -> every client: place them there.
    struct PiecesReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PropPieces");

        uint64_t missionGeneration = 0;
        bool sign = false;
        std::string name;
        uint32_t signKey = 0;
        std::vector<Pose> pieces;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            stream->Serialize(write, sign);
            if (!SerializeName(stream, write, name) || !stream->Serialize(write, signKey) || !SerializePieces(stream, write, pieces)) {
                pieces.clear();
            }
        }
    };

    // Client -> server: a physical prop this client sent somewhere came to
    // rest away from the pose the server has for it; home: back where the
    // mission placed it. Server -> every client: the pose it has now.
    struct RestReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PropRest");

        uint64_t missionGeneration = 0;
        Pose pose;
        bool home = false;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            pose.Serialize(stream, write);
            stream->Serialize(write, home);
        }
    };

    // Client -> server once its mission has loaded; the server answers with
    // a Snapshot of everything broken and moved since the last repair.
    struct SnapshotRequest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PropSnapshotRequest");

        uint64_t missionGeneration = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
        }
    };

    struct Snapshot {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PropSnapshot");

        uint64_t missionGeneration = 0;
        std::vector<Break> broken;
        std::vector<Pose> moved;
        std::vector<SignBreak> signs;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
            uint16_t brokenCount = static_cast<uint16_t>(std::min(broken.size(), kMaxBroken));
            uint16_t movedCount = static_cast<uint16_t>(std::min<size_t>(moved.size(), kMaxPhysical));
            uint16_t signCount = static_cast<uint16_t>(std::min(signs.size(), kMaxSigns));
            stream->Serialize(write, brokenCount);
            stream->Serialize(write, movedCount);
            stream->Serialize(write, signCount);
            if (!write) {
                if (brokenCount > kMaxBroken || movedCount > kMaxPhysical || signCount > kMaxSigns) {
                    Clear();
                    return;
                }
                broken.resize(brokenCount);
                moved.resize(movedCount);
                signs.resize(signCount);
            }
            for (uint16_t i = 0; i < brokenCount; ++i) {
                if (!broken[i].Serialize(stream, write) && !write) {
                    Clear();
                    return;
                }
            }
            for (uint16_t i = 0; i < movedCount; ++i) {
                moved[i].Serialize(stream, write);
            }
            for (uint16_t i = 0; i < signCount; ++i) {
                if (!signs[i].Serialize(stream, write) && !write) {
                    Clear();
                    return;
                }
            }
        }
        void Clear() {
            broken.clear();
            moved.clear();
            signs.clear();
        }
    };

    // Server -> every client: put every breakable and traffic sign back up
    // and every physical prop back where the mission placed it.
    struct Repair {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mafia1Online::PropRepair");

        uint64_t missionGeneration = 0;

        void Serialize(MafiaNet::BitStream *stream, bool write) {
            stream->Serialize(write, missionGeneration);
        }
    };
} // namespace Mafia1Online::Shared::World::Props
