#include "taxi_service.h"

#include "features/car/car_service.h"
#include "features/npc/npc_service.h"
#include "features/pedestrian/pedestrian_service.h"
#include "features/player/player_service.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/pedestrian/pedestrian_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/taxi/taxi_protocol.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace Mafia1Online::Features::Taxi {
    namespace {
        using Shared::Entities::NpcEntity;

        float FlatDistance(const glm::vec3 &a, const glm::vec3 &b) {
            return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
        }

        // Walker pools open their models by file name as the scene names
        // them; a scripted NPC takes the lower-case ".i3d" Npc.spawn does.
        std::string NpcModel(std::string model) {
            if (model.size() > 4) {
                std::transform(model.end() - 4, model.end(), model.end() - 4, [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            }
            return model;
        }
    } // namespace

    const char *TaxiService::ExitReasonName(ExitReason reason) {
        switch (reason) {
        case ExitReason::DropOff: return "dropOff";
        case ExitReason::DriverLeft: return "driverLeft";
        case ExitReason::Failed: return "failed";
        case ExitReason::Died: return "died";
        case ExitReason::Gone: return "gone";
        }
        return "gone";
    }

    void TaxiService::RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Car::CarService &cars, Pedestrian::PedestrianService &pedestrians,
                                  Npc::NpcService &npcs) {
        _simulation = &simulation;
        _players = &players;
        _cars = &cars;
        _pedestrians = &pedestrians;
        _npcs = &npcs;
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Shared::Taxi::Hail>([this](const Shared::Taxi::Hail &hail, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), hail.missionGeneration) || hail.wave == Shared::Taxi::kWaveNone || !Shared::Taxi::KnownWave(hail.wave)) {
                return;
            }
            const uint64_t pedestrianId = _pedestrians->NetworkIdOfKey(hail.walkerKey);
            auto *walker = pedestrianId ? _pedestrians->Find(pedestrianId) : nullptr;
            const auto *player = _players->FindByNetworkId(hail.driverId);
            if (!walker || !walker->alive || !player || !_hailable.contains(hail.driverId) || !_cars->Find(hail.carId)) {
                // Nobody here wants it: the host ends the wave.
                Shared::Taxi::EndHail end;
                end.missionGeneration = _missionGeneration;
                end.walkerKey = hail.walkerKey;
                Framework::CoreModules::GetNetworkPeer()->SendRPC(end, MafiaNet::ToGuid(MafiaNet::ToPeerGuid(packet->guid)), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
                return;
            }
            walker->taxiWave = hail.wave;
            _hails[hail.walkerKey] = {hail.driverId, hail.carId, pedestrianId, false, 0};
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Pedestrian {} hails player {}'s taxi {}", pedestrianId, hail.driverId, hail.carId);
            if (_events.hail) {
                _events.hail(hail.driverId, pedestrianId, hail.carId);
            }
        });
        network->RegisterRPC<Shared::Taxi::HailEnded>([this](const Shared::Taxi::HailEnded &ended, MafiaNet::Packet *packet) {
            if (FromHost(MafiaNet::ToPeerGuid(packet->guid), ended.missionGeneration)) {
                EndHail(ended.walkerKey, "ended");
            }
        });
        network->RegisterRPC<Shared::Taxi::Pickup>([this](const Shared::Taxi::Pickup &pickup, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), pickup.missionGeneration)) {
                return;
            }
            const auto hail = _hails.find(pickup.walkerKey);
            if (hail == _hails.end() || hail->second.playerId != pickup.driverId || hail->second.pickedUp || hail->second.passengerId) {
                return;
            }
            hail->second.pickedUp = true;
            hail->second.carId = pickup.carId;
            if (_events.pickup) {
                _events.pickup(pickup.driverId, hail->second.pedestrianId, pickup.carId);
            }
        });
    }

    bool TaxiService::FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const {
        return _simulation->Host() == sender && missionGeneration == _missionGeneration;
    }

    bool TaxiService::Available() const {
        return _simulation && _simulation->Host().has_value() && _missionGeneration != 0;
    }

    void TaxiService::SetMissionGeneration(uint64_t generation) {
        if (generation != _missionGeneration) {
            Clear();
            _missionGeneration = generation;
        }
    }

    void TaxiService::Clear() {
        // The walkers and NPCs of the old world go with it.
        const auto rides = std::move(_rides);
        _rides.clear();
        _hails.clear();
        for (const auto &[npcId, ride] : rides) {
            if (ride.phase != Phase::Released && _events.exit) {
                _events.exit(ride.playerId, npcId, ride.carId, ExitReason::Gone, ride.boarded);
            }
        }
    }

    void TaxiService::SendDrivers() const {
        if (!_host) {
            return;
        }
        Shared::Taxi::Drivers drivers;
        drivers.playerNetworkIds.assign(_hailable.begin(), _hailable.end());
        Framework::CoreModules::GetNetworkPeer()->SendRPC(drivers, MafiaNet::ToGuid(*_host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    bool TaxiService::SetHailable(uint64_t playerId, bool hailable) {
        if (!_players->FindByNetworkId(playerId)) {
            return false;
        }
        const bool changed = hailable ? _hailable.insert(playerId).second : _hailable.erase(playerId) > 0;
        if (changed) {
            SendDrivers();
        }
        if (!hailable) {
            std::vector<uint64_t> waving;
            for (const auto &[key, hail] : _hails) {
                if (hail.playerId == playerId && !hail.passengerId) {
                    waving.push_back(hail.pedestrianId);
                }
            }
            for (const uint64_t pedestrianId : waving) {
                Decline(pedestrianId);
            }
        }
        return true;
    }

    std::optional<TaxiService::HailInfo> TaxiService::FindHail(uint64_t pedestrianId) const {
        for (const auto &[key, hail] : _hails) {
            if (hail.pedestrianId == pedestrianId) {
                return HailInfo {hail.playerId, hail.carId, hail.pickedUp};
            }
        }
        return std::nullopt;
    }

    bool TaxiService::Decline(uint64_t pedestrianId) {
        for (const auto &[key, hail] : _hails) {
            if (hail.pedestrianId != pedestrianId || hail.passengerId || !_host) {
                continue;
            }
            Shared::Taxi::EndHail end;
            end.missionGeneration = _missionGeneration;
            end.walkerKey = key;
            Framework::CoreModules::GetNetworkPeer()->SendRPC(end, MafiaNet::ToGuid(*_host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            return true;
        }
        return false;
    }

    void TaxiService::EndHail(uint32_t key, const char *reason) {
        const auto found = _hails.find(key);
        if (found == _hails.end()) {
            return;
        }
        const Hail hail = found->second;
        _hails.erase(found);
        if (auto *walker = _pedestrians->Find(hail.pedestrianId)) {
            walker->taxiWave = Shared::Taxi::kWaveNone;
        }
        if (_events.hailEnd) {
            _events.hailEnd(hail.playerId, hail.pedestrianId, hail.passengerId ? "boarded" : reason);
        }
    }

    bool TaxiService::Drives(uint64_t playerId, uint64_t carId) const {
        const auto *player = _players->FindByNetworkId(playerId);
        if (!player || !player->spawned || !player->alive) {
            return false;
        }
        const auto seat = _cars->SeatForPlayer(playerId, player->spawnGeneration);
        return seat && seat->carId == carId && seat->seat == 0;
    }

    uint64_t TaxiService::Board(uint64_t playerId, uint64_t pedestrianId, std::optional<uint8_t> seat, std::string owner) {
        const auto hail = std::find_if(_hails.begin(), _hails.end(), [&](const auto &entry) { return entry.second.pedestrianId == pedestrianId; });
        const auto *player = _players->FindByNetworkId(playerId);
        const auto location = player ? _cars->SeatForPlayer(playerId, player->spawnGeneration) : std::nullopt;
        auto *car = location ? _cars->Find(location->carId) : nullptr;
        auto *walker = _pedestrians->Find(pedestrianId);
        if (hail == _hails.end() || hail->second.playerId != playerId || hail->second.passengerId || PassengerOf(playerId) || !location || location->seat != 0 || !car ||
            car->terminalState != Shared::Entities::CarEntity::TerminalState::Active || !walker || !walker->alive) {
            return 0;
        }
        // taxi_pas.dat: "car_getseatcount 1, 0; if flt[0] > 3" boards seat
        // 3, else seat 1.
        const auto free = [&](uint8_t candidate) { return candidate > 0 && candidate < car->seatCount && candidate < Shared::Entities::CarEntity::kMaxSeats && car->occupantIds[candidate] == 0; };
        uint8_t chosen = seat.value_or(car->seatCount > 3 ? 3 : 1);
        if (!free(chosen)) {
            if (seat) {
                return 0;
            }
            chosen = 0;
            for (uint8_t candidate = 1; candidate < car->seatCount && !chosen; ++candidate) {
                chosen = free(candidate) ? candidate : 0;
            }
            if (!chosen) {
                return 0;
            }
        }
        Npc::NpcService::SpawnRequest request;
        request.model = NpcModel(walker->model);
        request.position = walker->position;
        request.heading = 2.0f * std::atan2(walker->rotation.y, walker->rotation.w);
        request.walkerKey = hail->first;
        request.walkerId = pedestrianId;
        request.virtualWorld = walker->GetVirtualWorld();
        request.owner = std::move(owner);
        auto *npc = _npcs->Spawn(request);
        if (!npc) {
            return 0;
        }
        const uint64_t npcId = npc->GetNetworkID();
        Shared::Npc::Task task;
        task.type = Shared::Npc::TaskType::EnterVehicle;
        task.targetId = location->carId;
        task.seat = chosen;
        if (!_npcs->GiveTask(npcId, task)) {
            _npcs->Remove(npcId);
            return 0;
        }
        hail->second.passengerId = npcId;
        auto &ride = _rides[npcId];
        ride.playerId = playerId;
        ride.carId = location->carId;
        ride.seat = chosen;
        ride.phase = Phase::Boarding;
        ride.phaseAt = std::chrono::steady_clock::now();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Pedestrian {} boards player {}'s taxi {} as NPC {} (seat {})", pedestrianId, playerId, location->carId, npcId, chosen);
        return npcId;
    }

    bool TaxiService::SetDestination(uint64_t npcId, std::optional<glm::vec3> destination, float radius) {
        const auto ride = _rides.find(npcId);
        if (ride == _rides.end() || ride->second.phase == Phase::Leaving || ride->second.phase == Phase::Released || !(radius > 0.0f)) {
            return false;
        }
        ride->second.destination = destination;
        ride->second.radius = radius;
        ride->second.arrived = false;
        return true;
    }

    bool TaxiService::DropOff(uint64_t npcId) {
        const auto ride = _rides.find(npcId);
        if (ride == _rides.end() || ride->second.phase == Phase::Leaving || ride->second.phase == Phase::Released) {
            return false;
        }
        Leave(npcId, ride->second, ExitReason::DropOff);
        return true;
    }

    uint64_t TaxiService::PassengerOf(uint64_t playerId) const {
        for (const auto &[npcId, ride] : _rides) {
            if (ride.playerId == playerId && (ride.phase == Phase::Boarding || ride.phase == Phase::Riding)) {
                return npcId;
            }
        }
        return 0;
    }

    std::optional<TaxiService::RideInfo> TaxiService::FindRide(uint64_t npcId) const {
        const auto ride = _rides.find(npcId);
        if (ride == _rides.end()) {
            return std::nullopt;
        }
        constexpr const char *kPhases[] {"boarding", "riding", "leaving", "released"};
        const auto &r = ride->second;
        return RideInfo {r.playerId, r.carId, r.seat, kPhases[static_cast<uint8_t>(r.phase)], r.destination, r.radius, r.arrived};
    }

    void TaxiService::Leave(uint64_t npcId, Ride &ride, ExitReason reason) {
        ride.reason = reason;
        ride.phaseAt = std::chrono::steady_clock::now();
        const auto seated = _cars->SeatForPlayer(npcId, NpcEntity::kSpawnGeneration);
        if (seated) {
            // taxi_pas.dat's vystup: the cab stops, then enemy_usecar takes
            // the passenger out (the host waits for the car to be nearly still).
            Shared::Npc::Task task;
            task.type = Shared::Npc::TaskType::ExitVehicle;
            _npcs->GiveTask(npcId, task);
            ride.phase = Phase::Leaving;
            return;
        }
        // Still on its way to the door.
        Shared::Npc::Task stop;
        stop.type = Shared::Npc::TaskType::Stop;
        _npcs->GiveTask(npcId, stop);
        Exited(npcId, ride);
    }

    void TaxiService::Exited(uint64_t npcId, Ride &ride) {
        ride.phase = Phase::Released;
        ride.phaseAt = std::chrono::steady_clock::now();
        // Away from the car, as the retail passenger walks off after
        // human_returntotraff.
        if (const auto *npc = _npcs->Find(npcId); npc && npc->alive) {
            const auto *car = _cars->Find(ride.carId);
            glm::vec3 away = car ? npc->position - car->position : glm::vec3(1.0f, 0.0f, 0.0f);
            away.y = 0.0f;
            away = glm::length(away) > 0.01f ? glm::normalize(away) : glm::vec3(1.0f, 0.0f, 0.0f);
            Shared::Npc::Task walk;
            walk.type = Shared::Npc::TaskType::WalkTo;
            walk.distance = 1.0f;
            const glm::vec3 target = npc->position + away * kWalkAway;
            walk.x = target.x;
            walk.y = target.y;
            walk.z = target.z;
            _npcs->GiveTask(npcId, walk);
        }
        if (_events.exit) {
            _events.exit(ride.playerId, npcId, ride.carId, ride.reason, ride.boarded);
        }
    }

    void TaxiService::UpdateRide(uint64_t npcId, Ride &ride, std::chrono::steady_clock::time_point now) {
        const auto *npc = _npcs->Find(npcId);
        if (ride.phase == Phase::Released) {
            return;
        }
        if (!npc || !npc->alive) {
            ride.reason = npc ? ExitReason::Died : ExitReason::Gone;
            ride.phase = Phase::Released;
            ride.phaseAt = now;
            if (_events.exit) {
                _events.exit(ride.playerId, npcId, ride.carId, ride.reason, ride.boarded);
            }
            return;
        }
        const auto seated = _cars->SeatForPlayer(npcId, NpcEntity::kSpawnGeneration);
        const bool inCab = seated && seated->carId == ride.carId;
        switch (ride.phase) {
        case Phase::Boarding:
            if (inCab) {
                ride.phase = Phase::Riding;
                ride.boarded = true;
                ride.phaseAt = now;
                if (_events.enter) {
                    _events.enter(ride.playerId, npcId, ride.carId);
                }
                return;
            }
            if (!Drives(ride.playerId, ride.carId)) {
                Leave(npcId, ride, ExitReason::DriverLeft);
                return;
            }
            if (now - ride.phaseAt > kBoardTimeout || (!_npcs->GetTask(npcId) && !seated)) {
                Leave(npcId, ride, ExitReason::Failed);
            }
            return;
        case Phase::Riding: {
            if (!inCab) {
                ride.reason = ExitReason::Gone;
                Exited(npcId, ride);
                return;
            }
            // taxi_pas.dat's compareownerwithex: the passenger gets out once
            // the player no longer owns the cab.
            if (!Drives(ride.playerId, ride.carId)) {
                Leave(npcId, ride, ExitReason::DriverLeft);
                return;
            }
            const auto *car = _cars->Find(ride.carId);
            if (car && ride.destination && !ride.arrived && glm::length(car->velocity) <= kArriveSpeed && FlatDistance(car->position, *ride.destination) <= ride.radius) {
                ride.arrived = true;
                if (_events.arrive) {
                    _events.arrive(ride.playerId, npcId, ride.carId);
                }
            }
            return;
        }
        case Phase::Leaving: {
            // The seat clears as the exit animation starts; the host ends the
            // exitVehicle task once it is over. A walk given before then
            // would cut the animation short.
            const auto task = _npcs->GetTask(npcId);
            const bool exiting = task && task->type == Shared::Npc::TaskType::ExitVehicle;
            if (!seated && !exiting) {
                Exited(npcId, ride);
            }
            else if (now - ride.phaseAt > kExitTimeout) {
                // Once per timeout: the host answers with Retired.
                _npcs->Remove(npcId);
                ride.phaseAt = now;
            }
            return;
        }
        case Phase::Released: return;
        }
    }

    void TaxiService::Update() {
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        if (host != _host) {
            _host = host;
            // The former host's walkers and NPCs are gone with it.
            std::vector<uint32_t> keys;
            for (const auto &[key, hail] : _hails) {
                keys.push_back(key);
            }
            for (const uint32_t key : keys) {
                EndHail(key, "gone");
            }
            SendDrivers();
        }
        bool driversChanged = false;
        for (auto it = _hailable.begin(); it != _hailable.end();) {
            if (_players->FindByNetworkId(*it)) {
                ++it;
                continue;
            }
            it = _hailable.erase(it);
            driversChanged = true;
        }
        if (driversChanged) {
            SendDrivers();
        }
        // A walker that retired or died without its wave ending.
        std::vector<uint32_t> gone;
        for (const auto &[key, hail] : _hails) {
            const auto *walker = _pedestrians->Find(hail.pedestrianId);
            if (!walker || !walker->alive || !_players->FindByNetworkId(hail.playerId)) {
                gone.push_back(key);
            }
        }
        for (const uint32_t key : gone) {
            EndHail(key, "gone");
        }

        const auto now = std::chrono::steady_clock::now();
        std::vector<uint64_t> ids;
        for (const auto &[npcId, ride] : _rides) {
            ids.push_back(npcId);
        }
        for (const uint64_t npcId : ids) {
            auto found = _rides.find(npcId);
            if (found == _rides.end()) {
                continue;
            }
            auto &ride = found->second;
            UpdateRide(npcId, ride, now);
            if (ride.phase != Phase::Released) {
                continue;
            }
            const auto *npc = _npcs->Find(npcId);
            if (!npc) {
                _rides.erase(npcId);
                continue;
            }
            // Gone from sight: no player close by, or long enough.
            bool watched = false;
            _players->ForEach([&](Shared::Entities::PlayerEntity *player) {
                watched = watched || (player->spawned && glm::length(player->position - npc->position) < kReleaseRange);
            });
            if ((!watched && now - ride.phaseAt > std::chrono::seconds(3)) || now - ride.phaseAt > kReleaseLifetime || !npc->alive) {
                if (npc->alive) {
                    _npcs->Remove(npcId);
                }
                _rides.erase(npcId);
            }
        }
    }
} // namespace Mafia1Online::Features::Taxi
