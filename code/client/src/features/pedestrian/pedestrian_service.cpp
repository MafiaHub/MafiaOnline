#include <utils/safe_win32.h>

#include "pedestrian_service.h"

#include "core/application.h"
#include "features/combat/combat_hooks.h"
#include "features/combat/combat_service.h"
#include "features/pedestrian/pedestrian_replicas.h"
#include "features/simulation/simulation_config.h"
#include "shared/features/car/car_entity.h"
#include "features/world/world_service.h"
#include "shared/features/combat/hit_report.h"
#include "shared/features/combat/melee_rules.h"
#include "shared/features/pedestrian/pedestrian_entity.h"
#include "shared/features/pedestrian/pedestrian_control.h"
#include "shared/features/pedestrian/pedestrian_scream.h"
#include "shared/features/player/player_entity.h"

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
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Pedestrian {
    namespace {
        using SDK::Traffic::NativeTrafficGenerator;
        using GeneratorGameInit = void(__thiscall *)(NativeTrafficGenerator *);
        using GeneratorAI       = void(__thiscall *)(NativeTrafficGenerator *, uint32_t);
        using TickElements      = void(__fastcall *)(uint32_t);
        using RecruitPolice     = void(__fastcall *)(void *, uint32_t, void *, bool, float, bool);
        using ElementNewScream  = void(__fastcall *)(int32_t, int32_t, SDK::Player::Vector3 *, float, float, float, float);
        using ElementHit        = void(__thiscall *)(SDK::Traffic::NativeTrafficElement *, int32_t, const SDK::Player::Vector3 *, uint32_t, SDK::Player::NativeActor *, float);
        using WalkerCollision   = bool(__thiscall *)(void *, SDK::Player::NativeActor *, const SDK::Player::Vector3 *, const SDK::Player::Vector3 *,
                                                     const SDK::Player::Vector3 *, float, SDK::Scene::NativeFrame *);
        using WalkerHit         = bool(__thiscall *)(void *, int32_t, const SDK::Player::Vector3 &, const SDK::Player::Vector3 &, const SDK::Player::Vector3 &, float,
                                               SDK::Player::NativeActor *, uint32_t, SDK::Scene::NativeFrame *);

        GeneratorGameInit gGeneratorGameInitOriginal = nullptr;
        GeneratorAI gGeneratorAIOriginal             = nullptr;
        TickElements gTickElementsOriginal           = nullptr;
        ElementHit gElementHitOriginal               = nullptr;
        ElementNewScream gElementNewScreamOriginal   = nullptr;
        RecruitPolice gRecruitPoliceOriginal         = nullptr;
        uint64_t gScreamMissionGeneration            = 0;
        WalkerCollision gWalkerCollisionOriginal     = nullptr;
        WalkerHit gWalkerHitOriginal                 = nullptr;
        PedestrianService *gService                  = nullptr;
        constexpr uintptr_t kHookAddresses[] {SDK::Traffic::kGeneratorGameInit, SDK::Traffic::kGeneratorAI, SDK::Traffic::kTickElements,
                                              SDK::Traffic::kElementHit, SDK::Traffic::kWalkerCollision, SDK::Traffic::kElementNewScream,
                                              SDK::Traffic::kWalkerHit};

        // Retail selects the nearest distance-selected generator with no
        // range limit: the search starts at 1e30 (C_game::Tick 0x5a5962).
        constexpr float kUnlimitedDistance = 1e30f;
        // Activate spawns from the camera; an observer's camera sits at eye
        // height above the replicated feet position.
        constexpr float kObserverEyeHeight = 1.7f;
        // A player's pools hold the replicas of every cluster it can see.
        constexpr uint32_t kPlayerPoolScale = 4;

        float DistanceSq(const SDK::Player::Vector3 &a, const SDK::Player::Vector3 &b) {
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            return dx * dx + dz * dz;
        }

        void __fastcall GeneratorGameInitHook(NativeTrafficGenerator *generator, void *) {
            // GameInit sizes the pool from the per-model counts and scales the
            // walker count by the setup density. Both are put back afterwards:
            // only the pool it allocates keeps the scale. Players never spawn,
            // so only the host's density matters.
            const bool host = Simulation::IsHost();
            const uint32_t scale = host ? Simulation::GetConfig().pedestrianPoolScale : kPlayerPoolScale;
            std::vector<uint32_t> counts(generator->_modelInstanceCounts, generator->_modelInstanceCounts + generator->_modelCount);
            for (int32_t model = 0; model < generator->_modelCount; ++model) {
                generator->_modelInstanceCounts[model] *= scale;
            }
            auto &density = *reinterpret_cast<float *>(SDK::Traffic::kPedestrianDensity);
            const float setupDensity = density;
            if (host) {
                density = Simulation::GetConfig().pedestrianDensity;
            }
            gGeneratorGameInitOriginal(generator);
            density = setupDensity;
            std::copy(counts.begin(), counts.end(), generator->_modelInstanceCounts);
        }

        void __fastcall GeneratorAIHook(NativeTrafficGenerator *generator, void *, uint32_t frameMs) {
            // Players never spawn walkers of their own.
            if (Simulation::IsHost()) {
                gService->OnGeneratorAI(generator, frameMs);
            }
        }

        void __fastcall TickElementsHook(uint32_t frameMs) {
            if (Simulation::IsHost()) {
                gService->OnTickElements(frameMs);
            }
            else {
                gTickElementsOriginal(frameMs);
            }
        }

        // HIT_TYPE_GENERIC: a bullet (Hit's blood case).
        constexpr int32_t kBulletHit = 0;
        // C_don_ocas::Collision's car hit (0x44cba3-0x44cbb5).
        constexpr int32_t kCarImpactHit = 5;
        constexpr uint32_t kTorso = 5;
        constexpr float kCarImpactDamageScale = 4.2f;

        // The host alone decides what happens to a walker: on a player the
        // walker is a replica, so a local bullet, blast or car neither hurts
        // nor moves it. Its reaction arrives with the host's animation; the
        // bullet's blood shows at once, as in single player.
        void __fastcall ElementHitHook(SDK::Traffic::NativeTrafficElement *element, void *, int32_t type, const SDK::Player::Vector3 *position, uint32_t part,
                                       SDK::Player::NativeActor *attacker, float damage) {
            if (!Simulation::IsHost()) {
                if (type == kBulletHit) {
                    auto *mission = SDK::Core::Mission::Get();
                    SDK::Traffic::AddWalkerBlood(mission, mission->Game(), *element, *position);
                }
                return;
            }
            // A gangster walker is not damaged: CreateMafianos retires it for
            // an armed C_entity, which GangsterHost reports.
            if (element->PedestrianType() == SDK::Traffic::kGangsterPedestrian) {
                gElementHitOriginal(element, type, position, part, attacker, damage);
                return;
            }
            const float health = element->Health();
            gElementHitOriginal(element, type, position, part, attacker, damage);
            gService->OnWalkerHit(element, type, attacker, damage, health, element->Health());
        }

        // The host's walkers scream for the players, not for the host: it has
        // no sound driver and its global cooldown would let one cluster
        // silence another. Each player applies its own range and cooldown.
        void __fastcall ElementNewScreamHook(int32_t voice, int32_t screamId, SDK::Player::Vector3 *position, float minimumRange, float maximumRange, float curve,
                                             float minimumVolume) {
            if (!Simulation::IsHost()) {
                gElementNewScreamOriginal(voice, screamId, position, minimumRange, maximumRange, curve, minimumVolume);
                return;
            }
            if (!gScreamMissionGeneration) {
                return;
            }
            Shared::Pedestrian::Scream scream;
            scream.missionGeneration = gScreamMissionGeneration;
            scream.voice = voice;
            scream.screamId = screamId;
            scream.x = position->x;
            scream.y = position->y;
            scream.z = position->z;
            scream.minimumRange = minimumRange;
            scream.maximumRange = maximumRange;
            scream.curve = curve;
            scream.minimumVolume = minimumVolume;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(scream, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
        }

        // Police recruitment makes C_entity officers nobody else sees. Only
        // the simulation host recruits them (Npc::InstallPoliceSpawn); a
        // player's police manager stays off, so this guards stray calls.
        void __fastcall RecruitPoliceHook(void *, uint32_t, void *, bool, float, bool) {}

        // A car's physics runs only on its driver's client, which reports the
        // contact with a walker replica; the host applies it (Impact). The
        // host's own copies of networked cars never hit walkers.
        bool __fastcall WalkerCollisionHook(void *proxy, void *, SDK::Player::NativeActor *actor, const SDK::Player::Vector3 *position, const SDK::Player::Vector3 *direction,
                                            const SDK::Player::Vector3 *normal, float force, SDK::Scene::NativeFrame *frame) {
            if (!Simulation::IsHost()) {
                if (actor && direction) {
                    gService->OnReplicaCollision(actor, *direction, frame);
                }
                return false;
            }
            if (actor && actor->GetType() == SDK::Player::NativeActor::Type::Car && gService->IsNetworkObject(actor)) {
                return false;
            }
            return gWalkerCollisionOriginal(proxy, actor, position, direction, normal, force, frame);
        }

        // A melee swing reaches a walker through its collision's Hit
        // (C_human::AI 0x571377, hit type 1). On a player the walker is a
        // replica whose element Hit does nothing, so the local player's
        // swing is reported instead; the hit particle still plays. On the
        // host a player's replica only replays that player's swing, which
        // need not line up with the walker here: the hit the player saw
        // arrives as a MeleeHit, so the replay's own hit is dropped.
        bool __fastcall WalkerHitHook(void *collision, void *, int32_t type, const SDK::Player::Vector3 &direction, const SDK::Player::Vector3 &position,
                                      const SDK::Player::Vector3 &normal, float damage, SDK::Player::NativeActor *attacker, uint32_t flags, SDK::Scene::NativeFrame *model) {
            if (type == SDK::Traffic::kHardKnockdownHit && attacker) {
                if (!Simulation::IsHost()) {
                    if (attacker == SDK::Player::CurrentPlayer()) {
                        gService->OnReplicaMelee(model, direction, position);
                    }
                }
                else if (!Combat::ValidatedMeleeScope::Active() && gService->IsNetworkPlayer(attacker)) {
                    return true;
                }
            }
            return gWalkerHitOriginal(collision, type, direction, position, normal, damage, attacker, flags, model);
        }

        // Swaps the active camera and C_game's player speed for one
        // observer's around a retail call that reads them, then restores both.
        class ObserverView final {
          public:
            explicit ObserverView(const PedestrianService::Observer &observer) {
                auto *mission = SDK::Core::Mission::Get();
                _game         = mission->Game();
                _camera       = mission->GetScene()->ActiveCamera();
                _position     = _camera->WorldPosition();
                _direction    = _camera->WorldDirection();
                _speed        = _game->_playerSpeed;
                _camera->SetWorldPosition({observer.position.x, observer.position.y + kObserverEyeHeight, observer.position.z});
                _camera->SetDirection(observer.forward);
                _camera->Update();
                _game->_playerSpeed = observer.speed;
            }
            ~ObserverView() {
                _camera->SetWorldPosition(_position);
                _camera->SetDirection(_direction);
                _camera->Update();
                _game->_playerSpeed = _speed;
            }
            ObserverView(const ObserverView &)            = delete;
            ObserverView &operator=(const ObserverView &) = delete;

          private:
            SDK::Core::Game::NativeGame *_game = nullptr;
            SDK::Scene::NativeFrame *_camera   = nullptr;
            SDK::Player::Vector3 _position {};
            SDK::Player::Vector3 _direction {};
            float _speed = 0.0f;
        };
    } // namespace

    bool PedestrianService::Install() {
        gService = this;
        if (MH_CreateHook(reinterpret_cast<void *>(SDK::Traffic::kGeneratorGameInit), reinterpret_cast<void *>(&GeneratorGameInitHook), reinterpret_cast<void **>(&gGeneratorGameInitOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(SDK::Traffic::kGeneratorAI), reinterpret_cast<void *>(&GeneratorAIHook), reinterpret_cast<void **>(&gGeneratorAIOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(SDK::Traffic::kTickElements), reinterpret_cast<void *>(&TickElementsHook), reinterpret_cast<void **>(&gTickElementsOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(SDK::Traffic::kElementHit), reinterpret_cast<void *>(&ElementHitHook), reinterpret_cast<void **>(&gElementHitOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(SDK::Traffic::kWalkerCollision), reinterpret_cast<void *>(&WalkerCollisionHook), reinterpret_cast<void **>(&gWalkerCollisionOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(SDK::Traffic::kElementNewScream), reinterpret_cast<void *>(&ElementNewScreamHook), reinterpret_cast<void **>(&gElementNewScreamOriginal)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<void *>(SDK::Traffic::kWalkerHit), reinterpret_cast<void *>(&WalkerHitHook), reinterpret_cast<void **>(&gWalkerHitOriginal)) != MH_OK) {
            Uninstall();
            return false;
        }
        if (!Simulation::IsHost() &&
            (MH_CreateHook(reinterpret_cast<void *>(SDK::Traffic::kRecruitPolice), reinterpret_cast<void *>(&RecruitPoliceHook), reinterpret_cast<void **>(&gRecruitPoliceOriginal)) != MH_OK ||
             MH_EnableHook(reinterpret_cast<void *>(SDK::Traffic::kRecruitPolice)) != MH_OK)) {
            Uninstall();
            return false;
        }
        for (const uintptr_t address : kHookAddresses) {
            if (MH_EnableHook(reinterpret_cast<void *>(address)) != MH_OK) {
                Uninstall();
                return false;
            }
        }
        if (!_taxi.Install()) {
            Uninstall();
            return false;
        }
        return true;
    }

    void PedestrianService::Uninstall() {
        for (const uintptr_t address : kHookAddresses) {
            MH_DisableHook(reinterpret_cast<void *>(address));
            MH_RemoveHook(reinterpret_cast<void *>(address));
        }
        if (!Simulation::IsHost()) {
            MH_DisableHook(reinterpret_cast<void *>(SDK::Traffic::kRecruitPolice));
            MH_RemoveHook(reinterpret_cast<void *>(SDK::Traffic::kRecruitPolice));
        }
        _taxi.Uninstall();
        Reset();
        gService = nullptr;
    }

    void PedestrianService::OnWalkerHit(SDK::Traffic::NativeTrafficElement *element, int32_t type, SDK::Player::NativeActor *attacker, float damage, float before, float after) {
        const uint32_t key = _reporter.KeyOf(element);
        if (!key || !_world) {
            return;
        }
        Shared::Pedestrian::Damaged damaged;
        damaged.missionGeneration = _world->LoadedMissionGeneration();
        damaged.key = key;
        damaged.attackerId = attacker ? _world->NativeObjects().FindByNative(attacker).networkId : 0;
        damaged.hitType = type;
        damaged.damage = damage;
        damaged.healthBefore = before;
        damaged.healthAfter = after;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(damaged);
    }

    void PedestrianService::RegisterRPC() {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        _taxi.RegisterRPC();
        // Host: what server scripts set.
        network->RegisterRPC<Shared::Pedestrian::Control>([this](const Shared::Pedestrian::Control &control, MafiaNet::Packet *) {
            if (!Simulation::IsHost() || !(control.density >= 0.0f && control.density <= 1.0f)) {
                return;
            }
            const bool disabling = _control.enabled && !control.enabled;
            _control = control;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Pedestrians {} at density {:.2f}, corpse lifetime {} ms", control.enabled ? "on" : "off", control.density, control.corpseLifetimeMs);
            if (disabling) {
                for (auto **element = SDK::Traffic::ElementsBegin(); element != SDK::Traffic::ElementsEnd();) {
                    // Deactivate removes the walker from the vector.
                    if ((*element)->IsActive()) {
                        (*element)->Deactivate();
                        element = SDK::Traffic::ElementsBegin();
                    }
                    else {
                        ++element;
                    }
                }
            }
        });
        // Host: a car impact a driver reported and the server checked. Retail
        // C_don_ocas::Collision hits the walker with hit type 5 on the torso
        // (5) for the contact speed x 4.2 (0x44cba3-0x44cbb5).
        network->RegisterRPC<Shared::Pedestrian::Impact>([this](const Shared::Pedestrian::Impact &impact, MafiaNet::Packet *) {
            if (!Simulation::IsHost() || !_world || !_world->IsReady() || impact.missionGeneration != _world->LoadedMissionGeneration()) {
                return;
            }
            const SDK::Player::Vector3 direction {impact.directionX, impact.directionY, impact.directionZ};
            const float speed = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
            auto *element = _reporter.ActiveElement(impact.key);
            auto *car = static_cast<SDK::Player::NativeActor *>(_world->NativeObjects().Resolve(_world->NativeObjects().FindByNetwork(impact.carId)));
            // Hit reads the car's position for the dodge, so it must exist.
            if (!element || !car || car->GetType() != SDK::Player::NativeActor::Type::Car || !std::isfinite(speed)) {
                return;
            }
            // Hit charges a car's impact only when its driver (seat 0) is
            // C_game's player (0x45891e-0x458948): here, that driver's replica.
            auto *game = SDK::Core::Mission::Get()->Game();
            auto *driver = static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(car))->GetOwner(0);
            auto *police = SDK::Police::PoliceManager(game);
            // C_don_ocas::Collision 0x44ca50 runs before the Hit: a beat cop
            // (pedestrian type 1 or 3) run into is a hit patrolman for the
            // driver (DalJsemPochuzkariDoDrzky).
            const uint8_t type = element->PedestrianType();
            if (driver && (type == SDK::Traffic::kPolicePedestrian || type == SDK::Traffic::kPoliceSecondaryPedestrian)) {
                police->ReportPatrolmanHit(driver);
            }
            auto *previous = game->ExchangePlayer(driver);
            reinterpret_cast<ElementHit>(SDK::Traffic::kElementHit)(element, kCarImpactHit, &direction, kTorso, car, speed * kCarImpactDamageScale);
            game->ExchangePlayer(previous);
            // ...and after it, a walker it killed counts as a run-over
            // (NextNaSrackySoudruh) while the police are active.
            if (driver && element->Health() <= 0.0f && police->IsActive()) {
                police->CountRunOver(driver);
            }
        });
        // Host: a player's melee hit on a walker's replica that the server
        // checked. C_human::AI's walker branch (0x571323-0x571377): the
        // boxing crime while the police are on, then the walker collision's
        // Hit, whose backstab reads the swing the server accepted.
        network->RegisterRPC<Shared::Pedestrian::MeleeHit>([this](const Shared::Pedestrian::MeleeHit &hit, MafiaNet::Packet *) {
            if (!Simulation::IsHost() || !_world || !_world->IsReady() || hit.missionGeneration != _world->LoadedMissionGeneration() || !std::isfinite(hit.damage) ||
                hit.damage < 0.0f || !std::isfinite(hit.directionX) || !std::isfinite(hit.directionY) || !std::isfinite(hit.directionZ) || !std::isfinite(hit.x) ||
                !std::isfinite(hit.y) || !std::isfinite(hit.z)) {
                return;
            }
            auto *element = _reporter.ActiveElement(hit.key);
            auto *attacker = static_cast<SDK::Player::NativeActor *>(_world->NativeObjects().Resolve(_world->NativeObjects().FindByNetwork(hit.attackerId)));
            if (!element || !element->Model() || element->IsDead() || !attacker || attacker->IsDead() ||
                (attacker->GetType() != SDK::Player::NativeActor::Type::Player && attacker->GetType() != SDK::Player::NativeActor::Type::Entity)) {
                return;
            }
            auto *game = SDK::Core::Mission::Get()->Game();
            const Combat::ValidatedMeleeScope validated;
            if (auto *police = SDK::Police::PoliceManager(game); police->IsActive()) {
                police->ReportBoxing(attacker);
            }
            auto &human = *static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(attacker));
            using Attack = SDK::Player::NativeHuman::MeleeAttack;
            const Attack previous = human.ExchangeMeleeAttack(Attack {hit.heavy ? SDK::Player::NativeHuman::kHeavyMeleeAttackIndex : 0, hit.weaponId});
            const SDK::Player::Vector3 direction {hit.directionX, hit.directionY, hit.directionZ};
            const SDK::Player::Vector3 position {hit.x, hit.y, hit.z};
            const SDK::Player::Vector3 normal {-hit.directionX, -hit.directionY, -hit.directionZ};
            SDK::Traffic::HitWalker(game, direction, position, normal, hit.damage, attacker, element->Model());
            human.ExchangeMeleeAttack(previous);
        });
        network->RegisterRPC<Shared::Pedestrian::Remove>([this](const Shared::Pedestrian::Remove &remove, MafiaNet::Packet *) {
            if (Simulation::IsHost() && _world && remove.missionGeneration == _world->LoadedMissionGeneration()) {
                _reporter.Retire(remove.key);
            }
        });
        // Players: a scream the server forwarded from the host.
        network->RegisterRPC<Shared::Pedestrian::Scream>([this](const Shared::Pedestrian::Scream &scream, MafiaNet::Packet *) {
            if (Simulation::IsHost() || !_world || !_world->IsReady() || scream.missionGeneration != _world->LoadedMissionGeneration() ||
                !std::isfinite(scream.x) || !std::isfinite(scream.y) || !std::isfinite(scream.z)) {
                return;
            }
            SDK::Traffic::PlayScream(scream.voice, scream.screamId, {scream.x, scream.y, scream.z}, scream.minimumRange, scream.maximumRange, scream.curve, scream.minimumVolume);
        });
    }

    bool PedestrianService::IsNetworkObject(void *native) const {
        return _world && _world->NativeObjects().FindByNative(native).networkId != 0;
    }

    bool PedestrianService::IsNetworkPlayer(const SDK::Player::NativeActor *actor) const {
        auto *replication = Framework::CoreModules::GetReplication();
        const uint64_t id = _world && replication ? _world->NativeObjects().FindByNative(const_cast<SDK::Player::NativeActor *>(actor)).networkId : 0;
        return id != 0 && replication->GetEntity<Shared::Entities::PlayerEntity>(id) != nullptr;
    }

    void PedestrianService::OnReplicaMelee(SDK::Scene::NativeFrame *model, const SDK::Player::Vector3 &direction, const SDK::Player::Vector3 &position) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!_world || !_world->IsReady() || !replication || !model) {
            return;
        }
        auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
        const uint64_t walkerId = application->PedestrianReplicas().WalkerOfModel(model);
        const auto *walker = walkerId ? replication->GetEntity<Shared::Entities::PedestrianEntity>(walkerId) : nullptr;
        auto &combat = application->Combat();
        const auto sequence = combat.OpenLocalMelee();
        const auto *attacker = combat.GetLocalState(*_world);
        // The sweep direction is the swing segment, not a unit vector.
        const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (!walker || !walker->alive || walker->missionGeneration != _world->LoadedMissionGeneration() || !sequence || !attacker || !attacker->spawned ||
            !attacker->alive || !(length > 0.0001f) || !combat.ClaimLocalMeleeReport(*sequence)) {
            return;
        }
        Shared::Combat::HitReport report;
        report.kind                   = Shared::Combat::HitReport::Kind::Melee;
        report.shooterNetworkId       = attacker->networkId;
        report.targetNetworkId        = walkerId;
        report.missionGeneration      = attacker->missionGeneration;
        report.shooterSpawnGeneration = attacker->spawnGeneration;
        // A walker life is its own entity.
        report.targetSpawnGeneration  = 0;
        report.shotSequence           = *sequence;
        report.bodyPart               = Shared::Combat::kWalkerMeleeBodyPart;
        report.directionX             = direction.x / length;
        report.directionY             = direction.y / length;
        report.directionZ             = direction.z / length;
        report.hitX                   = position.x;
        report.hitY                   = position.y;
        report.hitZ                   = position.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(report);
    }

    void PedestrianService::OnReplicaCollision(SDK::Player::NativeActor *actor, const SDK::Player::Vector3 &direction, SDK::Scene::NativeFrame *model) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!_world || !_world->IsReady() || !replication || actor->GetType() != SDK::Player::NativeActor::Type::Car) {
            return;
        }
        const uint64_t carId = _world->NativeObjects().FindByNative(actor).networkId;
        const auto *car = carId ? replication->GetEntity<Shared::Entities::CarEntity>(carId) : nullptr;
        if (!car || car->simulationControllerGuid != static_cast<uint64_t>(replication->GetMyGUID())) {
            return;
        }
        auto *application = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance());
        const uint64_t walkerId = application->PedestrianReplicas().WalkerOfModel(model);
        const auto now = std::chrono::steady_clock::now();
        // The contact repeats every physics step while the car touches it.
        if (!walkerId || (_lastImpact.contains(walkerId) && now - _lastImpact[walkerId] < kImpactCooldown)) {
            return;
        }
        if (_lastImpact.size() > 256) {
            _lastImpact.clear();
        }
        _lastImpact[walkerId] = now;
        Shared::Pedestrian::CarImpact impact;
        impact.missionGeneration = _world->LoadedMissionGeneration();
        impact.walkerId = walkerId;
        impact.carId = carId;
        impact.directionX = direction.x;
        impact.directionY = direction.y;
        impact.directionZ = direction.z;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(impact, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void PedestrianService::Reset() {
        gScreamMissionGeneration = 0;
        _reporter.Reset();
        _taxi.Reset();
        _previousObserverIds.clear();
        _reportedGenerators = false;
        _observers.clear();
        _selected.clear();
    }

    void PedestrianService::ReportGenerators(const std::vector<NativeTrafficGenerator *> &generators) {
        for (auto *generator : generators) {
            const auto position = generator->WorldPosition();
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info(
                "Pedestrian generator '{}' at ({:.0f}, {:.0f}, {:.0f}): {} selection, spawn radius {:.0f}, spawn {:.0f}-{:.0f}, despawn {:.0f}, {} walkers per cluster, pool {}",
                generator->Actor().Frame()->Name(), position.x, position.y, position.z, generator->SelectsByDistance() ? "nearest" : "radius",
                generator->SpawnRadius(), generator->_innerSpawnRadius, generator->_outerSpawnRadius, generator->DespawnRadius(), generator->MaxSpawned(), generator->PoolSize());
        }
    }

    void PedestrianService::Update(World::WorldService &world) {
        _world = &world;
        auto *replication = Framework::CoreModules::GetReplication();
        if (!Simulation::IsHost() || !world.IsReady() || !replication) {
            Reset();
            return;
        }

        _observers.clear();
        replication->ForEach<Shared::Entities::PlayerEntity>([&](Shared::Entities::PlayerEntity *player) {
            if (!player->spawned || player->missionGeneration != world.LoadedMissionGeneration()) {
                return;
            }
            glm::vec3 forward = glm::mat3_cast(glm::normalize(player->rotation)) * glm::vec3(0.0f, 0.0f, 1.0f);
            forward.y = 0.0f;
            forward = glm::length(forward) > 0.0001f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, 1.0f);
            _observers.push_back({player->GetNetworkID(), {player->position.x, player->position.y, player->position.z}, {forward.x, forward.y, forward.z}, glm::length(player->velocity)});
        });

        // Mission actor order, walked backwards like the retail selector, so
        // a tie resolves the same way it would for a single camera.
        std::vector<NativeTrafficGenerator *> generators;
        SDK::Core::Mission::Get()->ForEachActor([&](SDK::Player::NativeActor *actor) {
            if (static_cast<uint32_t>(actor->GetType()) == SDK::Traffic::kGeneratorActorType) {
                generators.push_back(reinterpret_cast<NativeTrafficGenerator *>(actor));
            }
        });
        if (!_reportedGenerators) {
            _reportedGenerators = true;
            ReportGenerators(generators);
        }

        std::unordered_map<NativeTrafficGenerator *, std::vector<uint32_t>> observersByGenerator;
        for (uint32_t index = 0; index < _observers.size(); ++index) {
            const auto &observer = _observers[index];
            NativeTrafficGenerator *selected = nullptr;
            float nearest = kUnlimitedDistance;
            bool forced = false;
            for (auto it = generators.rbegin(); it != generators.rend(); ++it) {
                const auto position = (*it)->WorldPosition();
                const float distance = glm::length(glm::vec3(position.x - observer.position.x, position.y - observer.position.y, position.z - observer.position.z));
                if ((*it)->SelectsByDistance()) {
                    if (!forced && distance < nearest) {
                        nearest = distance;
                        selected = *it;
                    }
                }
                else if (distance < (*it)->SpawnRadius()) {
                    selected = *it;
                    forced = true;
                }
            }
            if (selected) {
                observersByGenerator[selected].push_back(index);
            }
        }

        // Clusters persist through any player they keep, with their timers.
        std::unordered_map<uint64_t, Cluster> previousByObserver;
        for (auto &[generator, selection] : _selected) {
            for (const auto &cluster : selection.clusters) {
                for (const uint32_t member : cluster.observers) {
                    if (member < _previousObserverIds.size()) {
                        previousByObserver.emplace(_previousObserverIds[member], cluster);
                    }
                }
            }
        }
        auto previous = std::move(_selected);
        _selected.clear();
        for (auto &[generator, members] : observersByGenerator) {
            auto &selection = _selected[generator];
            if (const auto old = previous.find(generator); old != previous.end()) {
                selection.next = old->second.next;
            }
            // Walkers live within the despawn radius of their nearest
            // observer, so players more than two radii apart cannot share one.
            const float shareDistanceSq = 4.0f * generator->DespawnRadius() * generator->DespawnRadius();
            for (const uint32_t member : members) {
                auto cluster = std::find_if(selection.clusters.begin(), selection.clusters.end(), [&](const Cluster &candidate) {
                    return std::any_of(candidate.observers.begin(), candidate.observers.end(), [&](uint32_t other) {
                        return DistanceSq(_observers[member].position, _observers[other].position) < shareDistanceSq;
                    });
                });
                if (cluster == selection.clusters.end()) {
                    cluster = selection.clusters.insert(selection.clusters.end(), Cluster {});
                }
                cluster->observers.push_back(member);
            }
            for (auto &cluster : selection.clusters) {
                for (const uint32_t member : cluster.observers) {
                    if (const auto kept = previousByObserver.find(_observers[member].id); kept != previousByObserver.end()) {
                        cluster.spawnTimerMs = kept->second.spawnTimerMs;
                        break;
                    }
                }
            }
            const float despawnSq = generator->DespawnRadius() * generator->DespawnRadius();
            for (auto **element = SDK::Traffic::ElementsBegin(); element != SDK::Traffic::ElementsEnd(); ++element) {
                if (!(*element)->IsActive() || !generator->OwnsElement(*element)) {
                    continue;
                }
                Cluster *owner = nullptr;
                float nearest = despawnSq;
                for (auto &cluster : selection.clusters) {
                    for (const uint32_t member : cluster.observers) {
                        const float distanceSq = DistanceSq((*element)->Position(), _observers[member].position);
                        if (distanceSq < nearest) {
                            nearest = distanceSq;
                            owner = &cluster;
                        }
                    }
                }
                if (owner) {
                    ++owner->walkers;
                }
            }
            generator->Activate();
        }
        _previousObserverIds.clear();
        for (const auto &observer : _observers) {
            _previousObserverIds.push_back(observer.id);
        }
        gScreamMissionGeneration = world.LoadedMissionGeneration();
        _reporter.Update(world.LoadedMissionGeneration(), generators, std::chrono::milliseconds(_control.corpseLifetimeMs));
    }

    void PedestrianService::OnGeneratorAI(NativeTrafficGenerator *generator, uint32_t frameMs) {
        const auto it = _selected.find(generator);
        if (it == _selected.end() || !_control.enabled) {
            // Retail AI with generation off only retires an empty generator.
            generator->SetGenerationActive(false);
            gGeneratorAIOriginal(generator, frameMs);
            return;
        }
        // Retail AI once per cluster, each as if it were the only camera: the
        // cluster's own spawn timer, a maximum leaving room for exactly its
        // shortfall, and spawns placed from one of its players. The host's
        // screams ignore the scream cooldown the extra calls count down.
        // Retail's initial batch spawns from 5 units out, hidden by the
        // loading screen. A player who joins or respawns would watch it, so
        // clusters fill through regular spawns ahead of the camera.
        *reinterpret_cast<bool *>(SDK::Traffic::kInitialBatch) = false;
        const int32_t perCluster = generator->MaxSpawned();
        const auto target = static_cast<int32_t>(std::lround(static_cast<float>(perCluster) * _control.density));
        const uint32_t spawnTimer = generator->_spawnTimerMs;
        for (auto &cluster : it->second.clusters) {
            const int32_t before = generator->SpawnedCount();
            generator->_maxSpawned = std::min(generator->PoolSize(), before + std::max(0, target - cluster.walkers));
            generator->_spawnTimerMs = cluster.spawnTimerMs;
            generator->SetGenerationActive(true);
            const auto &observer = _observers[cluster.observers[it->second.next++ % cluster.observers.size()]];
            {
                ObserverView view(observer);
                gGeneratorAIOriginal(generator, frameMs);
            }
            cluster.spawnTimerMs = generator->_spawnTimerMs;
            cluster.walkers += generator->SpawnedCount() - before;
        }
        generator->_spawnTimerMs = spawnTimer;
        generator->_maxSpawned = perCluster;
    }

    void PedestrianService::OnTickElements(uint32_t frameMs) {
        // TickElements (0x44ef70) without its camera: each walker measures
        // the nearest observer, and counts as drawn so its skeleton and
        // fading never depend on what the host happens to render. Car theft
        // stays off; taxi hailing runs for every driver (TaxiHost).
        if (!SDK::Traffic::IsVisible() || !_world) {
            return;
        }
        _taxi.Prepare(*_world);
        SDK::Traffic::CountDownCollisionCooldown(frameMs);
        const uint32_t renderStamp = SDK::Core::Mission::Get()->RenderStamp();
        auto **begin = SDK::Traffic::ElementsBegin();
        auto count = static_cast<size_t>(SDK::Traffic::ElementsEnd() - begin);
        while (count) {
            auto *element = begin[--count];
            if (!element->IsActive()) {
                continue;
            }
            float nearest = kUnlimitedDistance;
            for (const auto &observer : _observers) {
                nearest = std::min(nearest, DistanceSq(observer.position, element->Position()));
            }
            element->SetObserverDistanceSq(nearest);
            // Tick itself tolerates an active walker without a model.
            if (element->Model()) {
                element->MarkDrawn(renderStamp);
            }
            const bool waving = _taxi.BeginTick(element);
            element->Tick(frameMs);
            if (waving) {
                _taxi.EndTick();
            }
            // Deactivation removes the walker and can reorder the vector.
            begin = SDK::Traffic::ElementsBegin();
        }
        _taxi.Finish(frameMs, [this](SDK::Traffic::NativeTrafficElement *element) { return _reporter.KeyOf(element); });
    }

    bool PedestrianService::WalkerPose(uint32_t key, SDK::Player::Vector3 &position, float &yaw) const {
        auto *element = key ? _reporter.ActiveElement(key) : nullptr;
        if (!element || element->IsDead() || !element->Model()) {
            return false;
        }
        position = element->Model()->WorldPosition();
        const auto direction = element->Model()->WorldDirection();
        yaw = std::atan2(direction.x, direction.z);
        return true;
    }

    void PedestrianService::HandOverWalker(uint32_t key) {
        auto *element = key ? _reporter.ActiveElement(key) : nullptr;
        if (!element) {
            return;
        }
        _taxi.Forget(element);
        // Its Retired goes out with the reporter's next pass; the NPC's
        // sourcePedestrianId has players swap it in where it stands.
        element->Deactivate();
    }

    PedestrianService::Stats PedestrianService::GetStats() const {
        Stats stats;
        stats.activeGenerators = static_cast<uint32_t>(_selected.size());
        stats.observers = static_cast<uint32_t>(_observers.size());
        for (const auto &[generator, selection] : _selected) {
            stats.clusters += static_cast<uint32_t>(selection.clusters.size());
        }
        for (auto **element = SDK::Traffic::ElementsBegin(); element != SDK::Traffic::ElementsEnd(); ++element) {
            stats.walkers += (*element)->IsActive() ? 1 : 0;
        }
        return stats;
    }

    const PedestrianService *GetService() {
        return gService;
    }

    PedestrianService::Stats GetHostStats() {
        return gService && Simulation::IsHost() ? gService->GetStats() : PedestrianService::Stats {};
    }
} // namespace Mafia1Online::Features::Pedestrian
