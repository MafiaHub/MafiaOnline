#pragma once

#include <mafia1/sdk/scene/native_scene.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace Mafia1Online::SDK::Scene {
    struct NativeMeshLevel;
    struct NativeMeshObject;
    struct NativeFaceGroup;

    struct NativeFaceGroupVTable {
        void(__stdcall *setMaterial)(NativeFaceGroup *, void *);
    };

    struct NativeFaceGroup {
        NativeFaceGroupVTable *vtable;
        void *material;

        void SetMaterial(void *replacement) { vtable->setMaterial(this, replacement); }
    };
    static_assert(offsetof(NativeFaceGroup, material) == 0x04);

    struct NativeMeshVertex {
        Player::Vector3 position;
        Player::Vector3 normal;
        float u;
        float v;
    };
    static_assert(sizeof(NativeMeshVertex) == 0x20);
    static_assert(offsetof(NativeMeshVertex, normal) == 0x0c);
    static_assert(offsetof(NativeMeshVertex, u) == 0x18);

    struct NativeMeshLevelVTable {
        void *_release;
        NativeMeshVertex *(__stdcall *lockVertices)(NativeMeshLevel *, uint32_t);
        void(__stdcall *unlockVertices)(NativeMeshLevel *);
        void *_unused0c[2];
        NativeFaceGroup *(__stdcall *getFGroup)(NativeMeshLevel *, int32_t);
        void *_unused18[4];
        void(__stdcall *updateBBox)(NativeMeshLevel *);
    };
    static_assert(offsetof(NativeMeshLevelVTable, lockVertices) == 0x04);
    static_assert(offsetof(NativeMeshLevelVTable, unlockVertices) == 0x08);
    static_assert(offsetof(NativeMeshLevelVTable, getFGroup) == 0x14);
    static_assert(offsetof(NativeMeshLevelVTable, updateBBox) == 0x28);

    struct NativeMeshLevel {
        NativeMeshLevelVTable *vtable;
        int32_t _referenceCount;
        int32_t vertexCount;

        NativeMeshVertex *LockVertices() { return vtable->lockVertices(this, 0); }
        void UnlockVertices() { vtable->unlockVertices(this); }
        NativeFaceGroup *GetFGroup(int32_t index) { return vtable->getFGroup(this, index); }
        void UpdateBBox() { vtable->updateBBox(this); }
    };
    static_assert(offsetof(NativeMeshLevel, vertexCount) == 0x08);

    struct NativeMeshObjectVTable {
        void *_release;
        NativeMeshLevel *(__stdcall *getLOD)(NativeMeshObject *, int32_t);
        void *_unused08[3];
        void(__stdcall *updateBoundVolume)(NativeMeshObject *);
    };
    static_assert(offsetof(NativeMeshObjectVTable, getLOD) == 0x04);
    static_assert(offsetof(NativeMeshObjectVTable, updateBoundVolume) == 0x14);

    struct NativeMeshObject {
        NativeMeshObjectVTable *vtable;
        int32_t _referenceCount;
        uint32_t lodCount;

        NativeMeshLevel *GetLOD(int32_t lod) const { return vtable->getLOD(const_cast<NativeMeshObject *>(this), lod); }
        void UpdateBoundVolume() { vtable->updateBoundVolume(this); }
    };
    static_assert(offsetof(NativeMeshObject, lodCount) == 0x08);

    struct NativeObjectFrameVTable {
        std::byte _unused[0x78];
        void(__stdcall *updateVertices)(NativeFrame *, int32_t);
    };
    static_assert(offsetof(NativeObjectFrameVTable, updateVertices) == 0x78);

    inline int DecodeDeformComponent(uint32_t code, uint32_t shift) {
        const int component = static_cast<int>((code >> shift) & 0x3fU);
        return (component & 0x20) ? component - 64 : component;
    }

    inline bool ValidatePackedDeformCheckpoint(const NativeMeshObject *originalMesh, const NativeMeshObject *deformedMesh,
                                               const NativeFrame *deformedFrame, int32_t lod, std::span<const uint32_t> packed) {
        if (!originalMesh || !deformedMesh || !deformedFrame || originalMesh == deformedMesh ||
            lod < 0 || lod > 1 || originalMesh->lodCount > 10 || deformedMesh->lodCount > 10 ||
            static_cast<uint32_t>(lod) >= originalMesh->lodCount || static_cast<uint32_t>(lod) >= deformedMesh->lodCount) {
            return false;
        }
        auto *originalLevel = originalMesh->GetLOD(lod);
        auto *deformedLevel = deformedMesh->GetLOD(lod);
        if (!originalLevel || !deformedLevel || deformedLevel->vertexCount <= 0 ||
            deformedLevel->vertexCount > 16384 || originalLevel->vertexCount != deformedLevel->vertexCount ||
            packed.size() > static_cast<size_t>(deformedLevel->vertexCount)) {
            return false;
        }
        uint32_t previousIndex = 0;
        bool hasPrevious = false;
        for (const uint32_t code : packed) {
            const uint32_t index = code >> 18;
            if (index >= static_cast<uint32_t>(deformedLevel->vertexCount) || (hasPrevious && index <= previousIndex)) {
                return false;
            }
            previousIndex = index;
            hasPrevious = true;
        }
        return true;
    }

    // Applies one retail save checkpoint to a live deform-zone mesh. The
    // caller should validate every zone/LOD group before applying any group.
    inline bool ApplyPackedDeformCheckpoint(const NativeMeshObject *originalMesh, NativeMeshObject *deformedMesh,
                                            NativeFrame *deformedFrame, int32_t lod, std::span<const uint32_t> packed) {
        if (!ValidatePackedDeformCheckpoint(originalMesh, deformedMesh, deformedFrame, lod, packed)) {
            return false;
        }
        auto *originalLevel = originalMesh->GetLOD(lod);
        auto *deformedLevel = deformedMesh->GetLOD(lod);

        // Both locks use retail's flags 0. The meshes may share a pooled D3D
        // vertex buffer, and a read-only lock on one can invalidate the other.
        auto *original = originalLevel->LockVertices();
        if (!original) {
            return false;
        }
        auto *deformed = deformedLevel->LockVertices();
        if (!deformed) {
            originalLevel->UnlockVertices();
            return false;
        }
        for (int32_t index = 0; index < deformedLevel->vertexCount; ++index) {
            deformed[index].position = original[index].position;
        }
        for (const uint32_t code : packed) {
            const uint32_t index = code >> 18;
            const auto &base = original[index].position;
            deformed[index].position = {
                base.x + static_cast<float>(DecodeDeformComponent(code, 12)) * 0.02f,
                base.y + static_cast<float>(DecodeDeformComponent(code, 6)) * 0.02f,
                base.z + static_cast<float>(DecodeDeformComponent(code, 0)) * 0.02f,
            };
        }
        originalLevel->UnlockVertices();
        deformedLevel->UnlockVertices();

        auto *frameVTable = reinterpret_cast<NativeObjectFrameVTable *>(deformedFrame->vtable);
        frameVTable->updateVertices(deformedFrame, lod);
        deformedLevel->UpdateBBox();
        // UpdateBBox locks internally and leaves its pool buffer locked.
        deformedLevel->UnlockVertices();
        deformedMesh->UpdateBoundVolume();
        deformedFrame->Update();
        return true;
    }
} // namespace Mafia1Online::SDK::Scene
