#include <utils/safe_win32.h>

#include "pumper_service.h"

#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/police/native_police_action.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <mafia1/sdk/world/native_pumper.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Mafia1Online::Features::Pumper {
    namespace {
        using SDK::World::NativePumper;
        using State = SDK::World::NativePumper::State;

        using Lifecycle = void(__thiscall *)(NativePumper *);
        using Tick = void(__thiscall *)(NativePumper *, unsigned int);
        using Start = bool(__thiscall *)(NativePumper *, void *);
        using Collide = bool(__thiscall *)(NativePumper *, void *, const void *, const void *, const void *, float, void *);
        using Hit = bool(__thiscall *)(NativePumper *, int, const void *, const void *, const void *, float, void *, unsigned int, void *);
        using Panic = void *(__thiscall *)(NativePumper *);
        Lifecycle gGameInitOriginal = nullptr;
        Lifecycle gGameDoneOriginal = nullptr;
        Tick gAIOriginal = nullptr;
        Lifecycle gChooseNextActionOriginal = nullptr;
        Start gStartOriginal = nullptr;
        Lifecycle gEndPumpFuelOriginal = nullptr;
        Collide gCollisionOriginal = nullptr;
        Hit gHitOriginal = nullptr;
        Panic gPanicOriginal = nullptr;
        PumperService *gService = nullptr;

        // A replayed position follows the host's this fast (1/s), and jumps
        // to it from farther than this.
        constexpr float kFollowRate = 12.0f;
        constexpr float kSnapDistance = 3.0f;
        // The customer's own Start spoke the greeting this recently; the
        // host's report of it does not repeat it.
        constexpr auto kGreetingEcho = std::chrono::seconds(5);
        constexpr auto kPendingTimeout = std::chrono::seconds(10);

        void __fastcall GameInitHook(NativePumper *pumper, void *) {
            gGameInitOriginal(pumper);
            if (gService) {
                gService->OnGameInit(pumper);
            }
        }

        void __fastcall GameDoneHook(NativePumper *pumper, void *) {
            if (gService) {
                gService->OnGameDone(pumper);
            }
            gGameDoneOriginal(pumper);
        }

        void __fastcall AIHook(NativePumper *pumper, void *, unsigned int frameTimeMs) {
            if (gService) {
                gService->AI(pumper, frameTimeMs);
                return;
            }
            gAIOriginal(pumper, frameTimeMs);
        }

        void __fastcall ChooseNextActionHook(NativePumper *pumper, void *) {
            if (gService && Simulation::IsHost()) {
                gService->ChooseNextAction(pumper);
                return;
            }
            gChooseNextActionOriginal(pumper);
        }

        // Do_AB_OwnerCar ignores the result. A player's choice goes to the
        // server; the server's Accepted calls the original.
        bool __fastcall StartHook(NativePumper *pumper, void *, void *car) {
            if (gService && !Simulation::IsHost()) {
                gService->RequestService(pumper, car);
            }
            return false;
        }

        // Retail fills m_pCar's tank and takes 500 from the score here; the
        // server fills the network car instead, so only the fuel loop sound
        // stops.
        void __fastcall EndPumpFuelHook(NativePumper *pumper, void *) {
            if (gService) {
                gService->OnFuelled(pumper);
            }
            void *car = pumper->Car();
            pumper->SetCar(nullptr);
            gEndPumpFuelOriginal(pumper);
            pumper->SetCar(car);
        }

        // The attendant does not panic: a car or a shot on one client would
        // startle only that client's copy, and the panic hands his model to
        // the local pedestrian generator. Both return false as retail does
        // for an attendant at work.
        bool __fastcall CollisionHook(NativePumper *, void *, void *, const void *, const void *, const void *, float, void *) {
            return false;
        }

        bool __fastcall HitHook(NativePumper *, void *, int, const void *, const void *, const void *, float, void *, unsigned int, void *) {
            return false;
        }

        // Update panics him once his pump is blown up; he keeps standing.
        void *__fastcall PanicHook(NativePumper *, void *) {
            return nullptr;
        }

        struct HookTarget {
            uintptr_t address;
            void *detour;
            void **original;
        };

        const std::array<HookTarget, 9> &Hooks() {
            static const std::array<HookTarget, 9> hooks {{
                {NativePumper::kGameInit, reinterpret_cast<void *>(&GameInitHook), reinterpret_cast<void **>(&gGameInitOriginal)},
                {NativePumper::kGameDone, reinterpret_cast<void *>(&GameDoneHook), reinterpret_cast<void **>(&gGameDoneOriginal)},
                {NativePumper::kAI, reinterpret_cast<void *>(&AIHook), reinterpret_cast<void **>(&gAIOriginal)},
                {NativePumper::kChooseNextAction, reinterpret_cast<void *>(&ChooseNextActionHook), reinterpret_cast<void **>(&gChooseNextActionOriginal)},
                {NativePumper::kStart, reinterpret_cast<void *>(&StartHook), reinterpret_cast<void **>(&gStartOriginal)},
                {NativePumper::kEndPumpFuel, reinterpret_cast<void *>(&EndPumpFuelHook), reinterpret_cast<void **>(&gEndPumpFuelOriginal)},
                {NativePumper::kCollision, reinterpret_cast<void *>(&CollisionHook), reinterpret_cast<void **>(&gCollisionOriginal)},
                {NativePumper::kHit, reinterpret_cast<void *>(&HitHook), reinterpret_cast<void **>(&gHitOriginal)},
                {NativePumper::kPanic, reinterpret_cast<void *>(&PanicHook), reinterpret_cast<void **>(&gPanicOriginal)},
            }};
            return hooks;
        }

        Shared::Pumper::Vector ToWire(const SDK::Player::Vector3 &vector) {
            return {vector.x, vector.y, vector.z};
        }

        SDK::Player::Vector3 ToNative(const Shared::Pumper::Vector &vector) {
            return {vector.x, vector.y, vector.z};
        }

        // The local player and the car it drives.
        SDK::Seat::NativeHuman *LocalDriver(const void *car) {
            auto *local = static_cast<SDK::Seat::NativeHuman *>(static_cast<void *>(SDK::Player::CurrentPlayer()));
            return local && local->IsSeatedIn(static_cast<const SDK::Seat::NativeCar *>(car), 0) ? local : nullptr;
        }

        auto *Peer() {
            return Framework::CoreModules::GetNetworkPeer();
        }
    } // namespace

    bool PumperService::Install(World::WorldService &world) {
        gService = this;
        _world = &world;
        for (const auto &hook : Hooks()) {
            if (MH_CreateHook(reinterpret_cast<void *>(hook.address), hook.detour, hook.original) != MH_OK || MH_EnableHook(reinterpret_cast<void *>(hook.address)) != MH_OK) {
                Uninstall();
                return false;
            }
        }
        return true;
    }

    void PumperService::Uninstall() {
        for (const auto &hook : Hooks()) {
            MH_DisableHook(reinterpret_cast<void *>(hook.address));
            MH_RemoveHook(reinterpret_cast<void *>(hook.address));
            *hook.original = nullptr;
        }
        gService = nullptr;
        Reset();
        _world = nullptr;
    }

    void PumperService::Reset() {
        _pumpers.clear();
        _hostSession = 0;
        _lastHostTimeMs = 0;
        _hasReport = false;
        _pending = false;
        _lastSnapshot = {};
        _reportedMismatch = false;
    }

    void PumperService::RegisterRPC() {
        auto *peer = Peer();
        peer->RegisterRPC<Shared::Pumper::Command>([this](const Shared::Pumper::Command &command, MafiaNet::Packet *) { OnCommand(command); });
        if (Simulation::IsHost()) {
            return;
        }
        peer->RegisterRPC<Shared::Pumper::Snapshot>([this](const Shared::Pumper::Snapshot &snapshot, MafiaNet::Packet *) {
            if (snapshot.missionGeneration == 0 || snapshot.missionGeneration != _missionGeneration) {
                return;
            }
            for (uint8_t i = 0; i < snapshot.count; ++i) {
                const auto &state = snapshot.pumpers[i];
                if (!Shared::Pumper::Valid(state) || state.index >= _pumpers.size() || _pumpers[state.index].nameHash != state.nameHash) {
                    if (!_reportedMismatch) {
                        _reportedMismatch = true;
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Attendant report {} does not match the {} attendants loaded here", state.index, _pumpers.size());
                    }
                    return;
                }
            }
            // A change comes reliably and may overtake, or trail, the
            // periodic reports; the newest stands.
            if (_hasReport && snapshot.hostSession == _hostSession && static_cast<int32_t>(snapshot.hostTimeMs - _lastHostTimeMs) <= 0) {
                return;
            }
            _hasReport = true;
            _hostSession = snapshot.hostSession;
            _lastHostTimeMs = snapshot.hostTimeMs;
            for (uint8_t i = 0; i < snapshot.count; ++i) {
                auto &entry = _pumpers[snapshot.pumpers[i].index];
                entry.target = snapshot.pumpers[i];
                entry.hasTarget = true;
            }
        });
    }

    PumperService::Entry *PumperService::Find(const NativePumper *pumper) {
        for (auto &entry : _pumpers) {
            if (entry.pumper == pumper) {
                return &entry;
            }
        }
        return nullptr;
    }

    void *PumperService::ResolveCar(uint64_t carId) const {
        if (!_world || carId == 0) {
            return nullptr;
        }
        auto &objects = _world->NativeObjects();
        return objects.Resolve(objects.FindByNetwork(carId));
    }

    void PumperService::OnGameInit(NativePumper *pumper) {
        if (_pumpers.size() >= Shared::Pumper::kMaxPumpers) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("More than {} gas station attendants in the mission; the rest stay unsynchronized", Shared::Pumper::kMaxPumpers);
            return;
        }
        Entry entry;
        entry.pumper = pumper;
        auto *frame = pumper->Actor().Frame();
        entry.nameHash = Shared::Pumper::NameHash(frame ? frame->Name() : "");
        entry.startNode = pumper->CurrentNode();
        entry.firstNode = pumper->NextNode();
        entry.position = pumper->Actor().Position();
        entry.direction = pumper->Actor().Direction();
        _pumpers.push_back(entry);
    }

    void PumperService::OnGameDone(NativePumper *pumper) {
        // Indices are load order: the rest keep theirs. A mission closes all
        // of its attendants; the next one numbers its own from zero.
        bool remaining = false;
        for (auto &entry : _pumpers) {
            if (entry.pumper == pumper) {
                entry.pumper = nullptr;
            }
            remaining = remaining || entry.pumper;
        }
        if (!remaining) {
            _pumpers.clear();
        }
    }

    void PumperService::AI(NativePumper *pumper, unsigned int frameTimeMs) {
        auto *entry = Find(pumper);
        if (!entry) {
            gAIOriginal(pumper, frameTimeMs);
            return;
        }
        if (!Simulation::IsHost()) {
            Replay(*entry, frameTimeMs);
            return;
        }
        // The served car went (removed, streamed out): the attendant gives
        // the service up before PUMP_READY or FUELING read it.
        if (entry->serviceCarId != 0 && ResolveCar(entry->serviceCarId) != pumper->Car()) {
            HostAbandon(*entry);
        }
        const auto state = pumper->GetState();
        if (state == State::PumpReady || state == State::Fueling) {
            entry->outbound = false;
        }
        gAIOriginal(pumper, frameTimeMs);
    }

    void PumperService::Replay(Entry &entry, unsigned int frameTimeMs) {
        auto *pumper = entry.pumper;
        // The customer's own Start leads the host's report of it by a round
        // trip; until the report names the car, the local greeting stands.
        const bool leading = entry.customerCarId != 0 && entry.target.carId != entry.customerCarId && std::chrono::steady_clock::now() - entry.greetedAt < kGreetingEcho;
        if (entry.hasTarget && !leading) {
            const auto &target = entry.target;
            const auto position = ToNative(target.position);
            const auto direction = ToNative(target.direction);
            if (!entry.hasShown) {
                entry.hasShown = true;
                entry.shown = target.state;
                entry.position = position;
                entry.direction = direction;
            }
            else if (entry.shown != target.state) {
                const uint8_t from = entry.shown;
                entry.shown = target.state;
                OnReplayedTransition(entry, from, target.state);
            }
            const float dx = position.x - entry.position.x;
            const float dy = position.y - entry.position.y;
            const float dz = position.z - entry.position.z;
            if (std::sqrt(dx * dx + dy * dy + dz * dz) > kSnapDistance) {
                entry.position = position;
                entry.direction = direction;
            }
            else {
                const float follow = 1.0f - std::exp(-kFollowRate * static_cast<float>(frameTimeMs) / 1000.0f);
                entry.position = {entry.position.x + dx * follow, entry.position.y + dy * follow, entry.position.z + dz * follow};
                SDK::Player::Vector3 turned {entry.direction.x + (direction.x - entry.direction.x) * follow, entry.direction.y + (direction.y - entry.direction.y) * follow,
                                             entry.direction.z + (direction.z - entry.direction.z) * follow};
                const float length = std::sqrt(turned.x * turned.x + turned.y * turned.y + turned.z * turned.z);
                entry.direction = length > 0.001f ? SDK::Player::Vector3 {turned.x / length, turned.y / length, turned.z / length} : direction;
            }
            if (target.animation >= 0) {
                pumper->SetAnimation(target.animation, target.finite, target.track);
            }
            pumper->SetState(static_cast<State>(target.state));
        }
        // An animation track moves the actor inside the tick; the host's
        // position, which already has it, is put back every frame.
        pumper->Actor().SetStoredTransform(entry.position, entry.direction);

        // AI's idle state with a timer that never runs out does nothing but
        // AI's tail: SetAnim, the animation machine and the frame tick, which
        // fires the animation's notify events. The fuel flag StartPumpFuel
        // sets is cleared first, so FUELING's refill never reads m_pCar.
        const auto state = pumper->GetState();
        pumper->SetState(State::Idle);
        pumper->SetActionTimer(0, std::numeric_limits<uint32_t>::max());
        pumper->SetPumpingFuel(false);
        gAIOriginal(pumper, frameTimeMs);
        if (state == State::Fueling) {
            if (auto *hose = pumper->HoseModel()) {
                hose->Tick(static_cast<int>(frameTimeMs));
            }
        }
        pumper->SetState(state);
    }

    void PumperService::OnReplayedTransition(Entry &entry, uint8_t from, uint8_t to) {
        auto *pumper = entry.pumper;
        auto *frame = pumper->Actor().Frame();
        if (from == Shared::Pumper::kFueling) {
            // A service given up in the middle of the fuel animation never
            // reaches its event 43.
            pumper->StopFuelSound();
        }
        switch (to) {
        case Shared::Pumper::kRequestDialogue:
            pumper->PutOutSmoking();
            if (!(entry.customerCarId != 0 && std::chrono::steady_clock::now() - entry.greetedAt < kGreetingEcho)) {
                (void)SDK::World::DabingSpeak(frame, NativePumper::kGreetingSpeech);
            }
            break;
        case Shared::Pumper::kRequestReply:
        case Shared::Pumper::kBusyReply:
            if (auto *model = CustomerModel(entry, entry.target.carId)) {
                (void)SDK::World::DabingSpeak(model, to == Shared::Pumper::kRequestReply ? NativePumper::kRequestReplySpeech : NativePumper::kBusyReplySpeech);
            }
            if (to == Shared::Pumper::kBusyReply && (from == Shared::Pumper::kFueling || from == Shared::Pumper::kPumpReady)) {
                pumper->RewindHose();
            }
            break;
        case Shared::Pumper::kBusyDialogue:
            (void)SDK::World::DabingSpeak(frame, pumper->BusySpeech());
            break;
        case Shared::Pumper::kFueling:
            pumper->RewindHose();
            break;
        default: break;
        }
    }

    SDK::Scene::NativeFrame *PumperService::CustomerModel(const Entry &entry, uint64_t carId) const {
        // Retail has C_game::m_pPlayer answer; the customer here is the
        // served car's driver.
        auto *car = static_cast<SDK::Seat::NativeCar *>(ResolveCar(carId));
        if (!car) {
            return nullptr;
        }
        auto *driver = car->GetOwner(0);
        return driver ? driver->Frame() : nullptr;
    }

    void PumperService::ChooseNextAction(NativePumper *pumper) {
        auto *entry = Find(pumper);
        if (!entry) {
            gChooseNextActionOriginal(pumper);
            return;
        }
        auto *frame = pumper->Actor().Frame();
        switch (pumper->GetState()) {
        case State::RequestDialogue:
        case State::BusyDialogue: {
            // The player's answer, spoken by the customer's model on the host.
            const bool request = pumper->GetState() == State::RequestDialogue;
            pumper->SetState(request ? State::RequestReply : State::BusyReply);
            auto *model = CustomerModel(*entry, entry->serviceCarId);
            pumper->SetDialogueChannel(SDK::World::DabingSpeak(model ? model : frame, request ? NativePumper::kRequestReplySpeech : NativePumper::kBusyReplySpeech));
            return;
        }
        case State::PumpReady:
            if (!pumper->Car()) {
                // FUELING's rate reads m_pCar: without a car he goes back.
                pumper->SetState(State::BusyReply);
                pumper->SetAnimation(NativePumper::IdleAnimation(), false, false);
                return;
            }
            break;
        case State::Walking:
            if (const void *next = pumper->NextNode()) {
                const int32_t id = NativePumper::NodeId(next);
                if (id == pumper->RouteEndNode() && !pumper->Car()) {
                    // A service given up on his way out: he turns around.
                    pumper->SetNodes(pumper->NextNode(), pumper->CurrentNode());
                    return;
                }
                if (id == pumper->RouteStartNode()) {
                    // The retail release, without the car: its driver's
                    // client releases it when the server passes this on.
                    const bool served = pumper->Car() != nullptr;
                    pumper->SetCar(nullptr);
                    gChooseNextActionOriginal(pumper);
                    if (served && entry->serviceCarId != 0) {
                        HostReport(*entry, Shared::Pumper::Progress::Kind::Finished, entry->serviceCarId);
                        entry->serviceCarId = 0;
                    }
                    return;
                }
            }
            break;
        default: break;
        }
        gChooseNextActionOriginal(pumper);
    }

    void PumperService::RequestService(NativePumper *pumper, void *car) {
        auto *entry = Find(pumper);
        if (!entry || !_world || !_world->IsReady() || _pending || !LocalDriver(car)) {
            return;
        }
        for (const auto &other : _pumpers) {
            if (other.customerCarId != 0) {
                return;
            }
        }
        const auto handle = _world->NativeObjects().FindByNative(car);
        if (handle.networkId == 0) {
            return;
        }
        Shared::Pumper::Request request;
        request.missionGeneration = _world->LoadedMissionGeneration();
        request.index = static_cast<uint8_t>(entry - _pumpers.data());
        request.nameHash = entry->nameHash;
        request.carId = handle.networkId;
        request.kind = Shared::Pumper::Request::Kind::Serve;
        Peer()->BroadcastRPC(request);
        _pending = true;
        _pendingAt = std::chrono::steady_clock::now();
    }

    void PumperService::OnFuelled(NativePumper *pumper) {
        if (!Simulation::IsHost()) {
            return;
        }
        if (auto *entry = Find(pumper); entry && entry->serviceCarId != 0 && pumper->Car()) {
            HostReport(*entry, Shared::Pumper::Progress::Kind::Fuelled, entry->serviceCarId);
        }
    }

    void PumperService::OnCommand(const Shared::Pumper::Command &command) {
        using Kind = Shared::Pumper::Command::Kind;
        if (!_world || !_world->IsReady() || command.missionGeneration != _world->LoadedMissionGeneration() || command.index >= _pumpers.size() ||
            !_pumpers[command.index].pumper) {
            if (!Simulation::IsHost()) {
                _pending = false;
            }
            return;
        }
        auto &entry = _pumpers[command.index];
        if (Simulation::IsHost()) {
            if (command.kind == Kind::Serve) {
                HostServe(entry, command.carId);
            }
            else if (command.kind == Kind::Abort && entry.serviceCarId == command.carId) {
                HostAbandon(entry);
            }
            return;
        }
        switch (command.kind) {
        case Kind::Accepted:
            _pending = false;
            CustomerStart(entry, command.carId);
            break;
        case Kind::Refused: _pending = false; break;
        case Kind::Release:
            if (entry.customerCarId == command.carId) {
                CustomerRelease(entry);
            }
            break;
        default: break;
        }
    }

    void PumperService::HostServe(Entry &entry, uint64_t carId) {
        auto *pumper = entry.pumper;
        void *car = ResolveCar(carId);
        if (!car || entry.serviceCarId != 0 || NativePumper::Pumping(pumper->GetState())) {
            HostReport(entry, Shared::Pumper::Progress::Kind::Abandoned, carId);
            return;
        }
        // Start's attendant half (0x4801b0-0x48028c); its car half runs on
        // the driver's client, which simulates the car.
        pumper->SetState(State::RequestDialogue);
        pumper->SetAnimation(NativePumper::IdleAnimation(), false, false);
        pumper->SetDialogueChannel(SDK::World::DabingSpeak(pumper->Actor().Frame(), NativePumper::kGreetingSpeech));
        pumper->PutOutSmoking();
        pumper->SetCar(car);
        entry.serviceCarId = carId;
        entry.outbound = true;
    }

    void PumperService::HostAbandon(Entry &entry) {
        auto *pumper = entry.pumper;
        const auto state = pumper->GetState();
        pumper->SetCar(nullptr);
        pumper->SetPumpingFuel(false);
        pumper->StopFuelSound();
        switch (state) {
        case State::RequestDialogue:
        case State::RequestReply:
            // Still at his start node.
            pumper->SetState(State::Idle);
            pumper->SetAnimation(NativePumper::IdleAnimation(), false, false);
            pumper->SetActionTimer(0, 2000);
            break;
        case State::Walking:
            if (entry.outbound) {
                // AI_walk heads for m_pNextNode; he walks back to the node he
                // came from, and on along his route to its start.
                pumper->SetNodes(pumper->NextNode(), pumper->CurrentNode());
            }
            break;
        case State::Fueling:
        case State::PumpReady:
            pumper->RewindHose();
            [[fallthrough]];
        case State::PumpTurn:
        case State::PumpTransition:
        case State::BusyDialogueStart:
        case State::BusyDialogue:
            // BUSY_REPLY waits for the dialogue slot, then turns him round
            // and walks him back (ChooseNextAction 9).
            pumper->SetState(State::BusyReply);
            pumper->SetAnimation(NativePumper::IdleAnimation(), false, false);
            break;
        default: break;
        }
        HostReport(entry, Shared::Pumper::Progress::Kind::Abandoned, entry.serviceCarId);
        entry.serviceCarId = 0;
        entry.outbound = false;
    }

    void PumperService::HostReport(Entry &entry, Shared::Pumper::Progress::Kind kind, uint64_t carId) {
        Shared::Pumper::Progress progress;
        progress.missionGeneration = _missionGeneration;
        progress.index = static_cast<uint8_t>(&entry - _pumpers.data());
        progress.carId = carId;
        progress.kind = kind;
        Peer()->BroadcastRPC(progress);
    }

    void PumperService::CustomerStart(Entry &entry, uint64_t carId) {
        void *car = ResolveCar(carId);
        auto request = [&](Shared::Pumper::Request::Kind kind) {
            Shared::Pumper::Request cancel;
            cancel.missionGeneration = _missionGeneration;
            cancel.index = static_cast<uint8_t>(&entry - _pumpers.data());
            cancel.nameHash = entry.nameHash;
            cancel.carId = carId;
            cancel.kind = kind;
            Peer()->BroadcastRPC(cancel);
        };
        // The retail Start, behind Do_AB_OwnerCar's own CanPump test: the car
        // may have rolled off the pump while the server answered, and Start
        // itself would pull a blocked one back onto it.
        if (!car || !LocalDriver(car) || entry.pumper->CanPump(car) != NativePumper::kPumpAvailable || !gStartOriginal(entry.pumper, car)) {
            request(Shared::Pumper::Request::Kind::Cancel);
            return;
        }
        entry.customerCarId = carId;
        entry.greetedAt = std::chrono::steady_clock::now();
    }

    void PumperService::CustomerRelease(Entry &entry) {
        auto *pumper = entry.pumper;
        const uint64_t carId = entry.customerCarId;
        entry.customerCarId = 0;
        void *car = pumper->Car();
        if (!car) {
            return;
        }
        auto *local = SDK::Player::CurrentPlayer();
        // Start left the car to m_pCar; without its driver (or with the car
        // gone) the release keeps to the camera and the player's controls.
        // Only with the local player still driving: the retail release unlocks
        // the controls of the car's driver if that is the player, and would
        // leave this player's locked behind a remote driver.
        auto *native = static_cast<SDK::Seat::NativeCar *>(ResolveCar(carId));
        const bool driven = native == car && LocalDriver(car) != nullptr;
        if (!driven) {
            if (native == car) {
                // Start's control override would hold the car's power at 0
                // for whoever drives it next.
                NativePumper::ReleaseControlOverride(car);
            }
            pumper->SetCar(nullptr);
        }
        // The release is ChooseNextAction's WALKING branch for the start
        // node (camera PopMode through LockControls(false) at 0x47fea9), run
        // on this replica with his replayed state put back afterwards.
        const auto state = pumper->GetState();
        const int32_t animation = pumper->Animation();
        const bool finite = pumper->AnimationFinite();
        const bool track = pumper->AnimationTrack();
        void *current = pumper->CurrentNode();
        void *next = pumper->NextNode();
        pumper->SetState(State::Walking);
        pumper->SetNodes(entry.firstNode, entry.startNode);
        gChooseNextActionOriginal(pumper);
        pumper->SetState(state);
        pumper->SetAnimation(animation, finite, track);
        pumper->SetNodes(current, next);
        pumper->SetCar(nullptr);
        pumper->Actor().SetStoredTransform(entry.position, entry.direction);
        if (!driven && local) {
            SDK::Police::LockPlayerControls(local, false);
        }
    }

    void PumperService::Update(World::WorldService &world) {
        if (!world.IsReady()) {
            _missionGeneration = 0;
            return;
        }
        if (_missionGeneration != world.LoadedMissionGeneration()) {
            _missionGeneration = world.LoadedMissionGeneration();
            _hasReport = false;
            _pending = false;
            for (auto &entry : _pumpers) {
                entry.hasTarget = false;
                entry.hasShown = false;
                entry.reportedState = 0xff;
            }
        }
        if (!Simulation::IsHost()) {
            if (_pending && std::chrono::steady_clock::now() - _pendingAt > kPendingTimeout) {
                _pending = false;
            }
            // The car Start took went while he served it.
            for (auto &entry : _pumpers) {
                if (entry.pumper && entry.customerCarId != 0 && entry.pumper->Car() && ResolveCar(entry.customerCarId) != entry.pumper->Car()) {
                    CustomerRelease(entry);
                }
            }
            return;
        }
        bool changed = false;
        for (auto &entry : _pumpers) {
            if (entry.pumper) {
                changed = changed || entry.reportedState != static_cast<uint8_t>(entry.pumper->GetState()) || entry.reportedAnimation != entry.pumper->Animation() ||
                          entry.reportedCarId != entry.serviceCarId;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (changed || now - _lastSnapshot >= std::chrono::milliseconds(1000 / Shared::Pumper::Snapshot::kSnapshotHz)) {
            _lastSnapshot = now;
            SendSnapshot(changed);
        }
    }

    void PumperService::SendSnapshot(bool reliable) {
        Shared::Pumper::Snapshot snapshot;
        snapshot.missionGeneration = _missionGeneration;
        snapshot.hostSession = Simulation::HostSession();
        snapshot.hostTimeMs = static_cast<uint32_t>(MafiaNet::GetTime());
        for (size_t index = 0; index < _pumpers.size(); ++index) {
            auto &entry = _pumpers[index];
            if (!entry.pumper) {
                continue;
            }
            auto *pumper = entry.pumper;
            auto &state = snapshot.pumpers[snapshot.count++];
            state.index = static_cast<uint8_t>(index);
            state.nameHash = entry.nameHash;
            state.state = static_cast<uint8_t>(std::clamp<int32_t>(static_cast<int32_t>(pumper->GetState()), 0, Shared::Pumper::kStateCount - 1));
            state.animation = std::clamp<int32_t>(pumper->Animation(), -1, Shared::Pumper::kMaxAnimation);
            state.finite = pumper->AnimationFinite();
            state.track = pumper->AnimationTrack();
            state.position = ToWire(pumper->Actor().Position());
            state.direction = ToWire(pumper->Actor().Direction());
            state.carId = entry.serviceCarId;
            entry.reportedState = state.state;
            entry.reportedAnimation = pumper->Animation();
            entry.reportedCarId = entry.serviceCarId;
        }
        if (!snapshot.count) {
            return;
        }
        Peer()->BroadcastRPC(snapshot, MafiaNet::Priority::High, reliable ? MafiaNet::Reliability::ReliableOrdered : MafiaNet::Reliability::Unreliable);
    }
} // namespace Mafia1Online::Features::Pumper
