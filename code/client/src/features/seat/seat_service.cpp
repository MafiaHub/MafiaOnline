#include "seat_service.h"

#include "features/world/world_service.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <cstddef>

namespace Mafia1Online::Features::Seat {
    namespace {
        using Action = Shared::Car::SeatAction;
        using NativeActor = SDK::Player::NativeActor;
        using NativeHuman = SDK::Seat::NativeHuman;
        using NativeCar = SDK::Seat::NativeCar;
        constexpr auto kLocalTransitionLimit = std::chrono::seconds(8);
        constexpr auto kLocalAcknowledgementLimit = std::chrono::seconds(2);
        constexpr auto kTransientEventLimit = std::chrono::seconds(5);
        constexpr auto kRemoteExitGrace = std::chrono::milliseconds(500);
        // C_car::SetOwner, bool __thiscall(C_car*, C_actor*, int seat, float),
        // ret 0c. A null owner clears the seat, as C_game::InvalidateActor does.
        constexpr uintptr_t kCarSetOwner = 0x41d810;

        NativeHuman *Human(World::WorldService &world, uint64_t networkId) {
            const auto handle = world.NativeObjects().FindByNetwork(networkId);
            auto *actor = static_cast<NativeActor *>(world.NativeObjects().Resolve(handle));
            return actor && (actor->GetType() == NativeActor::Type::Player || actor->GetType() == NativeActor::Type::Entity) ? static_cast<NativeHuman *>(static_cast<void *>(actor)) : nullptr;
        }

        NativeCar *Car(World::WorldService &world, uint64_t networkId) {
            const auto handle = world.NativeObjects().FindByNetwork(networkId);
            auto *actor = static_cast<NativeActor *>(world.NativeObjects().Resolve(handle));
            return actor && actor->GetType() == NativeActor::Type::Car ? static_cast<NativeCar *>(static_cast<void *>(actor)) : nullptr;
        }

        void ClearSeatOwner(NativeCar *car, int seat) {
            using Call = bool(__thiscall *)(NativeCar *, NativeActor *, int, float);
            reinterpret_cast<Call>(kCarSetOwner)(car, nullptr, seat, 0.0f);
        }

        bool IsHuman(NativeActor *actor) {
            return actor->GetType() == NativeActor::Type::Player || actor->GetType() == NativeActor::Type::Entity;
        }
    } // namespace

    void ReleaseHumanSeat(NativeHuman *human) {
        auto *used = human->usedActorEnter ? human->usedActorEnter : human->usedActorLeave;
        if (!used || used->GetType() != NativeActor::Type::Car) {
            return;
        }
        auto *car = static_cast<NativeCar *>(static_cast<void *>(used));
        const int seat = human->seatId;
        if (seat < 0 || seat >= static_cast<int>(Shared::Entities::CarEntity::kMaxSeats) || car->GetOwner(seat) != &human->actor) {
            return;
        }
        if (human->HasPendingEntry(car) && seat == human->seatId) {
            // Releases the boarding lock, which otherwise stalls the driver.
            if (human->FinishPendingEntry(car, seat)) {
                // The queued native removal clears the seated owner. Ejecting
                // this soon-to-be-destroyed human would enable its collision
                // beside the moving car and can jolt the driver.
                return;
            }
        }
        if (human->IsSeatedIn(car, seat)) {
            // C_game::InvalidateActor clears this owner when the queued human
            // is removed; leave its in-car collision state intact until then.
            return;
        }
        ClearSeatOwner(car, seat);
    }

    void ReleaseCarOccupants(NativeCar *car) {
        for (int seat = 0; seat < static_cast<int>(Shared::Entities::CarEntity::kMaxSeats); ++seat) {
            if (!car->HasSeat(seat)) {
                continue;
            }
            auto *owner = car->GetOwner(seat);
            if (!owner || !IsHuman(owner)) {
                continue;
            }
            // A door reservation has no seated link for ForceExitCar. Complete
            // the link first so the exit also cancels the entry animation,
            // which would otherwise finish against the removed car.
            auto *human = static_cast<NativeHuman *>(static_cast<void *>(owner));
            const bool linked = human->HasPendingEntry(car) ? human->FinishPendingEntry(car, seat) : human->IsSeatedIn(car, seat) || human->PlaceInCar(car, seat);
            if (linked) {
                human->ForceExitCar();
            } else {
                ClearSeatOwner(car, seat);
            }
        }
    }

    void SeatService::RegisterRPC() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Car::SeatEvent>([this](const Shared::Car::SeatEvent &event, MafiaNet::Packet *) {
            if (event.serverSequence > _lastServerSequence) {
                _lastServerSequence = event.serverSequence;
                _events.push_back({event, std::chrono::steady_clock::now() + kTransientEventLimit});
                if (_events.size() > 256) {
                    _events.pop_front();
                }
            }
        });
    }

    void SeatService::Reset() {
        _world = nullptr;
        _pending = {};
        _events.clear();
        _animations.clear();
        _seatSequences.clear();
        _lastServerSequence = 0;
        _localSequence = 0;
    }

    NativeCar *SeatService::CurrentNetworkCar(NativeHuman *human) {
        if (!_world || !_world->IsReady() || !human) {
            return nullptr;
        }
        auto *usedCar = human->usedActorEnter ? human->usedActorEnter : human->usedActorLeave;
        if (!usedCar) {
            return nullptr;
        }
        const auto handle = _world->NativeObjects().FindByNative(usedCar);
        auto *actor = static_cast<NativeActor *>(_world->NativeObjects().Resolve(handle));
        return actor && actor->GetType() == NativeActor::Type::Car ? static_cast<NativeCar *>(static_cast<void *>(actor)) : nullptr;
    }

    void SeatService::Update(World::WorldService &world) {
        _world = &world;
        if (!world.IsReady()) {
            _pending = {};
            _events.clear();
            _animations.clear();
            _seatSequences.clear();
            return;
        }
        PollLocal(world);
        const auto now = std::chrono::steady_clock::now();
        for (auto it = _animations.begin(); it != _animations.end();) {
            it = now >= it->second.until ? _animations.erase(it) : ++it;
        }
        Replay(world);
        Reconcile(world);
    }

    void SeatService::Send(uint64_t carId, uint64_t playerId, uint8_t seat, Action action) {
        if (!_world || !_world->IsReady()) {
            return;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        auto *player = replication->GetEntity<Shared::Entities::PlayerEntity>(playerId);
        auto *car = replication->GetEntity<Shared::Entities::CarEntity>(carId);
        if (!player || !car || !player->spawned || !player->alive || player->controllerGuid != static_cast<uint64_t>(replication->GetMyGUID()) ||
            player->missionGeneration != _world->LoadedMissionGeneration() || car->missionGeneration != player->missionGeneration || seat >= car->seatCount) {
            return;
        }
        Shared::Car::SeatIntent intent;
        intent.carId = carId;
        intent.playerId = playerId;
        intent.spawnGeneration = player->spawnGeneration;
        intent.missionGeneration = player->missionGeneration;
        intent.sequence = ++_localSequence;
        intent.seat = seat;
        intent.action = action;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(intent);
    }

    void SeatService::OnNativeUse(NativeHuman *human, NativeCar *car, int action, int seat) {
        if (!_world || !_world->IsReady() || !human || !car || &human->actor != SDK::Player::CurrentPlayer() ||
            (action != static_cast<int>(SDK::Seat::UseAction::Enter) && action != static_cast<int>(SDK::Seat::UseAction::Exit))) {
            return;
        }
        const auto actor = _world->NativeObjects().FindByNative(human);
        const auto vehicle = _world->NativeObjects().FindByNative(car);
        if (!actor.networkId || !vehicle.networkId || !_world->NativeObjects().Resolve(actor) || !_world->NativeObjects().Resolve(vehicle)) {
            return;
        }
        const int nativeSeat = seat;
        if (nativeSeat < 0 || nativeSeat >= static_cast<int>(Shared::Entities::CarEntity::kMaxSeats)) {
            return;
        }
        _pending = {vehicle.networkId, actor.networkId, 0, _world->LoadedMissionGeneration(), static_cast<uint8_t>(nativeSeat), false,
                    std::chrono::steady_clock::now() + kLocalTransitionLimit};
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            if (auto *player = replication->GetEntity<Shared::Entities::PlayerEntity>(actor.networkId)) {
                _pending.spawnGeneration = player->spawnGeneration;
            }
        }
        if (action == static_cast<int>(SDK::Seat::UseAction::Exit)) {
            _pending.stealing = false;
            // Exit is inferred from the native owner leaving the seat.
            _pending.seat = static_cast<uint8_t>(nativeSeat | 0x80);
        } else if (car->GetOwner(nativeSeat) == &human->actor) {
            Send(vehicle.networkId, actor.networkId, static_cast<uint8_t>(nativeSeat), Action::EnterBegin);
        }
    }

    void SeatService::OnNativeSteal(NativeHuman *human, NativeCar *car, int seat) {
        if (!_world || !_world->IsReady() || !human || !car || &human->actor != SDK::Player::CurrentPlayer() || seat < 0 || seat >= Shared::Entities::CarEntity::kMaxSeats) {
            return;
        }
        const auto actor = _world->NativeObjects().FindByNative(human);
        const auto vehicle = _world->NativeObjects().FindByNative(car);
        if (!actor.networkId || !vehicle.networkId || !_world->NativeObjects().Resolve(actor) || !_world->NativeObjects().Resolve(vehicle)) {
            return;
        }
        Send(vehicle.networkId, actor.networkId, static_cast<uint8_t>(seat), Action::StealBegin);
        _pending = {vehicle.networkId, actor.networkId, 0, _world->LoadedMissionGeneration(), static_cast<uint8_t>(seat), true,
                    std::chrono::steady_clock::now() + kLocalTransitionLimit};
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            if (auto *player = replication->GetEntity<Shared::Entities::PlayerEntity>(actor.networkId)) {
                _pending.spawnGeneration = player->spawnGeneration;
            }
        }
    }

    void SeatService::PollLocal(World::WorldService &world) {
        if (!_pending.playerId) {
            return;
        }
        // An interrupted native door/throw animation must not suppress the
        // authoritative occupant array for the rest of this mission.
        if (std::chrono::steady_clock::now() >= _pending.until) {
            _pending = {};
            return;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        auto *player = replication ? replication->GetEntity<Shared::Entities::PlayerEntity>(_pending.playerId) : nullptr;
        auto *human = Human(world, _pending.playerId);
        auto *car = Car(world, _pending.carId);
        if (!player || !human || !car || player->spawnGeneration != _pending.spawnGeneration || player->missionGeneration != _pending.missionGeneration) {
            _pending = {};
            return;
        }
        const bool exit = (_pending.seat & 0x80) != 0;
        const uint8_t seat = _pending.seat & 0x7f;
        if (_pending.sent) {
            // Reconciliation would otherwise undo the native transition during
            // the round trip, before the server's occupant array changes.
            const auto *state = replication->GetEntity<Shared::Entities::CarEntity>(_pending.carId);
            const uint8_t acknowledgedSeat = _pending.moving ? static_cast<uint8_t>(seat ^ 1) : seat;
            const bool occupied = state && acknowledgedSeat < Shared::Entities::CarEntity::kMaxSeats && state->occupantIds[acknowledgedSeat] == _pending.playerId &&
                                  state->occupantGenerations[acknowledgedSeat] == _pending.spawnGeneration;
            if (!state || (_pending.moving ? occupied : (exit ? !occupied : occupied))) {
                _pending = {};
            }
            return;
        }
        bool completed = false;
        if (exit) {
            // reM Do_ClimbInCarLR moves native ownership to the paired seat
            // before the exit animation. Commit that transfer atomically.
            const uint8_t pairedSeat = static_cast<uint8_t>(seat ^ 1);
            if (human->usedActorEnter == static_cast<NativeActor *>(car) && human->seatId == pairedSeat &&
                car->GetOwner(pairedSeat) == &human->actor) {
                Send(_pending.carId, _pending.playerId, pairedSeat, Action::Move);
                _pending.moving = true;
                _pending.sent = true;
                _pending.until = std::chrono::steady_clock::now() + kLocalAcknowledgementLimit;
                return;
            }
            completed = human->usedActorEnter != static_cast<NativeActor *>(car) && car->GetOwner(seat) != &human->actor;
            if (completed) {
                Send(_pending.carId, _pending.playerId, seat, Action::Exit);
            }
        } else if (car->GetOwner(seat) == &human->actor && human->IsSeatedIn(car, seat)) {
            completed = true;
            Send(_pending.carId, _pending.playerId, seat, _pending.stealing ? Action::Steal : Action::Enter);
        }
        if (completed) {
            _pending.sent = true;
            _pending.until = std::chrono::steady_clock::now() + kLocalAcknowledgementLimit;
        }
    }

    void SeatService::Replay(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const std::size_t queued = _events.size();
        for (std::size_t index = 0; index < queued; ++index) {
            const auto queuedEvent = _events.front();
            _events.pop_front();
            if (now >= queuedEvent.until) {
                continue;
            }
            const auto &event = queuedEvent.event;
            if (event.missionGeneration != world.LoadedMissionGeneration()) {
                continue;
            }
            auto *player = replication->GetEntity<Shared::Entities::PlayerEntity>(event.playerId);
            auto *carState = replication->GetEntity<Shared::Entities::CarEntity>(event.carId);
            if (!player || !carState) {
                _events.push_back(queuedEvent);
                continue;
            }
            if (player->spawnGeneration != event.spawnGeneration || event.seat >= carState->seatCount ||
                player->controllerGuid == static_cast<uint64_t>(replication->GetMyGUID())) {
                continue;
            }
            auto *human = Human(world, event.playerId);
            auto *car = Car(world, event.carId);
            if (!human || !car) {
                _events.push_back(queuedEvent);
                continue;
            }
            switch (event.action) {
            case Action::EnterBegin:
            case Action::Enter:
                if (!car->GetOwner(event.seat)) {
                    if (!_animations.contains(event.playerId)) {
                        human->UseCar(car, SDK::Seat::UseAction::Enter, event.seat);
                        _animations[event.playerId] = {now + std::chrono::seconds(5), event.carId, event.seat};
                    }
                }
                break;
            case Action::StealBegin:
                if (car->GetOwner(event.seat) && car->GetOwner(event.seat) != &human->actor) {
                    const auto displaced = world.NativeObjects().FindByNative(car->GetOwner(event.seat));
                    if (human->TrySteal(car, event.seat)) {
                        const auto until = now + std::chrono::seconds(3);
                        _animations[event.playerId] = {until, event.carId, event.seat};
                        // Native throw can clear the old seat owner before the
                        // server receives the completed Steal outcome. Keep
                        // reconciliation from re-seating that victim mid-throw.
                        if (displaced.networkId) {
                            _animations[displaced.networkId] = {until, event.carId, event.seat};
                        }
                    }
                }
                break;
            case Action::Steal:
                _animations.erase(event.playerId);
                break;
            case Action::Exit:
                if (human->IsSeatedIn(car, event.seat)) {
                    human->UseCar(car, SDK::Seat::UseAction::Exit, event.seat);
                    _animations[event.playerId] = {now + std::chrono::seconds(2), event.carId, event.seat};
                }
                break;
            case Action::ExitBlocked: break;
            case Action::Move:
                if (human->IsSeatedIn(car, event.seat ^ 1) && !car->GetOwner(event.seat) &&
                    human->ClimbToPairedSeat()) {
                    _animations[event.playerId] = {now + std::chrono::seconds(2), event.carId, event.seat};
                }
                break;
            }
        }
    }

    void SeatService::Reconcile(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        replication->ForEach<Shared::Entities::CarEntity>([&](Shared::Entities::CarEntity *state) {
            if (state->missionGeneration != world.LoadedMissionGeneration()) {
                return;
            }
            auto &seatSequence = _seatSequences[state->GetNetworkID()];
            const bool freshExit = seatSequence != 0 && seatSequence != state->seatSequence &&
                                   state->seatResult == Shared::Entities::CarEntity::SeatResult::Exited && state->seatIndex < Shared::Entities::CarEntity::kMaxSeats;
            const bool freshMove = seatSequence != 0 && seatSequence != state->seatSequence &&
                                   state->seatResult == Shared::Entities::CarEntity::SeatResult::Moved && state->seatIndex < Shared::Entities::CarEntity::kMaxSeats;
            seatSequence = state->seatSequence;
            auto *car = Car(world, state->GetNetworkID());
            if (!car) {
                return;
            }
            if (freshExit && !_animations.contains(state->seatActorId)) {
                // The replicated outcome can precede its reliable SeatEvent.
                // Let Replay start the native exit before reconciliation forces it.
                if (auto *leaving = Human(world, state->seatActorId); leaving && leaving->IsSeatedIn(car, state->seatIndex)) {
                    _animations[state->seatActorId] = {now + kRemoteExitGrace, state->GetNetworkID(), state->seatIndex};
                }
            }
            if (freshMove && !_animations.contains(state->seatActorId)) {
                if (auto *moving = Human(world, state->seatActorId);
                    moving && moving->IsSeatedIn(car, state->seatIndex ^ 1) && !car->GetOwner(state->seatIndex) &&
                    moving->ClimbToPairedSeat()) {
                    _animations[state->seatActorId] = {now + std::chrono::seconds(2), state->GetNetworkID(), state->seatIndex};
                }
            }
            const auto place = [&](NativeHuman *human, uint8_t seat) {
                // A replayed door animation holds the vehicle lock; snapping
                // with Intern_UseCar(car, seat) would leave the car undrivable.
                const bool linked = human->HasPendingEntry(car) && human->seatId == seat ? human->FinishPendingEntry(car, seat) : human->PlaceInCar(car, seat);
                if (!linked) {
                    return false;
                }
                // Intern_UseCar starts a driver's engine; the replica owns it.
                if (seat == 0 && car->EngineOn() != state->engineOn) {
                    car->SetEngineOn(state->engineOn, true);
                }
                return true;
            };
            for (uint8_t seat = 0; seat < state->seatCount && seat < Shared::Entities::CarEntity::kMaxSeats; ++seat) {
                if (!car->HasSeat(seat)) {
                    continue;
                }
                // The local player's own native transition owns this seat
                // until the server answers. Mid-steal the throw leaves it
                // briefly empty while the server still lists the victim;
                // re-seating the victim then would undo the steal here only.
                if (_pending.playerId && _pending.carId == state->GetNetworkID() &&
                    (static_cast<uint8_t>(_pending.seat & 0x7f) == seat ||
                     ((_pending.seat & 0x80) && static_cast<uint8_t>((_pending.seat & 0x7f) ^ 1) == seat))) {
                    continue;
                }
                const uint64_t expectedId = state->occupantIds[seat];
                auto *player = expectedId ? replication->GetEntity<Shared::Entities::PlayerEntity>(expectedId) : nullptr;
                auto *expected = player && player->spawned && player->alive && player->missionGeneration == world.LoadedMissionGeneration() &&
                                         player->spawnGeneration == state->occupantGenerations[seat]
                                     ? Human(world, expectedId)
                                     : nullptr;
                auto *owner = car->GetOwner(seat);
                const auto ownerHandle = owner ? world.NativeObjects().FindByNative(owner) : Game::Entities::NativeObjectHandle{};
                if (owner == (expected ? &expected->actor : nullptr)) {
                    if (expected && !expected->IsSeatedIn(car, seat) && expectedId != _pending.playerId) {
                        const auto animation = _animations.find(expectedId);
                        const bool animationRunning = animation != _animations.end() && animation->second.carId == state->GetNetworkID() &&
                                                      animation->second.seat == seat && now < animation->second.until;
                        if (!animationRunning && (!expected->usedActorEnter || expected->usedActorEnter == car) && !place(expected, seat)) {
                            _animations[expectedId] = {now + std::chrono::milliseconds(500), state->GetNetworkID(), seat};
                        }
                    }
                    continue;
                }
                if ((expectedId == _pending.playerId || ownerHandle.networkId == _pending.playerId) && state->GetNetworkID() == _pending.carId) {
                    continue;
                }
                if (auto anim = _animations.find(expectedId ? expectedId : ownerHandle.networkId); anim != _animations.end()) {
                    if (anim->second.carId == state->GetNetworkID() && anim->second.seat == seat && now < anim->second.until) {
                        continue;
                    }
                    _animations.erase(anim);
                }
                if (owner) {
                    if (ownerHandle.networkId) {
                        if (owner->GetType() != NativeActor::Type::Player && owner->GetType() != NativeActor::Type::Entity) {
                            continue;
                        }
                        // A door animation may reserve the native owner before
                        // Intern_UseCar has attached the human. ForceExitCar is
                        // only valid once that seated link exists.
                        auto *nativeOwner = static_cast<NativeHuman *>(static_cast<void *>(owner));
                        if (!nativeOwner->IsSeatedIn(car, seat)) {
                            continue;
                        }
                        // Retail HitInCar leaves a killed occupant slumped in
                        // the seat with an in-car death animation. The server
                        // frees the seat at death; the body is released when
                        // the dead human is removed at respawn.
                        if (const auto *ownerPlayer = replication->GetEntity<Shared::Entities::PlayerEntity>(ownerHandle.networkId);
                            ownerPlayer && !ownerPlayer->alive) {
                            continue;
                        }
                        // The seat clear may replicate before the departing
                        // player's removal. Keep its collision disabled until
                        // queued native teardown clears the owner itself.
                        if (state->seatResult == Shared::Entities::CarEntity::SeatResult::Cleared &&
                            state->seatActorId == ownerHandle.networkId && state->seatIndex == seat) {
                            continue;
                        }
                        nativeOwner->ForceExitCar();
                    }
                }
                if (expected && !car->GetOwner(seat)) {
                    // A player moving between cars must leave the old native
                    // frame before being linked to the new one. The old car is
                    // reconciled from the same durable seat arrays.
                    if (expected->usedActorEnter && expected->usedActorEnter != car) {
                        continue;
                    }
                    if (!place(expected, seat)) {
                        // Native SetOwner may reject a seat while its prior
                        // exit is finishing. Retry from the durable replica.
                        _animations[expectedId] = {now + std::chrono::milliseconds(500), state->GetNetworkID(), seat};
                    }
                }
            }
        });
    }
} // namespace Mafia1Online::Features::Seat
