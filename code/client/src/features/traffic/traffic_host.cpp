#include <utils/safe_win32.h>

#include "traffic_host.h"

#include "features/npc/npc_host.h"
#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"
#include "shared/features/car/car_movement.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/traffic/traffic_report.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/car/native_car.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/police/native_police_manager.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <mafia1/sdk/traffic/native_traffic_car.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <unordered_set>

namespace Mafia1Online::Features::Traffic {
    namespace {
        using SDK::Car::NativeCar;
        using SDK::Player::NativeActor;
        using SDK::Player::Vector3;
        using SDK::Traffic::NativeTrafficCar;
        using SDK::Traffic::NativeTrafficCarElement;

        using GameInit       = void(__thiscall *)(NativeTrafficCar *);
        using DeactivateElem = void(__thiscall *)(NativeTrafficCar *, int32_t, bool);
        using Rammed         = void(__fastcall *)(NativeCar *, NativeActor *, float);
        using Replace        = NativeCar *(__fastcall *)(NativeCar *);
        using OccupantShot   = void(__fastcall *)(NativeCar *, void *);
        using PoliceCars     = void(__fastcall *)(NativeActor *, SDK::Traffic::NativeCarVector *, int32_t, bool, NativeCar *, float);

        GameInit gGameInitOriginal             = nullptr;
        TrafficHost::TrafficAI gAIOriginal     = nullptr;
        DeactivateElem gDeactivateOriginal     = nullptr;
        Rammed gRammedOriginal                 = nullptr;
        Replace gReplaceOriginal               = nullptr;
        OccupantShot gOccupantShotOriginal     = nullptr;
        PoliceCars gPoliceCarsOriginal         = nullptr;
        TrafficHost *gHost                     = nullptr;

        // Neither the pool's own despawn (AI) nor its camera spawn may run:
        // the host does both for every player.
        constexpr float kNoDespawn = 1.0e7f;
        constexpr uint32_t kSpawnIntervalMs = 350;
        // C_actor::m_eActorState: 0 active, 1 inactive (a car asleep), 2 dead.
        constexpr int32_t kDeadActorState = 2;
        // GameInit walks its 100-entry spawn schedule with the slot count as
        // the window; a larger count would run past the schedule.
        constexpr int32_t kMaximumSlots = 100;
        // TRAFFIC_CAR_OCCUPANT_VISIBILITY_HIGH_SQUARED: the host keeps crew
        // models on within this of any player, so bullets can hit them.
        constexpr float kOccupantVisibleSq = 19600.0f;
        // A new car stands at least this share of the spawn radius (150-170)
        // from every player.
        constexpr float kSpawnClearance = 0.95f;

        float Length(const Vector3 &value) {
            return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        }

        glm::quat FrameRotation(SDK::Scene::NativeFrame &frame) {
            const auto basis = frame.GetWorldBasis();
            const glm::vec3 axisZ = glm::normalize(glm::vec3(basis.forward.x, basis.forward.y, basis.forward.z));
            const glm::vec3 axisX = glm::normalize(glm::cross(glm::vec3(basis.up.x, basis.up.y, basis.up.z), axisZ));
            const glm::vec3 axisY = glm::normalize(glm::cross(axisZ, axisX));
            return glm::normalize(glm::quat_cast(glm::mat3(axisX, axisY, axisZ)));
        }

        void *Game() {
            return SDK::Core::Mission::Get()->Game();
        }

        NativeTrafficCarElement *ElementOf(const void *car) {
            for (auto **element = SDK::Traffic::CarElementsBegin(); element && element != SDK::Traffic::CarElementsEnd(); ++element) {
                if (*element && (*element)->_car == car) {
                    return *element;
                }
            }
            return nullptr;
        }

        bool IsTemporaryActor(const void *actor) {
            auto *game = Game();
            for (auto *const *it = SDK::Traffic::TemporaryActorsBegin(game); it && it != SDK::Traffic::TemporaryActorsEnd(game); ++it) {
                if (*it == actor) {
                    return true;
                }
            }
            return false;
        }

        void __fastcall GameInitHook(NativeTrafficCar *traffic, void *) {
            // A bigger pool: GameInit sizes the active slots and the pool from
            // m_iMaxActiveCarCount. The mission's own value comes back after.
            // The host's profile density (a freeride setting) does not thin
            // the traffic every player sees.
            auto &density = *reinterpret_cast<float *>(SDK::Traffic::kTrafficCarDensity);
            const float profileDensity = density;
            const int32_t retail = traffic->_maxActiveCarCount;
            density = 1.0f;
            traffic->_maxActiveCarCount = std::min<int32_t>(retail * static_cast<int32_t>(Simulation::GetConfig().trafficPoolScale), kMaximumSlots);
            gGameInitOriginal(traffic);
            traffic->_maxActiveCarCount = retail;
            density = profileDensity;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Traffic pool: {} active slots (mission maximum {})", traffic->SlotCount(), retail);
        }

        void __fastcall AIHook(NativeTrafficCar *traffic, void *, uint32_t frameMs) {
            gHost->TickTraffic(traffic, frameMs, gAIOriginal);
        }

        void __fastcall DeactivateElemHook(NativeTrafficCar *traffic, void *, int32_t slot, bool keepTemporary) {
            gHost->BeforeDeactivate(traffic, slot);
            gDeactivateOriginal(traffic, slot, keepTemporary);
        }

        void __fastcall RammedHook(NativeCar *car, NativeActor *actor, float force) {
            gHost->OnRammed(car, actor, force);
        }

        NativeCar *__fastcall ReplaceHook(NativeCar *car) {
            NativeCar *replacement = gReplaceOriginal(car);
            gHost->OnReplaced(car, replacement);
            return replacement;
        }

        void __fastcall OccupantShotHook(NativeCar *car, void *frame) {
            gOccupantShotOriginal(car, frame);
            gHost->OnOccupantShot(car, frame);
        }

        void __fastcall PoliceCarsHook(NativeActor *suspect, SDK::Traffic::NativeCarVector *cars, int32_t count, bool requireAhead, NativeCar *rammed, float radiusSq) {
            std::vector<NativeCar *> pending;
            gHost->BeforePoliceCars(pending);
            const size_t before = cars->first ? static_cast<size_t>(cars->last - cars->first) : 0;
            gPoliceCarsOriginal(suspect, cars, count, requireAhead, rammed, radiusSq);
            gHost->AfterPoliceCars(cars->first, cars->last, before, pending);
        }

        struct Hook {
            uintptr_t address;
            void *detour;
            void **original;
        };

        const std::array<Hook, 7> &Hooks() {
            static const std::array<Hook, 7> hooks {{
                {SDK::Traffic::kTrafficCarGameInit, reinterpret_cast<void *>(&GameInitHook), reinterpret_cast<void **>(&gGameInitOriginal)},
                {SDK::Traffic::kTrafficCarAI, reinterpret_cast<void *>(&AIHook), reinterpret_cast<void **>(&gAIOriginal)},
                {SDK::Traffic::kTrafficCarDeactivateElem, reinterpret_cast<void *>(&DeactivateElemHook), reinterpret_cast<void **>(&gDeactivateOriginal)},
                {SDK::Traffic::kTrafficCarRammed, reinterpret_cast<void *>(&RammedHook), reinterpret_cast<void **>(&gRammedOriginal)},
                {SDK::Traffic::kTrafficCarReplaceStolen, reinterpret_cast<void *>(&ReplaceHook), reinterpret_cast<void **>(&gReplaceOriginal)},
                {SDK::Traffic::kTrafficCarOccupantShot, reinterpret_cast<void *>(&OccupantShotHook), reinterpret_cast<void **>(&gOccupantShotOriginal)},
                {SDK::Traffic::kTrafficCarPoliceCars, reinterpret_cast<void *>(&PoliceCarsHook), reinterpret_cast<void **>(&gPoliceCarsOriginal)},
            }};
            return hooks;
        }
    } // namespace

    bool TrafficHost::Install(Car::CarService &cars, Npc::NpcHost &npcs) {
        gHost = this;
        _cars = &cars;
        _npcs = &npcs;
        for (const auto &hook : Hooks()) {
            if (MH_CreateHook(reinterpret_cast<void *>(hook.address), hook.detour, hook.original) != MH_OK ||
                MH_EnableHook(reinterpret_cast<void *>(hook.address)) != MH_OK) {
                Uninstall();
                return false;
            }
        }
        return true;
    }

    void TrafficHost::Uninstall() {
        for (const auto &hook : Hooks()) {
            MH_DisableHook(reinterpret_cast<void *>(hook.address));
            MH_RemoveHook(reinterpret_cast<void *>(hook.address));
        }
        Reset();
        gHost = nullptr;
    }

    void TrafficHost::RegisterRPC(World::WorldService &world) {
        _world = &world;
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Traffic::Takeover>([this](const Shared::Traffic::Takeover &takeover, MafiaNet::Packet *) {
            if (!_world || !_world->IsReady() || takeover.missionGeneration != _missionGeneration) {
                return;
            }
            // The replicated hostDriven = false may come before this RPC and
            // mark the record released already; the takeover still runs.
            const auto found = _records.find(takeover.key);
            if (found == _records.end() || found->second.takeoverApplied) {
                return;
            }
            auto &record = found->second;
            record.released = true;
            record.takeoverApplied = true;
            NativeCar *car = record.car;
            auto *element = record.origin == Car::CarService::Origin::Pool ? ElementOf(car) : nullptr;
            if (!element || !element->_active || element->_pendingDeactivation) {
                // A temporary car simply stops being the host's.
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Traffic car {} handed to player {}", takeover.key, takeover.playerId);
                return;
            }
            if (takeover.steal && !element->_driverRemoved && (element->_driverModel || element->_passengerModel)) {
                // Retail's steal on the thief's replica: Corovacka takes the
                // crew model, which becomes a C_entity thrown out beside the
                // door, hostile to the thief one time in three.
                auto *replication = Framework::CoreModules::GetReplication();
                auto *player = replication->GetEntity<Shared::Entities::PlayerEntity>(takeover.playerId);
                auto *thief = static_cast<SDK::Seat::NativeHuman *>(_world->NativeObjects().Resolve(_world->NativeObjects().FindByNetwork(takeover.playerId)));
                auto *police = SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game());
                const bool human = thief && (thief->actor.GetType() == NativeActor::Type::Entity || thief->actor.GetType() == NativeActor::Type::Player);
                if (player && player->spawnGeneration == takeover.playerSpawnGeneration && human && !thief->actor.IsDead() && !thief->usedActorEnter && !thief->usedActorLeave) {
                    const size_t before = police->RetirementCount();
                    (void)thief->TrySteal(static_cast<SDK::Seat::NativeCar *>(car), 0);
                    if (police->RetirementCount() == before + 1) {
                        // Players who saw the steal throw this NPC out of the car.
                        _npcs->TrackIfNew(police->RetirementEnd()[-1], Shared::Entities::NpcKind::Driver, _world->NativeObjects().FindByNative(car).networkId);
                    }
                }
            }
            // Retail's entry into a traffic car: an ordinary temporary car at
            // the pool car's pose takes over (the hook moves the network car).
            (void)SDK::Traffic::ReplaceStolen(car);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Traffic car {} {} by player {}", takeover.key, takeover.steal ? "stolen" : "taken", takeover.playerId);
        });
    }

    void TrafficHost::Reset() {
        _records.clear();
        _keyByCar.clear();
        _barrierKeys.clear();
        _observers.clear();
        _nextObserver = 0;
        _spawnTimerMs = 0;
        _traffic = nullptr;
    }

    TrafficHost::Record *TrafficHost::FindByCar(const void *car) {
        const auto key = _keyByCar.find(car);
        if (key == _keyByCar.end()) {
            return nullptr;
        }
        const auto record = _records.find(key->second);
        return record == _records.end() ? nullptr : &record->second;
    }

    void TrafficHost::Update(World::WorldService &world) {
        _world = &world;
        const uint64_t generation = world.LoadedMissionGeneration();
        if (generation != _missionGeneration) {
            Reset();
            _nextKey = 1;
            _nextBarrierKey = 1;
            _missionGeneration = generation;
        }
        if (!world.IsReady()) {
            return;
        }
        RefreshObservers(world);
        Discover(world);
        Bind(world);
        Watch(world);
        WatchBarriers();
    }

    void TrafficHost::RefreshObservers(World::WorldService &world) {
        _observers.clear();
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (!player->spawned || player->missionGeneration != _missionGeneration) {
                return;
            }
            const glm::vec3 forward = glm::normalize(player->rotation) * glm::vec3(0.0f, 0.0f, 1.0f);
            if (!std::isfinite(player->position.x) || !std::isfinite(player->position.z) || !std::isfinite(forward.x)) {
                return;
            }
            _observers.push_back({{player->position.x, player->position.y, player->position.z}, {forward.x, 0.0f, forward.z}, player->GetNetworkID()});
        });
    }

    float TrafficHost::NearestObserverSq(const Vector3 &position) const {
        float nearest = std::numeric_limits<float>::max();
        for (const auto &observer : _observers) {
            const float dx = position.x - observer.position.x;
            const float dy = position.y - observer.position.y;
            const float dz = position.z - observer.position.z;
            nearest = std::min(nearest, dx * dx + dy * dy + dz * dz);
        }
        return nearest;
    }

    void TrafficHost::TickTraffic(NativeTrafficCar *traffic, uint32_t frameMs, TrafficAI original) {
        _traffic = traffic;
        if (!traffic->HasPool() || *reinterpret_cast<const bool *>(SDK::Traffic::kTrafficCarsDisabled)) {
            original(traffic, frameMs);
            return;
        }
        // The drivers tick natively; the camera's spawn and despawn do not.
        const bool started = traffic->_gameInitStarted;
        const float despawn = traffic->_despawnRadius;
        traffic->_gameInitStarted = false;
        traffic->_despawnRadius = kNoDespawn;
        original(traffic, frameMs);
        traffic->_gameInitStarted = started;
        traffic->_despawnRadius = despawn;
        if (!started) {
            return;
        }
        Despawn(*traffic);
        _spawnTimerMs += frameMs;
        if (_spawnTimerMs >= kSpawnIntervalMs) {
            _spawnTimerMs = 0;
            if (SDK::Traffic::HasReturningCar() && !*reinterpret_cast<const bool *>(SDK::Traffic::kTrafficCarActivationSuppressed)) {
                ReturnCar(*traffic);
            }
            // One attempt per player, the rate retail spawns at for its one
            // camera; each goes to whoever has the fewest cars around.
            for (size_t attempt = 0; attempt < _observers.size() && !*reinterpret_cast<const bool *>(SDK::Traffic::kTrafficCarActivationSuppressed); ++attempt) {
                SpawnAround(*traffic);
            }
        }
    }

    void TrafficHost::Despawn(NativeTrafficCar &traffic) {
        // Nobody spawned (a sole player respawning): the traffic around them
        // stays for when they are back, as SpawnAround adds none meanwhile.
        if (_observers.empty()) {
            return;
        }
        for (int32_t slot = 0; slot < traffic.SlotCount(); ++slot) {
            auto *element = traffic.Slot(slot);
            if (!element || !element->_active || !element->_carModel || !element->_car) {
                continue;
            }
            const auto position = element->_carModel->WorldPosition();
            // Retail keeps a car waiting at a blocked crossing. A player's
            // camera can look anywhere, so the whole despawn radius counts,
            // not retail's narrower one behind the camera.
            const bool checked = element->_driverRemoved || !element->_driver || element->_driver->BlockedPathIndex() < 0;
            const float nearestSq = NearestObserverSq(position);
            if (checked && !(nearestSq < traffic._despawnRadius * traffic._despawnRadius)) {
                traffic.DeactivateElem(slot);
                continue;
            }
            if (!element->_driverRemoved && !element->_animationActive) {
                // Collision response near a player, as retail turns it on
                // within 8 of its own (TRAFFIC_CAR_DIRECTOR_LOCAL_CONTROL).
                if (nearestSq < SDK::Traffic::kDirectorControlDistanceSq) {
                    *reinterpret_cast<uint32_t *>(reinterpret_cast<std::byte *>(element->_car) + SDK::Traffic::kCarDirectorControl) |= SDK::Traffic::kDirectorCollisionActive;
                }
                auto *occupant = element->_passengerModel ? element->_passengerModel : element->_driverModel;
                if (occupant) {
                    occupant->SetOn(nearestSq < kOccupantVisibleSq && !(element->_driver && element->_driver->Parking()));
                }
            }
        }
    }

    size_t TrafficHost::DrivenCount() const {
        return static_cast<size_t>(std::count_if(_records.begin(), _records.end(), [](const auto &entry) { return entry.second.reported && !entry.second.released; }));
    }

    void TrafficHost::ReturnCar(NativeTrafficCar &traffic) {
        // Retail AI's full-pool branch, which the cleared m_bGameInitStarted
        // skips: free a slot for the returning police car, here the one of
        // the pool car farthest from every player (retail: an invisible one).
        const auto activeCount = static_cast<int32_t>(SDK::Traffic::CarElementsEnd() - SDK::Traffic::CarElementsBegin());
        if (activeCount >= traffic.SlotCount()) {
            int32_t freed = -1;
            float farthest = -1.0f;
            for (int32_t slot = 0; slot < traffic.SlotCount(); ++slot) {
                auto *element = traffic.Slot(slot);
                if (!element || !element->_active || !element->_carModel || element->_dynamicallyAllocated || element->_pendingDeactivation) {
                    continue;
                }
                const float nearestSq = NearestObserverSq(element->_carModel->WorldPosition());
                if (nearestSq > farthest) {
                    farthest = nearestSq;
                    freed = slot;
                }
            }
            if (freed < 0) {
                return;
            }
            traffic.DeactivateElem(freed);
        }
        else if (DrivenCount() >= Shared::Traffic::kMaxHostCars) {
            return;
        }
        // The returning car keeps its own pose (ActivateElems' requested
        // element path), so no camera is needed and the spawn clearance
        // does not apply to it.
        (void)traffic.ActivateElems();
    }

    void TrafficHost::SpawnAround(NativeTrafficCar &traffic) {
        if (_observers.empty() || DrivenCount() >= Shared::Traffic::kMaxHostCars) {
            return;
        }
        const auto activeCount = static_cast<int32_t>(SDK::Traffic::CarElementsEnd() - SDK::Traffic::CarElementsBegin());
        if (activeCount >= traffic.SlotCount()) {
            return;
        }
        // The player with the fewest cars around gets the next one.
        std::vector<int32_t> nearby(_observers.size(), 0);
        for (auto **element = SDK::Traffic::CarElementsBegin(); element && element != SDK::Traffic::CarElementsEnd(); ++element) {
            if (!*element || !(*element)->_active || !(*element)->_carModel) {
                continue;
            }
            const auto position = (*element)->_carModel->WorldPosition();
            for (size_t i = 0; i < _observers.size(); ++i) {
                const Vector3 offset {position.x - _observers[i].position.x, 0.0f, position.z - _observers[i].position.z};
                if (Length(offset) < traffic._despawnRadius) {
                    ++nearby[i];
                }
            }
        }
        size_t chosen = _nextObserver % _observers.size();
        for (size_t step = 0; step < _observers.size(); ++step) {
            const size_t i = (_nextObserver + step) % _observers.size();
            if (nearby[i] < nearby[chosen]) {
                chosen = i;
            }
        }
        _nextObserver = chosen + 1;
        // Every player gets retail's density around them, the mission's own
        // maximum within the despawn radius; the scaled pool serves players
        // spread over the map.
        if (nearby[chosen] >= traffic._maxActiveCarCount) {
            return;
        }
        const auto &observer = _observers[chosen];
        std::vector<NativeTrafficCarElement *> before(static_cast<size_t>(traffic.SlotCount()));
        for (int32_t slot = 0; slot < traffic.SlotCount(); ++slot) {
            before[static_cast<size_t>(slot)] = traffic.Slot(slot);
        }
        // ActivateElems places the car out of this "camera's" sight.
        traffic._cameraPosition = observer.position;
        traffic._cameraDirection = Length(observer.direction) > 0.001f ? observer.direction : Vector3 {0.0f, 0.0f, 1.0f};
        (void)traffic.ActivateElems();
        for (int32_t slot = 0; slot < traffic.SlotCount(); ++slot) {
            auto *element = traffic.Slot(slot);
            // A returning police car (gSpawnedCars) keeps its pose by design.
            if (!element || element == before[static_cast<size_t>(slot)] || !element->_active || !element->_carModel || element->_dynamicallyAllocated) {
                continue;
            }
            // It must not appear in sight of any player, whichever way their
            // camera looks: ActivateElems only kept it out of this player's
            // view cone.
            const auto position = element->_carModel->WorldPosition();
            const float minimum = traffic._currentSpawnRadius * kSpawnClearance;
            if (NearestObserverSq(position) < minimum * minimum) {
                traffic.DeactivateElem(slot);
            }
        }
    }

    uint32_t TrafficHost::Add(NativeCar *car, Car::CarService::Origin origin, Kind kind, const NativeTrafficCarElement *element) {
        Record record;
        record.key = _nextKey++;
        record.car = car;
        record.origin = origin;
        record.kind = kind;
        record.reportedCrew = CrewOf(record, element);
        const std::string model = SDK::Traffic::ModelName(car->Frame());
        Shared::Traffic::Spawned spawned;
        spawned.missionGeneration = _missionGeneration;
        spawned.key = record.key;
        spawned.kind = kind;
        spawned.model = model;
        spawned.driverModel = record.reportedCrew.driverModel;
        spawned.passengerModel = record.reportedCrew.passengerModel;
        spawned.driverState = record.reportedCrew.driver;
        spawned.passengerState = record.reportedCrew.passenger;
        spawned.seatCount = std::max<uint8_t>(1, SDK::Traffic::CarSeatCount(car));
        const auto position = car->Frame()->WorldPosition();
        const auto rotation = FrameRotation(*car->Frame());
        const auto velocity = car->Vehicle().LinearVelocity();
        spawned.x = position.x;
        spawned.y = position.y;
        spawned.z = position.z;
        spawned.qw = rotation.w;
        spawned.qx = rotation.x;
        spawned.qy = rotation.y;
        spawned.qz = rotation.z;
        spawned.vx = velocity.x;
        spawned.vy = velocity.y;
        spawned.vz = velocity.z;
        spawned.lightState = car->Vehicle().LightState() & Shared::Car::Movement::kReplicatedLightMask;
        spawned.sirenOn = car->Vehicle().SirenOn();
        record.reportedSiren = spawned.sirenOn;
        // A model the server cannot name is not driven for players; its key
        // stays so the car is not reported again.
        record.reported = !model.empty() && std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
        if (record.reported) {
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(spawned, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            record.unboundSince = std::chrono::steady_clock::now();
        }
        _keyByCar[car] = record.key;
        const uint32_t key = record.key;
        _records.emplace(key, std::move(record));
        return key;
    }

    void TrafficHost::Discover(World::WorldService &world) {
        for (auto **slot = SDK::Traffic::CarElementsBegin(); slot && slot != SDK::Traffic::CarElementsEnd(); ++slot) {
            auto *element = *slot;
            // An exploded pool car stays dead until ActivateElem's full
            // C_car::Reset reuses it.
            if (!element || !element->_active || !element->_car || !element->_carModel || element->_pendingDeactivation || FindByCar(element->_car) ||
                static_cast<const NativeActor *>(static_cast<const void *>(element->_car))->IsDead()) {
                continue;
            }
            if (DrivenCount() >= Shared::Traffic::kMaxHostCars) {
                // The server would not take it, so no player would see it:
                // a pool car leaves the road again, any other car waits here
                // unreported until there is room.
                if (!element->_dynamicallyAllocated && _traffic) {
                    for (int32_t index = 0; index < _traffic->SlotCount(); ++index) {
                        if (_traffic->Slot(index) == element) {
                            _traffic->DeactivateElem(index);
                            break;
                        }
                    }
                }
                break;
            }
            const Kind kind = element->_policeCar ? Kind::Police : element->_mafiaCar ? Kind::Mafia : Kind::Civil;
            // A police car back in traffic (gSpawnedCars) is a temporary car.
            Add(element->_car, element->_dynamicallyAllocated ? Car::CarService::Origin::Temporary : Car::CarService::Origin::Pool, kind, element);
        }
        // Cars an NPC crew sits in: mafia pursuit cars, police cars.
        _npcs->ForEachTracked([&](NativeActor *actor, Shared::Entities::NpcKind npcKind) {
            auto *human = static_cast<SDK::Seat::NativeHuman *>(static_cast<void *>(actor));
            NativeActor *used = human->usedActorEnter ? human->usedActorEnter : human->usedActorLeave;
            // CarExplosion (0x421d60) moves the burned crew into its wreckage,
            // a new temporary C_car it marks dead (+0x5e), as it does the
            // original: neither is a car to drive for players.
            if (!used || used->GetType() != NativeActor::Type::Car || used->_actorState == kDeadActorState || used->IsDead() || FindByCar(used) ||
                world.NativeObjects().FindByNative(used).networkId != 0 || !IsTemporaryActor(used) || DrivenCount() >= Shared::Traffic::kMaxHostCars) {
                return;
            }
            Add(static_cast<NativeCar *>(static_cast<void *>(used)), Car::CarService::Origin::Temporary,
                npcKind == Shared::Entities::NpcKind::Police ? Kind::Police : Kind::Mafia, nullptr);
        });
        // Police cars the police manager handed out with nobody in them yet:
        // a road block's cars (GenerateRoadBlock -> CreateCar).
        auto *police = SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game());
        for (auto *slot = police->CarPoolBegin(); slot && slot != police->CarPoolEnd(); ++slot) {
            NativeActor *car = slot->actor;
            if (slot->free || !car || car->_actorState == kDeadActorState || car->IsDead() || FindByCar(car) || world.NativeObjects().FindByNative(car).networkId != 0 ||
                !IsTemporaryActor(car) || DrivenCount() >= Shared::Traffic::kMaxHostCars) {
                continue;
            }
            Add(static_cast<NativeCar *>(static_cast<void *>(car)), Car::CarService::Origin::Temporary, Kind::Police, nullptr);
        }
    }

    void TrafficHost::WatchBarriers() {
        auto *police = SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game());
        std::unordered_set<const void *> standing;
        for (auto *slot = police->BarrierPoolBegin(); slot && slot != police->BarrierPoolEnd(); ++slot) {
            // SpawnRoadBlockIris takes a barrier from the pool into C_game's
            // temporary actors; FreeTempIris puts it back.
            NativeActor *barrier = slot->actor;
            if (slot->free || !barrier || !barrier->Frame() || !IsTemporaryActor(barrier)) {
                continue;
            }
            standing.insert(barrier);
            const auto position = barrier->Frame()->WorldPosition();
            const auto forward = barrier->Frame()->GetWorldBasis().forward;
            if (const auto reported = _barrierKeys.find(barrier); reported != _barrierKeys.end()) {
                // Retirement (TickDoRiciActors -> FreeTempIris) and the next
                // road block (GenerateRoadBlock -> SpawnRoadBlockIris) can run
                // in the same C_shvestky::Tick: the same pool barrier then
                // stands somewhere else without ever showing free here.
                const auto &was = reported->second;
                const float dx = position.x - was.position.x;
                const float dy = position.y - was.position.y;
                const float dz = position.z - was.position.z;
                const float turn = forward.x * was.forward.x + forward.y * was.forward.y + forward.z * was.forward.z;
                if (dx * dx + dy * dy + dz * dz < 0.0001f && turn > 0.9999f) {
                    continue;
                }
                Shared::Traffic::BarrierRemoved removed;
                removed.missionGeneration = _missionGeneration;
                removed.key = was.key;
                Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(removed, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
                _barrierKeys.erase(reported);
            }
            const auto rotation = FrameRotation(*barrier->Frame());
            Shared::Traffic::BarrierPlaced placed;
            placed.missionGeneration = _missionGeneration;
            placed.key = _nextBarrierKey++;
            placed.x = position.x;
            placed.y = position.y;
            placed.z = position.z;
            placed.qw = rotation.w;
            placed.qx = rotation.x;
            placed.qy = rotation.y;
            placed.qz = rotation.z;
            _barrierKeys[barrier] = {placed.key, position, forward};
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(placed, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        for (auto it = _barrierKeys.begin(); it != _barrierKeys.end();) {
            if (standing.contains(it->first)) {
                ++it;
                continue;
            }
            Shared::Traffic::BarrierRemoved removed;
            removed.missionGeneration = _missionGeneration;
            removed.key = it->second.key;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(removed, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            it = _barrierKeys.erase(it);
        }
    }

    void TrafficHost::Bind(World::WorldService &world) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        const auto myGuid = static_cast<uint64_t>(replication->GetMyGUID());
        std::unordered_set<uint64_t> present;
        replication->ForEach<Shared::Entities::CarEntity>([&](Shared::Entities::CarEntity *entity) {
            if (entity->missionGeneration != _missionGeneration) {
                return;
            }
            present.insert(entity->GetNetworkID());
            if (entity->trafficKey == 0) {
                return;
            }
            const auto found = _records.find(entity->trafficKey);
            if (found == _records.end()) {
                return;
            }
            auto &record = found->second;
            // A key only names this host's own cars: another host's former
            // traffic car with the same key is a player's now.
            if (record.networkId == 0 &&
                ((entity->hostDriven && entity->simulationControllerGuid == myGuid) || (record.retryId != 0 && record.retryId == entity->GetNetworkID())) &&
                _cars->Adopt(world, entity->GetNetworkID(), record.car, record.origin)) {
                record.networkId = entity->GetNetworkID();
                record.retryId = 0;
            }
            if (record.networkId == entity->GetNetworkID() && !entity->hostDriven) {
                record.released = true;
            }
        });
        std::vector<uint32_t> gone;
        for (const auto &[key, record] : _records) {
            const uint64_t id = record.networkId != 0 ? record.networkId : record.retryId;
            if (id != 0 && !present.contains(id)) {
                gone.push_back(key);
            }
        }
        // The server removed it (a script, a wreck's timer, or a former
        // traffic car left alone): a pool car leaves the road; CarService's
        // removal deletes a temporary one natively, so its binding stays.
        for (const uint32_t key : gone) {
            auto *car = _records[key].car;
            const bool pool = _records[key].origin == Car::CarService::Origin::Pool;
            Retire(key, false, pool);
            if (pool && _traffic) {
                for (int32_t slot = 0; slot < _traffic->SlotCount(); ++slot) {
                    if (auto *element = _traffic->Slot(slot); element && element->_car == car) {
                        _traffic->DeactivateElem(slot);
                        break;
                    }
                }
            }
        }
    }

    TrafficHost::CrewView TrafficHost::CrewOf(const Record &record, const NativeTrafficCarElement *element) const {
        CrewView view;
        if (record.crewGone || !element || element->_car != record.car) {
            // The crew models stayed with the pool car; players drop theirs.
            view.driverModel = record.reportedCrew.driverModel;
            view.passengerModel = record.reportedCrew.passengerModel;
            view.driver = view.driverModel.empty() ? CrewState::None : CrewState::Gone;
            view.passenger = view.passengerModel.empty() ? CrewState::None : CrewState::Gone;
            return view;
        }
        // ChodDoRici puts the pedestrian taking a parked car in the passenger
        // model slot (+0x20) and sets +0x14e; it stays the driver from then
        // on, posed by AddDriverMan once its entry animation ends.
        const bool taken = element->_driverEntering;
        view.driverModel = SDK::Traffic::ModelName(taken ? element->_passengerModel : element->_driverModel);
        view.passengerModel = taken ? std::string {} : SDK::Traffic::ModelName(element->_passengerModel);
        if (!view.driverModel.empty()) {
            // A parked car's driver got out and walked off: its model stays
            // in the element, switched off, with +0x14d clear.
            if (element->_driverRemoved || SDK::Traffic::IsParkedWithoutDriver(element)) {
                view.driver = CrewState::Gone;
            }
            else if (element->_animationActive) {
                view.driver = taken ? CrewState::Entering : CrewState::Exiting;
            }
            else {
                view.driver = record.driverDead ? CrewState::Dead : CrewState::Seated;
            }
        }
        if (!view.passengerModel.empty()) {
            view.passenger = element->_driverRemoved ? CrewState::Gone : record.passengerDead ? CrewState::Dead : CrewState::Seated;
        }
        return view;
    }

    void TrafficHost::Watch(World::WorldService &) {
        std::vector<uint32_t> retired;
        for (auto &[key, record] : _records) {
            auto *element = ElementOf(record.car);
            // A police car back in traffic (NoTakMiTedaDajTuPleckuJaJuOdvezu)
            // carries crew models again.
            if (record.crewGone && element && element->_active && element->_driverModel) {
                record.crewGone = false;
            }
            // A car a traffic element holds (a pool car, or a police car back
            // in traffic) lives as long as the element. Any other temporary
            // car until it is freed back to the police pool or removed (out
            // of C_game's temporary actors) or wrecked; a stopped car only
            // sleeps (ACT_STATE_INACTIVE) and stays.
            bool alive = element && element->_active;
            if (!alive && record.origin == Car::CarService::Origin::Temporary) {
                alive = IsTemporaryActor(record.car) && static_cast<NativeActor *>(static_cast<void *>(record.car))->_actorState != kDeadActorState;
            }
            if (!alive) {
                retired.push_back(key);
                continue;
            }
            if (!record.reported || record.networkId == 0 || record.released) {
                continue;
            }
            const CrewView view = CrewOf(record, element);
            // A new driver climbing in is alive, whoever sat there before.
            if (view.driver == CrewState::Entering) {
                record.driverDead = false;
            }
            const bool siren = record.car->Vehicle().SirenOn();
            if (view == record.reportedCrew && siren == record.reportedSiren) {
                continue;
            }
            record.reportedCrew = view;
            record.reportedSiren = siren;
            Shared::Traffic::Crew crew;
            crew.missionGeneration = _missionGeneration;
            crew.key = key;
            crew.driverModel = view.driverModel;
            crew.passengerModel = view.passengerModel;
            crew.driverState = view.driver;
            crew.passengerState = view.passenger;
            crew.sirenOn = siren;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(crew, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        for (const uint32_t key : retired) {
            Retire(key, true);
        }
    }

    void TrafficHost::Retire(uint32_t key, bool report, bool release) {
        const auto found = _records.find(key);
        if (found == _records.end()) {
            return;
        }
        auto &record = found->second;
        // A car a player took stays theirs: the host stops observing it
        // through this native car and creates its own copy if it needs one.
        if (release && record.networkId != 0 && _world) {
            _cars->ReleaseAdopted(*_world, record.networkId);
        }
        if (report && record.reported && !record.released) {
            Shared::Traffic::Retired retired;
            retired.missionGeneration = _missionGeneration;
            retired.key = key;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(retired, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        _keyByCar.erase(record.car);
        _records.erase(found);
    }

    void TrafficHost::Transfer(NativeCar *from, NativeCar *to) {
        auto *record = FindByCar(from);
        if (!record || !to || to == from) {
            return;
        }
        if (auto *stale = FindByCar(to)) {
            Retire(stale->key, true);
            record = FindByCar(from);
            if (!record) {
                return;
            }
        }
        if (record->networkId != 0 && _world) {
            _cars->ReleaseAdopted(*_world, record->networkId);
            if (!_cars->Adopt(*_world, record->networkId, to, Car::CarService::Origin::Temporary)) {
                // Bind adopts it again on its next pass.
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Could not move network car {} to its native replacement yet", record->networkId);
                record->retryId = record->networkId;
                record->networkId = 0;
                record->unboundSince = std::chrono::steady_clock::now();
            }
        }
        _keyByCar.erase(from);
        _keyByCar[to] = record->key;
        record->car = to;
        record->origin = Car::CarService::Origin::Temporary;
        record->crewGone = true;
    }

    void TrafficHost::BeforeDeactivate(NativeTrafficCar *traffic, int32_t slot) {
        if (slot < 0 || slot >= traffic->SlotCount()) {
            return;
        }
        auto *element = traffic->Slot(slot);
        auto *record = element ? FindByCar(element->_car) : nullptr;
        // A returned police car (dynamic element) lives on as a temporary car;
        // Watch retires it once retail frees or removes it.
        if (record && record->origin == Car::CarService::Origin::Pool) {
            Retire(record->key, true);
        }
    }

    bool TrafficHost::IsPlayerCaused(NativeActor *actor) const {
        if (!actor || !_world) {
            return false;
        }
        if (actor->GetType() == NativeActor::Type::Car) {
            actor = static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(actor))->GetOwner(0);
            if (!actor) {
                return false;
            }
        }
        const auto handle = _world->NativeObjects().FindByNative(actor);
        auto *replication = Framework::CoreModules::GetReplication();
        return handle.networkId && replication && replication->GetEntity<Shared::Entities::PlayerEntity>(handle.networkId);
    }

    void TrafficHost::OnRammed(NativeCar *car, NativeActor *actor, float force) {
        // CoDelasTyCuraku1 with "the player" read as any player: a mafia car
        // rammed harder than force 5 by a player or a player's car sends its
        // crew after that player (Krles).
        auto *element = ElementOf(car);
        if (!element || !element->_mafiaCar || element->_pendingDeactivation || !(force > SDK::Traffic::kMafiaResponseForce) || !IsPlayerCaused(actor)) {
            return;
        }
        const auto *first = SDK::Traffic::PursuitEntriesBegin();
        const size_t before = first ? static_cast<size_t>(SDK::Traffic::PursuitEntriesEnd() - first) : 0;
        SDK::Traffic::MafiaResponse(car, actor);
        auto *begin = SDK::Traffic::PursuitEntriesBegin();
        const size_t after = begin ? static_cast<size_t>(SDK::Traffic::PursuitEntriesEnd() - begin) : 0;
        if (after != before + 1) {
            return;
        }
        const auto &pursuit = begin[after - 1];
        Transfer(car, pursuit.car);
        // Krles seated the crew (Intern_UseCar); the AI may send it out
        // before the next seat pass, which would then see it on foot.
        for (NativeActor *member : {pursuit.driver, pursuit.passenger}) {
            if (member) {
                _npcs->TrackIfNew(member, Shared::Entities::NpcKind::Gangster);
                _npcs->ReportCrewSeat(member, static_cast<SDK::Seat::NativeCar *>(pursuit.car));
            }
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Mafia car crew turned on player {}", _world->NativeObjects().FindByNative(actor).networkId);
    }

    void TrafficHost::OnCarCollision(NativeCar *car, NativeActor *other, const Vector3 &normal, bool retailRuns) {
        // C_car::Call_Collision (0x425480, reM C_car.cpp:5647-5668): a pool
        // car rammed by a car whose driver the police track while they are
        // active, or by the retail player's own car (m_bActivePlayerInside),
        // reacts: a police car books the ram on the driver (CoDelasTyCuraku),
        // a mafia car sends its crew after them (CoDelasTyCuraku1). The host
        // has no player of its own, so a player's car never passed the test
        // unless the police were active and tracking that player: here any
        // car a player drives is retail's player's.
        if (!car || !other || other->GetType() != NativeActor::Type::Car) {
            return;
        }
        auto *rammed = static_cast<NativeCar *>(static_cast<void *>(other));
        if (!rammed->TrafficCar() || car->Vehicle().SeatSlotCount() == 0) {
            return;
        }
        const bool retail = (car->CollisionCallbacksEnabled() && SDK::Police::PoliceManager(SDK::Core::Mission::Get()->Game())->IsActive()) ||
                            car->ActivePlayerInside();
        if ((retailRuns && retail) || (!retail && !IsPlayerCaused(car))) {
            return;
        }
        // Its speed: the closing speed along the rammer's heading to the
        // contact normal, as Call_Collision measures it.
        using AngleTo     = float(__stdcall *)(const Vector3 *, const Vector3 *);
        using PoliceRammed = void(__fastcall *)(NativeCar *, NativeActor *, float);
        const auto velocity      = car->Vehicle().LinearVelocity();
        const auto otherVelocity = rammed->Vehicle().LinearVelocity();
        const float closing      = glm::length(glm::vec3(velocity.x - otherVelocity.x, velocity.y - otherVelocity.y, velocity.z - otherVelocity.z));
        const float speed        = std::fabs(std::cos(reinterpret_cast<AngleTo>(SDK::Car::kVectorAngleTo)(&velocity, &normal)) * closing);
        if (!std::isfinite(speed)) {
            return;
        }
        reinterpret_cast<PoliceRammed>(SDK::Traffic::kTrafficCarPoliceRammed)(rammed, static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(car))->GetOwner(0), speed);
        OnRammed(rammed, car, speed);
    }

    void TrafficHost::OnReplaced(NativeCar *poolCar, NativeCar *replacement) {
        if (replacement && replacement != poolCar) {
            Transfer(poolCar, replacement);
        }
    }

    void TrafficHost::OnOccupantShot(NativeCar *car, void *frame) {
        auto *record = FindByCar(car);
        auto *element = record ? ElementOf(car) : nullptr;
        if (!element) {
            return;
        }
        // After ChodDoRici the passenger slot holds the driver.
        const bool taken = element->_driverEntering;
        if (frame == (taken ? element->_passengerModel : element->_driverModel)) {
            record->driverDead = true;
        }
        else if (!taken && frame == element->_passengerModel) {
            record->passengerDead = true;
        }
    }

    void TrafficHost::BeforePoliceCars(std::vector<NativeCar *> &pending) {
        for (auto **element = SDK::Traffic::CarElementsBegin(); element && element != SDK::Traffic::CarElementsEnd(); ++element) {
            if (*element && (*element)->_pendingDeactivation) {
                pending.push_back((*element)->_car);
            }
        }
    }

    void TrafficHost::AfterPoliceCars(NativeCar *const *first, NativeCar *const *last, size_t before, const std::vector<NativeCar *> &pending) {
        if (!first || static_cast<size_t>(last - first) <= before) {
            return;
        }
        // Each new police car stands exactly where the traffic police car it
        // replaced was, and that element is newly flagged for deactivation.
        for (auto *const *policeCar = first + before; policeCar != last; ++policeCar) {
            if (!*policeCar || !(*policeCar)->Frame()) {
                continue;
            }
            const auto position = (*policeCar)->Frame()->WorldPosition();
            NativeCar *source = nullptr;
            float best = 1.0f;
            for (auto **element = SDK::Traffic::CarElementsBegin(); element && element != SDK::Traffic::CarElementsEnd(); ++element) {
                auto *candidate = *element;
                if (!candidate || !candidate->_pendingDeactivation || !candidate->_carModel ||
                    std::find(pending.begin(), pending.end(), candidate->_car) != pending.end()) {
                    continue;
                }
                const auto from = candidate->_carModel->WorldPosition();
                const float distance = Length({from.x - position.x, from.y - position.y, from.z - position.z});
                if (distance < best) {
                    best = distance;
                    source = candidate->_car;
                }
            }
            if (source) {
                Transfer(source, *policeCar);
            }
        }
    }

    bool TrafficHost::AwaitsBinding(const void *car) const {
        const auto key = _keyByCar.find(car);
        const auto record = key == _keyByCar.end() ? _records.end() : _records.find(key->second);
        return record != _records.end() && record->second.networkId == 0 && !record->second.released &&
               (record->second.reported || record->second.retryId != 0) && std::chrono::steady_clock::now() - record->second.unboundSince < kBindingWait;
    }

    void TrafficHost::OnNativeCarDestroyed(void *car) {
        if (auto *record = FindByCar(car)) {
            Retire(record->key, true);
        }
    }
} // namespace Mafia1Online::Features::Traffic
