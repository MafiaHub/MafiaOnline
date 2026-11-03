#pragma once

#include "shared/features/world/world_props.h"

#include <mafianet/types.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Mafia1Online::Features::Simulation {
    class SimulationService;
}
namespace Mafia1Online::Features::Player {
    class PlayerService;
}

namespace Mafia1Online::Features::World {
    // The mission's breakable and physical props (Shared::World::Props).
    // The client where a break or a push natively happened reports it; the
    // server keeps the first break of each breakable and traffic sign, where
    // that break's pieces came to rest as its breaker reports them, and the
    // newest resting pose of each physical prop, tells every client, and
    // hands the whole state to a client once its mission has loaded. A
    // repair clears it and has every client put the props back.
    //
    // The server has no scene: a break's name (a sign's cache placement and
    // primitive) is only known to be real by the clients that look it up.
    // Each player has kMaxBrokenPerPlayer breaks standing until a repair and
    // everyone together kMaxBroken breakables and kMaxSigns signs, so one
    // client inventing names cannot use up the others' sync.
    class PropsService final {
      public:
        // Reports a client may send per second: an explosion breaks a bin,
        // a booth and a barrel at once.
        static constexpr uint32_t kReportsPerSecond = 64;
        // Snapshot requests a client may make per mission, at least
        // kSnapshotRetry apart; a client asks once when its mission loads.
        static constexpr uint32_t kSnapshotsPerMission = 3;
        static constexpr auto kSnapshotRetry = std::chrono::seconds(10);

        void RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players);
        void SetMissionGeneration(uint64_t generation);

        // Breakables and traffic signs knocked down.
        [[nodiscard]] size_t BrokenCount() const {
            return _broken.size() + _signs.size();
        }
        // Physical props resting away from where the mission placed them.
        [[nodiscard]] size_t MovedCount() const;
        // Every client puts the breakables back up and the physical props
        // where the mission placed them; returns how many props that was.
        size_t Repair();

      private:
        // A sender's budget key: its player's network id; the host is 0.
        // Empty for a client that has not loaded this mission.
        std::optional<uint64_t> SenderKey(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const;
        // A player within reach of the place (the simulation host anywhere),
        // within its report budget.
        bool Accept(MafiaNet::PeerGuid sender, uint64_t key, const Shared::World::Props::Vector &place);
        void OnBreak(const Shared::World::Props::BreakReport &received, MafiaNet::PeerGuid sender);
        void OnSignBreak(const Shared::World::Props::SignBreakReport &received, MafiaNet::PeerGuid sender);
        void OnPieces(const Shared::World::Props::PiecesReport &received, MafiaNet::PeerGuid sender);
        void OnRest(const Shared::World::Props::RestReport &received, MafiaNet::PeerGuid sender);
        void OnSnapshotRequest(const Shared::World::Props::SnapshotRequest &request, MafiaNet::Packet *packet);

        struct Budget {
            std::chrono::steady_clock::time_point window {};
            uint32_t count = 0;
        };
        struct SnapshotBudget {
            std::chrono::steady_clock::time_point last {};
            uint32_t count = 0;
        };
        struct Resting {
            Shared::World::Props::Pose pose;
            // Back where the mission placed it.
            bool home = false;
        };

        const Simulation::SimulationService *_simulation = nullptr;
        Player::PlayerService *_players = nullptr;
        uint64_t _missionGeneration = 0;
        std::vector<Shared::World::Props::Break> _broken;
        // The sender key whose break of each of _broken (and _signs) stood:
        // only it reports the pieces.
        std::vector<uint64_t> _brokenOwners;
        std::vector<uint64_t> _signOwners;
        std::unordered_set<std::string> _brokenNames;
        std::vector<Shared::World::Props::SignBreak> _signs;
        // SignBreak::Key of each sign in _signs.
        std::unordered_set<uint32_t> _signKeys;
        // Standing breaks (breakables and signs) by sender key.
        std::unordered_map<uint64_t, size_t> _brokenBy;
        // The newest resting pose of each physical prop reported since the
        // last repair.
        std::map<uint16_t, Resting> _poses;
        // The name hash each index first came with this mission.
        std::unordered_map<uint16_t, uint32_t> _nameHashes;
        // By sender key.
        std::unordered_map<uint64_t, Budget> _budgets;
        // By peer guid.
        std::unordered_map<uint64_t, SnapshotBudget> _snapshots;
    };
} // namespace Mafia1Online::Features::World
