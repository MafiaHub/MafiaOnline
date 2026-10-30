#include "traffic_service.h"

#include "features/car/car_service.h"
#include "features/player/player_service.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/car/car_movement.h"
#include "shared/features/car/seat_action.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/npc/npc_report.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/traffic/road_barrier_entity.h"
#include "shared/features/traffic/traffic_report.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <limits>

namespace Mafia1Online::Features::Traffic {
    namespace {
        using Shared::Entities::CarEntity;
        using CrewState = CarEntity::CrewState;

        bool Finite(float x, float y, float z) {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
        }

        // C_traffic_car::GameInit's radar blips: TRAFFIC_CAR_POLICE/MAFIA/
        // CIVIL_RADAR_COLOR.
        uint32_t RadarColor(Shared::Traffic::Kind kind) {
            switch (kind) {
            case Shared::Traffic::Kind::Police: return 0xff009fffu;
            case Shared::Traffic::Kind::Mafia: return 0xffff9f00u;
            default: return 0xffb0e0b0u;
            }
        }

        // Retail crew models are plain file names ("pol01.i3d"; the host
        // strips the directory, ModelName), which every player's client
        // opens: printable, with no path separator, drive, wildcard or
        // parent part.
        bool CrewModel(const std::string &model) {
            if (model.size() > CarEntity::kMaxCrewModelLength || model.find("..") != std::string::npos) {
                return false;
            }
            return std::all_of(model.begin(), model.end(), [](char c) {
                return c >= 0x20 && c <= 0x7e && std::string_view("\\/:*?\"<>|").find(c) == std::string_view::npos;
            });
        }
    } // namespace

    void TrafficService::RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Car::CarService &cars) {
        _simulation = &simulation;
        _players = &players;
        _cars = &cars;
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Shared::Traffic::Spawned>([this](const Shared::Traffic::Spawned &spawned, MafiaNet::Packet *packet) {
            const auto sender = MafiaNet::ToPeerGuid(packet->guid);
            const glm::quat rotation(spawned.qw, spawned.qx, spawned.qy, spawned.qz);
            if (!FromHost(sender, spawned.missionGeneration) || spawned.key == 0 || _records.contains(spawned.key) || LiveRecords() >= kMaxCars ||
                spawned.kind == Shared::Traffic::Kind::None || !CrewModel(spawned.driverModel) || !CrewModel(spawned.passengerModel) ||
                spawned.seatCount == 0 || spawned.seatCount > CarEntity::kMaxSeats || !Finite(spawned.x, spawned.y, spawned.z) ||
                !Finite(spawned.vx, spawned.vy, spawned.vz) || !std::isfinite(glm::length(rotation)) || glm::length(rotation) < 0.5f) {
                return;
            }
            auto *car = _cars->Spawn(spawned.model, {spawned.x, spawned.y, spawned.z}, rotation, spawned.missionGeneration, static_cast<uint64_t>(sender));
            if (!car) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Simulation host reported traffic car {} with unusable model '{}'", spawned.key, spawned.model);
                return;
            }
            // Players see a traffic car only near them; the host is the
            // controller and its viewer reaches the whole map.
            car->streaming.alwaysVisible = false;
            car->streaming.range = kStreamRange;
            car->velocity = {spawned.vx, spawned.vy, spawned.vz};
            car->seatCount = spawned.seatCount;
            car->trafficKey = spawned.key;
            car->trafficKind = spawned.kind;
            car->driverModel = spawned.driverModel;
            car->passengerModel = spawned.passengerModel;
            car->hostDriven = true;
            car->driverState = spawned.driverModel.empty() ? CrewState::None : spawned.driverState;
            car->passengerState = spawned.passengerModel.empty() ? CrewState::None : spawned.passengerState;
            // Traffic always runs its engine; the host reports when it stops.
            car->engineOn = true;
            ++car->engineRevision;
            car->lightState = spawned.lightState & Shared::Car::Movement::kReplicatedLightMask;
            car->sirenOn = spawned.sirenOn;
            car->radarColor = RadarColor(spawned.kind);
            auto &record = _records[spawned.key];
            record.key = spawned.key;
            record.networkId = car->GetNetworkID();
            _keyByCar[record.networkId] = spawned.key;
        });
        network->RegisterRPC<Shared::Traffic::Retired>([this](const Shared::Traffic::Retired &retired, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), retired.missionGeneration)) {
                return;
            }
            const auto record = _records.find(retired.key);
            if (record == _records.end()) {
                return;
            }
            auto *car = _cars->Find(record->second.networkId);
            if (car && car->terminalState == CarEntity::TerminalState::Exploded) {
                // The host's car is gone; the wreck stays a while for the
                // players still applying the explosion.
                if (record->second.despawnAt == std::chrono::steady_clock::time_point {}) {
                    record->second.despawnAt = std::chrono::steady_clock::now() + kWreckLifetime;
                }
                return;
            }
            Despawn(retired.key);
        });
        network->RegisterRPC<Shared::Traffic::Crew>([this](const Shared::Traffic::Crew &crew, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), crew.missionGeneration)) {
                return;
            }
            auto *car = HostCar(crew.key);
            if (!car || !CrewModel(crew.driverModel) || !CrewModel(crew.passengerModel)) {
                return;
            }
            car->driverModel = crew.driverModel;
            car->passengerModel = crew.passengerModel;
            car->driverState = car->driverModel.empty() ? CrewState::None : crew.driverState;
            car->passengerState = car->passengerModel.empty() ? CrewState::None : crew.passengerState;
            if (car->sirenOn != crew.sirenOn) {
                _cars->SetSiren(car->GetNetworkID(), crew.sirenOn);
            }
        });
        network->RegisterRPC<Shared::Traffic::BarrierPlaced>([this](const Shared::Traffic::BarrierPlaced &placed, MafiaNet::Packet *packet) {
            const glm::quat rotation(placed.qw, placed.qx, placed.qy, placed.qz);
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), placed.missionGeneration) || placed.key == 0 || _barriers.contains(placed.key) ||
                _barriers.size() >= kMaxBarriers || !Finite(placed.x, placed.y, placed.z) || !std::isfinite(glm::length(rotation)) || glm::length(rotation) < 0.5f) {
                return;
            }
            auto *barrier = Framework::CoreModules::GetReplication()->CreateEntity<Shared::Entities::RoadBarrierEntity>();
            barrier->missionGeneration = placed.missionGeneration;
            barrier->placedPosition = {placed.x, placed.y, placed.z};
            barrier->placedRotation = glm::normalize(rotation);
            barrier->position = barrier->placedPosition;
            barrier->rotation = barrier->placedRotation;
            barrier->streaming.range = kStreamRange;
            barrier->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
            _barriers[placed.key] = barrier->GetNetworkID();
        });
        network->RegisterRPC<Shared::Traffic::BarrierRemoved>([this](const Shared::Traffic::BarrierRemoved &removed, MafiaNet::Packet *packet) {
            if (!FromHost(MafiaNet::ToPeerGuid(packet->guid), removed.missionGeneration)) {
                return;
            }
            const auto barrier = _barriers.find(removed.key);
            if (barrier == _barriers.end()) {
                return;
            }
            auto *replication = Framework::CoreModules::GetReplication();
            if (auto *entity = replication->GetEntity<Shared::Entities::RoadBarrierEntity>(barrier->second)) {
                replication->DestroyEntity(entity);
            }
            _barriers.erase(barrier);
        });
        network->RegisterRPC<Shared::Traffic::StealIntent>([this](const Shared::Traffic::StealIntent &intent, MafiaNet::Packet *packet) {
            auto *player = _players->FindByGuid(MafiaNet::ToPeerGuid(packet->guid));
            if (!player) {
                return;
            }
            // Every intent of a known player gets an answer: its client hid
            // the driver model and holds the car until one comes. The
            // refusal carries the intent's generation, which the thief's
            // client checks against its own mission.
            const auto refuse = [&](bool notify) {
                Shared::Traffic::StealRefused refused;
                refused.missionGeneration = intent.missionGeneration;
                refused.carId = intent.carId;
                Framework::CoreModules::GetNetworkPeer()->SendRPC(refused, packet->guid, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
                if (notify && _events.stealRefused && _cars->Find(intent.carId)) {
                    _events.stealRefused(intent.carId, player->GetNetworkID());
                }
            };
            const auto now = std::chrono::steady_clock::now();
            auto &last = _lastStealIntent[player->GetNetworkID()];
            const bool flooding = last != std::chrono::steady_clock::time_point {} && now - last < kStealIntentInterval;
            last = now;
            if (flooding || player->GetNetworkID() != intent.playerId || intent.missionGeneration != _missionGeneration) {
                refuse(false);
                return;
            }
            auto *car = _cars->Find(intent.carId);
            if (!car || !player->spawned || !player->alive || player->spawnGeneration != intent.spawnGeneration ||
                car->missionGeneration != _missionGeneration || !car->hostDriven || !_keyByCar.contains(intent.carId) ||
                // Only from on foot: a steal hands the car's physics over.
                _cars->SeatForPlayer(player->GetNetworkID(), player->spawnGeneration) ||
                car->trafficKind == Shared::Traffic::Kind::Police || car->terminalState != CarEntity::TerminalState::Active ||
                (car->driverState != CrewState::Seated && car->driverState != CrewState::Dead) || HasNpcOccupant(*car) || glm::distance(player->position, car->position) > kStealReach ||
                !(glm::length(car->velocity) <= kStealMaxSpeed) || !_simulation->Host()) {
                // The thief played the pull-out already; give it the car's
                // driver back and the next use a fresh try.
                refuse(true);
                return;
            }
            Takeover(*car, *player, true);
        });
    }

    bool TrafficService::FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const {
        return _simulation && _simulation->Host() == sender && missionGeneration == _missionGeneration;
    }

    CarEntity *TrafficService::HostCar(uint32_t key) const {
        const auto record = _records.find(key);
        if (record == _records.end()) {
            return nullptr;
        }
        auto *car = _cars->Find(record->second.networkId);
        return car && car->hostDriven ? car : nullptr;
    }

    size_t TrafficService::LiveRecords() const {
        return static_cast<size_t>(std::count_if(_records.begin(), _records.end(), [](const auto &entry) {
            return entry.second.despawnAt == std::chrono::steady_clock::time_point {};
        }));
    }

    bool TrafficService::NpcInSeat(uint64_t carId, uint8_t seat) const {
        for (const auto &[npc, where] : _npcSeats) {
            if (where.carId == carId && where.seat == seat) {
                return true;
            }
        }
        return false;
    }

    void TrafficService::OnNpcThrown(uint64_t carId, uint8_t seat) {
        // The car service already took it out of the seat; the host's own
        // seat report that follows its native throw finds nothing to change.
        std::erase_if(_npcSeats, [&](const auto &entry) { return entry.second.carId == carId && entry.second.seat == seat; });
    }

    bool TrafficService::HasNpcOccupant(CarEntity &car) const {
        for (const auto &[npc, seat] : _npcSeats) {
            if (seat.carId == car.GetNetworkID()) {
                return true;
            }
        }
        return false;
    }

    void TrafficService::Takeover(CarEntity &car, Shared::Entities::PlayerEntity &player, bool steal) {
        const auto key = _keyByCar.find(car.GetNetworkID());
        if (key == _keyByCar.end()) {
            return;
        }
        const uint32_t hostKey = key->second;
        _records.erase(hostKey);
        _keyByCar.erase(key);
        _released[car.GetNetworkID()] = {};
        car.hostDriven = false;
        // The host throws the traffic crew out (steal) and swaps the pool car
        // for an ordinary one, which has no crew models.
        if (car.driverState != CrewState::None) {
            car.driverState = CrewState::Gone;
        }
        if (car.passengerState != CrewState::None) {
            car.passengerState = CrewState::Gone;
        }
        _cars->SetController(car.GetNetworkID(), player.controllerGuid);
        // An ordinary car of a player now: no traffic blip.
        _cars->SetRadarColor(car.GetNetworkID(), 0);
        Shared::Traffic::Takeover takeover;
        takeover.missionGeneration = _missionGeneration;
        takeover.key = hostKey;
        takeover.playerId = player.GetNetworkID();
        takeover.playerSpawnGeneration = player.spawnGeneration;
        takeover.steal = steal;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(takeover, MafiaNet::ToGuid(*_simulation->Host()), MafiaNet::Priority::High,
                                                          MafiaNet::Reliability::ReliableOrdered);
        if (steal) {
            // Other players replay the thief's pull-out (Do_ThrowCocotFromCar);
            // the thief started it already, the host plays it with the throw.
            Shared::Car::SeatEvent event;
            event.carId = car.GetNetworkID();
            event.playerId = player.GetNetworkID();
            event.spawnGeneration = player.spawnGeneration;
            event.missionGeneration = _missionGeneration;
            event.seat = 0;
            event.action = Shared::Car::SeatAction::StealTraffic;
            event.serverSequence = _cars->NextSeatEventSequence();
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(event);
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} {} traffic car {}", player.GetNetworkID(), steal ? "stole" : "took", car.GetNetworkID());
        if (_events.taken) {
            _events.taken(car.GetNetworkID(), player.GetNetworkID(), steal);
        }
    }

    TrafficService::SeatGate TrafficService::CheckSeatIntent(const Shared::Car::SeatIntent &intent, Shared::Entities::PlayerEntity &player) {
        using Action = Shared::Car::SeatAction;
        auto *car = _cars->Find(intent.carId);
        if (!car || !car->hostDriven) {
            return SeatGate::Allow;
        }
        // Nobody but the host's NPCs sits in a car the host drives.
        if (intent.action != Action::Enter && intent.action != Action::EnterBegin && intent.action != Action::StealBegin && intent.action != Action::Steal) {
            return SeatGate::Reject;
        }
        // Retail's occupied-seat use throws a seated human straight out
        // (C_player::Do_AB_OwnerNULL 2009 -> Do_ThrowCocotFromCar), offered
        // only while the car stands nearly still. A player may pull the
        // host's NPC out of any seat but a police car's, which stays closed
        // as SetPlayerUseDisabled keeps it in retail.
        if (intent.action == Action::StealBegin && intent.seat < car->seatCount && NpcInSeat(intent.carId, intent.seat)) {
            if (!_keyByCar.contains(intent.carId) || car->trafficKind == Shared::Traffic::Kind::Police || car->terminalState != CarEntity::TerminalState::Active ||
                !_simulation->Host() || !player.spawned || !player.alive || glm::distance(player.position, car->position) > kStealReach ||
                !(glm::length(car->velocity) <= kStealMaxSpeed)) {
                return SeatGate::Reject;
            }
            return SeatGate::NpcThrow;
        }
        if (!_keyByCar.contains(intent.carId) || car->trafficKind == Shared::Traffic::Kind::Police || HasNpcOccupant(*car) || intent.seat >= car->seatCount ||
            car->terminalState != CarEntity::TerminalState::Active || !_simulation->Host() || !player.spawned || !player.alive ||
            glm::distance(player.position, car->position) > kStealReach) {
            return SeatGate::Reject;
        }
        // A seated traffic crew member is taken out only by a steal.
        const auto in = [](CrewState state) { return state == CrewState::Seated || state == CrewState::Dead; };
        if (in(car->driverState) || (intent.seat == 1 && in(car->passengerState))) {
            return SeatGate::Reject;
        }
        return SeatGate::Takeover;
    }

    void TrafficService::CompleteTakeover(uint64_t carId, Shared::Entities::PlayerEntity &player) {
        if (auto *car = _cars->Find(carId); car && car->hostDriven && _keyByCar.contains(carId) && _simulation->Host()) {
            Takeover(*car, player, false);
        }
    }

    bool TrafficService::ApplyNpcSeat(uint64_t npcId, const Shared::Npc::Seat &seat, Shared::Car::SeatEvent &event) {
        using Result = CarEntity::SeatResult;
        const auto previous = _npcSeats.find(npcId);
        if (seat.seat == Shared::Npc::Seat::kNoSeat) {
            if (previous == _npcSeats.end()) {
                // The host tracked it as it left (a mafia crew Krles seated):
                // players replay the exit from the seat it names, and the
                // occupant list, which never held it, stays as it is.
                const auto *car = seat.fromSeat != Shared::Npc::Seat::kNoSeat ? _cars->Find(seat.fromCarId) : nullptr;
                if (!seat.animated || !car || !car->hostDriven || car->missionGeneration != _missionGeneration || seat.fromSeat >= car->seatCount) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("NPC {} left a seat the server never gave it", npcId);
                    return false;
                }
                event.carId = seat.fromCarId;
                event.seat = seat.fromSeat;
            }
            else {
                const auto where = previous->second;
                _npcSeats.erase(previous);
                auto *car = _cars->Find(where.carId);
                if (!car || !_cars->RecordSeatOutcome(where.carId, where.seat, npcId, Shared::Entities::NpcEntity::kSpawnGeneration, Result::Exited)) {
                    return false;
                }
                event.carId = where.carId;
                event.seat = where.seat;
            }
            event.action = Shared::Car::SeatAction::Exit;
        }
        else {
            auto *car = _cars->Find(seat.carId);
            // A passenger seat of a car a player drives counts as well: a
            // scripted NPC rides with the player (a taxi fare, a patient).
            const bool playerDriven = car && seat.seat != 0 && car->occupantIds[0] != 0 && _players->FindByNetworkId(car->occupantIds[0]);
            if (!car || !(car->hostDriven || playerDriven) || car->missionGeneration != _missionGeneration || seat.seat >= car->seatCount ||
                car->terminalState != CarEntity::TerminalState::Active ||
                (car->occupantIds[seat.seat] != 0 && car->occupantIds[seat.seat] != npcId)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("NPC {} seat in car {} seat {} refused (car {}, host driven {})", npcId, seat.carId, seat.seat,
                                                                            car ? "found" : "missing", car && car->hostDriven);
                return false;
            }
            if (previous != _npcSeats.end() && previous->second.carId == seat.carId && previous->second.seat == seat.seat) {
                return false;
            }
            // A gangster or policeman climbing across to shoot out of the
            // other window (reM C_entity fire action, Do_ClimbInCarLR):
            // players replay the climb from the seat outcome.
            if (previous != _npcSeats.end() && previous->second.carId == seat.carId && previous->second.seat == (seat.seat ^ 1)) {
                if (_cars->MoveSeat(seat.carId, seat.seat, npcId, Shared::Entities::NpcEntity::kSpawnGeneration)) {
                    previous->second.seat = seat.seat;
                    return false;
                }
            }
            if (previous != _npcSeats.end()) {
                _cars->RecordSeatOutcome(previous->second.carId, previous->second.seat, npcId, Shared::Entities::NpcEntity::kSpawnGeneration, Result::Exited);
            }
            if (!_cars->RecordSeatOutcome(seat.carId, seat.seat, npcId, Shared::Entities::NpcEntity::kSpawnGeneration, Result::Entered)) {
                _npcSeats.erase(npcId);
                return false;
            }
            _npcSeats[npcId] = {seat.carId, seat.seat};
            event.carId = seat.carId;
            event.seat = seat.seat;
            event.action = Shared::Car::SeatAction::Enter;
        }
        event.playerId = npcId;
        event.spawnGeneration = Shared::Entities::NpcEntity::kSpawnGeneration;
        event.missionGeneration = _missionGeneration;
        event.serverSequence = _cars->NextSeatEventSequence();
        // Only an animated change plays on players; an instant one is placed
        // by their seat reconciliation.
        return seat.animated;
    }

    void TrafficService::OnNpcGone(uint64_t npcId) {
        const auto seat = _npcSeats.find(npcId);
        if (seat == _npcSeats.end()) {
            return;
        }
        _cars->ClearOccupant(npcId, Shared::Entities::NpcEntity::kSpawnGeneration);
        _npcSeats.erase(seat);
    }

    void TrafficService::Despawn(uint32_t key) {
        const auto record = _records.find(key);
        if (record == _records.end()) {
            return;
        }
        const uint64_t networkId = record->second.networkId;
        for (auto it = _npcSeats.begin(); it != _npcSeats.end();) {
            it = it->second.carId == networkId ? _npcSeats.erase(it) : std::next(it);
        }
        _keyByCar.erase(networkId);
        _records.erase(record);
        _cars->Despawn(networkId);
    }

    void TrafficService::Update() {
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        if (!host || host != _host) {
            // Leaves only the cars players took. A new host, even one that
            // attached in the same tick the old one left, drives none of the
            // old host's cars (keys, controller) and reports its own.
            Clear();
            _host = host;
        }
        const auto now = std::chrono::steady_clock::now();
        std::vector<uint32_t> gone;
        for (auto &[key, record] : _records) {
            auto *car = _cars->Find(record.networkId);
            // A script despawned it, or its wreck has stood long enough.
            if (!car || (record.despawnAt != std::chrono::steady_clock::time_point {} && now >= record.despawnAt)) {
                gone.push_back(key);
            }
        }
        for (const uint32_t key : gone) {
            Despawn(key);
        }
        std::vector<uint64_t> abandoned;
        for (auto &[networkId, abandonedSince] : _released) {
            auto *car = _cars->Find(networkId);
            if (!car || car->missionGeneration != _missionGeneration) {
                abandoned.push_back(networkId);
                continue;
            }
            bool occupied = false;
            for (uint8_t seat = 0; seat < car->seatCount && seat < CarEntity::kMaxSeats; ++seat) {
                occupied = occupied || car->occupantIds[seat] != 0;
            }
            float nearest = std::numeric_limits<float>::max();
            _players->ForEach([&](Shared::Entities::PlayerEntity *player) {
                if (player->spawned && player->missionGeneration == _missionGeneration) {
                    nearest = std::min(nearest, glm::distance(player->position, car->position));
                }
            });
            if (occupied || nearest < kAbandonedDistance) {
                abandonedSince = {};
            }
            else if (abandonedSince == std::chrono::steady_clock::time_point {}) {
                abandonedSince = now;
            }
            else if (now - abandonedSince >= kAbandonedAfter) {
                abandoned.push_back(networkId);
            }
        }
        for (const uint64_t networkId : abandoned) {
            DespawnReleased(networkId);
        }
        if (HostCurrent() && now - _lastJamCheck >= kJamCheckInterval) {
            _lastJamCheck = now;
            WatchJams(now);
        }
    }

    std::vector<glm::vec3> TrafficService::PlayersOnFoot() const {
        std::vector<glm::vec3> positions;
        _players->ForEach([&](Shared::Entities::PlayerEntity *player) {
            if (player->spawned && player->missionGeneration == _missionGeneration && !_cars->SeatForPlayer(player->GetNetworkID(), player->spawnGeneration)) {
                positions.push_back(player->position);
            }
        });
        return positions;
    }

    Shared::Traffic::JamCar TrafficService::ViewOf(const Record &record, const CarEntity &car, std::chrono::steady_clock::time_point now,
                                                   const std::vector<glm::vec3> &onFoot) const {
        Shared::Traffic::JamCar view;
        view.id = record.key;
        view.position = car.position;
        view.stuckSeconds = record.stuck.valid ? std::chrono::duration<double>(now.time_since_epoch()).count() - record.stuck.since : 0.0;
        view.active = car.terminalState == CarEntity::TerminalState::Active && record.despawnAt == std::chrono::steady_clock::time_point {} &&
                      car.missionGeneration == _missionGeneration;
        view.police = car.trafficKind == Shared::Traffic::Kind::Police;
        const auto sort = [&](uint64_t occupant) {
            if (occupant == 0) {
                return;
            }
            if (_players->FindByNetworkId(occupant)) {
                view.playerAboard = true;
            }
            else if (_events.scriptedNpc && _events.scriptedNpc(occupant)) {
                view.scriptedCrew = true;
            }
            else {
                view.npcCrew = true;
            }
        };
        for (uint8_t seat = 0; seat < car.seatCount && seat < CarEntity::kMaxSeats; ++seat) {
            sort(car.occupantIds[seat]);
        }
        for (const auto &[npc, seat] : _npcSeats) {
            if (seat.carId == record.networkId) {
                sort(npc);
            }
        }
        view.driverAboard = car.driverState == CrewState::Seated || car.driverState == CrewState::Dead;
        view.resetPending = record.resetAskedAt != std::chrono::steady_clock::time_point {} && now - record.resetAskedAt < kResetWait;
        view.resetKept = record.resetKept;
        for (const auto &position : onFoot) {
            view.nearestPlayer = std::min(view.nearestPlayer, glm::distance(position, car.position));
        }
        return view;
    }

    void TrafficService::WatchJams(std::chrono::steady_clock::time_point now) {
        const double clock = std::chrono::duration<double>(now.time_since_epoch()).count();
        std::erase_if(_recentJams, [&](const RecentJam &jam) { return now >= jam.until; });
        const auto onFoot = PlayersOnFoot();
        std::vector<Shared::Traffic::JamCar> views;
        views.reserve(_records.size());
        for (auto &[key, record] : _records) {
            auto *car = _cars->Find(record.networkId);
            if (!car) {
                continue;
            }
            // Asked and still here: the host keeps it (not a pool car).
            if (!record.resetKept && record.resetAskedAt != std::chrono::steady_clock::time_point {} && now - record.resetAskedAt >= kResetWait) {
                record.resetKept = true;
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Simulation host kept traffic car {} it was asked to reset", car->GetNetworkID());
            }
            (void)Shared::Traffic::UpdateStuck(record.stuck, car->position, glm::length(car->velocity), clock, _jam);
            views.push_back(ViewOf(record, *car, now, onFoot));
        }
        if (!_jam.enabled) {
            return;
        }
        for (const auto &jam : Shared::Traffic::FindJams(views, _jam)) {
            const bool cooling = std::any_of(_recentJams.begin(), _recentJams.end(), [&](const RecentJam &recent) { return glm::distance(recent.center, jam.center) <= _jam.radius; });
            if (cooling) {
                continue;
            }
            _recentJams.push_back({jam.center, now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(_jam.cooldownSeconds))});
            std::vector<uint32_t> keys;
            for (const uint64_t id : jam.cars) {
                const auto key = static_cast<uint32_t>(id);
                if (auto record = _records.find(key); record != _records.end()) {
                    record->second.resetAskedAt = now;
                    keys.push_back(key);
                }
            }
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info(
                "Traffic jam at ({:.0f}, {:.0f}, {:.0f}): {} cars stopped within {:.0f} m for {:.0f} s or more; asking the simulation host to clear them", jam.center.x,
                jam.center.y, jam.center.z, keys.size(), _jam.radius, jam.stuckSeconds);
            if (_events.jamReset) {
                _events.jamReset({jam.center, keys.size(), jam.stuckSeconds});
            }
            SendReset(std::move(keys), false);
        }
    }

    void TrafficService::SendReset(std::vector<uint32_t> keys, bool all) {
        const auto host = _simulation ? _simulation->Host() : std::nullopt;
        if (!host || (keys.empty() && !all)) {
            return;
        }
        Shared::Traffic::Reset reset;
        reset.missionGeneration = _missionGeneration;
        reset.all = all;
        reset.keys = std::move(keys);
        Framework::CoreModules::GetNetworkPeer()->SendRPC(reset, MafiaNet::ToGuid(*host), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    bool TrafficService::SetJamSettings(const Shared::Traffic::JamSettings &settings) {
        if (!Shared::Traffic::ValidJamSettings(settings)) {
            return false;
        }
        _jam = settings;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info(
            "Traffic jam watch {}: {} cars within {:.0f} m below {:.1f} m/s for {:.0f} s, {:.0f} s cooldown, {:.0f} m player clearance", _jam.enabled ? "on" : "off", _jam.minCars,
            _jam.radius, _jam.maxSpeed, _jam.stuckSeconds, _jam.cooldownSeconds, _jam.playerClearance);
        return true;
    }

    bool TrafficService::HostCurrent() const {
        // The records are the keys of the host they came from; a new host
        // that attached this tick is told nothing until Update cleared them.
        return _host && _simulation && _simulation->Host() == _host;
    }

    size_t TrafficService::ResetArea(const glm::vec3 &center, float radius) {
        if (!HostCurrent() || !std::isfinite(radius) || radius < 0.0f) {
            return 0;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto onFoot = PlayersOnFoot();
        std::vector<uint32_t> keys;
        for (auto &[key, record] : _records) {
            const auto *car = _cars->Find(record.networkId);
            if (!car || !(glm::distance(car->position, center) <= radius) ||
                !Shared::Traffic::JamEligible(ViewOf(record, *car, now, onFoot), _jam, Shared::Traffic::JamScope::Manual)) {
                continue;
            }
            record.resetAskedAt = now;
            keys.push_back(key);
        }
        const size_t asked = keys.size();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Traffic reset within {:.0f} m of ({:.0f}, {:.0f}, {:.0f}): asking the simulation host to clear {} cars", radius,
                                                                   center.x, center.y, center.z, asked);
        SendReset(std::move(keys), false);
        return asked;
    }

    bool TrafficService::ResetAll() {
        if (!HostCurrent()) {
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        for (auto &[key, record] : _records) {
            record.resetAskedAt = now;
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Traffic reset: asking the simulation host to clear its traffic pool and refill it ({} cars driven)",
                                                                   LiveRecords());
        SendReset({}, true);
        return true;
    }

    void TrafficService::DespawnReleased(uint64_t networkId) {
        _released.erase(networkId);
        _cars->Despawn(networkId);
    }

    void TrafficService::SetMissionGeneration(uint64_t generation) {
        if (generation != _missionGeneration) {
            Clear();
            _lastStealIntent.clear();
            _missionGeneration = generation;
        }
    }

    void TrafficService::Clear() {
        // A car a player took stays theirs; only what the host drives goes.
        std::vector<uint32_t> gone;
        for (const auto &[key, record] : _records) {
            gone.push_back(key);
        }
        if (!gone.empty()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Removed {} simulation host cars", gone.size());
        }
        for (const uint32_t key : gone) {
            Despawn(key);
        }
        _npcSeats.clear();
        _recentJams.clear();
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            for (const auto &[key, networkId] : _barriers) {
                if (auto *entity = replication->GetEntity<Shared::Entities::RoadBarrierEntity>(networkId)) {
                    replication->DestroyEntity(entity);
                }
            }
        }
        _barriers.clear();
    }
} // namespace Mafia1Online::Features::Traffic
