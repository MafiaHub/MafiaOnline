#include "pumper_service.h"

#include "features/car/car_service.h"
#include "features/player/player_service.h"
#include "features/pumper/pumper_scripting.h"
#include "features/script/script_events.h"
#include "features/script/script_runtime.h"
#include "features/simulation/simulation_service.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace Mafia1Online::Features::Pumper {
    void PumperService::RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Car::CarService &cars) {
        _simulation = &simulation;
        _players = &players;
        _cars = &cars;
        auto *peer = Framework::CoreModules::GetNetworkPeer();
        peer->RegisterRPC<Shared::Pumper::Snapshot>([this](const Shared::Pumper::Snapshot &received, MafiaNet::Packet *packet) { OnSnapshot(received, MafiaNet::ToPeerGuid(packet->guid)); });
        peer->RegisterRPC<Shared::Pumper::Request>([this](const Shared::Pumper::Request &request, MafiaNet::Packet *packet) { OnRequest(request, MafiaNet::ToPeerGuid(packet->guid)); });
        peer->RegisterRPC<Shared::Pumper::Progress>([this](const Shared::Pumper::Progress &progress, MafiaNet::Packet *packet) { OnProgress(progress, MafiaNet::ToPeerGuid(packet->guid)); });
    }

    void PumperService::SetMissionGeneration(uint64_t generation) {
        // Every client reloads the mission: its attendants and the cars they
        // served start over, so nothing is released here.
        _missionGeneration = generation;
        _known.fill(false);
        _services.fill({});
        _hasReport = false;
    }

    void PumperService::OnSnapshot(const Shared::Pumper::Snapshot &received, MafiaNet::PeerGuid sender) {
        if (_simulation->Host() != sender || received.missionGeneration != _missionGeneration) {
            return;
        }
        std::array<bool, Shared::Pumper::kMaxPumpers> seen {};
        for (uint8_t i = 0; i < received.count; ++i) {
            const auto &state = received.pumpers[i];
            if (!Shared::Pumper::Valid(state) || seen[state.index]) {
                return;
            }
            seen[state.index] = true;
        }
        if (_hasReport && received.hostSession == _hostSession && static_cast<int32_t>(received.hostTimeMs - _lastHostTimeMs) <= 0) {
            return;
        }
        _hasReport = true;
        _hostSession = received.hostSession;
        _lastHostTimeMs = received.hostTimeMs;
        bool changed = false;
        for (uint8_t i = 0; i < received.count; ++i) {
            const auto &state = received.pumpers[i];
            const auto &previous = _latest[state.index];
            changed = changed || !_known[state.index] || previous.state != state.state || previous.animation != state.animation || previous.carId != state.carId;
            _latest[state.index] = state;
            _known[state.index] = true;
        }
        // A change goes on reliably, as the host sent it.
        const auto reliability = changed ? MafiaNet::Reliability::ReliableOrdered : MafiaNet::Reliability::Unreliable;
        auto snapshot = received;
        auto *peer = Framework::CoreModules::GetNetworkPeer();
        _players->ForEach([&](Shared::Entities::PlayerEntity *player) {
            if (player->spawned && player->missionGeneration == _missionGeneration) {
                peer->SendRPC(snapshot, MafiaNet::ToGuid(_players->GuidForNetworkId(player->GetNetworkID())), MafiaNet::Priority::High, reliability);
            }
        });
    }

    void PumperService::Send(Shared::Pumper::Command::Kind kind, uint8_t index, uint64_t carId, MafiaNet::PeerGuid guid) {
        Shared::Pumper::Command command;
        command.missionGeneration = _missionGeneration;
        command.index = index;
        command.carId = carId;
        command.kind = kind;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(command, MafiaNet::ToGuid(guid));
    }

    void PumperService::Refuse(uint64_t playerId, MafiaNet::PeerGuid guid, const Shared::Pumper::Request &request, const char *reason, uint32_t cost) {
        Send(Shared::Pumper::Command::Kind::Refused, request.index, request.carId, guid);
        Scripting::EmitPumperRefused(playerId, request.carId, request.index, reason, cost);
    }

    void PumperService::OnRequest(const Shared::Pumper::Request &request, MafiaNet::PeerGuid sender) {
        using Kind = Shared::Pumper::Request::Kind;
        auto *player = _players->FindByGuid(sender);
        if (!player || request.missionGeneration != _missionGeneration || player->missionGeneration != _missionGeneration || request.index >= Shared::Pumper::kMaxPumpers) {
            return;
        }
        const uint64_t playerId = player->GetNetworkID();
        auto &service = _services[request.index];
        if (request.kind == Kind::Cancel) {
            // The customer's native Start failed after all (the car rolled
            // off the pump): the attendant goes back.
            if (service.active && service.playerId == playerId && service.carId == request.carId) {
                End(request.index, true);
            }
            return;
        }
        if (request.kind != Kind::Serve) {
            return;
        }
        const auto *state = Find(request.index);
        const auto seat = _cars->SeatForPlayer(playerId, player->spawnGeneration);
        const auto *car = _cars->Find(request.carId);
        // A request the player's client could not have made in retail is
        // dropped: not spawned, not this attendant, not the driver of this
        // live car, or not at this attendant's station. The client only
        // hears no, without a script event.
        if (!player->spawned || !player->alive || !state || state->nameHash != request.nameHash || !seat || seat->carId != request.carId || seat->seat != 0 || !car ||
            car->missionGeneration != _missionGeneration || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active ||
            glm::distance(car->position, glm::vec3(state->position.x, state->position.y, state->position.z)) > kServiceReach) {
            Send(Shared::Pumper::Command::Kind::Refused, request.index, request.carId, sender);
            return;
        }
        for (const auto &other : _services) {
            if (other.active && (other.playerId == playerId || other.carId == request.carId)) {
                Send(Shared::Pumper::Command::Kind::Refused, request.index, request.carId, sender);
                return;
            }
        }
        if (!_enabled) {
            Refuse(playerId, sender, request, "disabled", 0);
            return;
        }
        const auto host = _simulation->Host();
        if (!host) {
            Refuse(playerId, sender, request, "unavailable", 0);
            return;
        }
        if (service.active || Shared::Pumper::Pumping(state->state)) {
            Refuse(playerId, sender, request, "busy", 0);
            return;
        }
        if (!car->dynamicsValid || car->fuelTankCapacity <= 0.0f || car->fuelTankCapacity - car->fuel < kMinimumLitres) {
            Refuse(playerId, sender, request, "full", 0);
            return;
        }
        // The full tank's price; the fill takes what the money buys, at
        // least one litre.
        const auto full = Shared::Pumper::ChargeFor(car->fuel, car->fuelTankCapacity, _pricePerLitre, Shared::Entities::PlayerEntity::kMaxMoney);
        if (_pricePerLitre > 0.0 && static_cast<double>(player->money) < std::ceil(std::min<double>(1.0, car->fuelTankCapacity - car->fuel) * _pricePerLitre)) {
            Refuse(playerId, sender, request, "money", full.cost);
            return;
        }
        service.active = true;
        service.carId = request.carId;
        service.playerId = playerId;
        service.playerGeneration = player->spawnGeneration;
        service.startedAt = std::chrono::steady_clock::now();
        service.fuelled = false;
        service.host = *host;
        Send(Shared::Pumper::Command::Kind::Serve, request.index, request.carId, *host);
        Send(Shared::Pumper::Command::Kind::Accepted, request.index, request.carId, sender);
    }

    void PumperService::OnProgress(const Shared::Pumper::Progress &progress, MafiaNet::PeerGuid sender) {
        using Kind = Shared::Pumper::Progress::Kind;
        if (_simulation->Host() != sender || progress.missionGeneration != _missionGeneration || progress.index >= Shared::Pumper::kMaxPumpers) {
            return;
        }
        auto &service = _services[progress.index];
        if (!service.active || service.carId != progress.carId) {
            return;
        }
        switch (progress.kind) {
        case Kind::Fuelled:
            if (!service.fuelled) {
                service.fuelled = true;
                Fill(progress.index);
            }
            break;
        case Kind::Finished:
        case Kind::Abandoned: End(progress.index, false); break;
        default: break;
        }
    }

    void PumperService::Fill(uint8_t index) {
        const auto &service = _services[index];
        auto *car = _cars->Find(service.carId);
        auto *player = _players->FindByNetworkId(service.playerId);
        if (!car || !player || !car->dynamicsValid) {
            return;
        }
        // Retail EndPumpFuel fills the tank (C_Vehicle::SetFuel(capacity)) and
        // takes 500 from the score; here the player pays the server's price.
        const auto charge = Shared::Pumper::ChargeFor(car->fuel, car->fuelTankCapacity, _pricePerLitre, player->money);
        if (charge.litres <= 0.0f) {
            Scripting::EmitVehicleRefuel(service.carId, service.playerId, index, 0.0f, 0);
            return;
        }
        const uint32_t moneyBefore = player->money;
        if (charge.cost > 0 && !_players->TrySpendMoney(service.playerId, charge.cost)) {
            Scripting::EmitVehicleRefuel(service.carId, service.playerId, index, 0.0f, 0);
            return;
        }
        if (player->money != moneyBefore) {
            Features::Script::EmitMoneyChange(service.playerId, moneyBefore, player->money);
        }
        const float fuelBefore = car->fuel;
        if (_cars->SetFuel(service.carId, std::min(car->fuelTankCapacity, car->fuel + charge.litres)) && car->fuel != fuelBefore) {
            Features::Script::EmitCarEvent(Scripting::GetServer(), "vehicleFuelChange", *car);
        }
        Scripting::EmitVehicleRefuel(service.carId, service.playerId, index, car->fuel - fuelBefore, charge.cost);
    }

    void PumperService::End(uint8_t index, bool abortHost) {
        auto &service = _services[index];
        if (!service.active) {
            return;
        }
        if (abortHost) {
            if (const auto host = _simulation->Host()) {
                Send(Shared::Pumper::Command::Kind::Abort, index, service.carId, *host);
            }
        }
        if (_players->FindByNetworkId(service.playerId)) {
            Send(Shared::Pumper::Command::Kind::Release, index, service.carId, _players->GuidForNetworkId(service.playerId));
        }
        service = {};
    }

    void PumperService::Update() {
        const auto now = std::chrono::steady_clock::now();
        for (uint8_t index = 0; index < Shared::Pumper::kMaxPumpers; ++index) {
            const auto &service = _services[index];
            if (!service.active) {
                continue;
            }
            // The customer stays in the driver's seat of the live car for the
            // whole service, as retail locks the controls; anything else (a
            // script, a death, a disconnect, the host leaving or handing over:
            // a new host never heard of the service and would never end it).
            const auto *player = _players->FindByNetworkId(service.playerId);
            const auto *car = _cars->Find(service.carId);
            std::optional<Car::CarService::SeatLocation> seat;
            if (player) {
                seat = _cars->SeatForPlayer(service.playerId, service.playerGeneration);
            }
            if (!player || !player->spawned || !player->alive || !car || car->terminalState != Shared::Entities::CarEntity::TerminalState::Active || !seat ||
                seat->carId != service.carId || seat->seat != 0 || _simulation->Host() != service.host || now - service.startedAt > kServiceTimeout) {
                End(index, true);
            }
        }
    }
} // namespace Mafia1Online::Features::Pumper
