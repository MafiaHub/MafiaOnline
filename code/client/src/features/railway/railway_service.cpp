#include <utils/safe_win32.h>

#include "railway_service.h"

#include "features/seat/seat_hooks.h"
#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"
#include "shared/features/player/player_entity.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/rail/native_railway.h>
#include <mafia1/sdk/scene/native_scene.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <mafia1/sdk/traffic/native_traffic_car.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Mafia1Online::Features::Railway {
    namespace {
        using SDK::Rail::NativeRailway;
        using SDK::Player::Vector3;

        using Lifecycle = void(__thiscall *)(NativeRailway *);
        Lifecycle gGameInitOriginal = nullptr;
        Lifecycle gGameDoneOriginal = nullptr;
        RailwayService *gService = nullptr;

        // Two host reports behind: one lost packet still leaves a bracket.
        constexpr double kRenderDelayMs = 200.0;
        // A lost far report leaves a gap of two far intervals.
        constexpr double kMaximumRenderDelayMs = 2000.0;
        // Delay change per frame millisecond: a railway runs a quarter slower
        // or faster for a moment rather than jumping.
        constexpr double kRenderDelayEaseRate = 0.25;
        // Past the newest report, continue along it for at most this long.
        constexpr double kExtrapolationMs = 300.0;
        // A hit driver stops a railway until Update's out-of-range reset,
        // which never comes on the host: every railway counts as near.
        constexpr auto kDriverHitStop = std::chrono::seconds(30);
        constexpr uint8_t kNearBranchJnz = 0x75;
        constexpr uint8_t kNearBranchJmp = 0xeb;

        void __fastcall GameInitHook(NativeRailway *railway, void *) {
            gGameInitOriginal(railway);
            if (gService) {
                gService->OnGameInit(railway);
            }
        }

        void __fastcall GameDoneHook(NativeRailway *railway, void *) {
            if (gService) {
                gService->OnGameDone(railway);
            }
            gGameDoneOriginal(railway);
        }

        bool WriteCodeByte(uintptr_t address, uint8_t value) {
            DWORD previous = 0;
            if (!VirtualProtect(reinterpret_cast<void *>(address), 1, PAGE_EXECUTE_READWRITE, &previous)) {
                return false;
            }
            *reinterpret_cast<uint8_t *>(address) = value;
            VirtualProtect(reinterpret_cast<void *>(address), 1, previous, &previous);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(address), 1);
            return true;
        }

        Shared::Railway::Vector ToWire(const Vector3 &vector) {
            return {vector.x, vector.y, vector.z};
        }
        glm::vec3 Vec(const Shared::Railway::Vector &vector) {
            return {vector.x, vector.y, vector.z};
        }
        Vector3 ToNative(const glm::vec3 &vector) {
            return {vector.x, vector.y, vector.z};
        }
        glm::vec3 Direction(const glm::vec3 &a, const glm::vec3 &b, float u) {
            const glm::vec3 mixed = glm::mix(a, b, u);
            const float length = glm::length(mixed);
            return length > 0.0001f ? mixed / length : b;
        }

        // A railway part's basis: its direction with the world up. Every
        // client builds it the same way from its copy of the railway.
        struct Basis {
            glm::vec3 right;
            glm::vec3 up;
            glm::vec3 forward;
        };
        Basis MakeBasis(const glm::vec3 &direction) {
            const glm::vec3 forward = glm::length(direction) > 0.0001f ? glm::normalize(direction) : glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward);
            right = glm::length(right) > 0.0001f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
            return {right, glm::cross(forward, right), forward};
        }
        glm::vec3 Vec(const Vector3 &vector) {
            return {vector.x, vector.y, vector.z};
        }
        // Part 0 is the body, part n the wagon n - 1.
        bool PartPose(NativeRailway &railway, uint8_t part, glm::vec3 &origin, glm::vec3 &direction) {
            if (part == 0) {
                origin = Vec(railway.Actor().Position());
                direction = Vec(railway.Actor().Direction());
                return true;
            }
            if (part > railway.WagonCount()) {
                return false;
            }
            origin = Vec(railway.WagonPositions()[part - 1]);
            direction = Vec(railway.WagonDirections()[part - 1]);
            return true;
        }

        // A world pose in the basis of the railway part nearest to it.
        void LocalPose(NativeRailway &railway, const glm::vec3 &position, const glm::vec3 &facing, uint8_t &part, glm::vec3 &local, float &yaw) {
            float nearest = std::numeric_limits<float>::max();
            glm::vec3 origin {};
            glm::vec3 direction {};
            part = 0;
            for (uint8_t candidate = 0; candidate <= std::min<int32_t>(railway.WagonCount(), Shared::Railway::kMaxWagons); ++candidate) {
                glm::vec3 partOrigin;
                glm::vec3 partDirection;
                if (PartPose(railway, candidate, partOrigin, partDirection) && glm::distance(partOrigin, position) < nearest) {
                    nearest = glm::distance(partOrigin, position);
                    part = candidate;
                    origin = partOrigin;
                    direction = partDirection;
                }
            }
            const auto basis = MakeBasis(direction);
            const glm::vec3 offset = position - origin;
            local = {glm::dot(offset, basis.right), glm::dot(offset, basis.up), glm::dot(offset, basis.forward)};
            yaw = std::atan2(glm::dot(facing, basis.right), glm::dot(facing, basis.forward));
        }

        // ranims index 1, walk1.i3d.
        constexpr uint8_t kWalkAnimation = 1;
        // Players ease a puppet across one passenger report.
        constexpr auto kPuppetEase = std::chrono::milliseconds(1000 / Shared::Railway::Passengers::kPassengersHz);
        // A railway the host stopped listing (out of this player's range).
        constexpr auto kPassengersTimeout = std::chrono::seconds(2);
        // A finite (death) animation holds its last pose after this.
        constexpr int kFiniteAnimationMs = 5000;
        constexpr int kMaximumPuppetTickMs = 100;
        // A puppet takes its scene sector again after moving this far.
        constexpr float kPuppetSectorStep = 4.0f;

        float LerpAngle(float a, float b, float u) {
            float delta = std::remainder(b - a, 6.2831853f);
            return a + delta * u;
        }

        uint64_t LocalPlayerId(World::WorldService &world) {
            auto *player = SDK::Player::CurrentPlayer();
            return player ? world.NativeObjects().FindByNative(player).networkId : 0;
        }

        // C_player::Do_AB_OwnerNULL turns the player's frame off right after
        // its railway Use_Actor (0x594d54); Intern_UseRailway(false) turns it
        // back on. A step that never got the local player aboard does too.
        void ShowLocalPlayerUnlessAboard() {
            auto *player = SDK::Player::CurrentPlayer();
            const auto *human = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(player));
            if (player && player->Frame() && !human->usedActorEnter && !human->usedActorLeave) {
                player->Frame()->SetOn(true);
            }
        }
    } // namespace

    bool RailwayService::Install() {
        gService = this;
        if (MH_CreateHook(reinterpret_cast<void *>(NativeRailway::kGameInit), reinterpret_cast<void *>(&GameInitHook), reinterpret_cast<void **>(&gGameInitOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(NativeRailway::kGameDone), reinterpret_cast<void *>(&GameDoneHook), reinterpret_cast<void **>(&gGameDoneOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(NativeRailway::kGameInit)) != MH_OK || MH_EnableHook(reinterpret_cast<void *>(NativeRailway::kGameDone)) != MH_OK) {
            Uninstall();
            return false;
        }
        // The host simulates railways for players anywhere, not around its
        // own camera: AI takes its near branch for every railway.
        if (Simulation::IsHost()) {
            if (*reinterpret_cast<const uint8_t *>(NativeRailway::kNearCameraBranch) != kNearBranchJnz || !WriteCodeByte(NativeRailway::kNearCameraBranch, kNearBranchJmp)) {
                Uninstall();
                return false;
            }
            _patched = true;
        }
        Seat::SetRailwayUseHandler(&RailwayService::OnUse);
        return true;
    }

    void RailwayService::Uninstall() {
        Seat::SetRailwayUseHandler(nullptr);
        for (uintptr_t address : {NativeRailway::kGameInit, NativeRailway::kGameDone}) {
            MH_DisableHook(reinterpret_cast<void *>(address));
            MH_RemoveHook(reinterpret_cast<void *>(address));
        }
        if (_patched) {
            WriteCodeByte(NativeRailway::kNearCameraBranch, kNearBranchJnz);
            _patched = false;
        }
        gGameInitOriginal = nullptr;
        gGameDoneOriginal = nullptr;
        gService = nullptr;
        Reset();
    }

    void RailwayService::RegisterRPC() {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        if (Simulation::IsHost()) {
            // A player's step aboard holds the railway as retail Use_Actor's
            // Lock does, while it stands with its doors open.
            network->RegisterRPC<Shared::Railway::BoardRequest>([this](const Shared::Railway::BoardRequest &request, MafiaNet::Packet *) {
                Shared::Railway::BoardAnswer answer;
                answer.missionGeneration = request.missionGeneration;
                answer.index = request.index;
                answer.playerId = request.playerId;
                auto *railway = request.missionGeneration == _missionGeneration && request.index < _railways.size() ? _railways[request.index].railway : nullptr;
                const auto now = std::chrono::steady_clock::now();
                if (railway && Shared::Railway::HoldsForStep(static_cast<uint8_t>(std::min<uint32_t>(railway->State(), 0xff)), railway->Speed(), railway->IsMetro())) {
                    auto &entry = _railways[request.index];
                    if (entry.holdSince == std::chrono::steady_clock::time_point {}) {
                        entry.holdSince = now;
                    }
                    if (now - entry.holdSince < std::chrono::milliseconds(Shared::Railway::kMaxStopHoldMs)) {
                        ReleaseLocks(request.playerId);
                        railway->Lock();
                        _locks.push_back({request.playerId, request.index, now + std::chrono::milliseconds(Shared::Railway::kBoardLockLimitMs)});
                        answer.accepted = true;
                    }
                }
                Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(answer, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            });
            network->RegisterRPC<Shared::Railway::BoardDone>([this](const Shared::Railway::BoardDone &done, MafiaNet::Packet *) {
                if (done.missionGeneration == _missionGeneration) {
                    ReleaseLocks(done.playerId);
                }
            });
            return;
        }
        network->RegisterRPC<Shared::Railway::BoardAnswer>([this](const Shared::Railway::BoardAnswer &answer, MafiaNet::Packet *) {
            if (!_world || answer.missionGeneration != _missionGeneration || answer.index != _board.index || _board.approved || answer.playerId != LocalPlayerId(*_world)) {
                return;
            }
            auto *railway = answer.index < _railways.size() ? _railways[answer.index].railway : nullptr;
            auto *human = static_cast<SDK::Seat::NativeHuman *>(static_cast<void *>(SDK::Player::CurrentPlayer()));
            const bool enter = _board.action == static_cast<int>(SDK::Seat::UseAction::Enter);
            // The answer comes a round trip after the use: retail calls
            // Use_Actor only from an idle use, so the player must still be
            // alive, at the railway, and where the step starts (on foot to
            // step aboard, aboard this railway to step off).
            const bool ready = railway && human && human->actor.IsAlive() && !human->usedActorLeave &&
                               (enter ? !human->usedActorEnter : human->usedActorEnter == &railway->Actor()) && WithinReach(*railway, human);
            if (!answer.accepted || !ready) {
                // Already leaving: the step never starts, as retail AI would
                // have taken the doorway away by now.
                if (answer.accepted) {
                    FinishBoarding(*_world);
                    return;
                }
                _board = {};
                ShowLocalPlayerUnlessAboard();
                return;
            }
            _board.approved = true;
            _board.until = std::chrono::steady_clock::now() + std::chrono::milliseconds(Shared::Railway::kBoardLockLimitMs);
            Seat::UseActorNative(human, &railway->Actor(), _board.action, _board.seat, _board.animationSpeedState);
            // As C_player does after its railway Use_Actor, once the step runs.
            if (enter && human->usedActorLeave == &railway->Actor() && human->actor.Frame()) {
                human->actor.Frame()->SetOn(false);
            }
        });
        network->RegisterRPC<Shared::Railway::Snapshot>([this](const Shared::Railway::Snapshot &snapshot, MafiaNet::Packet *) {
            if (snapshot.missionGeneration == 0 || snapshot.missionGeneration != _missionGeneration) {
                return;
            }
            if (snapshot.hostSession != _hostSession) {
                // Another simulation host, with its own clock.
                _hostSession = snapshot.hostSession;
                _clockOffsets.clear();
                for (auto &entry : _railways) {
                    entry.samples.clear();
                    entry.renderDelayMs = 0.0;
                }
            }
            // The least delayed packet dates the host clock here.
            _clockOffsets.push_back(LocalMs() - static_cast<double>(snapshot.hostTimeMs));
            while (_clockOffsets.size() > 32) {
                _clockOffsets.pop_front();
            }
            _clockOffset = *std::min_element(_clockOffsets.begin(), _clockOffsets.end());
            _hasClockOffset = true;
            for (uint8_t i = 0; i < snapshot.count; ++i) {
                const auto &state = snapshot.railways[i];
                if (state.index >= _railways.size() || _railways[state.index].nameHash != state.nameHash) {
                    if (!_reportedMismatch) {
                        _reportedMismatch = true;
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Railway report {} does not match the {} railways loaded here", state.index, _railways.size());
                    }
                    continue;
                }
                if (!_reportedPlacement) {
                    _reportedPlacement = true;
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Placing {} railways from the simulation host", _railways.size());
                }
                auto &samples = _railways[state.index].samples;
                if (!samples.empty() && static_cast<int32_t>(snapshot.hostTimeMs - samples.back().hostTimeMs) <= 0) {
                    continue;
                }
                samples.push_back({snapshot.hostTimeMs, state});
                while (samples.size() > 16) {
                    samples.pop_front();
                }
                _railways[state.index].lastReport = std::chrono::steady_clock::now();
            }
        });
        network->RegisterRPC<Shared::Railway::Passengers>([this](const Shared::Railway::Passengers &passengers, MafiaNet::Packet *) {
            if (passengers.missionGeneration != 0 && passengers.missionGeneration == _missionGeneration && _world && _world->IsReady()) {
                ApplyPassengers(passengers);
            }
        });
    }

    void RailwayService::ApplyPassengers(const Shared::Railway::Passengers &passengers) {
        const auto now = std::chrono::steady_clock::now();
        // The listed railways' passengers are all of them: a puppet not in
        // the report got off (or became a walker at the doors) or died away.
        std::array<bool, Shared::Railway::kMaxRailways> listed {};
        for (uint8_t i = 0; i < passengers.railwayCount; ++i) {
            const uint8_t index = passengers.railways[i];
            if (index < _railways.size() && _railways[index].railway && _railways[index].prepared) {
                listed[index] = true;
                _railways[index].lastPassengers = now;
            }
        }
        std::array<std::vector<uint16_t>, Shared::Railway::kMaxRailways> reported;
        for (uint8_t i = 0; i < passengers.count; ++i) {
            const auto &passenger = passengers.passengers[i];
            if (passenger.index >= _railways.size() || !listed[passenger.index] || passenger.part > _railways[passenger.index].railway->WagonCount() ||
                !std::isfinite(passenger.local.x) || !std::isfinite(passenger.local.y) || !std::isfinite(passenger.local.z) || !std::isfinite(passenger.yaw)) {
                continue;
            }
            reported[passenger.index].push_back(passenger.id);
            ShowPuppet(_railways[passenger.index].puppets[passenger.id], passenger);
        }
        for (size_t index = 0; index < _railways.size(); ++index) {
            if (!listed[index]) {
                continue;
            }
            auto &puppets = _railways[index].puppets;
            const auto &ids = reported[index];
            for (auto it = puppets.begin(); it != puppets.end();) {
                if (std::find(ids.begin(), ids.end(), it->first) != ids.end()) {
                    ++it;
                    continue;
                }
                ReleasePuppet(it->second);
                it = puppets.erase(it);
            }
        }
    }

    void RailwayService::ShowPuppet(Puppet &puppet, const Shared::Railway::Passenger &passenger) {
        const auto now = std::chrono::steady_clock::now();
        Ride ride;
        ride.index = passenger.index;
        ride.part = passenger.part;
        ride.local = Vec(passenger.local);
        ride.yaw = passenger.yaw;
        // A walker's model may take over an id the host reused.
        if (puppet.model && !passenger.model.empty() && puppet.modelName != passenger.model) {
            ReleasePuppet(puppet);
        }
        // A new id waits for a report that names its model.
        if (!puppet.model && passenger.model.empty()) {
            return;
        }
        if (!puppet.model) {
            if (puppet.modelName == passenger.model) {
                // It failed to load before.
                return;
            }
            puppet.modelName = passenger.model;
            auto *frame = SDK::Scene::GetDriver()->CreateModel();
            if (!frame) {
                return;
            }
            if (!SDK::Scene::GetModelCache()->OpenModel(frame, passenger.model.c_str()) || !frame->LinkTo(SDK::Core::Mission::Get()->GetScene()->PrimarySector())) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Could not load railway passenger model '{}'", passenger.model);
                frame->Release();
                return;
            }
            // As GameInit and AddPerson do for a rail human's model.
            for (const char *target : {"targetE", "targetN"}) {
                if (auto *child = frame->FindChildFrame(target)) {
                    child->SetOn(false);
                }
            }
            SDK::Traffic::DisableShadows(frame);
            frame->SetOn(true);
            puppet.model = frame;
            puppet.animation = 0xff;
            puppet.from = ride;
        }
        else if (puppet.to.part != ride.part) {
            puppet.from = ride;
        }
        else {
            // From where it stands now.
            const float u = std::clamp(std::chrono::duration<float>(now - puppet.reportedAt) / std::chrono::duration<float>(kPuppetEase), 0.0f, 1.0f);
            puppet.from.local = glm::mix(puppet.from.local, puppet.to.local, u);
            puppet.from.yaw = LerpAngle(puppet.from.yaw, puppet.to.yaw, u);
            puppet.from.part = ride.part;
        }
        puppet.to = ride;
        puppet.reportedAt = now;
        if (puppet.animation != passenger.animation) {
            puppet.animation = passenger.animation;
            puppet.animatedMs = 0;
            // The boarding and alighting animations move the model along
            // their track; here the host's reports move it, so it walks.
            const auto *animation = SDK::Rail::RailHumanAnimation(passenger.animation);
            if (animation && animation->useTrack) {
                animation = SDK::Rail::RailHumanAnimation(kWalkAnimation);
            }
            puppet.animating = animation && SDK::Scene::ModelPlayAnimation(puppet.model, animation->name, !animation->finite);
            // A death holds its last pose; the women's seated and the panic
            // animations are finite too, but go on playing.
            if (animation && SDK::Rail::NativeRailHuman::IsDeathAnimation(passenger.animation)) {
                puppet.animatedMs = 1;
            }
        }
    }

    void RailwayService::UpdatePuppets(Entry &entry, int frameMs) {
        const auto now = std::chrono::steady_clock::now();
        if (!entry.puppets.empty() && now - entry.lastPassengers > kPassengersTimeout) {
            ReleasePuppets(entry);
            return;
        }
        for (auto &[id, puppet] : entry.puppets) {
            if (!puppet.model) {
                continue;
            }
            const float u = std::clamp(std::chrono::duration<float>(now - puppet.reportedAt) / std::chrono::duration<float>(kPuppetEase), 0.0f, 1.0f);
            Ride ride = puppet.to;
            ride.local = glm::mix(puppet.from.local, puppet.to.local, u);
            ride.yaw = LerpAngle(puppet.from.yaw, puppet.to.yaw, u);
            glm::vec3 position;
            glm::vec3 direction;
            // Its railway is not placed here: it waits hidden.
            const bool placed = PlaceRide(ride, position, direction);
            puppet.model->SetOn(placed);
            if (!placed) {
                continue;
            }
            puppet.model->SetWorldPosition(ToNative(position));
            puppet.model->SetDirection(ToNative(direction));
            // Into the scene sector it stands in, as a walker leaving the
            // doors is; refreshed as it moves on with the railway.
            if (glm::distance(position, puppet.sectorPosition) > kPuppetSectorStep) {
                puppet.sectorPosition = position;
                SDK::Core::Mission::Get()->GetScene()->SetFrameSectorPos(puppet.model, ToNative(position));
            }
            if (puppet.animating && frameMs > 0) {
                puppet.model->Tick(frameMs);
                // A death holds its last pose.
                if (puppet.animatedMs > 0 && (puppet.animatedMs += frameMs) >= kFiniteAnimationMs) {
                    SDK::Scene::ModelStopAnimation(puppet.model);
                    puppet.animating = false;
                }
            }
            puppet.model->Update();
        }
    }

    void RailwayService::ReleasePuppet(Puppet &puppet) {
        if (puppet.model) {
            puppet.model->SetOn(false);
            puppet.model->LinkTo(nullptr);
            puppet.model->Release();
        }
        puppet = {};
    }

    void RailwayService::ReleasePuppets(Entry &entry) {
        for (auto &[id, puppet] : entry.puppets) {
            ReleasePuppet(puppet);
        }
        entry.puppets.clear();
    }

    void RailwayService::SendPassengers(World::WorldService &world) {
        Shared::Railway::Passengers packet;
        packet.missionGeneration = world.LoadedMissionGeneration();
        const auto flush = [&] {
            if (packet.railwayCount) {
                Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(packet, MafiaNet::Priority::Medium, MafiaNet::Reliability::Unreliable);
            }
            packet.railwayCount = 0;
            packet.count = 0;
        };
        std::unordered_map<const void *, PassengerId> ids;
        std::vector<Shared::Railway::Passenger> riders;
        const uint32_t report = _passengerReports++;
        for (size_t index = 0; index < _railways.size(); ++index) {
            auto *railway = _railways[index].railway;
            if (!railway) {
                continue;
            }
            riders.clear();
            railway->ForEachPassenger([&](const SDK::Rail::NativeRailHuman &human) {
                Shared::Railway::Passenger passenger;
                passenger.model = SDK::Traffic::ModelName(human.model);
                if (riders.size() >= Shared::Railway::Passengers::kMaxPassengers || passenger.model.empty() ||
                    passenger.model.size() > Shared::Railway::Passengers::kMaxModelLength) {
                    return;
                }
                // A freed rail human's address can come back with another model.
                auto known = _passengerIds.find(&human);
                if (known == _passengerIds.end() || known->second.model != human.model) {
                    known = _passengerIds.insert_or_assign(&human, PassengerId {human.model, _nextPassengerId++}).first;
                }
                auto &id = known->second;
                if (id.reports >= Shared::Railway::Passengers::kModelFirstReports && (report + id.id) % Shared::Railway::Passengers::kModelEvery != 0) {
                    passenger.model.clear();
                }
                id.reports = static_cast<uint8_t>(std::min<uint32_t>(id.reports + 1u, 0xff));
                ids[&human] = id;
                passenger.index = static_cast<uint8_t>(index);
                passenger.id = id.id;
                passenger.animation = static_cast<uint8_t>(std::min<uint32_t>(human.animation, 0xff));
                glm::vec3 local;
                LocalPose(*railway, Vec(human.model->WorldPosition()), Vec(human.model->WorldDirection()), passenger.part, local, passenger.yaw);
                passenger.local = {local.x, local.y, local.z};
                riders.push_back(std::move(passenger));
            });
            if (packet.count + riders.size() > Shared::Railway::Passengers::kMaxPassengers) {
                flush();
            }
            packet.railways[packet.railwayCount++] = static_cast<uint8_t>(index);
            for (auto &rider : riders) {
                packet.passengers[packet.count++] = std::move(rider);
            }
        }
        flush();
        _passengerIds = std::move(ids);
    }

    void RailwayService::Reset() {
        // The mission closes its railways: a local rider goes back to world
        // space and the doorway use objects this client added go first.
        if (auto *player = SDK::Player::CurrentPlayer()) {
            DetachRider(player);
        }
        for (auto &entry : _railways) {
            if (entry.railway && entry.entries != Entry::Entries::None) {
                entry.railway->RemoveEntries();
            }
            ReleasePuppets(entry);
        }
        _railways.clear();
        _passengerIds.clear();
        _lastPassengers = {};
        _lastPuppetTick = {};
        _locks.clear();
        _board = {};
        _reportedPlacement = false;
        _reportedMismatch = false;
        _clockOffsets.clear();
        _hasClockOffset = false;
        _hostSession = 0;
        _lastHits.clear();
        _lastSnapshot = {};
    }

    double RailwayService::LocalMs() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void RailwayService::OnGameInit(NativeRailway *railway) {
        if (_railways.size() >= Shared::Railway::kMaxRailways) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("More than {} railways in the mission; the rest stay unsynchronized", Shared::Railway::kMaxRailways);
            return;
        }
        auto *frame = railway->Actor().Frame();
        _railways.push_back({railway, Shared::Railway::NameHash(frame ? frame->Name() : "")});
    }

    void RailwayService::OnGameDone(NativeRailway *railway) {
        // Indices are load order: the rest keep theirs. A mission closes all
        // of its railways; the next one numbers its own from zero.
        // A rider is the wagon's child and in its collision list.
        auto *player = SDK::Player::CurrentPlayer();
        if (player && static_cast<SDK::Seat::NativeHuman *>(static_cast<void *>(player))->usedActorEnter == &railway->Actor()) {
            DetachRider(player);
        }
        bool remaining = false;
        for (auto &entry : _railways) {
            if (entry.railway == railway) {
                if (entry.entries != Entry::Entries::None) {
                    railway->RemoveEntries();
                }
                ReleasePuppets(entry);
                entry.railway = nullptr;
                entry.samples.clear();
            }
            remaining = remaining || entry.railway;
        }
        if (!remaining) {
            _railways.clear();
        }
    }

    void RailwayService::Prepare(Entry &entry) {
        // The constructor (0x4865b0) and GameInit leave the passenger traffic
        // mode unset; AI and Update act on 0 (active) only.
        entry.railway->SetMode(NativeRailway::PassengerTrafficMode::Active);
        if (!Simulation::IsHost()) {
            entry.railway->Actor().SetNetworkControlled(true);
            // GameInit seats random passengers that only AI (trafficAI,
            // 0x4845f0) would animate and let off; the host's are shown.
            entry.railway->ForEachPassenger([](const SDK::Rail::NativeRailHuman &human) { human.model->SetOn(false); });
        }
        entry.prepared = true;
    }

    void RailwayService::Update(World::WorldService &world) {
        _world = &world;
        if (!world.IsReady()) {
            _missionGeneration = 0;
            _board = {};
            for (auto &entry : _railways) {
                ReleasePuppets(entry);
            }
            return;
        }
        if (_missionGeneration != world.LoadedMissionGeneration()) {
            // A new mission's host clock and reports.
            _missionGeneration = world.LoadedMissionGeneration();
            _clockOffsets.clear();
            _hasClockOffset = false;
            for (auto &entry : _railways) {
                entry.samples.clear();
                entry.renderDelayMs = 0.0;
                ReleasePuppets(entry);
            }
        }
        for (auto &entry : _railways) {
            if (entry.railway && !entry.prepared) {
                Prepare(entry);
            }
        }
        if (Simulation::IsHost()) {
            const auto now = std::chrono::steady_clock::now();
            // A step aboard that never reported its end, or a player gone.
            for (auto it = _locks.begin(); it != _locks.end();) {
                if (now < it->until) {
                    ++it;
                    continue;
                }
                if (it->index < _railways.size() && _railways[it->index].railway) {
                    _railways[it->index].railway->Unlock();
                }
                it = _locks.erase(it);
            }
            for (auto &entry : _railways) {
                // A stop's hold ends as the railway leaves it.
                if (!entry.railway ||
                    !Shared::Railway::OffersEntries(static_cast<uint8_t>(std::min<uint32_t>(entry.railway->State(), 0xff)), entry.railway->Speed(), entry.railway->IsMetro())) {
                    entry.holdSince = {};
                }
                if (!entry.railway || entry.railway->State() != NativeRailway::kDriverHitState) {
                    entry.driverHitSince = {};
                    continue;
                }
                if (entry.driverHitSince == std::chrono::steady_clock::time_point {}) {
                    entry.driverHitSince = now;
                }
                // Not under a player's step: it would carry the player off.
                else if (now - entry.driverHitSince >= kDriverHitStop && entry.railway->LockCount() == 0) {
                    entry.railway->ResumeFromDriverHit();
                    entry.driverHitSince = {};
                }
            }
            if (now - _lastSnapshot >= std::chrono::milliseconds(1000 / Shared::Railway::Snapshot::kSnapshotHz)) {
                _lastSnapshot = now;
                SendSnapshot(world);
            }
            if (now - _lastPassengers >= std::chrono::milliseconds(1000 / Shared::Railway::Passengers::kPassengersHz)) {
                _lastPassengers = now;
                SendPassengers(world);
            }
            if (now - _lastSummary >= std::chrono::seconds(10)) {
                _lastSummary = now;
                size_t moving = 0;
                size_t stopped = 0;
                size_t driverHit = 0;
                for (const auto &entry : _railways) {
                    if (!entry.railway) {
                        continue;
                    }
                    const uint32_t state = entry.railway->State();
                    driverHit += state == NativeRailway::kDriverHitState;
                    (entry.railway->Speed() > 0.1f ? moving : stopped) += 1;
                }
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Railways: {} moving, {} stopped ({} by a hit driver), {} passengers", moving, stopped, driverHit,
                                                                             _passengerIds.size());
            }
            return;
        }
        UpdateBoarding(world);
        if (!_hasClockOffset) {
            return;
        }
        const double hostNow = LocalMs() - _clockOffset;
        auto *localHuman = SDK::Player::CurrentPlayer();
        const auto now = std::chrono::steady_clock::now();
        const int frameMs = _lastPuppetTick == std::chrono::steady_clock::time_point {}
                                ? 0
                                : std::clamp(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastPuppetTick).count()), 0, kMaximumPuppetTickMs);
        _lastPuppetTick = now;
        if (now - _lastSummary >= std::chrono::seconds(10)) {
            _lastSummary = now;
            size_t shown = 0;
            size_t puppets = 0;
            size_t reported = 0;
            double farthestDelay = 0.0;
            for (const auto &entry : _railways) {
                reported += entry.railway && !entry.samples.empty() && now - entry.lastReport < std::chrono::seconds(1);
                farthestDelay = std::max(farthestDelay, entry.renderDelayMs);
                for (const auto &[id, puppet] : entry.puppets) {
                    puppets += puppet.model != nullptr;
                    shown += puppet.model && entry.railway && !entry.samples.empty();
                }
            }
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Railways placed: {} of {} reported in the last second (longest delay {:.0f} ms); passengers: {} puppets, {} on placed railways",
                                                                         reported, _railways.size(), farthestDelay, puppets, shown);
        }
        for (auto &entry : _railways) {
            if (entry.railway && entry.prepared && !entry.samples.empty()) {
                Place(entry, hostNow, frameMs);
                UpdateEntries(entry, localHuman);
            }
            // After Place: the puppets stand on this frame's railway.
            if (entry.railway) {
                UpdatePuppets(entry, frameMs);
            }
        }
    }

    void RailwayService::UpdateEntries(Entry &entry, void *localHuman) {
        // Retail AI registers the doorway use objects for the local player
        // while the doors are open (station states) and takes them away as
        // it departs: inner ones while that player is aboard, else outer.
        using Entries = Entry::Entries;
        auto *railway = entry.railway;
        const auto *human = static_cast<const SDK::Seat::NativeHuman *>(localHuman);
        Entries wanted = Entries::None;
        if (Shared::Railway::OffersEntries(static_cast<uint8_t>(std::min<uint32_t>(railway->State(), 0xff)), railway->Speed(), railway->IsMetro())) {
            wanted = human && human->usedActorEnter == &railway->Actor() ? Entries::Inner : Entries::Outer;
        }
        if (wanted == entry.entries) {
            return;
        }
        // Intern_UseRailway swaps them itself on a step; start from none.
        railway->RemoveEntries();
        if (wanted == Entries::Outer) {
            railway->AddOuterEntries();
        }
        else if (wanted == Entries::Inner) {
            railway->AddInnerEntries();
        }
        entry.entries = wanted;
    }

    bool RailwayService::OnUse(SDK::Seat::NativeHuman *human, SDK::Player::NativeActor *railway, int action, int seat, int animationSpeedState) {
        // Stepping aboard and off both wait for the host's hold, as retail
        // Use_Actor locks the railway for either step.
        const bool enter = action == static_cast<int>(SDK::Seat::UseAction::Enter);
        if (!gService || Simulation::IsHost() || !gService->_world || !gService->_world->IsReady() || &human->actor != SDK::Player::CurrentPlayer() ||
            (!enter && action != static_cast<int>(SDK::Seat::UseAction::Exit))) {
            return true;
        }
        auto &self = *gService;
        const int index = self.IndexOf(railway);
        const uint64_t playerId = LocalPlayerId(*self._world);
        if (index < 0 || !playerId) {
            return false;
        }
        // C_player turns the frame off as a step aboard returns; Update turns
        // it on again until the host's answer starts the real step.
        self._reshowPlayer = self._reshowPlayer || enter;
        // One step at a time; the host answers within a round trip.
        if (self._board.index != Shared::Railway::kNoRailway) {
            return false;
        }
        self._board = {static_cast<uint8_t>(index), action, seat, animationSpeedState, false,
                       std::chrono::steady_clock::now() + std::chrono::milliseconds(Shared::Railway::kBoardLockLimitMs)};
        Shared::Railway::BoardRequest request;
        request.missionGeneration = self._missionGeneration;
        request.index = static_cast<uint8_t>(index);
        request.playerId = playerId;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return false;
    }

    void RailwayService::UpdateBoarding(World::WorldService &world) {
        if (_reshowPlayer) {
            _reshowPlayer = false;
            ShowLocalPlayerUnlessAboard();
        }
        if (_board.index == Shared::Railway::kNoRailway) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!_board.approved) {
            // Unanswered (the host left): the next use asks again.
            if (now >= _board.until) {
                _board = {};
                ShowLocalPlayerUnlessAboard();
            }
            return;
        }
        auto *railway = _board.index < _railways.size() ? _railways[_board.index].railway : nullptr;
        const auto *human = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(SDK::Player::CurrentPlayer()));
        // Stepping aboard, Use_Actor holds the railway in +0x9c and at the
        // end Intern_UseRailway moves it to +0x98 (aboard); stepping off, it
        // stays in +0x98 until Intern_UseRailway(false) clears it.
        const bool stepping = railway && human &&
                              (_board.action == static_cast<int>(SDK::Seat::UseAction::Enter) ? human->usedActorLeave == &railway->Actor()
                                                                                                : human->usedActorEnter == &railway->Actor());
        if (!stepping || now >= _board.until) {
            FinishBoarding(world);
        }
    }

    void RailwayService::FinishBoarding(World::WorldService &world) {
        Shared::Railway::BoardDone done;
        done.missionGeneration = _missionGeneration;
        done.index = _board.index;
        done.playerId = LocalPlayerId(world);
        if (done.playerId) {
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(done, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        _board = {};
        ShowLocalPlayerUnlessAboard();
    }

    void RailwayService::ReleaseLocks(uint64_t playerId) {
        for (auto it = _locks.begin(); it != _locks.end();) {
            if (it->playerId != playerId) {
                ++it;
                continue;
            }
            if (it->index < _railways.size() && _railways[it->index].railway) {
                _railways[it->index].railway->Unlock();
            }
            it = _locks.erase(it);
        }
    }

    bool RailwayService::WithinReach(NativeRailway &railway, void *human) {
        const glm::vec3 position = Vec(SDK::Player::WorldPosition(human));
        for (uint8_t part = 0; part <= std::min<int32_t>(railway.WagonCount(), Shared::Railway::kMaxWagons); ++part) {
            glm::vec3 origin;
            glm::vec3 direction;
            if (PartPose(railway, part, origin, direction) && glm::distance(origin, position) <= Shared::Railway::kBoardReach) {
                return true;
            }
        }
        return false;
    }

    int RailwayService::IndexOf(const void *railway) const {
        for (size_t index = 0; index < _railways.size(); ++index) {
            if (_railways[index].railway && &_railways[index].railway->Actor() == railway) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    std::optional<RailwayService::Ride> RailwayService::RideOf(void *human) const {
        const auto *seat = static_cast<const SDK::Seat::NativeHuman *>(human);
        const int index = seat && seat->usedActorEnter && seat->usedActorEnter->GetType() == SDK::Player::NativeActor::Type::Railway ? IndexOf(seat->usedActorEnter) : -1;
        if (index < 0) {
            return std::nullopt;
        }
        Ride ride;
        ride.index = static_cast<uint8_t>(index);
        LocalPose(*_railways[index].railway, Vec(SDK::Player::WorldPosition(human)), Vec(SDK::Player::WorldDirection(human)), ride.part, ride.local, ride.yaw);
        return ride;
    }

    bool RailwayService::PlaceRide(const Ride &ride, glm::vec3 &position, glm::vec3 &direction) const {
        if (ride.index >= _railways.size() || !_railways[ride.index].railway) {
            return false;
        }
        const auto &entry = _railways[ride.index];
        // A player only gets the railways near it; one it has not placed
        // lately stands somewhere else.
        if (!Simulation::IsHost() && (entry.samples.empty() || std::chrono::steady_clock::now() - entry.lastReport > std::chrono::seconds(1))) {
            return false;
        }
        glm::vec3 origin;
        glm::vec3 partDirection;
        if (!PartPose(*entry.railway, ride.part, origin, partDirection)) {
            return false;
        }
        const auto basis = MakeBasis(partDirection);
        position = origin + basis.right * ride.local.x + basis.up * ride.local.y + basis.forward * ride.local.z;
        direction = basis.right * std::sin(ride.yaw) + basis.forward * std::cos(ride.yaw);
        return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
    }

    void RailwayService::DetachRider(void *human) {
        auto *seat = static_cast<SDK::Seat::NativeHuman *>(human);
        auto *used = seat ? seat->usedActorEnter : nullptr;
        if (!used || used->GetType() != SDK::Player::NativeActor::Type::Railway) {
            return;
        }
        // Intern_UseRailway(false) Unlocks; the extra Lock keeps the count
        // (one high after a step off it cuts short, on a player's copy,
        // which runs no AI to read it).
        auto *railway = static_cast<NativeRailway *>(static_cast<void *>(used));
        railway->Lock();
        seat->LeaveRailwayNow();
    }

    void RailwayService::SendSnapshot(World::WorldService &world) {
        Shared::Railway::Snapshot snapshot;
        snapshot.missionGeneration = world.LoadedMissionGeneration();
        snapshot.hostSession = Simulation::HostSession();
        snapshot.hostTimeMs = static_cast<uint32_t>(MafiaNet::GetTime());
        for (size_t index = 0; index < _railways.size(); ++index) {
            auto &entry = _railways[index];
            if (!entry.railway) {
                continue;
            }
            auto &state = snapshot.railways[snapshot.count++];
            state.index = static_cast<uint8_t>(index);
            state.nameHash = entry.nameHash;
            state.state = static_cast<uint8_t>(std::min<uint32_t>(entry.railway->State(), 0xff));
            state.speed = entry.railway->Speed();
            state.doorAngle = entry.railway->DoorAngle();
            state.position = ToWire(entry.railway->Actor().Position());
            state.direction = ToWire(entry.railway->Actor().Direction());
            state.wagonCount = static_cast<uint8_t>(std::clamp<int32_t>(entry.railway->WagonCount(), 0, Shared::Railway::kMaxWagons));
            for (uint8_t wagon = 0; wagon < state.wagonCount; ++wagon) {
                state.wagonPositions[wagon] = ToWire(entry.railway->WagonPositions()[wagon]);
                state.wagonDirections[wagon] = ToWire(entry.railway->WagonDirections()[wagon]);
            }
        }
        if (snapshot.count) {
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(snapshot, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
            if (!_reportedPlacement) {
                _reportedPlacement = true;
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Reporting {} railways to the server", snapshot.count);
            }
        }
    }

    void RailwayService::Place(Entry &entry, double hostNow, int frameMs) {
        const auto &samples = entry.samples;
        // Two reports behind at the rate this railway is reported (the far
        // ones every kFarSnapshotDivider snapshots), eased so a railway
        // coming into or leaving the stream range does not jump.
        double wantedDelay = kRenderDelayMs;
        if (samples.size() > 1) {
            const double interval = static_cast<double>(static_cast<int32_t>(samples.back().hostTimeMs - samples[samples.size() - 2].hostTimeMs));
            wantedDelay = std::clamp(interval * 2.0, kRenderDelayMs, kMaximumRenderDelayMs);
        }
        // The first pair of reports shows the rate; ease only from there.
        if (entry.renderDelayMs <= 0.0) {
            if (samples.size() > 1) {
                entry.renderDelayMs = wantedDelay;
            }
        }
        else {
            const double step = frameMs * kRenderDelayEaseRate;
            entry.renderDelayMs = std::clamp(wantedDelay, entry.renderDelayMs - step, entry.renderDelayMs + step);
        }
        const double hostTime = hostNow - (entry.renderDelayMs > 0.0 ? entry.renderDelayMs : wantedDelay);
        // Host times are dated relative to the newest sample, as a 32-bit
        // millisecond clock wraps.
        const uint32_t newestTime = samples.back().hostTimeMs;
        const auto age = [&](const Sample &sample) { return static_cast<double>(static_cast<int32_t>(newestTime - sample.hostTimeMs)); };
        const double target = static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(hostTime)) - newestTime));
        Shared::Railway::State placed = samples.back().state;
        glm::vec3 position = Vec(placed.position);
        glm::vec3 direction = Vec(placed.direction);
        std::array<glm::vec3, Shared::Railway::kMaxWagons> wagonPositions {};
        std::array<glm::vec3, Shared::Railway::kMaxWagons> wagonDirections {};
        for (uint8_t i = 0; i < placed.wagonCount; ++i) {
            wagonPositions[i] = Vec(placed.wagonPositions[i]);
            wagonDirections[i] = Vec(placed.wagonDirections[i]);
        }
        // Sample times relative to the newest are <= 0, oldest first.
        if (target >= 0.0) {
            // Continue along the newest report's heading at its speed.
            const float seconds = static_cast<float>(std::min(target, kExtrapolationMs) * 0.001);
            const glm::vec3 step = glm::normalize(direction) * placed.speed * seconds;
            if (std::isfinite(step.x) && std::isfinite(step.y) && std::isfinite(step.z)) {
                position += step;
                for (uint8_t i = 0; i < placed.wagonCount; ++i) {
                    wagonPositions[i] += step;
                }
            }
        }
        else if (samples.size() > 1) {
            // The first pair whose later sample is not before the render time;
            // before the oldest sample the oldest pair clamps to its start.
            size_t later = 1;
            while (later + 1 < samples.size() && -age(samples[later]) < target) {
                ++later;
            }
            {
                const auto &a = samples[later - 1];
                const auto &b = samples[later];
                const double timeA = -age(a);
                const double timeB = -age(b);
                const float u = timeB > timeA ? static_cast<float>(std::clamp((target - timeA) / (timeB - timeA), 0.0, 1.0)) : 1.0f;
                position = glm::mix(Vec(a.state.position), Vec(b.state.position), u);
                direction = Direction(Vec(a.state.direction), Vec(b.state.direction), u);
                placed = b.state;
                placed.speed = glm::mix(a.state.speed, b.state.speed, u);
                placed.doorAngle = glm::mix(a.state.doorAngle, b.state.doorAngle, u);
                const uint8_t wagons = std::min(a.state.wagonCount, b.state.wagonCount);
                for (uint8_t w = 0; w < wagons; ++w) {
                    wagonPositions[w] = glm::mix(Vec(a.state.wagonPositions[w]), Vec(b.state.wagonPositions[w]), u);
                    wagonDirections[w] = Direction(Vec(a.state.wagonDirections[w]), Vec(b.state.wagonDirections[w]), u);
                }
                placed.wagonCount = wagons;
            }
        }
        auto *railway = entry.railway;
        railway->Actor().SetStoredTransform(ToNative(position), ToNative(direction));
        railway->SetSpeed(placed.speed);
        // The doorway use objects follow the state; Update swings the doors.
        railway->SetState(placed.state);
        if (std::isfinite(placed.doorAngle)) {
            railway->SetDoorAngle(std::clamp(placed.doorAngle, 0.0f, 1.5707964f));
        }
        // The native wagon arrays have the mission's own wagon count.
        const int32_t wagons = std::min<int32_t>(railway->WagonCount(), placed.wagonCount);
        for (int32_t i = 0; i < wagons; ++i) {
            railway->WagonPositions()[i] = ToNative(wagonPositions[i]);
            railway->WagonDirections()[i] = ToNative(wagonDirections[i]);
        }
    }

    void RailwayService::OnHitPlayer(World::WorldService &world, SDK::Player::NativeHuman &target, SDK::Player::NativeActor *railway, const Vector3 &direction, float damage) {
        if (!Simulation::IsHost() || !world.IsReady() || !std::isfinite(damage) || damage <= 0.0f) {
            return;
        }
        const auto index = std::find_if(_railways.begin(), _railways.end(), [&](const Entry &entry) { return entry.railway && &entry.railway->Actor() == railway; });
        const auto victim = world.NativeObjects().FindByNative(&target.Actor());
        auto *replication = Framework::CoreModules::GetReplication();
        const auto *player = victim.networkId && replication ? replication->GetEntity<Shared::Entities::PlayerEntity>(victim.networkId) : nullptr;
        if (index == _railways.end() || !player) {
            return;
        }
        // C_railway::Collide sweeps every frame it touches the human.
        const auto now = std::chrono::steady_clock::now();
        if (auto &last = _lastHits[victim.networkId]; now - last >= std::chrono::milliseconds(500)) {
            last = now;
        }
        else {
            return;
        }
        Shared::Railway::PlayerHit hit;
        hit.missionGeneration = world.LoadedMissionGeneration();
        hit.index = static_cast<uint8_t>(index - _railways.begin());
        hit.targetNetworkId = victim.networkId;
        hit.targetSpawnGeneration = player->spawnGeneration;
        hit.damage = damage;
        hit.direction = ToWire(direction);
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(hit, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Railway {} hit player {} (damage {:.1f})", hit.index, hit.targetNetworkId, damage);
    }
} // namespace Mafia1Online::Features::Railway
