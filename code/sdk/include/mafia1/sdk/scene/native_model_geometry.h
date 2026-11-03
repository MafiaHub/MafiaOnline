#pragma once

#include <mafia1/sdk/scene/native_mesh.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Mafia1Online::SDK::Scene {
    // I3D_frame hierarchy and mesh fields, verified against reM LS3DF
    // (I3D_frame.h, I3D_visual.h, I3D_object.h, I3D_mesh_level.h,
    // I3D_face_group.h; all LS3D_ASSERT_OFFSET-checked).
    namespace FrameLayout {
        inline constexpr size_t kModelName   = 0x104; // the part after "<model>."
        inline constexpr size_t kFirstChild  = 0x124;
        inline constexpr size_t kNextSibling = 0x12c;
        inline constexpr size_t kVisualType  = 0x1d4;
        inline constexpr size_t kObjectMesh  = 0x1e0; // I3D_object::m_pMesh
        inline constexpr uint32_t kFrameVisual = 1;
    } // namespace FrameLayout

    // Visual types whose class is I3D_object or derives from it, and so
    // carries an LOD mesh at +0x1e0.
    inline constexpr uint32_t kVisualObject      = 0x5f4a424f; // I3D_VISUAL_OBJECT
    inline constexpr uint32_t kVisualLitObject   = 0x50414d4c; // I3D_VISUAL_LIT_OBJECT
    inline constexpr uint32_t kVisualSingleMesh  = 0x4d474e53; // I3D_VISUAL_SINGLEMESH
    inline constexpr uint32_t kVisualSingleMorph = 0x524d4d53; // I3D_VISUAL_SINGLEMORPH
    inline constexpr uint32_t kVisualMorph       = 0x4850524d; // I3D_VISUAL_MORPH

    inline NativeFrame *FirstChild(NativeFrame *frame) {
        return *reinterpret_cast<NativeFrame **>(reinterpret_cast<std::byte *>(frame) + FrameLayout::kFirstChild);
    }
    inline NativeFrame *NextSibling(NativeFrame *frame) {
        return *reinterpret_cast<NativeFrame **>(reinterpret_cast<std::byte *>(frame) + FrameLayout::kNextSibling);
    }
    // A model child's own name: I3D_frame::SetName keeps "<model>.<child>"
    // and points m_pSzModelName at the child part.
    inline const char *ModelLocalName(const NativeFrame *frame) {
        const char *name = *reinterpret_cast<char *const *>(reinterpret_cast<const std::byte *>(frame) + FrameLayout::kModelName);
        return name ? name : "";
    }

    // The LOD 0 mesh of an I3D_object-derived visual, or null.
    inline NativeMeshObject *ObjectMesh(NativeFrame *frame) {
        if (frame->FrameType() != FrameLayout::kFrameVisual) {
            return nullptr;
        }
        switch (*reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(frame) + FrameLayout::kVisualType)) {
        case kVisualObject:
        case kVisualLitObject:
        case kVisualSingleMesh:
        case kVisualSingleMorph:
        case kVisualMorph: return *reinterpret_cast<NativeMeshObject **>(reinterpret_cast<std::byte *>(frame) + FrameLayout::kObjectMesh);
        default: return nullptr;
        }
    }

    // I3D_face_group vtable: SetMaterial +0, SetFaces +4, CreateFaces +8,
    // GetFaces +0xc (locks the index buffer), UnlockFaces +0x10.
    struct NativeFaceList;
    struct NativeFaceListVTable {
        void *_unused00[3];
        const uint16_t *(__stdcall *getFaces)(NativeFaceList *);
        void(__stdcall *unlockFaces)(NativeFaceList *);
    };
    static_assert(offsetof(NativeFaceListVTable, getFaces) == 0x0c);
    static_assert(offsetof(NativeFaceListVTable, unlockFaces) == 0x10);
    struct NativeFaceList {
        NativeFaceListVTable *vtable;
        void *material;
        uint32_t faceCount;
        NativeFaceList *next;
    };
    static_assert(sizeof(void *) != 4 || offsetof(NativeFaceList, faceCount) == 0x08);
    static_assert(sizeof(void *) != 4 || offsetof(NativeFaceList, next) == 0x0c);
    // I3D_mesh_level: the face group chain head at +0x1c.
    inline constexpr size_t kMeshLevelFaceGroups = 0x1c;

    // Copies an LOD's vertex positions and triangles (u16 indices). Both
    // locks are the retail ones LoadCollision and the deform code use.
    inline bool ReadMeshLevel(NativeMeshLevel *level, std::vector<Player::Vector3> &positions, std::vector<std::array<uint16_t, 3>> &triangles) {
        positions.clear();
        triangles.clear();
        if (level->vertexCount <= 0 || level->vertexCount > 65536) {
            return false;
        }
        auto *vertices = level->LockVertices();
        if (!vertices) {
            return false;
        }
        positions.reserve(static_cast<size_t>(level->vertexCount));
        for (int32_t i = 0; i < level->vertexCount; ++i) {
            positions.push_back(vertices[i].position);
        }
        level->UnlockVertices();
        auto *group = *reinterpret_cast<NativeFaceList **>(reinterpret_cast<std::byte *>(level) + kMeshLevelFaceGroups);
        for (int guard = 0; group && guard < 4096; group = group->next, ++guard) {
            if (group->faceCount == 0 || group->faceCount > 65536) {
                continue;
            }
            const uint16_t *indices = group->vtable->getFaces(group);
            if (!indices) {
                continue;
            }
            for (uint32_t face = 0; face < group->faceCount; ++face) {
                const std::array<uint16_t, 3> triangle {indices[face * 3], indices[face * 3 + 1], indices[face * 3 + 2]};
                if (triangle[0] < positions.size() && triangle[1] < positions.size() && triangle[2] < positions.size()) {
                    triangles.push_back(triangle);
                }
            }
            group->vtable->unlockFaces(group);
        }
        return true;
    }
} // namespace Mafia1Online::SDK::Scene
