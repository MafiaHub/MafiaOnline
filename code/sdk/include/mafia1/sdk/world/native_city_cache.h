#pragma once

#include <mafia1/sdk/collision/native_collision_tree.h>
#include <mafia1/sdk/core/system.h>
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace Mafia1Online::SDK::World {
    // The city cache (reM systems/C_cache_base_block.{h,cpp}, IDA): cache.bin
    // places the city's small models in segments, and Tick (0x4020b0)
    // streams a segment in around the camera by giving each active
    // placement a pooled "off<group>seg<n>" model (ApplySegmentFrames
    // 0x4025d0) and out by taking them back (DeactivateSegmentFrames
    // 0x402a50). Every client loads the same cache.bin, so a segment and
    // placement index name the same placement everywhere.
    //
    // Its traffic signs (C_game::m_SignalMarkIds, the anim groups of
    // dopravni1B-8B.I3D) break through C_game::HandleSignalMarkCrash
    // (0x5b7be0): a car body faster than 6 with someone in it
    // (Collision_Filter_Body 0x426d7e) or a shot (TickShoot 0x5aa76e) that
    // meets a static primitive with kSignalMarkRequired and not
    // kCollisionPrimitiveOff takes the nearest active sign of an active
    // segment within 10 m across and 5 m up, throws the pieces of its pooled
    // model (CreateCrashActors with the model's file name, force |impulse|),
    // sets kCollisionPrimitiveOff on that primitive, closes the placement
    // and tells the police. A closed placement stays off until GameInit or
    // GameDone call VisibleAllFrames; nothing clears the primitive's flag.
    inline constexpr uintptr_t kHandleSignalMarkCrash = 0x5b7be0;
    // __thiscall(C_game*, g_collision_header*, const S_vector& hit, const
    // S_vector& unused, const S_vector& impulse, C_human*), ret 0x14.
    using HandleSignalMarkCrashCall = void(__thiscall *)(void *, uint32_t *, const Player::Vector3 *, const Player::Vector3 *, const Player::Vector3 *, Player::NativeActor *);
    // g_collision_header flags byte bit 1 (test eax, 20000h at 0x5b7bf9).
    inline constexpr uint32_t kSignalMarkRequired = 0x20000;
    inline constexpr float kSignalMarkReach = 10.0f;
    inline constexpr float kSignalMarkHeight = 5.0f;
    // C_game::m_SignalMarkIds: a VC6 vector of unsigned short at +0x1e4
    // (_First +0x1e8, _Last +0x1ec, as 0x5b7cbe reads them).
    inline constexpr size_t kSignalMarkIdsFirstOffset = 0x1e8;
    inline constexpr size_t kSignalMarkIdsLastOffset = 0x1ec;
    // I3D_model::m_pszFileName, the crash model's base (0x5b7ddb).
    inline constexpr size_t kModelFileNameOffset = 0x154;

    // S_cache_block_frame (0x44 bytes).
    struct NativeCachePlacement {
        uint16_t segment;
        uint16_t group;
        // The pooled model it has while its segment is in; 0xffff before.
        uint16_t pooled;
        uint16_t padding;
        Player::Vector3 position;
        float rotation[4];
        Player::Vector3 modelScale;
        uint32_t unused;
        bool active;
        uint8_t padding2[3];
        Player::Vector3 visualScale;
    };
    // The retail layouts are the 32-bit ones.
    static_assert(sizeof(NativeCachePlacement) == 0x44);
    static_assert(offsetof(NativeCachePlacement, position) == 0x08);
    static_assert(offsetof(NativeCachePlacement, active) == 0x34);

    // A VC6 std::vector: allocator word, _First, _Last, _End.
    template <typename T>
    struct NativeVector {
        uint32_t allocator;
        T *first;
        T *last;
        T *end;

        [[nodiscard]] std::span<T> Items() const {
            return first ? std::span<T>(first, last) : std::span<T>();
        }
    };
    static_assert(sizeof(void *) != 4 || sizeof(NativeVector<int>) == 0x10);

    // S_cache_block_segment (0x74 bytes).
    struct NativeCacheSegment {
        Scene::NativeFrame *root;
        bool active;
        uint8_t padding[3];
        NativeVector<Scene::NativeFrame *> visuals;
        float center[2];
        float radius;
        float points[4][2];
        float directions[4][2];
        NativeVector<NativeCachePlacement> placements;
    };
    static_assert(sizeof(void *) != 4 || sizeof(NativeCacheSegment) == 0x74);
    static_assert(sizeof(void *) != 4 || offsetof(NativeCacheSegment, placements) == 0x64);

    // S_cache_block_anim_frame and S_cache_block_anim_group.
    struct NativeCachePooled {
        Scene::NativeFrame *model;
        bool assigned;
        uint8_t padding[3];
    };
    static_assert(sizeof(void *) != 4 || sizeof(NativeCachePooled) == 0x08);
    struct NativeCacheGroup {
        const char *name;
        NativeVector<NativeCachePooled> pooled;
    };
    static_assert(sizeof(void *) != 4 || sizeof(NativeCacheGroup) == 0x14);

    // C_cache_base_block at g_cacheBaseBlock.
    struct NativeCityCache {
        static constexpr uintptr_t kCloseSegmentFrame = 0x402cf0;
        static constexpr uintptr_t kDeactivateSegmentFrames = 0x402a50;
        static constexpr uintptr_t kApplySegmentFrames = 0x4025d0;

        NativeVector<NativeCacheSegment> segments;
        NativeVector<NativeCacheGroup> groups;
        uint32_t cleanupGroup;
        bool open;

        [[nodiscard]] static NativeCityCache *Get() {
            return reinterpret_cast<NativeCityCache *>(Core::System::kCacheBaseBlock);
        }
        [[nodiscard]] bool SegmentStreamed(uint32_t segment) const {
            const auto all = segments.Items();
            return segment < all.size() && all[segment].active;
        }
        // The model the placement has while its segment is in, or null.
        [[nodiscard]] Scene::NativeFrame *PooledModel(const NativeCachePlacement &placement) const {
            const auto all = groups.Items();
            if (placement.group >= all.size()) {
                return nullptr;
            }
            const auto pool = all[placement.group].pooled.Items();
            return placement.pooled < pool.size() && pool[placement.pooled].assigned ? pool[placement.pooled].model : nullptr;
        }
        // bool __thiscall(C_cache_base_block*, S_cache_block_frame*), ret 4:
        // clears active and takes the pooled model back.
        void Close(NativeCachePlacement *placement) {
            reinterpret_cast<bool(__thiscall *)(NativeCityCache *, NativeCachePlacement *)>(kCloseSegmentFrame)(this, placement);
        }
        // void __thiscall(this, unsigned), ret 4, as Tick streams a segment
        // out and in (0x40249e, 0x40250a). Out takes the pooled models of
        // its active placements back by their pooled index: turn a closed
        // placement back on only between the two, or its stale index takes
        // a model another placement holds now.
        void StreamOut(uint32_t segment) {
            reinterpret_cast<void(__thiscall *)(NativeCityCache *, uint32_t)>(kDeactivateSegmentFrames)(this, segment);
        }
        void StreamIn(uint32_t segment) {
            reinterpret_cast<void(__thiscall *)(NativeCityCache *, uint32_t)>(kApplySegmentFrames)(this, segment);
        }
    };
    static_assert(sizeof(void *) != 4 || offsetof(NativeCityCache, groups) == 0x10);
    static_assert(sizeof(void *) != 4 || offsetof(NativeCityCache, open) == 0x24);

    // A static primitive of the loaded tree by its kind's array and index:
    // LoadCollision reads them from tree.klz, the same on every client.
    enum class TreePrimitiveKind : uint8_t {
        None,
        Face,
        OrientedBox,
        AxisBox,
        Sphere,
        Plate,
        Cylinder,
    };
    inline constexpr uint8_t kTreePrimitiveKinds = 7;

    struct TreePrimitiveId {
        TreePrimitiveKind kind = TreePrimitiveKind::None;
        uint32_t index = 0;
    };

    namespace Detail {
        template <typename T>
        bool IndexIn(const void *header, const T *array, uint32_t count, uint32_t &index) {
            const auto *at = static_cast<const std::byte *>(header);
            const auto *first = reinterpret_cast<const std::byte *>(array);
            if (!array || at < first || at >= first + static_cast<size_t>(count) * sizeof(T) || (at - first) % sizeof(T) != 0) {
                return false;
            }
            index = static_cast<uint32_t>((at - first) / sizeof(T));
            return true;
        }
    } // namespace Detail

    // None for a primitive outside the tree's arrays (a map object's).
    inline TreePrimitiveId IdentifyTreePrimitive(const Collision::TreeLevel *tree, const void *header) {
        TreePrimitiveId id;
        if (!tree || !header) {
            return id;
        }
        if (Detail::IndexIn(header, tree->faces, tree->faceCount, id.index)) {
            id.kind = TreePrimitiveKind::Face;
        } else if (Detail::IndexIn(header, tree->orientedBoxes, tree->orientedBoxCount, id.index)) {
            id.kind = TreePrimitiveKind::OrientedBox;
        } else if (Detail::IndexIn(header, tree->axisBoxes, tree->axisBoxCount, id.index)) {
            id.kind = TreePrimitiveKind::AxisBox;
        } else if (Detail::IndexIn(header, tree->spheres, tree->sphereCount, id.index)) {
            id.kind = TreePrimitiveKind::Sphere;
        } else if (Detail::IndexIn(header, tree->plates, tree->plateCount, id.index)) {
            id.kind = TreePrimitiveKind::Plate;
        } else if (Detail::IndexIn(header, tree->cylinders, tree->cylinderCount, id.index)) {
            id.kind = TreePrimitiveKind::Cylinder;
        }
        return id;
    }

    // The packed word of that primitive, or null when the tree has none.
    inline uint32_t *ResolveTreePrimitive(Collision::TreeLevel *tree, TreePrimitiveId id) {
        if (!tree) {
            return nullptr;
        }
        const auto pick = [&](auto *array, uint32_t count) -> uint32_t * {
            return array && id.index < count ? reinterpret_cast<uint32_t *>(&array[id.index].header) : nullptr;
        };
        switch (id.kind) {
        case TreePrimitiveKind::Face: return pick(tree->faces, tree->faceCount);
        case TreePrimitiveKind::OrientedBox: return pick(tree->orientedBoxes, tree->orientedBoxCount);
        case TreePrimitiveKind::AxisBox: return pick(tree->axisBoxes, tree->axisBoxCount);
        case TreePrimitiveKind::Sphere: return pick(tree->spheres, tree->sphereCount);
        case TreePrimitiveKind::Plate: return pick(tree->plates, tree->plateCount);
        case TreePrimitiveKind::Cylinder: return pick(tree->cylinders, tree->cylinderCount);
        default: return nullptr;
        }
    }
} // namespace Mafia1Online::SDK::World
