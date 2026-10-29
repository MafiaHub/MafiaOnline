#include <utils/safe_win32.h>

#include "bridge_service.h"

#include "features/death/death_service.h"
#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/world/native_bridge.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Bridge {
    namespace {
        using SDK::World::NativeBridge;
        using SDK::World::NativeBridgeRuntime;

        using Lifecycle = void(__thiscall *)(NativeBridge *);
        using Tick = void(__thiscall *)(NativeBridge *, unsigned int);
        using Collide = bool(__thiscall *)(NativeBridge *, SDK::Player::NativeActor *, const void *, const void *, const void *, float, void *);
        Lifecycle gGameInitOriginal = nullptr;
        Lifecycle gGameDoneOriginal = nullptr;
        Tick gUpdateOriginal = nullptr;
        Collide gCollisionOriginal = nullptr;
        BridgeService *gService = nullptr;

        // Two host reports behind: one lost packet still leaves a bracket.
        constexpr double kRenderDelayMs = 2000.0 / Shared::Bridge::Snapshot::kSnapshotHz;
        // Past the newest report the leaves keep moving as the last two did,
        // within one phase, rather than stop until the next arrives.
        constexpr double kMaxExtrapolationMs = 300.0;
        // A steamer sails at a steady 5 m/s and brakes at 1 m/s^2 (C_bridge
        // Update), so it carries on further: a late report would otherwise
        // stop it dead and jump it forward when it lands.
        constexpr double kMaxSteamerExtrapolationMs = 1000.0;
        // Update divides a steamer's progress past 1 by its step; it never
        // gets there with the speed of 0 players give it.
        constexpr float kMaxProgress = 0.999f;
        // A host silent this long has gone: the bridge closes until the next
        // one reports, so it never blocks the river road raised.
        constexpr auto kHostSilence = std::chrono::seconds(3);

        void __fastcall GameInitHook(NativeBridge *bridge, void *) {
            gGameInitOriginal(bridge);
            if (gService) {
                gService->OnGameInit(bridge);
            }
        }

        void __fastcall GameDoneHook(NativeBridge *bridge, void *) {
            if (gService) {
                gService->OnGameDone(bridge);
            }
            gGameDoneOriginal(bridge);
        }

        void __fastcall UpdateHook(NativeBridge *bridge, void *, unsigned int frameTimeMs) {
            if (gService && !Simulation::IsHost()) {
                gService->BeforeUpdate(bridge);
            }
            gUpdateOriginal(bridge, frameTimeMs);
        }

        // C_bridge::Collision (0x56a7e0, ret 18h): a raised leaf zeroes a
        // player's health (+0x644) and hits it with a fall, which leaves the
        // protected local player alive (Death runs only inside Hit). The
        // local player reports the death as a fall; replicas keep the
        // server's health.
        bool __fastcall CollisionHook(NativeBridge *bridge, void *, SDK::Player::NativeActor *actor, const void *position, const void *direction, const void *normal, float force,
                                      void *frame) {
            if (!actor || actor->GetType() != SDK::Player::NativeActor::Type::Player) {
                return gCollisionOriginal(bridge, actor, position, direction, normal, force, frame);
            }
            if (gService && actor == SDK::Player::CurrentPlayer()) {
                gService->OnCrushed(*static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor)));
            }
            return false;
        }

        const Shared::Bridge::State *Find(const Shared::Bridge::Snapshot &snapshot, int index) {
            for (uint8_t i = 0; i < snapshot.count; ++i) {
                if (snapshot.bridges[i].index == index) {
                    return &snapshot.bridges[i];
                }
            }
            return nullptr;
        }

        // Metres sailed along the steamer's route. Segments differ in length,
        // so blending point + progress would change its speed at each point.
        float RouteDistance(const Shared::Bridge::Steamer &steamer) {
            float distance = 0.0f;
            for (uint8_t point = 0; point < steamer.point; ++point) {
                distance += NativeBridgeRuntime::SegmentLength(steamer.route, point);
            }
            return distance + steamer.progress * NativeBridgeRuntime::SegmentLength(steamer.route, steamer.point);
        }

        float RouteLength(uint8_t route) {
            float length = 0.0f;
            for (uint8_t point = 0; point < Shared::Bridge::kSegmentCount; ++point) {
                length += NativeBridgeRuntime::SegmentLength(route, point);
            }
            return length;
        }
    } // namespace

    bool BridgeService::Install(Death::DeathService &death) {
        gService = this;
        _death = &death;
        if (MH_CreateHook(reinterpret_cast<void *>(NativeBridge::kGameInit), reinterpret_cast<void *>(&GameInitHook), reinterpret_cast<void **>(&gGameInitOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(NativeBridge::kGameDone), reinterpret_cast<void *>(&GameDoneHook), reinterpret_cast<void **>(&gGameDoneOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(NativeBridge::kUpdate), reinterpret_cast<void *>(&UpdateHook), reinterpret_cast<void **>(&gUpdateOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(NativeBridge::kCollision), reinterpret_cast<void *>(&CollisionHook), reinterpret_cast<void **>(&gCollisionOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(NativeBridge::kGameInit)) != MH_OK || MH_EnableHook(reinterpret_cast<void *>(NativeBridge::kGameDone)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(NativeBridge::kUpdate)) != MH_OK || MH_EnableHook(reinterpret_cast<void *>(NativeBridge::kCollision)) != MH_OK) {
            Uninstall();
            return false;
        }
        return true;
    }

    void BridgeService::Uninstall() {
        for (uintptr_t address : {NativeBridge::kGameInit, NativeBridge::kGameDone, NativeBridge::kUpdate, NativeBridge::kCollision}) {
            MH_DisableHook(reinterpret_cast<void *>(address));
            MH_RemoveHook(reinterpret_cast<void *>(address));
        }
        gGameInitOriginal = nullptr;
        gGameDoneOriginal = nullptr;
        gUpdateOriginal = nullptr;
        gCollisionOriginal = nullptr;
        gService = nullptr;
        _death = nullptr;
        Reset();
    }

    void BridgeService::OnCrushed(SDK::Player::NativeHuman &human) {
        if (_death) {
            _death->OnSuppressedFatalFall(human, Shared::Combat::FatalFallReport::Cause::FallVolume);
        }
    }

    void BridgeService::RegisterRPC() {
        if (Simulation::IsHost()) {
            return;
        }
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Bridge::Snapshot>([this](const Shared::Bridge::Snapshot &snapshot, MafiaNet::Packet *) {
            if (snapshot.missionGeneration == 0 || snapshot.missionGeneration != _missionGeneration) {
                return;
            }
            for (uint8_t i = 0; i < snapshot.count; ++i) {
                const auto &state = snapshot.bridges[i];
                if (!Shared::Bridge::Valid(state) || state.index >= _bridges.size() || _bridges[state.index].nameHash != state.nameHash) {
                    if (!_reportedMismatch) {
                        _reportedMismatch = true;
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Bridge report {} does not match the {} bridges loaded here", state.index, _bridges.size());
                    }
                    return;
                }
            }
            for (uint8_t i = 0; i < snapshot.steamerCount; ++i) {
                if (!Shared::Bridge::Valid(snapshot.steamers[i])) {
                    return;
                }
            }
            if (snapshot.hostSession != _hostSession) {
                // Another simulation host, with its own clock.
                _hostSession = snapshot.hostSession;
                _samples.clear();
                _clockOffsets.clear();
            }
            else if (!_samples.empty() && static_cast<int32_t>(snapshot.hostTimeMs - _samples.back().hostTimeMs) <= 0) {
                // A phase change comes reliably and may overtake, or trail,
                // the periodic reports.
                return;
            }
            _lastSampleAt = std::chrono::steady_clock::now();
            // The least delayed packet dates the host clock here.
            _clockOffsets.push_back(LocalMs() - static_cast<double>(snapshot.hostTimeMs));
            while (_clockOffsets.size() > 32) {
                _clockOffsets.pop_front();
            }
            _clockOffset = *std::min_element(_clockOffsets.begin(), _clockOffsets.end());
            _hasClockOffset = true;
            if (!_reportedPlacement) {
                _reportedPlacement = true;
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Placing {} bridges from the simulation host", _bridges.size());
            }
            _samples.push_back({snapshot.hostTimeMs, snapshot});
            while (_samples.size() > 16) {
                _samples.pop_front();
            }
        });
    }

    void BridgeService::Reset() {
        _bridges.clear();
        _samples.clear();
        _clockOffsets.clear();
        _hasClockOffset = false;
        _hostSession = 0;
        _reportedPhases.fill(0xff);
        _lastSnapshot = {};
        _reportedPlacement = false;
        _reportedMismatch = false;
    }

    double BridgeService::LocalMs() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void BridgeService::OnGameInit(NativeBridge *bridge) {
        if (_bridges.size() >= Shared::Bridge::kMaxBridges) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("More than {} bridges in the mission; the rest stay unsynchronized", Shared::Bridge::kMaxBridges);
            return;
        }
        auto *frame = bridge->Actor().Frame();
        _bridges.push_back({bridge, Shared::Bridge::NameHash(frame ? frame->Name() : "")});
    }

    void BridgeService::OnGameDone(NativeBridge *bridge) {
        // Indices are load order: the rest keep theirs. A mission closes all
        // of its bridges; the next one numbers its own from zero.
        bool remaining = false;
        for (auto &entry : _bridges) {
            if (entry.bridge == bridge) {
                entry.bridge = nullptr;
            }
            remaining = remaining || entry.bridge;
        }
        if (!remaining) {
            _bridges.clear();
            _samples.clear();
        }
    }

    int BridgeService::IndexOf(const NativeBridge *bridge) const {
        for (size_t i = 0; i < _bridges.size(); ++i) {
            if (_bridges[i].bridge == bridge) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void BridgeService::Prepare(Entry &entry) {
        if (!Simulation::IsHost()) {
            entry.bridge->Actor().SetNetworkControlled(true);
            // GameInit's warm-up ran a minute of local AI; until the host
            // reports, the bridge stands closed and silent.
            if (entry.bridge->GetPhase() != NativeBridge::Phase::Closed) {
                entry.bridge->EnterPhase(NativeBridge::Phase::Closed);
            }
            entry.bridge->SetRuntimeOpen(false);
            entry.bridge->PlaceLeaves(0.0f);
        }
        entry.prepared = true;
    }

    void BridgeService::Update(World::WorldService &world) {
        if (!world.IsReady()) {
            _missionGeneration = 0;
            return;
        }
        if (_missionGeneration != world.LoadedMissionGeneration()) {
            // A new mission's host clock and reports.
            _missionGeneration = world.LoadedMissionGeneration();
            _samples.clear();
            _clockOffsets.clear();
            _hasClockOffset = false;
            _reportedPhases.fill(0xff);
        }
        for (auto &entry : _bridges) {
            if (entry.bridge && !entry.prepared) {
                Prepare(entry);
            }
        }
        if (!Simulation::IsHost()) {
            if (!_samples.empty() && std::chrono::steady_clock::now() - _lastSampleAt > kHostSilence) {
                CloseWithoutHost();
            }
            return;
        }
        bool changed = false;
        for (size_t index = 0; index < _bridges.size(); ++index) {
            if (_bridges[index].bridge) {
                changed = changed || _reportedPhases[index] != static_cast<uint8_t>(_bridges[index].bridge->GetPhase());
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (changed || now - _lastSnapshot >= std::chrono::milliseconds(1000 / Shared::Bridge::Snapshot::kSnapshotHz)) {
            _lastSnapshot = now;
            SendSnapshot(world, changed);
        }
    }

    void BridgeService::CloseWithoutHost() {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("No bridge report from the simulation host for {} s; closing the bridges", kHostSilence.count());
        for (auto &entry : _bridges) {
            if (!entry.bridge || !entry.prepared) {
                continue;
            }
            if (entry.bridge->GetPhase() != NativeBridge::Phase::Closed) {
                entry.bridge->EnterPhase(NativeBridge::Phase::Closed);
            }
            entry.bridge->SetRuntimeOpen(false);
            entry.bridge->PlaceLeaves(0.0f);
        }
        _samples.clear();
        _clockOffsets.clear();
        _hasClockOffset = false;
        _hostSession = 0;
    }

    void BridgeService::SendSnapshot(World::WorldService &world, bool reliable) {
        Shared::Bridge::Snapshot snapshot;
        snapshot.missionGeneration = world.LoadedMissionGeneration();
        snapshot.hostSession = Simulation::HostSession();
        snapshot.hostTimeMs = static_cast<uint32_t>(MafiaNet::GetTime());
        bool right = false;
        for (size_t index = 0; index < _bridges.size(); ++index) {
            auto *bridge = _bridges[index].bridge;
            if (!bridge) {
                continue;
            }
            auto &state = snapshot.bridges[snapshot.count++];
            state.index = static_cast<uint8_t>(index);
            state.nameHash = _bridges[index].nameHash;
            state.phase = static_cast<uint8_t>(std::clamp<int32_t>(static_cast<int32_t>(bridge->GetPhase()), 0, Shared::Bridge::kPhaseCount - 1));
            state.phaseTimerMs = std::clamp(bridge->PhaseTimer(), -Shared::Bridge::kMaxPhaseTimerMs, Shared::Bridge::kMaxPhaseTimerMs);
            state.angle = bridge->Angle();
            const auto position = bridge->Actor().Frame()->WorldPosition();
            state.position = {position.x, position.y, position.z};
            _reportedPhases[index] = state.phase;
            right = right || bridge->IsRight();
        }
        if (right) {
            snapshot.steamerCount = Shared::Bridge::kSteamerCount;
            const auto &runtime = NativeBridgeRuntime::Get();
            for (uint8_t i = 0; i < Shared::Bridge::kSteamerCount; ++i) {
                const auto &steamer = runtime.steamers[i];
                if (steamer.route >= Shared::Bridge::kRouteCount || steamer.point >= Shared::Bridge::kSegmentCount) {
                    snapshot.steamerCount = 0;
                    break;
                }
                snapshot.steamers[i] = {static_cast<uint8_t>(steamer.route), static_cast<uint8_t>(steamer.point), std::clamp(steamer.progress, 0.0f, 1.0f)};
            }
        }
        if (!snapshot.count) {
            return;
        }
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(snapshot, MafiaNet::Priority::High, reliable ? MafiaNet::Reliability::ReliableOrdered : MafiaNet::Reliability::Unreliable);
        if (!_reportedPlacement) {
            _reportedPlacement = true;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Reporting {} bridges to the server", snapshot.count);
        }
    }

    void BridgeService::BeforeUpdate(NativeBridge *bridge) {
        const int index = IndexOf(bridge);
        if (index < 0 || !_bridges[index].prepared || _samples.empty() || !_hasClockOffset) {
            return;
        }
        // Host times are dated relative to the newest sample, as a 32-bit
        // millisecond clock wraps.
        const double hostTime = LocalMs() - _clockOffset - kRenderDelayMs;
        const uint32_t newestTime = _samples.back().hostTimeMs;
        const double target = static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(hostTime)) - newestTime));
        const auto time = [&](const Sample &sample) { return -static_cast<double>(static_cast<int32_t>(newestTime - sample.hostTimeMs)); };
        // The first pair whose later sample is not before the render time;
        // past the newest the last pair is carried on (u above 1), before the
        // oldest it clamps.
        const Sample *a = &_samples.back();
        const Sample *b = a;
        float u = 1.0f;
        float steamerU = 1.0f;
        if (target >= 0.0 && _samples.size() > 1) {
            a = &_samples[_samples.size() - 2];
            const double span = -time(*a);
            u = span > 0.0 ? static_cast<float>(1.0 + std::min(target, kMaxExtrapolationMs) / span) : 1.0f;
            steamerU = span > 0.0 ? static_cast<float>(1.0 + std::min(target, kMaxSteamerExtrapolationMs) / span) : 1.0f;
        }
        else if (target < 0.0 && _samples.size() > 1) {
            size_t later = 1;
            while (later + 1 < _samples.size() && time(_samples[later]) < target) {
                ++later;
            }
            a = &_samples[later - 1];
            b = &_samples[later];
            const double timeA = time(*a);
            const double timeB = time(*b);
            u = timeB > timeA ? static_cast<float>(std::clamp((target - timeA) / (timeB - timeA), 0.0, 1.0)) : 1.0f;
            steamerU = u;
        }
        const auto *from = Find(a->snapshot, index);
        const auto *to = Find(b->snapshot, index);
        if (!from || !to) {
            from = to = from ? from : to;
        }
        if (!from) {
            return;
        }
        // Across a phase change the motion of the last pair says nothing
        // about the next phase's.
        if (u > 1.0f && from->phase != to->phase) {
            u = 1.0f;
        }
        // The phase changes when the later report is reached; the leaves and
        // the timer move between the two.
        const auto phase = static_cast<NativeBridge::Phase>(u < 1.0f ? from->phase : to->phase);
        if (bridge->GetPhase() != phase) {
            bridge->EnterPhase(phase);
        }
        bridge->SetPhaseTimer(static_cast<int32_t>(std::max<long>(0, std::lround(from->phaseTimerMs + (to->phaseTimerMs - from->phaseTimerMs) * static_cast<double>(u)))));
        bridge->SetRuntimeOpen(phase == NativeBridge::Phase::Open);
        bridge->PlaceLeaves(from->angle + (to->angle - from->angle) * u);

        if (!bridge->IsRight() || a->snapshot.steamerCount != Shared::Bridge::kSteamerCount || b->snapshot.steamerCount != Shared::Bridge::kSteamerCount) {
            return;
        }
        auto &runtime = NativeBridgeRuntime::Get();
        for (uint8_t i = 0; i < Shared::Bridge::kSteamerCount; ++i) {
            const auto &steamerA = a->snapshot.steamers[i];
            const auto &steamerB = b->snapshot.steamers[i];
            // Along one route the distance only grows, and past the newest
            // report it goes on at the last pair's speed. Update's wrap picks
            // the next route at random, so a steamer waits at its route's end
            // for the report naming it, and starts over once that is reached.
            uint8_t route = steamerB.route;
            float distance = RouteDistance(steamerB);
            if (steamerA.route == steamerB.route && RouteDistance(steamerB) >= RouteDistance(steamerA)) {
                distance = RouteDistance(steamerA) + (RouteDistance(steamerB) - RouteDistance(steamerA)) * steamerU;
            }
            else if (steamerU < 1.0f) {
                route = steamerA.route;
                distance = RouteDistance(steamerA);
            }
            distance = std::min(distance, RouteLength(route));
            uint32_t point = 0;
            while (point + 1 < Shared::Bridge::kSegmentCount && distance > NativeBridgeRuntime::SegmentLength(route, point)) {
                distance -= NativeBridgeRuntime::SegmentLength(route, point);
                ++point;
            }
            auto &steamer = runtime.steamers[i];
            steamer.route = route;
            steamer.point = point;
            steamer.progress = std::clamp(distance / NativeBridgeRuntime::SegmentLength(route, point), 0.0f, kMaxProgress);
            steamer.speed = 0.0f;
        }
    }
} // namespace Mafia1Online::Features::Bridge
