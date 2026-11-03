#include "fire_service.h"

#include "features/car/car_service.h"
#include "features/combat/combat_service.h"
#include "features/player/player_service.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Fire {
    namespace {
        using Shared::Entities::FireEntity;
        using Shared::Entities::WaterJetEntity;

        // CombatService's fire areas tick every 500 ms; a fire renews its
        // area at that rhythm, so damage x 0.5 lands each tick.
        constexpr auto kDamageTick = std::chrono::milliseconds(500);
        constexpr auto kDouseMemory = std::chrono::milliseconds(1500);
        // Regrowth waits this long after the last water.
        constexpr auto kGrowthDelay = std::chrono::seconds(3);
        // A jet's target counts within the fire's radius plus this.
        constexpr float kSplash = 1.5f;
        // A person's hands, a truck's roof monitor.
        constexpr float kHandHeight = 1.3f;
        constexpr float kRoofHeight = 2.4f;
        constexpr float kChangeEpsilon = 0.004f;

        bool Finite(const glm::vec3 &v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // MafiaNet::VIRTUAL_WORLD_GLOBAL is in every world.
        bool SharesWorld(uint32_t left, uint32_t right) {
            return left == right || left == 0xFFFFFFFFu || right == 0xFFFFFFFFu;
        }

        Framework::Networking::Replication::ReplicationManager *Replication() {
            return Framework::CoreModules::GetReplication();
        }
    } // namespace

    void FireService::Init(Player::PlayerService &players, Car::CarService &cars, Combat::CombatService &combat) {
        _players = &players;
        _cars    = &cars;
        _combat  = &combat;
    }

    void FireService::Shutdown() {
        ResetForMission();
        _players = nullptr;
        _cars    = nullptr;
        _combat  = nullptr;
    }

    FireEntity *FireService::Create(const glm::vec3 &position, const Options &options, uint64_t missionGeneration, std::string owner) {
        if (_fires.size() >= kMaxFires || !Finite(position)) {
            return nullptr;
        }
        auto *fire              = Replication()->CreateEntity<FireEntity>();
        fire->missionGeneration = missionGeneration;
        fire->position          = position;
        fire->radius            = std::clamp(options.radius, FireEntity::kMinRadius, FireEntity::kMaxRadius);
        fire->height            = std::clamp(options.height, 0.0f, FireEntity::kMaxHeight);
        fire->intensity         = std::clamp(options.intensity, 0.05f, 1.0f);
        fire->attachedId        = options.attachedId;
        // xorshift; each fire looks different but the same everywhere.
        _nextSeed ^= _nextSeed << 13;
        _nextSeed ^= _nextSeed >> 17;
        _nextSeed ^= _nextSeed << 5;
        fire->seed                    = _nextSeed;
        fire->streaming.alwaysVisible = true;
        fire->SetVirtualWorld(options.virtualWorld);
        FireRecord record;
        record.owner     = std::move(owner);
        record.damage    = std::max(0.0f, options.damage);
        record.growth    = std::max(0.0f, options.growth);
        record.toughness = std::max(0.1f, options.toughness);
        record.lifetime  = options.lifetime;
        record.startedAt = std::chrono::steady_clock::now();
        _fires.emplace(fire->GetNetworkID(), std::move(record));
        return fire;
    }

    FireEntity *FireService::Find(uint64_t id) const {
        return _fires.contains(id) ? Replication()->GetEntity<FireEntity>(id) : nullptr;
    }

    std::vector<uint64_t> FireService::List() const {
        std::vector<uint64_t> ids;
        ids.reserve(_fires.size());
        for (const auto &[id, record] : _fires) {
            ids.push_back(id);
        }
        return ids;
    }

    bool FireService::Destroy(uint64_t id) {
        auto *fire = Find(id);
        _fires.erase(id);
        if (!fire) {
            return false;
        }
        Replication()->DestroyEntity(fire);
        return true;
    }

    void FireService::Out(uint64_t id, Reason reason) {
        auto found = _fires.find(id);
        auto *fire = Find(id);
        if (found == _fires.end() || !fire || found->second.out) {
            return;
        }
        found->second.out = true;
        fire->intensity   = 0.0f;
        fire->dousing     = false;
        const auto contributors = Contributors(id);
        if (_extinguished) {
            _extinguished(id, reason, contributors);
        }
    }

    float FireService::Douse(uint64_t id, float amount, uint64_t playerId) {
        auto found = _fires.find(id);
        auto *fire = Find(id);
        if (found == _fires.end() || !fire || !std::isfinite(amount)) {
            return -1.0f;
        }
        if (found->second.out || amount <= 0.0f) {
            return fire->intensity;
        }
        const float applied = std::min(amount, fire->intensity);
        fire->intensity     = std::max(0.0f, fire->intensity - applied);
        found->second.lastDoused = std::chrono::steady_clock::now();
        if (playerId != 0) {
            found->second.contributions[playerId] += applied;
        }
        if (fire->intensity <= 0.0f) {
            Out(id, Reason::Doused);
        }
        return fire->intensity;
    }

    bool FireService::SetIntensity(uint64_t id, float intensity) {
        auto found = _fires.find(id);
        auto *fire = Find(id);
        if (found == _fires.end() || !fire || !std::isfinite(intensity)) {
            return false;
        }
        fire->intensity = std::clamp(intensity, 0.0f, 1.0f);
        if (fire->intensity <= 0.0f) {
            Out(id, Reason::Script);
        }
        else {
            // A fire set alight again burns, and can be put out again.
            found->second.out = false;
        }
        return true;
    }

    bool FireService::SetRadius(uint64_t id, float radius) {
        auto *fire = Find(id);
        if (!fire || !std::isfinite(radius)) {
            return false;
        }
        fire->radius = std::clamp(radius, FireEntity::kMinRadius, FireEntity::kMaxRadius);
        return true;
    }

    bool FireService::SetPosition(uint64_t id, const glm::vec3 &position) {
        auto *fire = Find(id);
        if (!fire || !Finite(position)) {
            return false;
        }
        fire->attachedId = 0;
        fire->position   = position;
        return true;
    }

    bool FireService::AttachTo(uint64_t id, uint64_t vehicleId) {
        auto *fire = Find(id);
        if (!fire || (vehicleId != 0 && !_cars->Find(vehicleId))) {
            return false;
        }
        fire->attachedId = vehicleId;
        return true;
    }

    bool FireService::Extinguish(uint64_t id) {
        if (!Find(id)) {
            return false;
        }
        Out(id, Reason::Script);
        return true;
    }

    std::vector<FireService::Contribution> FireService::Contributors(uint64_t id) const {
        std::vector<Contribution> list;
        if (const auto found = _fires.find(id); found != _fires.end()) {
            for (const auto &[playerId, amount] : found->second.contributions) {
                list.push_back({playerId, amount});
            }
        }
        std::sort(list.begin(), list.end(), [](const Contribution &a, const Contribution &b) { return a.amount > b.amount; });
        return list;
    }

    WaterJetEntity *FireService::CreateJet(uint64_t sourceId, bool sourceIsVehicle, const JetOptions &options, uint64_t missionGeneration, std::string owner) {
        if (_jets.size() >= kMaxJets || sourceId == 0) {
            return nullptr;
        }
        auto *jet              = Replication()->CreateEntity<WaterJetEntity>();
        jet->missionGeneration = missionGeneration;
        jet->sourceId          = sourceId;
        jet->sourceIsVehicle   = sourceIsVehicle;
        jet->pressure          = std::clamp(options.pressure, WaterJetEntity::kMinPressure, WaterJetEntity::kMaxPressure);
        jet->streaming.alwaysVisible = true;
        jet->SetVirtualWorld(options.virtualWorld);
        if (const auto nozzle = Nozzle(*jet)) {
            jet->position = *nozzle;
        }
        _jets.emplace(jet->GetNetworkID(), JetRecord {std::move(owner), options.creditId});
        return jet;
    }

    WaterJetEntity *FireService::FindJet(uint64_t id) const {
        return _jets.contains(id) ? Replication()->GetEntity<WaterJetEntity>(id) : nullptr;
    }

    std::vector<uint64_t> FireService::ListJets() const {
        std::vector<uint64_t> ids;
        ids.reserve(_jets.size());
        for (const auto &[id, record] : _jets) {
            ids.push_back(id);
        }
        return ids;
    }

    bool FireService::DestroyJet(uint64_t id) {
        auto *jet = FindJet(id);
        _jets.erase(id);
        if (!jet) {
            return false;
        }
        Replication()->DestroyEntity(jet);
        return true;
    }

    bool FireService::AimJet(uint64_t id, std::optional<glm::vec3> target) {
        auto *jet = FindJet(id);
        if (!jet || (target && !Finite(*target))) {
            return false;
        }
        if (!target) {
            jet->active = false;
            return true;
        }
        jet->active = true;
        if (glm::distance(jet->target, *target) > 0.05f) {
            jet->target = *target;
        }
        return true;
    }

    bool FireService::SetJetPressure(uint64_t id, float pressure) {
        auto *jet = FindJet(id);
        if (!jet || !std::isfinite(pressure)) {
            return false;
        }
        jet->pressure = std::clamp(pressure, WaterJetEntity::kMinPressure, WaterJetEntity::kMaxPressure);
        return true;
    }

    std::optional<glm::vec3> FireService::Nozzle(const WaterJetEntity &jet) const {
        if (jet.sourceIsVehicle) {
            const auto *car = _cars->Find(jet.sourceId);
            return car ? std::optional<glm::vec3>(car->position + glm::vec3(0.0f, kRoofHeight, 0.0f)) : std::nullopt;
        }
        const auto *player = _players->FindByNetworkId(jet.sourceId);
        if (!player || !player->spawned || !player->alive) {
            return std::nullopt;
        }
        return player->position + glm::vec3(0.0f, kHandHeight, 0.0f);
    }

    void FireService::Update() {
        const auto now = std::chrono::steady_clock::now();
        const float dt = _lastUpdate == std::chrono::steady_clock::time_point {}
                             ? 0.0f
                             : std::min(0.25f, std::chrono::duration<float>(now - _lastUpdate).count());
        _lastUpdate = now;

        // Jets follow their nozzle and douse what they hit.
        std::unordered_map<uint64_t, std::vector<std::pair<uint64_t, float>>> hits;
        for (auto &[jetId, record] : _jets) {
            auto *jet = FindJet(jetId);
            if (!jet) {
                continue;
            }
            const auto nozzle = Nozzle(*jet);
            if (!nozzle) {
                jet->active = false;
                continue;
            }
            if (glm::distance(*nozzle, jet->position) > 0.05f) {
                jet->position = *nozzle;
            }
            if (!jet->active || glm::distance(*nozzle, jet->target) > kJetReach) {
                continue;
            }
            const uint64_t credit = jet->sourceIsVehicle ? record.creditId : jet->sourceId;
            for (const auto &[fireId, fireRecord] : _fires) {
                const auto *fire = Find(fireId);
                if (!fire || fireRecord.out || fire->missionGeneration != jet->missionGeneration || !SharesWorld(fire->GetVirtualWorld(), jet->GetVirtualWorld())) {
                    continue;
                }
                const glm::vec3 offset = jet->target - fire->position;
                const float flat       = std::sqrt(offset.x * offset.x + offset.z * offset.z);
                if (flat <= fire->radius + kSplash && offset.y > -3.0f && offset.y < fire->height + 3.0f) {
                    hits[fireId].emplace_back(credit, jet->pressure);
                }
            }
        }

        std::vector<uint64_t> fireIds = List();
        for (const uint64_t id : fireIds) {
            auto found = _fires.find(id);
            auto *fire = Find(id);
            if (found == _fires.end() || !fire) {
                _fires.erase(id);
                continue;
            }
            if (fire->attachedId != 0) {
                if (const auto *car = _cars->Find(fire->attachedId)) {
                    const glm::vec3 target = car->position + glm::vec3(0.0f, 0.4f, 0.0f);
                    if (Finite(target) && glm::distance(target, fire->position) > 0.05f) {
                        fire->position = target;
                    }
                }
                else {
                    fire->attachedId = 0;
                }
            }
            if (found->second.out) {
                continue;
            }
            const auto jetHits = hits.find(id);
            if (jetHits != hits.end() && dt > 0.0f) {
                const float scale = std::max(1.0f, fire->radius / kReferenceRadius) * found->second.toughness;
                for (const auto &[credit, pressure] : jetHits->second) {
                    Douse(id, kDousePerSecond * (pressure / kReferencePressure) * dt / scale, credit);
                }
                // The extinguished callback may have destroyed or added fires.
                found = _fires.find(id);
                fire  = Find(id);
                if (found == _fires.end() || !fire || found->second.out) {
                    continue;
                }
            }
            auto &record = found->second;
            const bool dousing = now - record.lastDoused < kDouseMemory;
            if (fire->dousing != dousing) {
                fire->dousing = dousing;
            }
            if (!dousing && record.growth > 0.0f && now - record.lastDoused >= kGrowthDelay && fire->intensity < 1.0f) {
                // Growth below the epsilon per tick builds up until it counts.
                record.pendingGrowth += record.growth * dt;
                const float grown = std::min(1.0f, fire->intensity + record.pendingGrowth);
                if (grown - fire->intensity >= kChangeEpsilon || grown >= 1.0f) {
                    fire->intensity      = grown;
                    record.pendingGrowth = 0.0f;
                }
            }
            else {
                record.pendingGrowth = 0.0f;
            }
            if (record.lifetime.count() > 0 && now - record.startedAt >= record.lifetime) {
                Out(id, Reason::BurnedOut);
                continue;
            }
            if (record.damage > 0.0f && now - record.lastDamage >= kDamageTick) {
                record.lastDamage = now;
                const float reach = std::max(1.0f, fire->radius * (0.5f + 0.5f * fire->intensity));
                // The area's damage lands at once and the area expires before
                // its next tick: damage x 0.5 s every 500 ms.
                _combat->StartFire(0, fire->missionGeneration, fire->position + glm::vec3(0.0f, 0.5f, 0.0f), kDamageTick, reach, record.damage * fire->intensity,
                                   fire->GetVirtualWorld());
            }
        }
    }

    void FireService::ResetForMission() {
        for (const uint64_t id : ListJets()) {
            DestroyJet(id);
        }
        for (const uint64_t id : List()) {
            Destroy(id);
        }
        _jets.clear();
        _fires.clear();
        _lastUpdate = {};
    }

    void FireService::DestroyOwnedBy(const std::string &owner) {
        std::vector<uint64_t> jets;
        for (const auto &[id, record] : _jets) {
            if (record.owner == owner) {
                jets.push_back(id);
            }
        }
        for (const uint64_t id : jets) {
            DestroyJet(id);
        }
        std::vector<uint64_t> fires;
        for (const auto &[id, record] : _fires) {
            if (record.owner == owner) {
                fires.push_back(id);
            }
        }
        for (const uint64_t id : fires) {
            Destroy(id);
        }
    }
} // namespace Mafia1Online::Features::Fire
