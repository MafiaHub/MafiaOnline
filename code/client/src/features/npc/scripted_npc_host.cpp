#include <utils/safe_win32.h>

#include "scripted_npc_host.h"

#include "npc_host.h"

#include "core/application.h"
#include "features/pedestrian/pedestrian_service.h"
#include "features/world/world_service.h"
#include "shared/features/car/car_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/game.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/player/native_entity_ai.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/player/native_human_factory.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Mafia1Online::Features::Npc {
    namespace {
        namespace Ai = SDK::Player::EntityAi;
        using SDK::Player::NativeActor;
        using Result = Shared::Npc::TaskDone::Result;
        using TaskType = Shared::Npc::TaskType;

        SDK::Player::NativeHuman &Human(NativeActor *actor) {
            return *static_cast<SDK::Player::NativeHuman *>(static_cast<void *>(actor));
        }

        SDK::Seat::NativeHuman &Seat(NativeActor *actor) {
            return *static_cast<SDK::Seat::NativeHuman *>(static_cast<void *>(actor));
        }

        bool Dead(NativeActor *actor) {
            return actor->IsDead() || Human(actor).Health() <= 0.0f;
        }

        float FlatDistanceSq(const SDK::Player::Vector3 &a, const SDK::Player::Vector3 &b) {
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            return dx * dx + dz * dz;
        }

        uint32_t BehaviourMask(Shared::Npc::Behaviour behaviour) {
            switch (behaviour) {
            case Shared::Npc::Behaviour::Friendly: return Ai::kBehaviourGuard;
            case Shared::Npc::Behaviour::Hostile: return Ai::kBehaviourMovesAway;
            case Shared::Npc::Behaviour::Neutral: break;
            }
            return Ai::kBehaviourReactsOnHit;
        }

        // Enemy_UseCar's work-state test (reM C_entity.cpp 3442): idle,
        // moving or seated.
        bool ReadyForCar(NativeActor *actor) {
            const int32_t work = Human(actor)._canWork;
            return work == 1 || work == 2 || work == 9;
        }

        // Retail enters below 0.1 and leaves below 0.5 (C_car::GetSpeed);
        // the host's reported pose of its own car stands in for it.
        bool NearlyStill(uint64_t carId) {
            constexpr float kStillSpeed = 0.5f;
            auto *replication = Framework::CoreModules::GetReplication();
            const auto *car = replication ? replication->GetEntity<Shared::Entities::CarEntity>(carId) : nullptr;
            return !car || glm::length(car->velocity) < kStillSpeed;
        }

        uint32_t GameTime() {
            return SDK::Core::Mission::Get()->Game()->GameTime();
        }

        void ChangeWeaponModel(NativeActor *actor) {
            reinterpret_cast<void(__thiscall *)(NativeActor *)>(SDK::Player::kChangeWeaponModel)(actor);
        }
    } // namespace

    void ScriptedNpcHost::RegisterRPC(World::WorldService &) {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        const auto queue = [this](Message message) {
            if (_queue.size() >= kMaxQueued) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Scripted NPC orders overflow; dropped one for NPC {}", message.key);
                return;
            }
            message.received = std::chrono::steady_clock::now();
            _queue.push_back(std::move(message));
        };
        network->RegisterRPC<Shared::Npc::Create>([queue](const Shared::Npc::Create &create, MafiaNet::Packet *) {
            if (!Shared::Npc::IsScriptedKey(create.key)) {
                return;
            }
            Message message;
            message.kind = MessageKind::Create;
            message.generation = create.missionGeneration;
            message.key = create.key;
            message.create = create;
            queue(std::move(message));
        });
        network->RegisterRPC<Shared::Npc::Task>([queue](const Shared::Npc::Task &task, MafiaNet::Packet *) {
            if (!Shared::Npc::IsScriptedKey(task.key) || !Shared::Npc::KnownTaskType(task.type)) {
                return;
            }
            Message message;
            message.kind = MessageKind::Task;
            message.generation = task.missionGeneration;
            message.key = task.key;
            message.task = task;
            queue(std::move(message));
        });
        network->RegisterRPC<Shared::Npc::Animation>([queue](const Shared::Npc::Animation &animation, MafiaNet::Packet *) {
            if (!Shared::Npc::IsScriptedKey(animation.key)) {
                return;
            }
            Message message;
            message.kind = MessageKind::Animation;
            message.generation = animation.missionGeneration;
            message.key = animation.key;
            message.animation = animation;
            queue(std::move(message));
        });
        network->RegisterRPC<Shared::Npc::Order>([queue](const Shared::Npc::Order &order, MafiaNet::Packet *) {
            if (!Shared::Npc::IsScriptedKey(order.key)) {
                return;
            }
            Message message;
            message.kind = MessageKind::Order;
            message.generation = order.missionGeneration;
            message.key = order.key;
            message.order = order;
            queue(std::move(message));
        });
    }

    void ScriptedNpcHost::RequestRemove(uint64_t generation, uint32_t key) {
        if (_queue.size() >= kMaxQueued) {
            return;
        }
        Message message;
        message.kind = MessageKind::Remove;
        message.generation = generation;
        message.key = key;
        message.received = std::chrono::steady_clock::now();
        _queue.push_back(std::move(message));
    }

    void ScriptedNpcHost::Reset() {
        _queue.clear();
        _running.clear();
        _animating.clear();
        _generation = 0;
    }

    void ScriptedNpcHost::Forget(uint32_t key) {
        _running.erase(key);
        _animating.erase(key);
    }

    void ScriptedNpcHost::Update(World::WorldService &world, uint64_t generation) {
        if (generation != _generation) {
            // The mission closed: its NPCs went with it.
            _running.clear();
            _animating.clear();
            _generation = generation;
        }
        const auto now = std::chrono::steady_clock::now();
        // In arrival order: a task never runs before its NPC's Create.
        while (!_queue.empty()) {
            auto &message = _queue.front();
            if (message.generation < generation || (message.generation != generation && now - message.received > kQueueWait)) {
                _queue.pop_front();
                continue;
            }
            if (message.generation != generation || !world.IsReady() || !Apply(world, generation, message)) {
                break;
            }
            _queue.pop_front();
        }
        if (!world.IsReady()) {
            return;
        }
        std::vector<uint32_t> keys;
        keys.reserve(_running.size());
        for (const auto &[key, running] : _running) {
            keys.push_back(key);
        }
        for (const uint32_t key : keys) {
            auto found = _running.find(key);
            if (found == _running.end()) {
                continue;
            }
            auto *npc = _host.FindByKey(key);
            if (!npc) {
                _running.erase(found);
                continue;
            }
            if (Tick(world, npc, found->second)) {
                _running.erase(key);
            }
        }
        // A clip with a displacement track plays once whatever its loop
        // flag (Shared::Npc::ScriptClip): a loop starts again where it ended.
        for (auto it = _animating.begin(); it != _animating.end();) {
            auto *npc = _host.FindByKey(it->first);
            if (!npc || Dead(npc)) {
                it = _animating.erase(it);
                continue;
            }
            auto &human = Human(npc);
            const int32_t work = human.Melee().canWork;
            // A reaction or move replaced the clip: the script's next
            // playAnimation starts it again instead of being kept.
            if (!Shared::Npc::ScriptClip::Holds(work)) {
                it = _animating.erase(it);
                continue;
            }
            if (it->second.NeedsReplay(true, work)) {
                human.PlayScriptAnimation(it->second.name.c_str(), true);
            }
            else if (!it->second.loop && !it->second.Running(work)) {
                // A finished one-shot leaves the NPC to its own idle.
                it = _animating.erase(it);
                continue;
            }
            ++it;
        }
    }

    bool ScriptedNpcHost::Apply(World::WorldService &world, uint64_t, Message &message) {
        switch (message.kind) {
        case MessageKind::Create: Create(world, message.create); return true;
        case MessageKind::Remove: Destroy(message.key); return true;
        case MessageKind::Task:
            if (auto *npc = _host.FindByKey(message.key)) {
                Begin(world, npc, message.task);
            }
            return true;
        case MessageKind::Order:
            if (auto *npc = _host.FindByKey(message.key)) {
                ApplyOrder(npc, message.order);
            }
            return true;
        case MessageKind::Animation:
            if (auto *npc = _host.FindByKey(message.key)) {
                ApplyAnimation(npc, message.animation);
            }
            return true;
        }
        return true;
    }

    void ScriptedNpcHost::Create(World::WorldService &, const Shared::Npc::Create &create) {
        const auto logger = Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT);
        if (_host.FindByKey(create.key)) {
            return;
        }
        const bool valid = !create.model.empty() && std::isfinite(create.x) && std::isfinite(create.y) && std::isfinite(create.z) && std::isfinite(create.yaw) &&
                           std::isfinite(create.health) && create.health > 0.0f && Shared::Npc::KnownBehaviour(create.behaviour);
        if (!valid || !SDK::Player::NativeHumanFactory::CanLoadHumanModel(create.model)) {
            logger->error("Scripted NPC {} could not use model '{}'", create.key, create.model);
            _host.ReportRetired(create.key);
            return;
        }
        const std::string name = "Mafia1OnlineScriptedNpc_" + std::to_string(create.key & ~Shared::Npc::kScriptedKeyBit);
        SDK::Player::Vector3 position {create.x, create.y, create.z};
        float yaw = create.yaw;
        // In place of a walker (a taxi passenger): where it stands on the
        // host now, as retail duplicates the walker's model where it waved
        // (MavaNaTaxi 0x452335-0x452c46). A walker gone meanwhile leaves no
        // one to make.
        auto &pedestrians = static_cast<Core::Application *>(Framework::CoreModules::GetClientInstance())->Pedestrians();
        if (create.walkerKey && !pedestrians.WalkerPose(create.walkerKey, position, yaw)) {
            logger->info("Scripted NPC {}: walker {} is gone", create.key, create.walkerKey);
            _host.ReportRetired(create.key);
            return;
        }
        const SDK::Player::Vector3 direction {std::sin(yaw), 0.0f, std::cos(yaw)};
        auto *npc = SDK::Player::NativeHumanFactory::CreateAiEntity(*SDK::Core::Mission::Get(), name.c_str(), create.model.c_str(), position, direction, [&](NativeActor *entity) {
            // As CreateMafianos sets its gangster up between Init and
            // AddTemporaryActor: behaviour mask, health, morale and
            // aggressivity (GameInit copies them into m_Properties).
            Ai::Field<uint32_t>(entity, Ai::kHumanBehaviour) = BehaviourMask(create.behaviour);
            Ai::Field<float>(entity, Ai::kInitialHealth) = create.health;
            Ai::Field<float>(entity, Ai::kInitialMorale) = 1.0f;
            Ai::Field<float>(entity, Ai::kInitialAggressivity) = 1.0f;
        });
        if (!npc) {
            logger->error("Scripted NPC {} could not be created with model '{}'", create.key, create.model);
            _host.ReportRetired(create.key);
            return;
        }
        if (create.weaponId != 0) {
            Shared::Npc::Order give;
            give.action = Shared::Npc::Order::Action::GiveWeapon;
            give.weaponId = create.weaponId;
            give.loaded = create.loaded;
            give.reserve = create.reserve;
            ApplyOrder(npc, give);
        }
        if (create.walkerKey) {
            pedestrians.HandOverWalker(create.walkerKey);
        }
        _host.Track(npc, Shared::Entities::NpcKind::Scripted, create.model, create.walkerKey, 0, create.key);
        logger->info("Scripted NPC {} spawned ({}, {})", create.key, create.model, Shared::Npc::BehaviourName(create.behaviour));
    }

    void ScriptedNpcHost::ApplyOrder(NativeActor *npc, const Shared::Npc::Order &order) {
        auto &human = Human(npc);
        switch (order.action) {
        case Shared::Npc::Order::Action::GiveWeapon:
            // PROGRAM_CMD_HUMAN_ADDWEAPON (reM C_program_process_000_132.inl).
            if (order.weaponId == 0 || order.weaponId >= Shared::Combat::kWeaponSlots) {
                return;
            }
            Ai::InsertItem(npc, order.weaponId, order.loaded, order.reserve);
            ChangeWeaponModel(npc);
            Ai::WeaponChoose(npc, human.SelectedWeapon());
            return;
        case Shared::Npc::Order::Action::SetHealth:
            if (!std::isfinite(order.health) || order.health <= 0.0f || Dead(npc)) {
                return;
            }
            // ai_logs::ComputeAttitude weighs fear by health over the
            // initial health.
            if (Ai::Field<float>(npc, Ai::kInitialHealth) < order.health) {
                Ai::Field<float>(npc, Ai::kInitialHealth) = order.health;
            }
            human.SetHealth(order.health);
            return;
        case Shared::Npc::Order::Action::Face: {
            // PROGRAM_CMD_ACTOR_SETDIR (reM C_program_process_000_132.inl
            // 2283): C_actor::SetDirection, I3D_frame::SetDir(direction, 0),
            // then the frame update. Only the yaw is given: a human stands
            // upright.
            if (!std::isfinite(order.heading) || Dead(npc)) {
                return;
            }
            const SDK::Player::Vector3 direction {std::sin(order.heading), 0.0f, std::cos(order.heading)};
            npc->SetStoredTransform(npc->Position(), direction);
            npc->Frame()->SetDirection(direction);
            npc->Frame()->Update();
            return;
        }
        }
    }

    void ScriptedNpcHost::ApplyAnimation(NativeActor *npc, const Shared::Npc::Animation &animation) {
        if (Dead(npc)) {
            return;
        }
        auto &human = Human(npc);
        if (animation.name.empty()) {
            if (_animating.erase(animation.key)) {
                human.StopAnimation();
            }
            return;
        }
        // A task would walk or turn it out of the clip at once; the server
        // already ended it (GiveTask clears the clip the same way).
        if (const auto running = _running.find(animation.key); running != _running.end()) {
            Halt(npc, running->second);
            Finish(animation.key, running->second, Result::Cancelled);
            _running.erase(running);
        }
        // A routine refreshing the loop it already plays keeps it running.
        if (const auto current = _animating.find(animation.key);
            current != _animating.end() && current->second.Continues(animation.name, animation.loop, human.Melee().canWork)) {
            return;
        }
        // As PERSON_PLAYANIM (reM C_program_process_000_132.inl 1967,
        // 0x470393): Do_PlayAnim(name, simple, 1). Without a displacement
        // track the clip plays on the animation stage and the human stays in
        // its idle work state, which C_entity's own Do_Breath leaves alone.
        human.PlayScriptAnimation(animation.name.c_str(), animation.loop);
        _animating[animation.key] = {animation.name, animation.loop, human.IsPlayingAnimation()};
    }

    void ScriptedNpcHost::Destroy(uint32_t key) {
        auto *npc = _host.FindByKey(key);
        if (!npc) {
            // Never created (or already gone): the server's record goes.
            _host.ReportRetired(key);
            return;
        }
        if (const auto running = _running.find(key); running != _running.end()) {
            Halt(npc, running->second);
            _running.erase(running);
        }
        // Retired first: the actor pointer is not used after the removal is
        // queued. C_game::InvalidateActor then clears every native reference
        // to it (actions, followships, car seats).
        _host.Release(npc);
        SDK::Core::Mission::Get()->Game()->RemoveTemporaryActor(npc);
    }

    void ScriptedNpcHost::Halt(NativeActor *npc, Running &running) {
        if (running.driving) {
            if (auto *car = Seat(npc).usedActorEnter; car && car->GetType() == NativeActor::Type::Car) {
                Ai::CarRemove(SDK::Core::Mission::Get()->Game(), car);
            }
            running.driving = false;
        }
        Ai::DelFollower(npc);
        Ai::ClearActions(npc);
        // A straight walk put its AI to sleep: awake, standing where it is.
        // An AI that woke up into a fight keeps it.
        if (running.task.type == TaskType::WalkDirect && Ai::Sleeping(npc)) {
            Human(npc).Breathe();
            Ai::ForceIdle(npc);
        }
    }

    NativeActor *ScriptedNpcHost::ResolveTarget(World::WorldService &world, const Shared::Npc::Task &task) const {
        if (task.targetKey) {
            return _host.FindByKey(task.targetKey);
        }
        auto *actor = static_cast<NativeActor *>(world.NativeObjects().Resolve(world.NativeObjects().FindByNetwork(task.targetId)));
        return actor && (actor->GetType() == NativeActor::Type::Entity || actor->GetType() == NativeActor::Type::Player) ? actor : nullptr;
    }

    NativeActor *ScriptedNpcHost::ResolveCar(World::WorldService &world, uint64_t carId) const {
        auto *actor = static_cast<NativeActor *>(world.NativeObjects().Resolve(world.NativeObjects().FindByNetwork(carId)));
        return actor && actor->GetType() == NativeActor::Type::Car ? actor : nullptr;
    }

    void ScriptedNpcHost::Finish(uint32_t key, const Running &running, Result result) {
        Shared::Npc::TaskDone done;
        done.missionGeneration = _generation;
        done.key = key;
        done.taskId = running.task.taskId;
        done.type = running.task.type;
        done.result = result;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(done, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Scripted NPC {} task {} ({}) {}", key, done.taskId, Shared::Npc::TaskName(done.type),
                                                                    Shared::Npc::TaskResultName(result));
    }

    void ScriptedNpcHost::Begin(World::WorldService &world, NativeActor *npc, const Shared::Npc::Task &task) {
        const uint32_t key = task.key;
        if (const auto previous = _running.find(key); previous != _running.end()) {
            Halt(npc, previous->second);
            Finish(key, previous->second, Result::Cancelled);
            _running.erase(previous);
        }
        Running running;
        running.task = task;
        running.phaseAt = std::chrono::steady_clock::now();
        if (Dead(npc)) {
            Finish(key, running, Result::Failed);
            return;
        }
        // A script clip (npc.playAnimation) ends with any task, as the
        // server cleared it for the players' copies.
        if (_animating.erase(key)) {
            Human(npc).StopAnimation();
        }
        auto &seat = Seat(npc);
        NativeActor *car = seat.usedActorEnter && seat.usedActorEnter->GetType() == NativeActor::Type::Car ? seat.usedActorEnter : nullptr;
        switch (task.type) {
        case TaskType::Stop:
            // Entity_ActionsClear.
            Ai::DelFollower(npc);
            Ai::ClearActions(npc);
            Finish(key, running, Result::Completed);
            return;
        case TaskType::ExitVehicle:
            if (!car && !seat.usedActorLeave) {
                Finish(key, running, Result::Completed);
                return;
            }
            Ai::DelFollower(npc);
            Ai::ClearActions(npc);
            running.phase = Phase::Leaving;
            break;
        case TaskType::DriveTo:
            // Entity_CarMoveTo drives only the car its entity sits in.
            if (!car || seat.seatId != 0) {
                Finish(key, running, Result::Failed);
                return;
            }
            Ai::DelFollower(npc);
            Ai::ClearActions(npc);
            running.task.targetId = world.NativeObjects().FindByNative(car).networkId;
            if (!Ai::CarMoveTo(SDK::Core::Mission::Get()->Game(), car, {task.x, task.y, task.z})) {
                Finish(key, running, Result::Failed);
                return;
            }
            running.driving = true;
            break;
        default:
            if (!Issue(world, npc, running)) {
                Finish(key, running, Result::Failed);
                return;
            }
            break;
        }
        _running[key] = running;
    }

    bool ScriptedNpcHost::Issue(World::WorldService &world, NativeActor *npc, Running &running) {
        auto &task = running.task;
        auto &human = Human(npc);
        running.issuedAt = std::chrono::steady_clock::now();
        switch (task.type) {
        case TaskType::Follow: {
            // Entity_ActionFollow (0x520c04-0x520cdc).
            auto *leader = ResolveTarget(world, task);
            if (!leader || leader == npc || Dead(leader)) {
                return false;
            }
            Ai::ClearActions(npc);
            human.SetAiming(false);
            auto *follow = Ai::NewFollow(npc, leader, task.distance, 0);
            auto *turn = Ai::NewTurnTo(npc, leader, follow->uid);
            Ai::Push(Ai::kAddJob, npc, follow);
            Ai::Push(Ai::kAddIdle, npc, turn);
            Ai::AddFollower(npc, leader);
            Ai::ForceIdle(npc);
            running.jobUid = follow->uid;
            return true;
        }
        case TaskType::WalkTo: {
            // EntityMoveToFrame (0x521f3d) with Entity_Move's run bit.
            Ai::ClearActions(npc);
            human.SetCrouching(false);
            human.SetAiming(false);
            auto *move = Ai::NewMove(npc, {task.x, task.y, task.z}, Ai::kMoveArrive | (task.run ? Ai::kMoveRun : 0u), task.distance);
            Ai::Push(Ai::kAddJob, npc, move);
            Ai::ForceIdle(npc);
            running.jobUid = move->uid;
            return true;
        }
        case TaskType::WalkDirect: {
            // Nothing to find a way over: Tick steps it towards the point
            // once a frame (AI_human_goto_target) while its AI sleeps, so
            // actionsTick does not stand it again (set_state SLEEP).
            if (Seat(npc).usedActorEnter) {
                return false;
            }
            Ai::DelFollower(npc);
            Ai::ClearActions(npc);
            human.SetCrouching(false);
            human.SetAiming(false);
            Ai::Sleep(npc);
            const auto position = SDK::Player::WorldPosition(npc);
            running.gameTime = GameTime();
            running.progressX = position.x;
            running.progressZ = position.z;
            running.phaseAt = running.issuedAt;
            return true;
        }
        case TaskType::EnterVehicle: {
            // Entity_MoveToCar (0x52063f).
            auto *car = ResolveCar(world, task.targetId);
            if (!car) {
                return false;
            }
            Ai::ClearActions(npc);
            auto *move = Ai::NewMoveToCar(npc, car, task.seat, task.run);
            Ai::Push(Ai::kAddJob, npc, move);
            Ai::ForceIdle(npc);
            running.jobUid = move->uid;
            return true;
        }
        case TaskType::Attack: {
            // Entity_ActionFire (0x5213a3-0x5214ae).
            auto *target = ResolveTarget(world, task);
            if (!target || target == npc) {
                return false;
            }
            Ai::WeaponChoose(npc);
            const float range = Ai::WeaponBestRange(npc);
            const uint32_t flags = Ai::FireFlags(npc);
            if (Seat(npc).usedActorEnter) {
                auto *fire = Ai::NewFire(npc, target, flags, 0);
                Ai::Push(Ai::kNewJob, npc, fire);
                running.jobUid = fire->uid;
            }
            else {
                auto *follow = Ai::NewFollow(npc, target, range, Ai::kFireFollowState);
                auto *turn = Ai::NewTurnTo(npc, target, follow->uid);
                auto *fire = Ai::NewFire(npc, target, flags, follow->uid);
                Ai::Push(Ai::kNewJob, npc, follow);
                Ai::Push(Ai::kNewIdle, npc, turn);
                Ai::Push(Ai::kNewModifier, npc, fire);
                Ai::Push(Ai::kAddModifier, npc, Ai::NewCarJump(npc, 1, follow->uid));
                running.jobUid = follow->uid;
            }
            Ai::ForceIdle(npc);
            return true;
        }
        default: return false;
        }
    }

    bool ScriptedNpcHost::Tick(World::WorldService &world, NativeActor *npc, Running &running) {
        const uint32_t key = running.task.key;
        const auto now = std::chrono::steady_clock::now();
        if (Dead(npc)) {
            // C_entity::Death already cleared its actions and followship.
            if (running.driving) {
                Halt(npc, running);
            }
            Finish(key, running, Result::Failed);
            return true;
        }
        const auto end = [&](Result result) {
            Halt(npc, running);
            Finish(key, running, result);
            return true;
        };
        // A job retail still holds but cannot run (ForceAI refused while the
        // process interval was high, or the AI left the full substate for a
        // reason that let it keep the job) gets the idle substate again once
        // the entity is back in its idle state.
        const auto keepRunning = [&] {
            if (Ai::Idle(npc) && !Ai::RunsActions(npc)) {
                Ai::ForceIdle(npc);
            }
            return false;
        };
        // The job is gone without the goal reached: a fight replaced it or
        // the path failed. Issue it again once the AI is idle.
        const auto retry = [&](bool limited) {
            if (!Ai::Idle(npc) || now - running.issuedAt < kRetryInterval) {
                return false;
            }
            if (limited && ++running.retries > kMaxRetries) {
                return end(Result::Failed);
            }
            return Issue(world, npc, running) ? false : end(Result::Failed);
        };
        auto &seat = Seat(npc);
        switch (running.task.type) {
        case TaskType::Follow: {
            auto *leader = ResolveTarget(world, running.task);
            if (!leader || Dead(leader)) {
                return end(Result::Failed);
            }
            return Ai::HasJob(npc, running.jobUid) ? keepRunning() : retry(false);
        }
        case TaskType::WalkTo: {
            const SDK::Player::Vector3 target {running.task.x, running.task.y, running.task.z};
            const auto position = SDK::Player::WorldPosition(npc);
            const float reach = std::max(running.task.distance, 0.3f) + 0.5f;
            if (Ai::HasJob(npc, running.jobUid)) {
                return keepRunning();
            }
            if (FlatDistanceSq(position, target) <= reach * reach && std::abs(position.y - target.y) < 2.0f) {
                Finish(key, running, Result::Completed);
                return true;
            }
            return retry(true);
        }
        case TaskType::WalkDirect: {
            const SDK::Player::Vector3 target {running.task.x, running.task.y, running.task.z};
            const auto position = SDK::Player::WorldPosition(npc);
            const float reach = std::max(running.task.distance, 0.3f);
            const uint32_t gameTime = GameTime();
            const uint32_t frameTime = std::min(gameTime - running.gameTime, kDirectMaxStepMs);
            running.gameTime = gameTime;
            if (seat.usedActorEnter) {
                return end(Result::Failed);
            }
            if (FlatDistanceSq(position, target) <= reach * reach && std::abs(position.y - target.y) < 2.0f) {
                return end(Result::Completed);
            }
            const auto held = [&] {
                running.progressX = position.x;
                running.progressZ = position.z;
                running.phaseAt = now;
                return false;
            };
            // A fight or a fright took the AI over; the walk goes on once it
            // is idle again.
            if (!Ai::Idle(npc)) {
                return held();
            }
            if (!Ai::Sleeping(npc)) {
                Ai::Sleep(npc);
            }
            // A hit reaction owns the human until it ends (actionsTick moves
            // its entity only in the same work states, reM C_entity.cpp
            // 19057).
            if (!ReadyForCar(npc)) {
                return held();
            }
            if (frameTime == 0) {
                return false;
            }
            // As C_entity::scriptTick (0x5236a6) steps along its script
            // path: false once it stands on the point.
            if (!Ai::GotoTarget(npc, {target.x, target.y, target.z}, frameTime, running.task.run)) {
                return end(Result::Completed);
            }
            const float dx = position.x - running.progressX;
            const float dz = position.z - running.progressZ;
            if (dx * dx + dz * dz >= kDirectProgress * kDirectProgress) {
                return held();
            }
            return now - running.phaseAt > kDirectStall ? end(Result::Failed) : false;
        }
        case TaskType::EnterVehicle: {
            auto *car = ResolveCar(world, running.task.targetId);
            if (!car) {
                return end(Result::Failed);
            }
            if (seat.usedActorEnter == car) {
                Ai::ClearActions(npc);
                Finish(key, running, Result::Completed);
                return true;
            }
            if (running.phase == Phase::Boarding) {
                // Use_Actor's door animation holds the car in +0x9c.
                return now - running.phaseAt > kSeatTimeout ? end(Result::Failed) : false;
            }
            if (Ai::HasJob(npc, running.jobUid)) {
                return keepRunning();
            }
            if (!Ai::Idle(npc)) {
                return false;
            }
            auto *owner = static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(car))->GetOwner(running.task.seat);
            if (owner && owner != npc) {
                return end(Result::Failed);
            }
            const auto position = SDK::Player::WorldPosition(npc);
            const auto carPosition = SDK::Player::WorldPosition(car);
            const float dy = position.y - carPosition.y;
            if (FlatDistanceSq(position, carPosition) + dy * dy < kEnterReachSq) {
                // Enemy_UseCar (reM C_entity.cpp 3431-3480).
                if (!ReadyForCar(npc) || !NearlyStill(running.task.targetId)) {
                    return false;
                }
                seat.UseCar(static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(car)), SDK::Seat::UseAction::Enter, running.task.seat);
                running.phase = Phase::Boarding;
                running.phaseAt = now;
                return false;
            }
            return retry(true);
        }
        case TaskType::ExitVehicle: {
            NativeActor *car = seat.usedActorEnter && seat.usedActorEnter->GetType() == NativeActor::Type::Car ? seat.usedActorEnter : nullptr;
            if (!car && !seat.usedActorLeave) {
                Finish(key, running, Result::Completed);
                return true;
            }
            if (now - running.phaseAt > kSeatTimeout) {
                return end(Result::Failed);
            }
            // Enemy_UseCar with no car: leave while seated and slower than
            // 0.5 (reM C_entity.cpp 3439-3451). The exit animation keeps the
            // car in +0x9c until it ends.
            if (car && !seat.usedActorLeave && running.retries == 0 && ReadyForCar(npc) && NearlyStill(world.NativeObjects().FindByNative(car).networkId)) {
                seat.UseCar(static_cast<SDK::Seat::NativeCar *>(static_cast<void *>(car)), SDK::Seat::UseAction::Exit, 0);
                running.retries = 1;
            }
            return false;
        }
        case TaskType::DriveTo: {
            auto *car = ResolveCar(world, running.task.targetId);
            if (!car) {
                // C_game::InvalidateActor dropped the route with the car.
                running.driving = false;
                return end(Result::Failed);
            }
            if (seat.usedActorEnter != car) {
                return end(Result::Failed);
            }
            const uint32_t status = Ai::CarStatus(SDK::Core::Mission::Get()->Game(), car);
            if (status == Ai::kCarActive) {
                return false;
            }
            // Entity_CarMoveTo: arrived (1), failed (2), blocked (3).
            return end(status == Ai::kCarArrived ? Result::Completed : Result::Failed);
        }
        case TaskType::Attack: {
            auto *target = ResolveTarget(world, running.task);
            if (!target) {
                return end(Result::Failed);
            }
            if (Dead(target)) {
                return end(Result::Completed);
            }
            return Ai::HasJob(npc, running.jobUid) ? keepRunning() : retry(false);
        }
        case TaskType::Stop: return true;
        }
        return true;
    }
} // namespace Mafia1Online::Features::Npc
