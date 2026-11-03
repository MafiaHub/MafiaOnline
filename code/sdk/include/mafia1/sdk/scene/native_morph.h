#pragma once

#include <mafia1/sdk/scene/native_model_geometry.h>
#include <mafia1/sdk/scene/native_scene.h>

#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Scene {
    // I3D_void_interface (reM LS3DF I3D_visual.h), I3D_visual's second base,
    // its vtable pointer at visual +0x140. Retail calls it through that
    // pointer: DabingSetHuman (0x5f4d3e) SetWeight [+8], LoadRAWSet
    // (0x4990d0) NumLevels [+0] and NumRegions [+4], all __stdcall.
    // I3D_singlemorph::SetWeight(group, target, weight) range-checks both
    // and stores weights[group * NumLevels + target]; each group's targets
    // are normalized to sum 1 at render (morphBase::NormalizeWeights), and
    // target 0 is the undeformed mesh.
    struct NativeMorphWeights;
    struct NativeMorphWeightsVTable {
        int(__stdcall *numLevels)(const NativeMorphWeights *);
        int(__stdcall *numRegions)(const NativeMorphWeights *);
        int(__stdcall *setWeight)(NativeMorphWeights *, int group, int target, float weight);
    };
    static_assert(offsetof(NativeMorphWeightsVTable, setWeight) == 0x08);
    struct NativeMorphWeights {
        NativeMorphWeightsVTable *vtable;

        // Targets per group.
        [[nodiscard]] int Targets() const {
            return vtable->numLevels(this);
        }
        [[nodiscard]] int Groups() const {
            return vtable->numRegions(this);
        }
        void SetWeight(int group, int target, float weight) {
            vtable->setWeight(this, group, target, weight);
        }
    };
    inline constexpr size_t kVisualMorphWeights = 0x140;

    // The weights of a single-morph visual, as DabingSetHuman (0x5f4d1e)
    // accepts a human's base mesh: FRAME_VISUAL of type SINGLEMORPH.
    inline NativeMorphWeights *SingleMorphWeights(NativeFrame *frame) {
        if (!frame || frame->FrameType() != FrameLayout::kFrameVisual ||
            *reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(frame) + FrameLayout::kVisualType) != kVisualSingleMorph) {
            return nullptr;
        }
        return reinterpret_cast<NativeMorphWeights *>(reinterpret_cast<std::byte *>(frame) + kVisualMorphWeights);
    }

    // Retail lip-sync (reM core/Dabing.cpp): 16 S_dabing_slot of 0x44 bytes
    // at 0x6bdb50 (DabingSetHuman 0x5f4ceb), state at +0x0c, morph visual
    // at +0x1c. DabingTick writes a slot's visual every frame unless the
    // slot is FREE (0).
    inline constexpr uintptr_t kDabingSlots    = 0x6bdb50;
    inline constexpr size_t kDabingSlotCount   = 16;
    inline constexpr size_t kDabingSlotSize    = 0x44;
    inline constexpr size_t kDabingSlotState   = 0x0c;
    inline constexpr size_t kDabingSlotVisual  = 0x1c;

    // Whether a retail speech line or cutscene is animating this visual.
    inline bool DabingAnimates(const NativeFrame *visual) {
        for (size_t slot = 0; slot < kDabingSlotCount; ++slot) {
            const auto *entry = reinterpret_cast<const std::byte *>(kDabingSlots + slot * kDabingSlotSize);
            if (*reinterpret_cast<const uint32_t *>(entry + kDabingSlotState) != 0 &&
                *reinterpret_cast<NativeFrame *const *>(entry + kDabingSlotVisual) == visual) {
                return true;
            }
        }
        return false;
    }
} // namespace Mafia1Online::SDK::Scene
