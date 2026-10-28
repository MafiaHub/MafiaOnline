#include "npc_scripting.h"

#include "core/server.h"
#include "features/combat/weapon_scripting.h"
#include "features/script/script_runtime.h"
#include "shared/features/car/car_entity.h"
#include "shared/scripting_catalog.h"

#include <v8pp/convert.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <sstream>

namespace Mafia1Online::Scripting {
    std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Npc>>> Npc::_classes;

    namespace {
        void List(const v8::FunctionCallbackInfo<v8::Value> &info, const std::function<bool(const Shared::Entities::NpcEntity &)> &include) {
            auto *isolate  = info.GetIsolate();
            auto context   = isolate->GetCurrentContext();
            auto list      = v8::Array::New(isolate);
            uint32_t index = 0;
            GetServer().Npcs().ForEach([&](Shared::Entities::NpcEntity &npc) {
                if (include(npc)) {
                    list->Set(context, index++, WrapNpc(isolate, npc.GetNetworkID())).Check();
                }
            });
            info.GetReturnValue().Set(list);
        }

        void JS_NpcGetAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            List(info, [](const Shared::Entities::NpcEntity &) { return true; });
        }

        // Npc.getInRange(position, radius)
        void JS_NpcGetInRange(const v8::FunctionCallbackInfo<v8::Value> &info) {
            glm::vec3 center;
            float radius = 0.0f;
            int next = 0;
            if (!ScriptArgs::ReadPosition(info, 0, center, next) || info.Length() != next + 1 || !ScriptArgs::ReadFloat(info[next], radius) || radius < 0.0f) {
                ScriptArgs::Throw(info.GetIsolate(), "Npc.getInRange(position, radius) expects a finite position and a radius of 0 or more");
                return;
            }
            const float radiusSq = radius * radius;
            List(info, [&](const Shared::Entities::NpcEntity &npc) {
                const auto offset = npc.position - center;
                return offset.x * offset.x + offset.y * offset.y + offset.z * offset.z <= radiusSq;
            });
        }

        void JS_NpcGetCount(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<uint32_t>(GetServer().Npcs().Count()));
        }

        void JS_NpcGetInventory(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Npc>::unwrap_object(info.GetIsolate(), info.This());
            info.GetReturnValue().Set(self ? self->GetInventory(info.GetIsolate()) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        // The car a stolen traffic car's driver was thrown out of, if it is
        // still a vehicle of this mission.
        void JS_NpcGetThrownFromVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self = v8pp::class_<Npc>::unwrap_object(isolate, info.This());
            const auto *npc = self ? self->ResolveNpc() : nullptr;
            const uint64_t carId = npc ? npc->thrownFromCarId : 0;
            info.GetReturnValue().Set(carId && GetServer().Cars().Find(carId) ? WrapVehicle(isolate, carId) : v8::Null(isolate).As<v8::Value>());
        }

        // ---- Scripted NPCs (Npc.spawn) -------------------------------------

        Npc *SelfNpc(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Npc>::unwrap_object(info.GetIsolate(), info.This());
        }

        // options.name, or an empty handle when options is not an object or
        // the field is undefined.
        v8::Local<v8::Value> Option(v8::Isolate *isolate, v8::Local<v8::Value> options, const char *name) {
            v8::Local<v8::Value> value;
            if (options.IsEmpty() || !options->IsObject() ||
                !options.As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, name)).ToLocal(&value) || value->IsUndefined()) {
                return {};
            }
            return value;
        }

        bool ReadOptionalBool(v8::Isolate *isolate, v8::Local<v8::Value> options, const char *name, bool &out) {
            const auto value = Option(isolate, options, name);
            if (value.IsEmpty()) {
                return true;
            }
            if (!value->IsBoolean()) {
                return false;
            }
            out = value->BooleanValue(isolate);
            return true;
        }

        bool ReadOptionalFloat(v8::Isolate *isolate, v8::Local<v8::Value> options, const char *name, float &out) {
            const auto value = Option(isolate, options, name);
            return value.IsEmpty() || ScriptArgs::ReadFloat(value, out);
        }

        bool OptionsOrNothing(v8::Local<v8::Value> value) {
            return value->IsUndefined() || value->IsObject();
        }

        // Npc.spawn(model, position, heading?, options?)
        void JS_NpcSpawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            constexpr const char *kUsage =
                "Npc.spawn(model, position, heading?, options?) expects an .i3d model, a finite position, a heading in radians and "
                "{ health?, behaviour?: \"neutral\" | \"friendly\" | \"hostile\", weapon?, ammo?, reserve?, virtualWorld? }";
            Features::Npc::NpcService::SpawnRequest request;
            int next = 0;
            if (info.Length() < 2 || !info[0]->IsString() || !ScriptArgs::ReadPosition(info, 1, request.position, next) || info.Length() > next + 2) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            request.model = v8pp::from_v8<std::string>(isolate, info[0]);
            if (info.Length() > next && !info[next]->IsUndefined() && !ScriptArgs::ReadFloat(info[next], request.heading)) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            const v8::Local<v8::Value> options = info.Length() > next + 1 ? info[next + 1] : v8::Undefined(isolate).As<v8::Value>();
            if (!OptionsOrNothing(options) || !ReadOptionalFloat(isolate, options, "health", request.health) || !(request.health > 0.0f)) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            if (const auto behaviour = Option(isolate, options, "behaviour"); !behaviour.IsEmpty()) {
                const std::string name = behaviour->IsString() ? v8pp::from_v8<std::string>(isolate, behaviour) : std::string {};
                if (name == "neutral") {
                    request.behaviour = Shared::Npc::Behaviour::Neutral;
                }
                else if (name == "friendly") {
                    request.behaviour = Shared::Npc::Behaviour::Friendly;
                }
                else if (name == "hostile") {
                    request.behaviour = Shared::Npc::Behaviour::Hostile;
                }
                else {
                    ScriptArgs::Throw(isolate, kUsage);
                    return;
                }
            }
            uint32_t weapon = 0, ammo = 0, reserve = 0, world = 0;
            const auto weaponValue = Option(isolate, options, "weapon");
            const auto ammoValue = Option(isolate, options, "ammo");
            const auto reserveValue = Option(isolate, options, "reserve");
            const auto worldValue = Option(isolate, options, "virtualWorld");
            if ((!weaponValue.IsEmpty() && (!ScriptArgs::ReadUInt(weaponValue, Shared::Combat::kWeaponSlots - 1, weapon) || !Weapon::IsSupported(weapon))) ||
                (!ammoValue.IsEmpty() && !ScriptArgs::ReadUInt(ammoValue, std::numeric_limits<uint16_t>::max(), ammo)) ||
                (!reserveValue.IsEmpty() && !ScriptArgs::ReadUInt(reserveValue, std::numeric_limits<uint16_t>::max(), reserve)) ||
                (!worldValue.IsEmpty() && !ScriptArgs::ReadUInt(worldValue, std::numeric_limits<uint32_t>::max(), world))) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            request.weaponId = static_cast<uint8_t>(weapon);
            request.loaded = weapon && ammoValue.IsEmpty() ? Weapon::DefaultLoaded(static_cast<uint8_t>(weapon)) : static_cast<uint16_t>(ammo);
            request.reserve = static_cast<uint16_t>(reserve);
            if (!worldValue.IsEmpty()) {
                request.virtualWorld = world;
            }
            auto *npc = GetServer().Npcs().Spawn(request);
            info.GetReturnValue().Set(npc ? WrapNpc(isolate, npc->GetNetworkID()) : v8::Null(isolate).As<v8::Value>());
        }

        void ReturnTask(const v8::FunctionCallbackInfo<v8::Value> &info, Npc *self, const Shared::Npc::Task &task) {
            info.GetReturnValue().Set(self ? GetServer().Npcs().GiveTask(self->GetId(), task) : 0u);
        }

        // A Player or Npc handle.
        uint64_t ReadHuman(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index) {
                return 0;
            }
            const uint64_t id = ScriptArgs::ReadEntityId(info.GetIsolate(), info[index]);
            return id && (GetServer().Players().FindByNetworkId(id) || GetServer().Npcs().Find(id)) ? id : 0;
        }

        // npc.follow(target, distance?)
        void JS_NpcFollow(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Shared::Npc::Task task;
            task.type = Shared::Npc::TaskType::Follow;
            task.distance = 2.0f;
            task.targetId = ReadHuman(info, 0);
            if (!task.targetId || info.Length() > 2 || (info.Length() == 2 && !info[1]->IsUndefined() && (!ScriptArgs::ReadFloat(info[1], task.distance) || task.distance < 0.5f || task.distance > 50.0f))) {
                ScriptArgs::Throw(info.GetIsolate(), "Npc.follow(target, distance?) expects a Player or Npc and a distance from 0.5 to 50");
                return;
            }
            ReturnTask(info, SelfNpc(info), task);
        }

        // npc.walkTo(position, options?)
        void JS_NpcWalkTo(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            constexpr const char *kUsage = "Npc.walkTo(position, options?) expects a finite position and { run?: boolean, radius?: number from 0.3 to 20 }";
            Shared::Npc::Task task;
            task.type = Shared::Npc::TaskType::WalkTo;
            task.distance = 0.5f;
            glm::vec3 position;
            int next = 0;
            if (!ScriptArgs::ReadPosition(info, 0, position, next) || info.Length() > next + 1) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            const v8::Local<v8::Value> options = info.Length() > next ? info[next] : v8::Undefined(isolate).As<v8::Value>();
            if (!OptionsOrNothing(options) || !ReadOptionalBool(isolate, options, "run", task.run) || !ReadOptionalFloat(isolate, options, "radius", task.distance) ||
                task.distance < 0.3f || task.distance > 20.0f) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            task.x = position.x;
            task.y = position.y;
            task.z = position.z;
            ReturnTask(info, SelfNpc(info), task);
        }

        // npc.enterVehicle(vehicle, seat?, options?)
        void JS_NpcEnterVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            constexpr const char *kUsage = "Npc.enterVehicle(vehicle, seat?, options?) expects a Vehicle, a seat index and { run?: boolean }";
            Shared::Npc::Task task;
            task.type = Shared::Npc::TaskType::EnterVehicle;
            uint32_t seat = 0;
            task.targetId = info.Length() >= 1 ? ScriptArgs::ReadEntityId(isolate, info[0]) : 0;
            const auto *car = task.targetId ? GetServer().Cars().Find(task.targetId) : nullptr;
            if (!car || info.Length() > 3 || (info.Length() >= 2 && !info[1]->IsUndefined() && !ScriptArgs::ReadUInt(info[1], Shared::Entities::CarEntity::kMaxSeats - 1, seat)) ||
                (info.Length() == 3 && (!OptionsOrNothing(info[2]) || !ReadOptionalBool(isolate, info[2], "run", task.run)))) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            if (seat >= car->seatCount) {
                info.GetReturnValue().Set(0u);
                return;
            }
            task.seat = static_cast<uint8_t>(seat);
            ReturnTask(info, SelfNpc(info), task);
        }

        void JS_NpcExitVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Shared::Npc::Task task;
            task.type = Shared::Npc::TaskType::ExitVehicle;
            ReturnTask(info, SelfNpc(info), task);
        }

        // npc.driveTo(position)
        void JS_NpcDriveTo(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Shared::Npc::Task task;
            task.type = Shared::Npc::TaskType::DriveTo;
            glm::vec3 position;
            int next = 0;
            if (!ScriptArgs::ReadPosition(info, 0, position, next) || info.Length() != next) {
                ScriptArgs::Throw(info.GetIsolate(), "Npc.driveTo(position) expects a finite position");
                return;
            }
            task.x = position.x;
            task.y = position.y;
            task.z = position.z;
            ReturnTask(info, SelfNpc(info), task);
        }

        // npc.attack(target)
        void JS_NpcAttack(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Shared::Npc::Task task;
            task.type = Shared::Npc::TaskType::Attack;
            task.targetId = ReadHuman(info, 0);
            if (!task.targetId || info.Length() != 1) {
                ScriptArgs::Throw(info.GetIsolate(), "Npc.attack(target) expects a Player or Npc");
                return;
            }
            ReturnTask(info, SelfNpc(info), task);
        }

        void JS_NpcStop(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Shared::Npc::Task task;
            task.type = Shared::Npc::TaskType::Stop;
            ReturnTask(info, SelfNpc(info), task);
        }

        // npc.face(heading | position)
        void JS_NpcFace(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self = SelfNpc(info);
            Shared::Npc::Order order;
            order.action = Shared::Npc::Order::Action::Face;
            glm::vec3 point;
            int next = 0;
            if (info.Length() == 1 && info[0]->IsNumber()) {
                if (!ScriptArgs::ReadFloat(info[0], order.heading)) {
                    ScriptArgs::Throw(isolate, "Npc.face(target) expects a heading in radians or a position");
                    return;
                }
            }
            else if (ScriptArgs::ReadPosition(info, 0, point, next) && info.Length() == next) {
                const auto *npc = self ? self->ResolveNpc() : nullptr;
                if (!npc) {
                    info.GetReturnValue().Set(false);
                    return;
                }
                const glm::vec3 offset = point - npc->position;
                if (offset.x * offset.x + offset.z * offset.z < 1.0e-6f) {
                    info.GetReturnValue().Set(false);
                    return;
                }
                order.heading = std::atan2(offset.x, offset.z);
            }
            else {
                ScriptArgs::Throw(isolate, "Npc.face(target) expects a heading in radians or a position");
                return;
            }
            info.GetReturnValue().Set(self && GetServer().Npcs().SendOrder(self->GetId(), order));
        }

        // npc.giveWeapon(weaponId, ammo?, reserve?)
        void JS_NpcGiveWeapon(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfNpc(info);
            uint32_t weaponId = 0, loaded = 0, reserve = 0;
            const bool known = info.Length() >= 1 && ScriptArgs::ReadUInt(info[0], Shared::Combat::kWeaponSlots - 1, weaponId) && Weapon::IsSupported(weaponId);
            if (!known || info.Length() > 3 || (info.Length() >= 2 && !ScriptArgs::ReadUInt(info[1], std::numeric_limits<uint16_t>::max(), loaded)) ||
                (info.Length() == 3 && !ScriptArgs::ReadUInt(info[2], std::numeric_limits<uint16_t>::max(), reserve))) {
                ScriptArgs::Throw(info.GetIsolate(), "Npc.giveWeapon(weaponId, ammo?, reserve?) expects a supported weapon ID and ammunition from 0 to 65535");
                return;
            }
            Shared::Npc::Order order;
            order.action = Shared::Npc::Order::Action::GiveWeapon;
            order.weaponId = static_cast<uint8_t>(weaponId);
            order.loaded = info.Length() < 2 ? Weapon::DefaultLoaded(order.weaponId) : static_cast<uint16_t>(loaded);
            order.reserve = static_cast<uint16_t>(reserve);
            info.GetReturnValue().Set(self && GetServer().Npcs().SendOrder(self->GetId(), order));
        }

        // npc.setHealth(health)
        void JS_NpcSetHealth(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfNpc(info);
            Shared::Npc::Order order;
            order.action = Shared::Npc::Order::Action::SetHealth;
            if (info.Length() != 1 || !ScriptArgs::ReadFloat(info[0], order.health) || order.health <= 0.0f || order.health > 100000.0f) {
                ScriptArgs::Throw(info.GetIsolate(), "Npc.setHealth(health) expects a health above 0 and up to 100000");
                return;
            }
            info.GetReturnValue().Set(self && GetServer().Npcs().SendOrder(self->GetId(), order));
        }

        void JS_NpcGetTask(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self = SelfNpc(info);
            const auto task = self ? GetServer().Npcs().GetTask(self->GetId()) : std::nullopt;
            if (!task) {
                info.GetReturnValue().SetNull();
                return;
            }
            auto context = isolate->GetCurrentContext();
            auto object = v8::Object::New(isolate);
            SetField(isolate, context, object, "id", v8::Uint32::New(isolate, task->id));
            SetField(isolate, context, object, "task", v8pp::to_v8(isolate, Shared::Npc::TaskName(task->type)));
            info.GetReturnValue().Set(object);
        }

        // The car the NPC sits in, as the server records its seats.
        void JS_NpcGetVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self = SelfNpc(info);
            const auto seat = self ? GetServer().Cars().SeatForPlayer(self->GetId(), Shared::Entities::NpcEntity::kSpawnGeneration) : std::nullopt;
            info.GetReturnValue().Set(seat ? WrapVehicle(isolate, seat->carId) : v8::Null(isolate).As<v8::Value>());
        }
    } // namespace

    Shared::Entities::NpcEntity *Npc::ResolveNpc() const {
        return GetServer().Npcs().Find(_id);
    }

    std::string Npc::GetKind() const {
        const auto *npc = ResolveNpc();
        return npc ? Shared::Entities::NpcKindName(npc->kind) : std::string {};
    }

    std::string Npc::GetModel() const {
        const auto *npc = ResolveNpc();
        return npc ? npc->model : std::string {};
    }

    Framework::Scripting::Builtins::Vector3 Npc::GetNpcPosition() const {
        const auto *npc = ResolveNpc();
        return npc ? Framework::Scripting::Builtins::Vector3(npc->position) : Framework::Scripting::Builtins::Vector3();
    }

    double Npc::GetHeading() const {
        const auto *npc = ResolveNpc();
        // The host reports a rotation about the Y axis only.
        return npc ? 2.0 * std::atan2(npc->rotation.y, npc->rotation.w) : 0.0;
    }

    bool Npc::GetAlive() const {
        const auto *npc = ResolveNpc();
        return npc && npc->alive;
    }

    double Npc::GetHealth() const {
        const auto *state = GetServer().Npcs().GetState(_id);
        return state ? state->health : 0.0;
    }

    uint32_t Npc::GetWeapon() const {
        const auto *state = GetServer().Npcs().GetState(_id);
        return state ? state->selectedWeapon : 0;
    }

    v8::Local<v8::Value> Npc::GetInventory(v8::Isolate *isolate) const {
        const auto *state = GetServer().Npcs().GetState(_id);
        if (!state) {
            return v8::Null(isolate);
        }
        return Weapon::Inventory(isolate, isolate->GetCurrentContext(), *state);
    }

    std::string Npc::GetBehaviour() const {
        const auto behaviour = GetServer().Npcs().GetBehaviour(_id);
        return behaviour ? Shared::Npc::BehaviourName(*behaviour) : std::string {};
    }

    bool Npc::Remove() {
        return GetServer().Npcs().Remove(_id);
    }

    std::string Npc::ToString() const {
        std::ostringstream stream;
        stream << "Npc{ id: " << _id << ", kind: " << GetKind() << ", model: " << GetModel() << " }";
        return stream.str();
    }

    void Npc::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    v8pp::class_<Npc> &Npc::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (_class) {
            return *_class;
        }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Npc>>(isolate, ServerCatalog(), "Npc",
            "A human the simulation host runs with the retail AI: a gangster pedestrian someone hit that draws a Thompson and fights back, a policeman, a thrown-out driver, or one a script spawned and gives tasks.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing NPC."));
        cls.function("toString", &Npc::ToString, docs("string", {}, "Formats this NPC for logging.", "The NPC ID, kind and model."));
        cls.property("kind", &Npc::GetKind,
            property_docs("string", "What it is: \"gangster\", \"police\", \"driver\" (a traffic driver thrown out of its car) or \"scripted\" (Npc.spawn)."));
        cls.property("model", &Npc::GetModel, property_docs("string", "Retail .i3d model filename."));
        cls.property("position", &Npc::GetNpcPosition, property_docs("Vector3", "Position the simulation host last reported."));
        cls.property("heading", &Npc::GetHeading, property_docs("number", "Facing in radians."));
        cls.property("alive", &Npc::GetAlive, property_docs("boolean", "False once it died; the corpse stays until it retires."));
        cls.property("health", &Npc::GetHealth, property_docs("number", "Health the simulation host last reported; a gangster starts at 200."));
        cls.property("weapon", &Npc::GetWeapon, property_docs("number", "Retail item id of the weapon in hand, 0 for none; 10 is the Thompson."));
        cls.prototype_function("getInventory", &JS_NpcGetInventory,
            docs("Inventory | null", {}, "Returns the selected weapon and the held weapons the server records for it.", "The inventory, or null when it no longer exists."));
        cls.prototype_function("getThrownFromVehicle", &JS_NpcGetThrownFromVehicle,
            docs("Vehicle | null", {}, "Returns the car a driver NPC was thrown out of when a player stole it.",
                "The vehicle, or null for another kind of NPC or once that car is gone."));
        cls.function("remove", &Npc::Remove, docs("boolean", {}, "Asks the simulation host to retire it now; npcRetire follows.", "False when it no longer exists or no host is attached."));
        cls.property("behaviour", &Npc::GetBehaviour,
            property_docs("string", "A scripted NPC's spawn behaviour, \"neutral\", \"friendly\" or \"hostile\"; an empty string for another kind."));
        const auto task = [](const char *what) {
            return std::string(what) + " The task replaces the current one; npcTaskComplete reports how it ends. Scripted NPCs only.";
        };
        const char *taskReturns = "The task id, or 0 when refused (not a living scripted NPC, no host, a target that is gone).";
        cls.prototype_function("follow", &JS_NpcFollow,
            docs("number", {param("target", "Player | Npc", false, "Who to follow."), param("distance", "number", true, "How close it keeps, 0.5 to 50; defaults to 2.")},
                task("Follows the target the way retail companions follow Tommy (Entity_ActionFollow), until another task or the target is gone. It runs to catch up and resumes after a fight."),
                taskReturns));
        cls.prototype_function("walkTo", &JS_NpcWalkTo,
            docs("number", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Where to go; three numbers are accepted too."),
                               param("options", "{ run?: boolean; radius?: number }", true, "run: run instead of walking; radius: how close it must come, 0.3 to 20 (default 0.5).")},
                task("Walks over the mission's pedestrian paths to the point (EntityMoveToFrame)."), taskReturns));
        cls.prototype_function("enterVehicle", &JS_NpcEnterVehicle,
            docs("number", {param("vehicle", "Vehicle", false, "A script's vehicle with no player in it; not a traffic car."), param("seat", "number", true, "Seat index; 0, the driver's, by default."),
                               param("options", "{ run?: boolean }", true, "run: run to the car.")},
                task("Walks to the car's door and gets in (Entity_MoveToCar, Enemy_UseCar). The simulation host drives the car while a scripted NPC boards or sits in it, and players cannot get in meanwhile."),
                taskReturns));
        cls.prototype_function("exitVehicle", &JS_NpcExitVehicle, docs("number", {}, task("Gets out of its car once the car is nearly still (Enemy_UseCar)."), taskReturns));
        cls.prototype_function("driveTo", &JS_NpcDriveTo,
            docs("number", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Destination; three numbers are accepted too.")},
                task("Drives the car it sits in as driver over the road network to the point and stops there (Entity_CarMoveTo)."), taskReturns));
        cls.prototype_function("attack", &JS_NpcAttack,
            docs("number", {param("target", "Player | Npc", false, "Who to fight.")},
                task("Chases and shoots the target with its best weapon (Entity_ActionFire) until the target is dead (completed) or gone."), taskReturns));
        cls.prototype_function("stop", &JS_NpcStop, docs("number", {}, task("Drops every action and stands (Entity_ActionsClear)."), taskReturns));
        cls.prototype_function("face", &JS_NpcFace,
            docs("boolean", {param("target", "number | Vector3 | { x: number; y: number; z: number }", false, "A heading in radians, or a position to turn towards.")},
                "Turns it on the spot (ACTOR_SETDIR). Scripted NPCs only; a task that moves it turns it again.", "False when it is not a living scripted NPC or no host is attached."));
        cls.prototype_function("giveWeapon", &JS_NpcGiveWeapon,
            docs("boolean",
                {param("weaponId", "number", false, "Supported stock weapon, 2 through 15."), param("ammo", "number", true, "Loaded rounds; defaults to a full magazine for a firearm and 1 otherwise."),
                    param("reserve", "number", true, "Reserve rounds; defaults to 0.")},
                "Adds a weapon to a scripted NPC's native inventory (HUMAN_ADDWEAPON); the retail AI picks what it holds. getInventory shows it once the host reports it.",
                "False when it is not a living scripted NPC or no host is attached."));
        cls.prototype_function("setHealth", &JS_NpcSetHealth,
            docs("boolean", {param("health", "number", false, "Above 0, up to 100000.")}, "Sets a living scripted NPC's health.", "False when it is not a living scripted NPC or no host is attached."));
        cls.prototype_function("getTask", &JS_NpcGetTask,
            docs("{ id: number; task: string } | null", {}, "Returns the scripted NPC's running task.", "The task, or null when it runs none."));
        cls.prototype_function("getVehicle", &JS_NpcGetVehicle, docs("Vehicle | null", {}, "Returns the car it sits in.", "The vehicle, or null on foot."));
        return cls;
    }

    void Npc::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        auto &cls = GetClass(isolate);
        cls.static_function("getAll", &JS_NpcGetAll, docs("Npc[]", {}, "Lists every NPC.", "The current NPCs."));
        cls.static_function("getInRange", &JS_NpcGetInRange,
            docs("Npc[]", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Center; three numbers are accepted too."), param("radius", "number", false, "Distance in units.")},
                "Lists the NPCs within radius of position.", "The NPCs in range."));
        cls.static_function("getCount", &JS_NpcGetCount, docs("number", {}, "Counts the NPCs.", "The number of NPCs."));
        cls.static_function("spawn", &JS_NpcSpawn,
            docs("Npc | null",
                {param("model", "string", false, "Stock human .i3d filename, such as \"pol01.i3d\" for a policeman."),
                    param("position", "Vector3 | { x: number; y: number; z: number }", false, "Where it stands; three numbers are accepted too."),
                    param("heading", "number", true, "Facing in radians; defaults to 0."),
                    param("options", "{ health?: number; behaviour?: \"neutral\" | \"friendly\" | \"hostile\"; weapon?: number; ammo?: number; reserve?: number; virtualWorld?: number }", true,
                        "health: defaults to 100; behaviour: how the retail AI rates players (default neutral); weapon, ammo, reserve: a weapon to hold; virtualWorld: the only world whose players see it (default every world).")},
                "Spawns a human the simulation host runs with the retail AI and that scripts give tasks. The handle is usable at once; npcSpawn follows once the host created it, npcRetire if it could not.",
                "The NPC, or null when no simulation host is attached, no mission is loaded, 64 scripted NPCs exist or an argument is unusable."));
        auto context = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "Npc"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }
} // namespace Mafia1Online::Scripting
