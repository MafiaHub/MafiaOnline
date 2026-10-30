#pragma once

#include "shared/features/world/world_props.h"

#include <mafia1/sdk/player/native_actor.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Mafia1Online::Features::Props {
    // The C_crash_obj pieces a break threw here, by break. Every client that
    // breaks a prop throws its pieces from the same hit with the same
    // impulse, and each runs their physics itself. The client that broke it
    // reports where they came to rest once they all sleep (the server keeps
    // the report of the break that stood), and every client puts its pieces
    // there: as they fall asleep, or at once when it threw them only now
    // (a late joiner, a break far away that waited for the report).
    //
    // The game drops pieces of its own (C_crash_obj::AI on REMOVE, the
    // farthest from the camera past its limit, 0x432740), so a piece is
    // looked up among C_game's temporary actors before it is touched.
    class CrashPieces final {
      public:
        struct Key {
            bool sign = false;
            std::string name;
            uint32_t signKey = 0;

            bool operator==(const Key &other) const {
                return sign == other.sign && (sign ? signKey == other.signKey : name == other.name);
            }
        };
        using Report = std::function<void(const Key &, const std::vector<Shared::World::Props::Pose> &)>;

        void SetReport(Report report) {
            _report = std::move(report);
        }
        // Where the next pieces thrown start in C_game's temporary actors.
        [[nodiscard]] static size_t Mark();
        // The pieces thrown since mark; local: this client broke it and
        // reports where they come to rest.
        void Capture(const Key &key, size_t mark, bool local);
        // The breaker's resting poses; now: put the pieces there at once.
        void Apply(const Key &key, const std::vector<Shared::World::Props::Pose> &poses, bool now);
        void Update();
        void Reset();

      private:
        struct Group {
            Key key;
            bool local = false;
            bool reported = false;
            std::chrono::steady_clock::time_point thrown {};
            std::vector<SDK::Player::NativeActor *> pieces;
            std::vector<Shared::World::Props::Pose> pending;
            std::chrono::steady_clock::time_point pendingSince {};
        };
        // Pieces the game dropped are cleared.
        void Prune(Group &group) const;

        Report _report;
        std::vector<Group> _groups;
    };
} // namespace Mafia1Online::Features::Props
