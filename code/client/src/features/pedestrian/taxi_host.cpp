#include <utils/safe_win32.h>

#include "taxi_host.h"

#include "features/simulation/simulation_config.h"
#include "features/world/world_service.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/taxi/taxi_protocol.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/game.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/police/native_police_manager.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <mafia1/sdk/traffic/native_traffic.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Mafia1Online::Features::Pedestrian {
    namespace {
        using SDK::Player::NativeActor;
        using SDK::Traffic::NativeTrafficElement;
        using MavaNaTaxi = void(__thiscall *)(NativeTrafficElement *, uint32_t);

        MavaNaTaxi gMavaNaTaxiOriginal = nullptr;
        TaxiHost *gTaxiHost             = nullptr;

        void __fastcall MavaNaTaxiHook(NativeTrafficElement *element, void *, uint32_t frameMs) {
            gTaxiHost->OnWave(element, frameMs, gMavaNaTaxiOriginal);
        }

        // C_game's taxi points (m_TaxiPoints, _First +0x1f8, _Last +0x1fc):
        // GeneratePassengerForTaxi hails nobody in a mission without them
        // (0x44f85d-0x44f86f).
        bool MissionHasTaxiPoints(SDK::Core::Game::NativeGame *game) {
            const auto *base = reinterpret_cast<const std::byte *>(game);
            const auto *first = *reinterpret_cast<void *const *const *>(base + 0x1f8);
            const auto *last = *reinterpret_cast<void *const *const *>(base + 0x1fc);
            return first && last != first;
        }

        // C_car's seat vector (+0xd14 _First, +0xd18 _Last, 36 bytes a seat),
        // which GeneratePassengerForTaxi walks from seat 1 (0x44f8a2-0x44f8cb).
        int32_t NativeSeatCount(const NativeActor *car) {
            const auto *base = reinterpret_cast<const std::byte *>(car);
            const auto *first = *reinterpret_cast<const std::byte *const *>(base + 0xd14);
            const auto *last = *reinterpret_cast<const std::byte *const *>(base + 0xd18);
            return first ? static_cast<int32_t>((last - first) / 36) : 0;
        }

        SDK::Seat::NativeCar *SeatCar(NativeActor *car) {
            return static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(car));
        }

        float LengthSq(const SDK::Player::Vector3 &v) {
            return v.x * v.x + v.y * v.y + v.z * v.z;
        }

        // S_vector::AngleTo (0x60fc7e): the angle between two directions.
        float AngleBetween(const SDK::Player::Vector3 &a, const SDK::Player::Vector3 &b) {
            const float lengths = std::sqrt(LengthSq(a) * LengthSq(b));
            if (!(lengths > 0.0f)) {
                return 0.0f;
            }
            return std::acos(std::clamp((a.x * b.x + a.y * b.y + a.z * b.z) / lengths, -1.0f, 1.0f));
        }

        uint64_t MissionGeneration(World::WorldService *world) {
            return world ? world->LoadedMissionGeneration() : 0;
        }
    } // namespace

    bool TaxiHost::Install() {
        gTaxiHost = this;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Traffic::kElementMavaNaTaxi), reinterpret_cast<void *>(&MavaNaTaxiHook), reinterpret_cast<void **>(&gMavaNaTaxiOriginal)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void *>(SDK::Traffic::kElementMavaNaTaxi)) != MH_OK) {
            Uninstall();
            return false;
        }
        return true;
    }

    void TaxiHost::Uninstall() {
        MH_DisableHook(reinterpret_cast<void *>(SDK::Traffic::kElementMavaNaTaxi));
        MH_RemoveHook(reinterpret_cast<void *>(SDK::Traffic::kElementMavaNaTaxi));
        Reset();
        gTaxiHost = nullptr;
    }

    void TaxiHost::RegisterRPC() {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        network->RegisterRPC<Shared::Taxi::Drivers>([this](const Shared::Taxi::Drivers &drivers, MafiaNet::Packet *) {
            if (!Simulation::IsHost()) {
                return;
            }
            _allowed = {drivers.playerNetworkIds.begin(), drivers.playerNetworkIds.end()};
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Taxi hails for {} player(s)", _allowed.size());
        });
        network->RegisterRPC<Shared::Taxi::EndHail>([this](const Shared::Taxi::EndHail &end, MafiaNet::Packet *) {
            if (!Simulation::IsHost() || end.missionGeneration != MissionGeneration(_world)) {
                return;
            }
            for (auto &[element, hail] : _hails) {
                if (hail.key == end.walkerKey && element->IsActive() && element->TaxiWaving()) {
                    element->EndTaxiWave();
                }
            }
        });
    }

    void TaxiHost::Reset() {
        // The walkers went with the mission; their waves need no ending.
        _drivers.clear();
        _timers.clear();
        _hails.clear();
        _ticking = nullptr;
        _previousPlayer = nullptr;
        _missionGeneration = 0;
    }

    void TaxiHost::Forget(NativeTrafficElement *element) {
        // Retail's passenger leaves the countdown at 20 s (0x452c4b).
        if (const auto hail = _hails.find(element); hail != _hails.end()) {
            _timers[hail->second.driverId] = SDK::Traffic::kTaxiPassengerCooldownMs;
        }
        if (element->TaxiWaving()) {
            element->EndTaxiWave();
        }
    }

    SDK::Police::NativePoliceManager *TaxiHost::Police() {
        auto *game = SDK::Core::Mission::Get()->Game();
        return reinterpret_cast<SDK::Police::NativePoliceManager *>(reinterpret_cast<std::byte *>(game) + SDK::Police::kGamePoliceManager);
    }

    bool TaxiHost::Wanted(NativeActor *human) {
        // Tick runs MavaNaTaxi only while the police manager is on
        // (0x45335f) and dismisses every wave when C_game's player has
        // officers on it (0x45337a-0x453383); hailing checks the same
        // (0x44f86f-0x44f88f). Here each driver's own record counts.
        auto *police = Police();
        if (!police->IsActive()) {
            return false;
        }
        using Call = int32_t(__thiscall *)(SDK::Police::NativePoliceManager *, NativeActor *);
        return reinterpret_cast<Call>(SDK::Traffic::kPoliceOfficersOn)(police, human) > 0;
    }

    void TaxiHost::Prepare(World::WorldService &world) {
        _world = &world;
        if (_missionGeneration != world.LoadedMissionGeneration()) {
            Reset();
            _missionGeneration = world.LoadedMissionGeneration();
        }
        _drivers.clear();
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return;
        }
        for (const uint64_t id : _allowed) {
            const auto *player = replication->GetEntity<Shared::Entities::PlayerEntity>(id);
            if (!player || !player->spawned || !player->alive || player->missionGeneration != world.LoadedMissionGeneration()) {
                continue;
            }
            auto *human = static_cast<NativeActor *>(world.NativeObjects().Resolve(world.NativeObjects().FindByNetwork(id)));
            if (!human) {
                continue;
            }
            const auto *seated = static_cast<const SDK::Seat::NativeHuman *>(static_cast<const void *>(human));
            Driver driver;
            driver.human = human;
            if (seated->usedActorEnter && seated->usedActorEnter->GetType() == NativeActor::Type::Car) {
                driver.car = seated->usedActorEnter;
                driver.seat = static_cast<uint8_t>(std::clamp(seated->seatId, 0, 0xff));
                driver.carId = world.NativeObjects().FindByNative(driver.car).networkId;
                const auto *car = driver.carId ? replication->GetEntity<Shared::Entities::CarEntity>(driver.carId) : nullptr;
                driver.speed = car ? glm::length(car->velocity) : 0.0f;
                driver.taxi = driver.carId && seated->seatId == 0 && SDK::Traffic::IsTaxiCar(driver.car);
            }
            _drivers[id] = driver;
        }
        for (auto it = _timers.begin(); it != _timers.end();) {
            it = _allowed.contains(it->first) ? std::next(it) : _timers.erase(it);
        }
    }

    bool TaxiHost::BeginTick(NativeTrafficElement *element) {
        if (!element->TaxiWaving()) {
            return false;
        }
        const auto hail = _hails.find(element);
        const auto driver = hail == _hails.end() ? _drivers.end() : _drivers.find(hail->second.driverId);
        // MavaNaTaxi ends a wave whose player sits in no car (0x45201d-
        // 0x452043); here also one whose driver left or may not be hailed.
        // With the police manager off Tick would freeze the walker
        // (0x453367), so the wave ends instead.
        if (driver == _drivers.end() || !driver->second.car || !Police()->IsActive() || Wanted(driver->second.human)) {
            element->EndTaxiWave();
            return false;
        }
        auto *game = SDK::Core::Mission::Get()->Game();
        _ticking = &hail->second;
        _previousPlayer = game->ExchangePlayer(driver->second.human);
        return true;
    }

    void TaxiHost::EndTick() {
        SDK::Core::Mission::Get()->Game()->ExchangePlayer(_previousPlayer);
        _ticking = nullptr;
        _previousPlayer = nullptr;
    }

    void TaxiHost::OnWave(NativeTrafficElement *element, uint32_t frameMs, MavaNaTaxi original) {
        if (!Simulation::IsHost() || !_ticking) {
            original(element, frameMs);
            return;
        }
        auto &hail = *_ticking;
        const auto &driver = _drivers.at(hail.driverId);
        const auto now = std::chrono::steady_clock::now();
        if (!hail.pickupReported) {
            // MavaNaTaxi's boarding test (0x4522ed-0x45232f) with the speed
            // the driver reports and the same model-to-car offset.
            const auto model = element->Model()->WorldPosition();
            const auto car = SDK::Player::WorldPosition(driver.car);
            const SDK::Player::Vector3 offset {car.x - model.x, car.y - model.y, car.z - model.z};
            if (driver.speed < SDK::Traffic::kTaxiBoardMaxSpeed && LengthSq(offset) < SDK::Traffic::kTaxiBoardDistanceSq) {
                hail.pickupReported = true;
                hail.pickupAt = now;
                Shared::Taxi::Pickup pickup;
                pickup.missionGeneration = _missionGeneration;
                pickup.walkerKey = hail.key;
                pickup.driverId = hail.driverId;
                pickup.carId = driver.carId;
                Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(pickup, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }
        else if (now - hail.pickupAt > kPickupWait) {
            element->EndTaxiWave();
            return;
        }
        // Retail would make the walker its taxi_pas.dat passenger right here;
        // that passenger is the script's to make. The rest of MavaNaTaxi
        // turns the walker to the taxi and keeps it waving.
        auto &speed = SeatCar(driver.car)->Vehicle()._speed;
        const float actual = speed;
        speed = std::max(actual, kNoBoardingSpeed);
        original(element, frameMs);
        speed = actual;
    }

    void TaxiHost::Finish(uint32_t frameMs, const std::function<uint32_t(NativeTrafficElement *)> &keyOf) {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        // TickElements' tail (0x44f052-0x44f07c), per driver.
        for (auto &[id, driver] : _drivers) {
            if (!driver.taxi) {
                continue;
            }
            auto &timer = _timers[id];
            if (timer < 0) {
                Generate(id, driver, keyOf);
            }
            else {
                timer -= static_cast<int32_t>(frameMs);
            }
        }
        for (auto it = _hails.begin(); it != _hails.end();) {
            auto *element = it->first;
            if (element->IsActive() && element->TaxiWaving()) {
                ++it;
                continue;
            }
            Shared::Taxi::HailEnded ended;
            ended.missionGeneration = _missionGeneration;
            ended.walkerKey = it->second.key;
            network->BroadcastRPC(ended, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            it = _hails.erase(it);
        }
    }

    void TaxiHost::Generate(uint64_t driverId, Driver &driver, const std::function<uint32_t(NativeTrafficElement *)> &keyOf) {
        auto &timer = _timers[driverId];
        timer = SDK::Traffic::kTaxiTimerBaseMs + SDK::Traffic::kTaxiTimerStepMs * static_cast<int32_t>(_random() % SDK::Traffic::kTaxiTimerVariants);
        auto *game = SDK::Core::Mission::Get()->Game();
        // 0x44f844-0x44f88f: slow enough, a mission with taxi points, no
        // officers on the driver.
        if (driver.speed > SDK::Traffic::kTaxiHailMaxSpeed || !MissionHasTaxiPoints(game) || Wanted(driver.human)) {
            return;
        }
        // The police manager off would leave a waving walker frozen: Tick
        // runs MavaNaTaxi only while it is on (0x45335f).
        if (!Police()->IsActive()) {
            return;
        }
        // No passenger aboard (0x44f8a2-0x44f8d7).
        auto *car = SeatCar(driver.car);
        for (int32_t seat = 1; seat < NativeSeatCount(driver.car); ++seat) {
            if (car->GetOwner(seat)) {
                return;
            }
        }
        // At most one other walker waving (0x44f8fb-0x44f92a), here this
        // driver's own.
        const auto waving = std::count_if(_hails.begin(), _hails.end(), [&](const auto &entry) {
            return entry.second.driverId == driverId && entry.first->IsActive() && entry.first->TaxiWaving();
        });
        if (waving > SDK::Traffic::kTaxiMaxOtherWavers) {
            timer += SDK::Traffic::kTaxiRetryDelayMs;
            return;
        }
        const auto taxiPosition = driver.car->Frame()->WorldPosition();
        const auto taxiDirection = driver.car->Frame()->WorldDirection();
        // The last eligible walker of the active vector (0x44fad2-0x44fc3f).
        auto **begin = SDK::Traffic::ElementsBegin();
        for (auto **entry = SDK::Traffic::ElementsEnd(); entry != begin;) {
            auto *element = *--entry;
            const uint8_t type = element->PedestrianType();
            if (type == SDK::Traffic::kPolicePedestrian || type == SDK::Traffic::kPoliceSecondaryPedestrian || type == SDK::Traffic::kGangsterPedestrian) {
                continue;
            }
            if (!element->IsActive() || element->PanicBlocked() || element->Panicking() || !element->HasGenerator() || element->FollowsCar() ||
                element->TaxiWaving() || element->CurrentAnimation() != SDK::Traffic::kWalkAnimation || !(element->Alpha() >= 1.0f) || !element->CanHailTaxi() ||
                !element->Model()) {
                continue;
            }
            // A walker the server does not know yet cannot be shown waving.
            const uint32_t key = keyOf(element);
            if (!key) {
                continue;
            }
            const auto position = element->Model()->WorldPosition();
            const SDK::Player::Vector3 offset {position.x - taxiPosition.x, position.y - taxiPosition.y, position.z - taxiPosition.z};
            const float distanceSq = LengthSq(offset);
            if (!(distanceSq > SDK::Traffic::kTaxiHailMinDistanceSq) || !(distanceSq < SDK::Traffic::kTaxiHailMaxDistanceSq) ||
                !(AngleBetween(taxiDirection, offset) < SDK::Traffic::kTaxiHailAngle)) {
                continue;
            }
            const auto wave = static_cast<uint8_t>(1 + _random() % Shared::Taxi::kWaveVariants);
            element->StartTaxiWave(wave);
            timer += SDK::Traffic::kTaxiRetryDelayMs;
            _hails[element] = {driverId, key, wave, false, {}};
            Shared::Taxi::Hail hail;
            hail.missionGeneration = _missionGeneration;
            hail.walkerKey = key;
            hail.driverId = driverId;
            hail.carId = driver.carId;
            hail.wave = wave;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(hail, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Walker {} hails player {}'s taxi", key, driverId);
            return;
        }
    }
} // namespace Mafia1Online::Features::Pedestrian
