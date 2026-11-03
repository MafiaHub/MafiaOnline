#include "police_service.h"

#include "features/player/player_service.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/npc/npc_report.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Mafia1Online::Features::Npc {
    void PoliceService::RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players) {
        _simulation = &simulation;
        _players = &players;
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Npc::PoliceRecord>([this](const Shared::Npc::PoliceRecord &report, MafiaNet::Packet *packet) {
            // An officer's replica leaves the host's records as the host
            // learns of the duty; a report sent before then is stale.
            if (_simulation->Host() != MafiaNet::ToPeerGuid(packet->guid) || report.missionGeneration != _missionGeneration || report.level > kLevelLethal ||
                _officers.contains(report.playerNetworkId)) {
                return;
            }
            auto *player = _players->FindByNetworkId(report.playerNetworkId);
            if (!player || !player->spawned || player->spawnGeneration != report.playerSpawnGeneration) {
                return;
            }
            auto &record = _records[report.playerNetworkId];
            // A new life starts with a clean record.
            const Record previous = record.spawnGeneration == report.playerSpawnGeneration ? record : Record {};
            record = {report.playerSpawnGeneration, report.level, report.crimes, report.officers};
            if (_events.crime) {
                const uint16_t added = static_cast<uint16_t>(report.crimes & ~previous.crimes);
                for (uint16_t bit = 1; bit != 0; bit = static_cast<uint16_t>(bit << 1)) {
                    if ((added & bit) != 0) {
                        _events.crime(report.playerNetworkId, bit);
                    }
                }
            }
            if (_events.wanted && previous.level != report.level) {
                _events.wanted(report.playerNetworkId, report.level, previous.level);
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Npc::PoliceBackupSent>([this](const Shared::Npc::PoliceBackupSent &reply, MafiaNet::Packet *packet) {
            if (_simulation->Host() != MafiaNet::ToPeerGuid(packet->guid) || reply.missionGeneration != _missionGeneration) {
                return;
            }
            const auto found = _backups.find(reply.requestId);
            if (found == _backups.end()) {
                return;
            }
            const uint64_t playerId = found->second;
            _backups.erase(found);
            if (_events.backup && _players->FindByNetworkId(playerId)) {
                _events.backup(reply.requestId, playerId, reply.sent);
            }
        });
    }

    void PoliceService::SetMissionGeneration(uint64_t generation) {
        _missionGeneration = generation;
        _records.clear();
        FailBackups();
    }

    void PoliceService::FailBackups() {
        // Every pending request is answered once: refused when its host or
        // mission is gone.
        auto backups = std::move(_backups);
        _backups.clear();
        for (const auto &[requestId, playerId] : backups) {
            if (_events.backup && _players->FindByNetworkId(playerId)) {
                _events.backup(requestId, playerId, false);
            }
        }
    }

    void PoliceService::Update() {
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        if (host != _host) {
            _host = host;
            // The records were the former host's police manager's; a new
            // host starts every player clean and reports only changes from
            // there, so a level left here would never come down.
            auto records = std::move(_records);
            _records.clear();
            for (const auto &[playerId, record] : records) {
                const auto *player = _players->FindByNetworkId(playerId);
                if (_events.wanted && record.level != kLevelNone && player && player->spawnGeneration == record.spawnGeneration) {
                    _events.wanted(playerId, kLevelNone, record.level);
                }
            }
            FailBackups();
            if (_host) {
                SendSwitch();
                SendOfficers();
            }
        }
        std::vector<uint64_t> gone;
        for (const auto &[playerId, record] : _records) {
            const auto *player = _players->FindByNetworkId(playerId);
            if (!player || player->spawnGeneration != record.spawnGeneration) {
                gone.push_back(playerId);
            }
        }
        for (const uint64_t playerId : gone) {
            _records.erase(playerId);
        }
        // A backup for a player who left is answered to nobody: the event
        // names a Player.
        std::erase_if(_backups, [this](const auto &backup) { return !_players->FindByNetworkId(backup.second); });
        if (std::erase_if(_officers, [this](uint64_t playerId) { return !_players->FindByNetworkId(playerId); }) > 0) {
            SendOfficers();
        }
    }

    std::optional<PoliceService::Record> PoliceService::Find(uint64_t playerId) const {
        const auto *player = _players ? _players->FindByNetworkId(playerId) : nullptr;
        if (!player || !player->spawned) {
            return std::nullopt;
        }
        const auto found = _records.find(playerId);
        if (found == _records.end() || found->second.spawnGeneration != player->spawnGeneration) {
            return Record {player->spawnGeneration};
        }
        return found->second;
    }

    bool PoliceService::SetWanted(uint64_t playerId, uint8_t level) {
        // An officer has no record to raise.
        if (_officers.contains(playerId)) {
            return level == kLevelNone && Find(playerId).has_value();
        }
        // With the police off the host sends nobody, so a raise is refused;
        // lowering and clearing still apply.
        if (!_enabled && level != kLevelNone) {
            const auto record = Find(playerId);
            if (!record || level > record->level) {
                return false;
            }
        }
        return SendOrder(playerId, level == kLevelNone, std::min(level, kLevelLethal));
    }

    bool PoliceService::Clear(uint64_t playerId) {
        if (_officers.contains(playerId)) {
            return Find(playerId).has_value();
        }
        return SendOrder(playerId, true, kLevelNone);
    }

    bool PoliceService::SetOfficer(uint64_t playerId, bool officer) {
        if (!_players || !_players->FindByNetworkId(playerId)) {
            return false;
        }
        if (officer == _officers.contains(playerId)) {
            return true;
        }
        if (officer) {
            _officers.insert(playerId);
            // The host drops the replica's record as this reaches it; the
            // script hears the level go now.
            const auto found = _records.find(playerId);
            const Record previous = found != _records.end() ? found->second : Record {};
            if (found != _records.end()) {
                _records.erase(found);
            }
            const auto *player = _players->FindByNetworkId(playerId);
            if (_events.wanted && previous.level != kLevelNone && player->spawnGeneration == previous.spawnGeneration) {
                _events.wanted(playerId, kLevelNone, previous.level);
            }
        }
        else {
            // Back as a person, the host records the replica afresh.
            _officers.erase(playerId);
            _records.erase(playerId);
        }
        SendOfficers();
        return true;
    }

    uint32_t PoliceService::SendBackup(uint64_t playerId) {
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        const auto *player = _players ? _players->FindByNetworkId(playerId) : nullptr;
        const auto record = Find(playerId);
        if (!_enabled || !host || !player || player->missionGeneration != _missionGeneration || _officers.contains(playerId) || !record ||
            record->level == kLevelNone || _backups.size() >= kMaxBackups) {
            return 0;
        }
        const uint32_t requestId = _nextBackup++;
        if (_nextBackup == 0) {
            _nextBackup = 1;
        }
        _backups.emplace(requestId, playerId);
        Shared::Npc::PoliceBackup order;
        order.missionGeneration = _missionGeneration;
        order.requestId = requestId;
        order.playerNetworkId = playerId;
        order.playerSpawnGeneration = player->spawnGeneration;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(order, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return requestId;
    }

    void PoliceService::SetEnabled(bool enabled) {
        _enabled = enabled;
        SendSwitch();
    }

    bool PoliceService::SendOrder(uint64_t playerId, bool clear, uint8_t level) {
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        const auto *player = _players ? _players->FindByNetworkId(playerId) : nullptr;
        if (!host || !player || !player->spawned || player->missionGeneration != _missionGeneration) {
            return false;
        }
        // Lowering or clearing a record calls off every officer on the player
        // (NpcHost::BeforePoliceTick); the log names who ended those cases.
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Script order for player {}: {}", playerId, clear ? std::string("clear the record") : "wanted level " + std::to_string(level));
        Shared::Npc::PoliceOrder order;
        order.missionGeneration = _missionGeneration;
        order.playerNetworkId = playerId;
        order.playerSpawnGeneration = player->spawnGeneration;
        order.action = clear ? Shared::Npc::PoliceOrder::Action::Clear : Shared::Npc::PoliceOrder::Action::SetWanted;
        order.level = level;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(order, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    void PoliceService::SendOfficers() const {
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        if (!host) {
            return;
        }
        Shared::Npc::PoliceOfficers officers;
        officers.playerNetworkIds.assign(_officers.begin(), _officers.end());
        Framework::CoreModules::GetNetworkPeer()->SendRPC(officers, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void PoliceService::SendSwitch() const {
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        if (!host) {
            return;
        }
        Shared::Npc::PoliceSwitch change;
        change.enabled = _enabled;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(change, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }
} // namespace Mafia1Online::Features::Npc
