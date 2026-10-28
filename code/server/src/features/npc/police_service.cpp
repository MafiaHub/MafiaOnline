#include "police_service.h"

#include "features/player/player_service.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/npc/npc_report.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <vector>

namespace Mafia1Online::Features::Npc {
    void PoliceService::RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players) {
        _simulation = &simulation;
        _players = &players;
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Npc::PoliceRecord>([this](const Shared::Npc::PoliceRecord &report, MafiaNet::Packet *packet) {
            if (_simulation->Host() != MafiaNet::ToPeerGuid(packet->guid) || report.missionGeneration != _missionGeneration || report.level > kLevelLethal) {
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
    }

    void PoliceService::SetMissionGeneration(uint64_t generation) {
        _missionGeneration = generation;
        _records.clear();
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
            if (_host) {
                SendSwitch();
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
        return SendOrder(playerId, true, kLevelNone);
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
        Shared::Npc::PoliceOrder order;
        order.missionGeneration = _missionGeneration;
        order.playerNetworkId = playerId;
        order.playerSpawnGeneration = player->spawnGeneration;
        order.action = clear ? Shared::Npc::PoliceOrder::Action::Clear : Shared::Npc::PoliceOrder::Action::SetWanted;
        order.level = level;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(order, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
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
