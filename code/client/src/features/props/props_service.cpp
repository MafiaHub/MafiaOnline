#include <utils/safe_win32.h>

#include "props_service.h"

#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"
#include "shared/features/player/player_entity.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/combat/native_grenade.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace Mafia1Online::Features::Props {
    namespace {
        using SDK::Player::NativeActor;
        using SDK::Player::Vector3;
        using SDK::Scene::NativeFrame;
        using SDK::World::NativePhysicalProp;
        namespace Wire = Shared::World::Props;

        using Lifecycle = void(__thiscall *)(NativePhysicalProp *);
        using CollisionCall = bool(__thiscall *)(NativePhysicalProp *, NativeActor *, const Vector3 *, const Vector3 *, const Vector3 *, float, NativeFrame *);
        using HitCall = bool(__thiscall *)(NativePhysicalProp *, int32_t, const Vector3 *, const Vector3 *, const Vector3 *, float, NativeActor *, uint32_t, NativeFrame *);
        Lifecycle gGameInitOriginal = nullptr;
        Lifecycle gGameDoneOriginal = nullptr;
        CollisionCall gCollisionOriginal = nullptr;
        HitCall gHitOriginal = nullptr;
        SDK::World::SpawnCrashActorFromFrameCall gSpawnCrashOriginal = nullptr;
        SDK::World::CreateCrashActorsCall gCreateCrashActorsOriginal = nullptr;
        PropsService *gService = nullptr;
        // A late joiner's breakables, and breaks far from the camera, go off
        // without their pieces: CreateCrashActors is skipped for that call.
        bool gSilent = false;

        constexpr std::array<uintptr_t, 6> kHookAddresses {
            NativePhysicalProp::kGameInit,
            NativePhysicalProp::kGameDone,
            NativePhysicalProp::kCollision,
            NativePhysicalProp::kHit,
            SDK::World::kSpawnCrashActorFromFrame,
            SDK::World::kCreateCrashActors,
        };

        constexpr auto kScanInterval = std::chrono::milliseconds(250);
        // One report per prop at most this often.
        constexpr auto kReportInterval = std::chrono::seconds(2);
        // A prop that fell asleep away from the server's pose without this
        // client being the one to report it goes back after this long, unless
        // the report of the client that moved it arrives first.
        constexpr auto kStrayLimit = std::chrono::seconds(8);
        // Moved: farther than this from the server's pose, or turned more
        // than about 10 degrees. Placed: the server's pose differs that much.
        constexpr float kMovedDistance = 0.25f;
        constexpr float kMovedCosine = 0.985f;
        constexpr float kPlacedDistance = 0.05f;
        constexpr float kPlacedCosine = 0.999f;
        // A remote break farther from the camera than this skips the pieces.
        constexpr float kDebrisRange = 200.0f;
        constexpr size_t kMaxPendingBreaks = 64;

        void __fastcall GameInitHook(NativePhysicalProp *prop, void *) {
            gGameInitOriginal(prop);
            if (gService) {
                gService->OnPhysicalGameInit(prop);
            }
        }

        void __fastcall GameDoneHook(NativePhysicalProp *prop, void *) {
            if (gService) {
                gService->OnPhysicalGameDone(prop);
            }
            gGameDoneOriginal(prop);
        }

        bool __fastcall CollisionHook(NativePhysicalProp *prop, void *, NativeActor *actor, const Vector3 *position, const Vector3 *direction, const Vector3 *normal, float force,
                                      NativeFrame *frame) {
            if (gService) {
                gService->OnPhysicalDisturbed(prop, actor);
            }
            return gCollisionOriginal(prop, actor, position, direction, normal, force, frame);
        }

        bool __fastcall HitHook(NativePhysicalProp *prop, void *, int32_t type, const Vector3 *position, const Vector3 *direction, const Vector3 *normal, float damage,
                                NativeActor *attacker, uint32_t flags, NativeFrame *frame) {
            if (gService) {
                gService->OnPhysicalDisturbed(prop, attacker);
            }
            return gHitOriginal(prop, type, position, direction, normal, damage, attacker, flags, frame);
        }

        bool __fastcall SpawnCrashHook(NativeFrame *struck, const Vector3 *origin, const Vector3 *impulse, float force) {
            return gService ? gService->OnSpawnCrash(struck, origin, impulse, force) : gSpawnCrashOriginal(struck, origin, impulse, force);
        }

        bool __fastcall CreateCrashActorsHook(NativeFrame *model, Vector3 *origin, Vector3 *impulse, float force, char *modelName, bool numbered, uint8_t modelIndex,
                                              int parentLimit, bool activateAll, bool forwardImpact) {
            if (gSilent) {
                return true;
            }
            return gCreateCrashActorsOriginal(model, origin, impulse, force, modelName, numbered, modelIndex, parentLimit, activateAll, forwardImpact);
        }

        Wire::Vector ToWire(const Vector3 &vector) {
            return {vector.x, vector.y, vector.z};
        }
        Vector3 ToNative(const Wire::Vector &vector) {
            return {vector.x, vector.y, vector.z};
        }
        Wire::Vector Normalized(const Vector3 &vector) {
            const float length = std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
            return length > 0.0001f ? Wire::Vector {vector.x / length, vector.y / length, vector.z / length} : Wire::Vector {0.0f, 0.0f, 1.0f};
        }
        bool Differs(const Wire::Pose &a, const Wire::Pose &b, float distance, float cosine) {
            return Wire::Distance(a.position, b.position) > distance || Wire::Dot(a.direction, b.direction) < cosine || Wire::Dot(a.up, b.up) < cosine;
        }

        // SpawnCrashActorFromFrame's own walk to the struck frame's model.
        NativeFrame *ModelOf(NativeFrame *frame) {
            while (frame && frame->FrameType() != SDK::World::kFrameModel) {
                frame = frame->Parent();
            }
            return frame;
        }
    } // namespace

    bool PropsService::Install() {
        gService = this;
        if (MH_CreateHook(reinterpret_cast<void *>(NativePhysicalProp::kGameInit), reinterpret_cast<void *>(&GameInitHook), reinterpret_cast<void **>(&gGameInitOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(NativePhysicalProp::kGameDone), reinterpret_cast<void *>(&GameDoneHook), reinterpret_cast<void **>(&gGameDoneOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(NativePhysicalProp::kCollision), reinterpret_cast<void *>(&CollisionHook), reinterpret_cast<void **>(&gCollisionOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(NativePhysicalProp::kHit), reinterpret_cast<void *>(&HitHook), reinterpret_cast<void **>(&gHitOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(SDK::World::kSpawnCrashActorFromFrame), reinterpret_cast<void *>(&SpawnCrashHook), reinterpret_cast<void **>(&gSpawnCrashOriginal)) !=
                MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(SDK::World::kCreateCrashActors), reinterpret_cast<void *>(&CreateCrashActorsHook),
                          reinterpret_cast<void **>(&gCreateCrashActorsOriginal)) != MH_OK) {
            Uninstall();
            return false;
        }
        for (const uintptr_t address : kHookAddresses) {
            if (MH_EnableHook(reinterpret_cast<void *>(address)) != MH_OK) {
                Uninstall();
                return false;
            }
        }
        return true;
    }

    void PropsService::Uninstall() {
        for (const uintptr_t address : kHookAddresses) {
            MH_DisableHook(reinterpret_cast<void *>(address));
            MH_RemoveHook(reinterpret_cast<void *>(address));
        }
        gGameInitOriginal = nullptr;
        gGameDoneOriginal = nullptr;
        gCollisionOriginal = nullptr;
        gHitOriginal = nullptr;
        gSpawnCrashOriginal = nullptr;
        gCreateCrashActorsOriginal = nullptr;
        gService = nullptr;
        Reset();
    }

    void PropsService::RegisterRPC() {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Wire::BreakReport>([this](const Wire::BreakReport &report, MafiaNet::Packet *) {
            // The client that reported it has it off already.
            if (_missionGeneration != 0 && report.missionGeneration == _missionGeneration && report.prop.Valid()) {
                ApplyBreak(report.prop, false);
            }
        });
        network->RegisterRPC<Wire::RestReport>([this](const Wire::RestReport &report, MafiaNet::Packet *) {
            if (_missionGeneration == 0 || report.missionGeneration != _missionGeneration || !report.pose.Valid()) {
                return;
            }
            if (report.pose.index >= _physical.size() || _physical[report.pose.index].nameHash != report.pose.nameHash) {
                if (!_reportedMismatch) {
                    _reportedMismatch = true;
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Physical prop report {} does not match the {} props loaded here", report.pose.index, _physical.size());
                }
                return;
            }
            auto &entry = _physical[report.pose.index];
            // The server holds a move to kMaxPropTravel from the pose it had;
            // the first move of a prop it cannot check, this client can.
            if (Wire::Distance(report.pose.position, entry.synced.position) > Wire::kMaxPropTravel) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Physical prop {} report jumps {:.0f} m; ignored", report.pose.index,
                                                                             Wire::Distance(report.pose.position, entry.synced.position));
                return;
            }
            if (entry.prop) {
                ApplyPose(entry, report.pose);
            }
        });
        network->RegisterRPC<Wire::Snapshot>([this](const Wire::Snapshot &snapshot, MafiaNet::Packet *) {
            if (_missionGeneration == 0 || snapshot.missionGeneration != _missionGeneration) {
                return;
            }
            for (const auto &prop : snapshot.broken) {
                if (prop.Valid()) {
                    ApplyBreak(prop, true);
                }
            }
            size_t placed = 0;
            for (const auto &pose : snapshot.moved) {
                if (pose.Valid() && pose.index < _physical.size() && _physical[pose.index].prop && _physical[pose.index].nameHash == pose.nameHash) {
                    ApplyPose(_physical[pose.index], pose);
                    ++placed;
                }
            }
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("World props: {} broken and {} of {} moved from the server", snapshot.broken.size(), placed, snapshot.moved.size());
        });
        network->RegisterRPC<Wire::Repair>([this](const Wire::Repair &repair, MafiaNet::Packet *) {
            if (_missionGeneration != 0 && repair.missionGeneration == _missionGeneration) {
                RepairAll();
            }
        });
    }

    void PropsService::Reset() {
        _broken.clear();
        _physical.clear();
        _physicalIndex.clear();
        _pendingBreaks.clear();
        _missionGeneration = 0;
        _lastScan = {};
        _reportedMismatch = false;
    }

    void PropsService::Update(World::WorldService &world) {
        _world = &world;
        if (!world.IsReady()) {
            _missionGeneration = 0;
            return;
        }
        if (_missionGeneration != world.LoadedMissionGeneration()) {
            _missionGeneration = world.LoadedMissionGeneration();
            Wire::SnapshotRequest request;
            request.missionGeneration = _missionGeneration;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("World props: {} physical props loaded; asking the server for the mission's state", _physical.size());
            for (const auto &prop : _pendingBreaks) {
                SendBreak(prop);
            }
            _pendingBreaks.clear();
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastScan >= kScanInterval) {
            _lastScan = now;
            ScanPhysical();
        }
    }

    void PropsService::OnPhysicalGameInit(NativePhysicalProp *prop) {
        if (_physical.size() >= Wire::kMaxPhysical) {
            if (_physical.size() == Wire::kMaxPhysical) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("More than {} physical props in the mission; the rest stay unsynchronized", Wire::kMaxPhysical);
            }
            return;
        }
        auto *frame = prop->Actor().Frame();
        Physical entry;
        entry.prop = prop;
        entry.nameHash = Wire::NameHash(frame ? frame->Name() : "");
        entry.spawnTransform = prop->SaveTransform();
        _physical.push_back(entry);
        _physicalIndex[prop] = _physical.size() - 1;
        auto &added = _physical.back();
        added.spawn = PoseOf(_physical.size() - 1);
        added.synced = added.spawn;
    }

    void PropsService::OnPhysicalGameDone(NativePhysicalProp *prop) {
        // Indices are load order: the rest keep theirs. A mission closes all
        // of its props; the next one numbers its own from zero.
        _physicalIndex.erase(prop);
        bool remaining = false;
        for (auto &entry : _physical) {
            if (entry.prop == prop) {
                entry.prop = nullptr;
            }
            remaining = remaining || entry.prop;
        }
        if (!remaining) {
            _physical.clear();
            _physicalIndex.clear();
        }
    }

    void PropsService::OnPhysicalDisturbed(NativePhysicalProp *prop, NativeActor *actor) {
        const auto found = _physicalIndex.find(prop);
        if (found == _physicalIndex.end()) {
            return;
        }
        auto &entry = _physical[found->second];
        entry.cause = std::max(entry.cause, CauseOf(actor));
    }

    PropsService::Cause PropsService::CauseOf(NativeActor *actor) const {
        if (!actor || !_world) {
            return Cause::Unattributed;
        }
        const auto type = actor->GetType();
        if (static_cast<uint32_t>(type) == NativePhysicalProp::kActorType) {
            const auto found = _physicalIndex.find(actor);
            return found != _physicalIndex.end() ? _physical[found->second].cause : Cause::Unattributed;
        }
        const bool host = Simulation::IsHost();
        if (type == NativeActor::Type::Railway) {
            // Trams and metro trains run natively on the simulation host.
            return host ? Cause::Local : Cause::Remote;
        }
        NativeActor *person = actor;
        if (type == NativeActor::Type::Car) {
            person = static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(actor))->GetOwner(0);
        } else if (type != NativeActor::Type::Player && type != NativeActor::Type::Entity) {
            return Cause::Unattributed;
        }
        if (!person) {
            return Cause::Unattributed;
        }
        if (!host) {
            return person == SDK::Player::CurrentPlayer() ? Cause::Local : Cause::Remote;
        }
        // The host simulates everyone but the players.
        return IsPlayerReplica(person) ? Cause::Remote : Cause::Local;
    }

    bool PropsService::IsPlayerReplica(NativeActor *human) const {
        const auto handle = _world->NativeObjects().FindByNative(human);
        auto *replication = Framework::CoreModules::GetReplication();
        return handle.networkId && replication && replication->GetEntity<Shared::Entities::PlayerEntity>(handle.networkId);
    }

    bool PropsService::IsNearestPlayer(const Wire::Vector &position) const {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication || !_world) {
            return false;
        }
        const uint64_t myGuid = static_cast<uint64_t>(replication->GetMyGUID());
        const glm::vec3 at(position.x, position.y, position.z);
        float mine = std::numeric_limits<float>::infinity();
        float others = std::numeric_limits<float>::infinity();
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (!player->spawned || player->missionGeneration != _missionGeneration) {
                return;
            }
            const float distance = glm::distance(player->position, at);
            if (player->controllerGuid == myGuid) {
                mine = distance;
            } else {
                others = std::min(others, distance);
            }
        });
        return std::isfinite(mine) && mine <= others;
    }

    Shared::World::Props::Pose PropsService::PoseOf(size_t index) const {
        auto *prop = _physical[index].prop;
        Wire::Pose pose;
        pose.index = static_cast<uint16_t>(index);
        pose.nameHash = _physical[index].nameHash;
        pose.position = ToWire(prop->Actor().Position());
        // A scaled frame's matrix rows carry its scale; MoveCount places
        // from the unit vectors.
        pose.direction = Normalized(prop->Actor().Direction());
        pose.up = Normalized(prop->Up());
        return pose;
    }

    NativeFrame *PropsService::Spawn(NativeFrame *struck, const Vector3 &origin, const Vector3 &impulse, float force, std::vector<uint32_t *> live) {
        auto *model = ModelOf(struck);
        if (model && live.empty()) {
            SDK::World::ForEachCollisionPrimitive(model, [&](uint32_t *primitive) {
                if (!(*primitive & SDK::World::kCollisionPrimitiveOff)) {
                    live.push_back(primitive);
                }
            });
        }
        if (!gSpawnCrashOriginal(struck, &origin, &impulse, force) || !model) {
            return nullptr;
        }
        // A city cache instance is pooled: its segment can release it while
        // it is off, so it is left to retail.
        if (SDK::World::IsCachePoolName(model->Name())) {
            return model;
        }
        auto broken = std::find_if(_broken.begin(), _broken.end(), [&](const Broken &entry) { return entry.model == model; });
        if (broken == _broken.end()) {
            broken = _broken.insert(_broken.end(), Broken {model, {}});
        }
        for (auto *primitive : live) {
            if (*primitive & SDK::World::kCollisionPrimitiveOff) {
                broken->primitives.push_back(primitive);
            }
        }
        return model;
    }

    bool PropsService::OnSpawnCrash(NativeFrame *struck, const Vector3 *origin, const Vector3 *impulse, float force) {
        auto *model = Spawn(struck, *origin, *impulse, force);
        if (!model) {
            return false;
        }
        // Only a model the others find by its name: a scene object, not a
        // city cache instance or one of two frames with the same name.
        const char *name = model->Name();
        if (SDK::World::IsCachePoolName(name) || !Wire::ValidName(name) || SDK::Core::Mission::Get()->GetScene()->FindFrame(name) != model) {
            return true;
        }
        Wire::Break prop;
        prop.name = name;
        if (struck != model && Wire::ValidName(struck->Name())) {
            prop.frame = struck->Name();
        }
        prop.position = ToWire(model->WorldPosition());
        prop.origin = ToWire(*origin);
        prop.impulse = ToWire(*impulse);
        prop.force = force;
        if (!prop.Valid()) {
            return true;
        }
        if (!_world || !_world->IsReady() || _missionGeneration == 0) {
            // Broken while the mission was still coming up: sent with the
            // snapshot request.
            if (_pendingBreaks.size() < kMaxPendingBreaks) {
                _pendingBreaks.push_back(std::move(prop));
            }
            return true;
        }
        SendBreak(prop);
        return true;
    }

    void PropsService::SendBreak(const Wire::Break &prop) {
        Wire::BreakReport report;
        report.missionGeneration = _missionGeneration;
        report.prop = prop;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void PropsService::ApplyBreak(Wire::Break prop, bool silent) {
        auto *mission = SDK::Core::Mission::Get();
        auto *scene = mission->GetScene();
        auto *model = scene->FindFrame(prop.name.c_str());
        // Off already: broken here too (or the reporter's own echo).
        if (!model || model->FrameType() != SDK::World::kFrameModel || !model->IsOn() || SDK::World::IsCachePoolName(model->Name())) {
            return;
        }
        if (Wire::Distance(ToWire(model->WorldPosition()), prop.position) > Wire::kBreakablePlaceTolerance) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("World prop '{}' is not where the server has it broken", prop.name);
            return;
        }
        // The frame the reporter's native call struck, if it belongs to
        // this model: ParseCrashConfigFromFrameName reads it.
        NativeFrame *struck = model;
        if (!prop.frame.empty()) {
            auto *frame = SDK::World::FindInSubtree(model, prop.frame.c_str());
            if (frame && ModelOf(frame) == model) {
                struck = frame;
            } else {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("World prop '{}' has no part '{}'; breaking the model", prop.name, prop.frame);
            }
        }
        // Only what the native code breaks: a collision primitive of the
        // crash material, still on.
        std::vector<uint32_t *> live;
        bool breakable = false;
        SDK::World::ForEachCollisionPrimitive(model, [&](uint32_t *primitive) {
            if (!(*primitive & SDK::World::kCollisionPrimitiveOff)) {
                live.push_back(primitive);
                breakable = breakable || SDK::World::PrimitiveMaterial(primitive) == SDK::World::kCrashMaterial;
            }
        });
        if (!breakable) {
            return;
        }
        if (!silent) {
            auto *camera = scene->ActiveCamera();
            silent = !camera || Wire::Distance(ToWire(camera->WorldPosition()), prop.position) > kDebrisRange;
        }
        prop.Clamp();
        SDK::World::NativeCrashWorld world {mission->Game()};
        const size_t before = world.TemporaryActors().size();
        gSilent = silent;
        Spawn(struck, ToNative(prop.origin), ToNative(prop.impulse), prop.force, std::move(live));
        gSilent = false;
        // A replayed fuel pump's piece would blow up and burn here too, on
        // top of the reporter's own: it hurts this client's player and
        // breaks more props. The others see the blast and the fire without
        // their damage.
        const auto pieces = world.TemporaryActors();
        for (size_t i = before; i < pieces.size(); ++i) {
            if (!pieces[i] || static_cast<uint32_t>(pieces[i]->GetType()) != SDK::World::NativeCrashWorld::kCrashObjectType) {
                continue;
            }
            auto *piece = reinterpret_cast<SDK::World::NativeCrashPiece *>(pieces[i]);
            const uint32_t effects = piece->Flags() & (SDK::World::NativeCrashPiece::kExplode | SDK::World::NativeCrashPiece::kStartFire);
            if (!effects) {
                continue;
            }
            const bool goesOff = piece->GoesOff();
            piece->Flags() &= ~effects;
            if (!goesOff) {
                continue;
            }
            const auto at = piece->Actor().Position();
            if (effects & SDK::World::NativeCrashPiece::kStartFire) {
                SDK::Combat::NewFire(mission->Game(), at, SDK::World::NativeCrashPiece::kFireLifeMs, SDK::World::NativeCrashPiece::kFireRadius, 0.0f, false);
            }
            if (effects & SDK::World::NativeCrashPiece::kExplode) {
                SDK::Combat::NewExplosion(mission->Game(), at, SDK::World::NativeCrashPiece::kExplosionRadius, 0.0f, false);
            }
        }
    }

    void PropsService::Place(Physical &entry, const Wire::Pose &pose) {
        if (!Differs(pose, entry.spawn, kPlacedDistance, kPlacedCosine)) {
            // Back where the mission put it: exactly so, scale included.
            entry.prop->RestoreTransform(entry.spawnTransform);
        } else if (pose.Valid()) {
            const auto unit = pose.Orthonormalized();
            entry.prop->Place(ToNative(unit.position), ToNative(unit.direction), ToNative(unit.up));
        }
        entry.cause = Cause::None;
        entry.strayedAt = {};
    }

    void PropsService::ApplyPose(Physical &entry, const Wire::Pose &pose) {
        entry.synced = pose;
        entry.strayedAt = {};
        if (!Differs(PoseOf(pose.index), pose, kPlacedDistance, kPlacedCosine)) {
            return;
        }
        // Still moving here: once it sleeps, ScanPhysical reports it if this
        // client moved it and otherwise puts it here.
        if (!entry.prop->Asleep()) {
            return;
        }
        Place(entry, pose);
    }

    void PropsService::ScanPhysical() {
        const auto now = std::chrono::steady_clock::now();
        for (size_t index = 0; index < _physical.size(); ++index) {
            auto &entry = _physical[index];
            if (!entry.prop || !entry.prop->Asleep()) {
                continue;
            }
            const auto pose = PoseOf(index);
            if (!Differs(pose, entry.synced, kMovedDistance, kMovedCosine)) {
                entry.cause = Cause::None;
                entry.strayedAt = {};
                continue;
            }
            const bool reports = pose.Valid() && (entry.cause == Cause::Local || (entry.cause == Cause::Unattributed && !Simulation::IsHost() && IsNearestPlayer(pose.position)));
            if (reports) {
                if (now - entry.lastReport < kReportInterval) {
                    continue;
                }
                Wire::RestReport report;
                report.missionGeneration = _missionGeneration;
                report.pose = pose;
                report.home = !Differs(pose, entry.spawn, kMovedDistance, kMovedCosine);
                Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report, MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
                entry.synced = pose;
                entry.lastReport = now;
                entry.cause = Cause::None;
                entry.strayedAt = {};
                continue;
            }
            // Moved here by a copy of someone else's actor: that client's
            // report places it; without one it goes back to the server's pose.
            if (entry.strayedAt == std::chrono::steady_clock::time_point {}) {
                entry.strayedAt = now;
            } else if (now - entry.strayedAt >= kStrayLimit) {
                Place(entry, entry.synced);
            }
        }
    }

    void PropsService::RepairAll() {
        SDK::World::NativeCrashWorld world {SDK::Core::Mission::Get()->Game()};
        size_t repaired = 0;
        for (auto &broken : _broken) {
            for (auto *primitive : broken.primitives) {
                *primitive &= ~SDK::World::kCollisionPrimitiveOff;
            }
            broken.model->SetOn(true);
            ++repaired;
        }
        // Out of C_game::Done's list too; city cache instances stay in it.
        const auto deactivated = world.Deactivated();
        size_t kept = 0;
        for (auto *frame : deactivated) {
            if (std::none_of(_broken.begin(), _broken.end(), [&](const Broken &broken) { return broken.model == frame; })) {
                deactivated[kept++] = frame;
            }
        }
        world.TruncateDeactivated(kept);
        _broken.clear();
        // The pieces go as C_crash_obj::AI drops one (0x431be0).
        std::vector<SDK::Player::NativeActor *> pieces;
        for (auto *actor : world.TemporaryActors()) {
            if (actor && static_cast<uint32_t>(actor->GetType()) == SDK::World::NativeCrashWorld::kCrashObjectType) {
                pieces.push_back(actor);
            }
        }
        for (auto *piece : pieces) {
            world.InvalidateActor(piece);
            world.game->RemoveTemporaryActor(piece);
        }
        world.DelAllHydrants();
        // The booths are callable again for the police, and a traffic light
        // DelSemaphore may have dropped (it matches an unset field) is lit
        // again.
        world.LoadTelephones();
        world.LoadSemaphores();
        size_t moved = 0;
        for (auto &entry : _physical) {
            if (!entry.prop) {
                continue;
            }
            entry.synced = entry.spawn;
            entry.cause = Cause::None;
            entry.strayedAt = {};
            if (!Differs(PoseOf(entry.spawn.index), entry.spawn, kPlacedDistance, kPlacedCosine)) {
                continue;
            }
            entry.prop->RestoreTransform(entry.spawnTransform);
            ++moved;
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("World props repaired: {} breakables back up, {} pieces removed, {} physical props back in place", repaired,
                                                                    pieces.size(), moved);
    }
} // namespace Mafia1Online::Features::Props
