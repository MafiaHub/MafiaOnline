#pragma once

#include "shared/features/world/world_props.h"

#include <mafia1/sdk/world/native_city_cache.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Mafia1Online::Features::Props {
    // The city cache's traffic signs (SDK::World::kHandleSignalMarkCrash).
    // A sign this client's native code breaks (its player's car or shot, a
    // copy of someone else's; on the simulation host its traffic and NPCs)
    // goes to the report callback; one another client broke is broken here
    // the same way, and a repair puts them all back up with their collision.
    //
    // The native code looks for the sign only in segments streamed in around
    // the camera and otherwise lets the car through the pole: a car striking
    // one away from the camera (the simulation host's traffic) breaks it here
    // without pieces, so it falls for those who see it.
    class CitySigns final {
      public:
        // piecesMark: where the pieces it threw start (CrashPieces::Mark).
        using Report = std::function<void(const Shared::World::Props::SignBreak &, size_t piecesMark)>;

        bool Install(Report report);
        void Uninstall();
        // silent: without the pieces. The pieces need its segment streamed
        // in.
        void Apply(const Shared::World::Props::SignBreak &sign, bool silent);
        // Every sign broken here back up, with its collision; how many.
        size_t Repair();
        void Reset();
        // The mission's signs and the tree primitives that break one.
        [[nodiscard]] std::pair<size_t, size_t> Count() const;

        void OnStrike(void *game, uint32_t *primitive, const SDK::Player::Vector3 *hit, const SDK::Player::Vector3 *unused, const SDK::Player::Vector3 *impulse,
                      SDK::Player::NativeActor *person);

      private:
        struct Broken {
            uint16_t segment = 0;
            uint16_t placement = 0;
            SDK::World::TreePrimitiveId primitive;
        };
        void Record(uint16_t segment, uint16_t placement, SDK::World::TreePrimitiveId primitive);

        Report _report;
        std::vector<Broken> _broken;
    };
} // namespace Mafia1Online::Features::Props
