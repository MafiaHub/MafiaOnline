#pragma once

#include <mafianet/types.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Mafia1Online::Features::Simulation {
    class SimulationService;
}
namespace Mafia1Online::Features::Player {
    class PlayerService;
}

namespace Mafia1Online::Features::Npc {
    // The retail police manager's crime record of each player, which the
    // simulation host runs and reports. Scripts read it, change it and turn
    // the police off; the host applies the changes the retail way.
    class PoliceService final {
      public:
        // Retail S_shvestky levels.
        static constexpr uint8_t kLevelNone   = 0;
        static constexpr uint8_t kLevelLethal = 3;

        struct Record {
            uint64_t spawnGeneration = 0;
            uint8_t level = 0;
            // Retail crime bits since the record was last settled.
            uint16_t crimes = 0;
            uint8_t officers = 0;
        };
        struct Events {
            // One crime bit newly in the record.
            std::function<void(uint64_t playerId, uint16_t crime)> crime;
            std::function<void(uint64_t playerId, uint8_t level, uint8_t previous)> wanted;
            // The host answered a SendBackup: whether its police car set out.
            std::function<void(uint32_t requestId, uint64_t playerId, bool sent)> backup;
        };

        void RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players);
        void SetEvents(Events events) {
            _events = std::move(events);
        }
        void SetMissionGeneration(uint64_t generation);
        // Sends the on/off switch and the officers to a newly attached host
        // and forgets players who left.
        void Update();

        // The record of a spawned player, or nothing.
        std::optional<Record> Find(uint64_t playerId) const;
        // False when the player is not spawned or no host is attached.
        bool SetWanted(uint64_t playerId, uint8_t level);
        bool Clear(uint64_t playerId);
        // A police officer has no crime record, as retail's own officers
        // have none: the host takes the player's replica out of the records
        // and gives it the police behaviour class. Going on duty settles
        // the record here at once. False for an unknown player.
        bool SetOfficer(uint64_t playerId, bool officer);
        bool IsOfficer(uint64_t playerId) const {
            return _officers.contains(playerId);
        }
        // Sends a police car with two officers after a wanted player the
        // retail way. Returns the request id the backup event answers, or
        // 0 when the player is not wanted, is an officer, the police are off
        // or no host is attached.
        uint32_t SendBackup(uint64_t playerId);
        void SetEnabled(bool enabled);
        bool IsEnabled() const {
            return _enabled;
        }

      private:
        bool SendOrder(uint64_t playerId, bool clear, uint8_t level);
        void SendSwitch() const;
        void SendOfficers() const;
        void FailBackups();
        // The host answers each within a frame or two; more pending than
        // this means it is not answering.
        static constexpr size_t kMaxBackups = 16;

        const Simulation::SimulationService *_simulation = nullptr;
        Player::PlayerService *_players = nullptr;
        Events _events;
        uint64_t _missionGeneration = 0;
        bool _enabled = true;
        std::optional<MafiaNet::PeerGuid> _host;
        std::unordered_map<uint64_t, Record> _records;
        std::unordered_set<uint64_t> _officers;
        // Backups the host has not answered yet: request id to player.
        std::unordered_map<uint32_t, uint64_t> _backups;
        uint32_t _nextBackup = 1;
    };
} // namespace Mafia1Online::Features::Npc
