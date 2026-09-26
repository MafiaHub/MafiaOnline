#include "player_scripting.h"

#include "features/world/world_service.h"
#include "script_runtime.h"
#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/scene/native_scene.h>

#include "shared/features/car/car_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/scripting_catalog.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>
#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <stdexcept>

namespace Mafia1Online::Scripting {
    namespace {
        using Shared::Entities::CarEntity;
        using Shared::Entities::PlayerEntity;
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;

        Framework::Networking::Replication::ReplicationManager *Replication() {
            return Framework::CoreModules::GetReplication();
        }

        // The seat a player occupies in a streamed vehicle, from the server's
        // occupant array.
        CarEntity *FindSeat(uint64_t playerId, int &seat) {
            CarEntity *found = nullptr;
            seat                   = -1;
            if (auto *replication = Replication(); replication && playerId != 0) {
                replication->ForEach<CarEntity>([&](CarEntity *car) {
                    for (uint8_t i = 0; i < CarEntity::kMaxSeats && !found; ++i) {
                        if (car->occupantIds[i] == playerId) {
                            found = car;
                            seat  = i;
                        }
                    }
                });
            }
            return found;
        }

        void JS_PlayerGetWorldPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self         = v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info.This());
            auto &world        = GetWorld();
            const auto *player = self ? self->ResolvePlayer() : nullptr;
            if (!player || !player->spawned || player->missionGeneration != world.LoadedMissionGeneration()) {
                info.GetReturnValue().SetNull();
                return;
            }
            auto &registry = world.NativeObjects();
            auto *actor    = static_cast<SDK::Player::NativeActor *>(registry.Resolve(registry.FindByNetwork(self->GetId())));
            auto *frame    = actor ? actor->Frame() : nullptr;
            if (!frame) {
                info.GetReturnValue().SetNull();
                return;
            }
            const auto position = frame->WorldPosition();
            info.GetReturnValue().Set(Args::Position(info.GetIsolate(), {position.x, position.y, position.z}));
        }

        v8::Local<v8::Value> NativeWorldTransform(v8::Isolate *isolate, uint64_t id) {
            auto &world = GetWorld();
            if (!world.IsReady())
                return v8::Null(isolate);
            auto &registry = world.NativeObjects();
            auto *actor    = static_cast<SDK::Player::NativeActor *>(registry.Resolve(registry.FindByNetwork(id)));
            auto *frame    = actor ? actor->Frame() : nullptr;
            if (!frame)
                return v8::Null(isolate);
            const auto position     = frame->WorldPosition();
            const auto basis        = frame->GetWorldBasis();
            const glm::vec3 forward = glm::normalize(glm::vec3(basis.forward.x, basis.forward.y, basis.forward.z));
            const glm::vec3 right   = glm::normalize(glm::cross(glm::vec3(basis.up.x, basis.up.y, basis.up.z), forward));
            const glm::vec3 up      = glm::normalize(glm::cross(forward, right));
            // Match the full quaternion used by native car replication and Vehicle.spawn.
            const auto q  = glm::normalize(glm::quat_cast(glm::mat3(right, up, forward)));
            auto context  = isolate->GetCurrentContext();
            auto rotation = v8::Object::New(isolate);
            rotation->Set(context, v8pp::to_v8(isolate, "w"), v8pp::to_v8(isolate, q.w)).Check();
            rotation->Set(context, v8pp::to_v8(isolate, "x"), v8pp::to_v8(isolate, q.x)).Check();
            rotation->Set(context, v8pp::to_v8(isolate, "y"), v8pp::to_v8(isolate, q.y)).Check();
            rotation->Set(context, v8pp::to_v8(isolate, "z"), v8pp::to_v8(isolate, q.z)).Check();
            auto result = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "position"), Args::Position(isolate, {position.x, position.y, position.z})).Check();
            result->Set(context, v8pp::to_v8(isolate, "rotation"), rotation).Check();
            return result;
        }

        void JS_PlayerGetWorldTransform(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self         = v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info.This());
            const auto *player = self ? self->ResolvePlayer() : nullptr;
            info.GetReturnValue().Set(player && player->spawned && player->missionGeneration == GetWorld().LoadedMissionGeneration() ? NativeWorldTransform(info.GetIsolate(), self->GetId()) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_VehicleGetWorldTransform(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self      = v8pp::class_<Vehicle>::unwrap_object(info.GetIsolate(), info.This());
            const auto *car = self ? self->ResolveCar() : nullptr;
            info.GetReturnValue().Set(car && car->missionGeneration == GetWorld().LoadedMissionGeneration() ? NativeWorldTransform(info.GetIsolate(), self->GetId()) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_PlayerGetVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info.This());
            int seat   = -1;
            auto *car  = self ? FindSeat(self->GetId(), seat) : nullptr;
            info.GetReturnValue().Set(car ? WrapVehicle(info.GetIsolate(), car->GetNetworkID()) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_PlayerGetSeat(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info.This());
            int seat   = -1;
            if (self) {
                FindSeat(self->GetId(), seat);
            }
            info.GetReturnValue().Set(seat);
        }

        void JS_VehicleGetOccupant(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = v8pp::class_<Vehicle>::unwrap_object(isolate, info.This());
            const auto *car = self ? self->ResolveCar() : nullptr;
            if (info.Length() != 1 || !info[0]->IsInt32()) {
                Args::Throw(isolate, "Vehicle.getOccupant(seat) expects a seat index");
                return;
            }
            const int seat = info[0].As<v8::Int32>()->Value();
            if (!car || seat < 0 || seat >= CarEntity::kMaxSeats || car->occupantIds[seat] == 0) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(WrapPlayer(isolate, car->occupantIds[seat]));
        }

        void JS_VehicleGetOccupants(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate   = info.GetIsolate();
            auto context    = isolate->GetCurrentContext();
            auto *self      = v8pp::class_<Vehicle>::unwrap_object(isolate, info.This());
            const auto *car = self ? self->ResolveCar() : nullptr;
            auto list       = v8::Array::New(isolate);
            uint32_t index  = 0;
            for (uint8_t seat = 0; car && seat < CarEntity::kMaxSeats; ++seat) {
                auto player = car->occupantIds[seat] != 0 ? WrapPlayer(isolate, car->occupantIds[seat]) : v8::Null(isolate).As<v8::Value>();
                if (!player->IsNull()) {
                    list->Set(context, index++, player).Check();
                }
            }
            info.GetReturnValue().Set(list);
        }

        void JS_LocalPlayer(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(WrapPlayer(info.GetIsolate(), LocalPlayerId()));
        }
    } // namespace

    std::unique_ptr<v8pp::class_<Player>> Player::_class;
    std::unique_ptr<v8pp::class_<Vehicle>> Vehicle::_class;

    Player::Player(uint64_t networkId): Framework::Scripting::Builtins::Player(networkId) {
        if (!ResolvePlayer()) {
            throw std::runtime_error("Player handle does not refer to a streamed player");
        }
    }

    Vehicle::Vehicle(uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {
        if (!ResolveCar()) {
            throw std::runtime_error("Vehicle handle does not refer to a streamed vehicle");
        }
    }

    uint64_t LocalPlayerId() {
        auto *replication = Replication();
        if (!replication) {
            return 0;
        }
        const auto guid = static_cast<uint64_t>(replication->GetMyGUID());
        uint64_t id     = 0;
        replication->ForEach<PlayerEntity>([&](PlayerEntity *player) {
            if (id == 0 && player->controllerGuid == guid) {
                id = player->GetNetworkID();
            }
        });
        return id;
    }

    v8::Local<v8::Value> WrapPlayer(v8::Isolate *isolate, uint64_t networkId) {
        auto *replication = Replication();
        if (networkId == 0 || !replication || !replication->GetEntity<PlayerEntity>(networkId)) {
            return v8::Null(isolate);
        }
        Player::GetClass(isolate);
        return v8pp::class_<Player>::create_object(isolate, networkId);
    }

    v8::Local<v8::Value> WrapVehicle(v8::Isolate *isolate, uint64_t networkId) {
        auto *replication = Replication();
        if (networkId == 0 || !replication || !replication->GetEntity<CarEntity>(networkId)) {
            return v8::Null(isolate);
        }
        Vehicle::GetClass(isolate);
        return v8pp::class_<Vehicle>::create_object(isolate, networkId);
    }

    PlayerEntity *Player::ResolvePlayer() const {
        return dynamic_cast<PlayerEntity *>(Resolve());
    }

    std::string Player::GetNickname() const {
        const auto *player = ResolvePlayer();
        return player ? player->nickname : std::string();
    }

    std::string Player::GetModel() const {
        const auto *player = ResolvePlayer();
        return player ? player->model : std::string();
    }

    double Player::GetHealth() const {
        const auto *player = ResolvePlayer();
        return player ? player->health : 0.0;
    }

    double Player::GetMoney() const {
        const auto *player = ResolvePlayer();
        return player ? player->money : 0.0;
    }

    bool Player::IsAlive() const {
        const auto *player = ResolvePlayer();
        return player && player->spawned && player->alive;
    }

    bool Player::IsSpawned() const {
        const auto *player = ResolvePlayer();
        return player && player->spawned;
    }

    bool Player::IsLocal() const {
        const auto *player = ResolvePlayer();
        return player && Replication() && player->controllerGuid == static_cast<uint64_t>(Replication()->GetMyGUID());
    }

    double Player::GetMissionGeneration() const {
        const auto *player = ResolvePlayer();
        return player ? static_cast<double>(player->missionGeneration) : 0.0;
    }

    double Player::GetSpawnGeneration() const {
        const auto *player = ResolvePlayer();
        return player ? static_cast<double>(player->spawnGeneration) : 0.0;
    }

    std::string Player::ToString() const {
        const auto *player = ResolvePlayer();
        return player ? "Player(" + std::to_string(GetId()) + ", " + player->nickname + ")" : "Player(" + std::to_string(GetId()) + ", gone)";
    }

    v8pp::class_<Player> &Player::GetClass(v8::Isolate *isolate) {
        if (_class) {
            return *_class;
        }
        Framework::Scripting::Builtins::Player::GetClass(isolate);
        _class    = std::make_unique<v8pp::class_<Player>>(isolate, ClientCatalog(), "Player", "A streamed Mafia 1 player, local or remote. Properties read replicated state; getWorldPosition reads the live native pose. A handle stops resolving once the player leaves.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Player>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for a streamed player; throws when the ID does not resolve to a player."));
        cls.function("toString", &Player::ToString, docs("string", {}, "Formats this player for logging.", "The player ID and nickname."));
        cls.property("nickname", &Player::GetNickname, property_docs("string", "Server nickname."));
        cls.property("model", &Player::GetModel, property_docs("string", "Server-selected native human model file, such as \"Tommy.i3d\"; empty after the player streams out."));
        cls.property("health", &Player::GetHealth, property_docs("number", "Server health from 0 to 100."));
        cls.property("money", &Player::GetMoney, property_docs("number", "Last replicated server-owned Free Ride balance. Read only, including for the local player."));
        cls.property("alive", &Player::IsAlive, property_docs("boolean", "Whether the player is spawned and alive."));
        cls.property("spawned", &Player::IsSpawned, property_docs("boolean", "Whether the player has a life in the current mission."));
        cls.property("isLocal", &Player::IsLocal, property_docs("boolean", "Whether this is the player this client controls."));
        cls.property("missionGeneration", &Player::GetMissionGeneration, property_docs("number", "Mission generation of the current life."));
        cls.property("spawnGeneration", &Player::GetSpawnGeneration, property_docs("number", "Generation of the current life; it increases on every spawn."));
        cls.prototype_function("getVehicle", &JS_PlayerGetVehicle, docs("Vehicle | null", {}, "Returns the streamed vehicle the player sits in.", "The vehicle, or null on foot."));
        cls.prototype_function("getWorldPosition", &JS_PlayerGetWorldPosition,
            docs("{ x: number; y: number; z: number } | null", {}, "Reads the live native world position, including seated motion. Local players use the current simulation pose; remote players use their rendered pose. Null while the native actor is absent."));
        cls.prototype_function("getSeat", &JS_PlayerGetSeat, docs("number", {}, "Returns the seat the player occupies.", "The seat index (0 is the driver), or -1 on foot."));
        cls.prototype_function("getWorldTransform", &JS_PlayerGetWorldTransform,
            docs("{ position: { x: number; y: number; z: number }; rotation: { w: number; x: number; y: number; z: number } } | null", {},
                "Reads live native world position and full quaternion in the same convention as server entity transforms. Null while the native actor is absent."));
        return cls;
    }

    void Player::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        GetClass(isolate).publish(global);
    }

    CarEntity *Vehicle::ResolveCar() const {
        return dynamic_cast<CarEntity *>(Resolve());
    }

    std::string Vehicle::GetModel() const {
        const auto *car = ResolveCar();
        return car ? car->model : std::string();
    }

    double Vehicle::GetHealth() const {
        const auto *car = ResolveCar();
        return car ? car->health : 0.0;
    }

    bool Vehicle::IsEngineOn() const {
        const auto *car = ResolveCar();
        return car && car->engineOn;
    }

    bool Vehicle::IsSirenOn() const {
        const auto *car = ResolveCar();
        return car && car->sirenOn;
    }

    bool Vehicle::IsHornOn() const {
        const auto *car = ResolveCar();
        return car && car->hornOn;
    }

    double Vehicle::GetFuel() const {
        const auto *car = ResolveCar();
        return car ? car->fuel : 0.0;
    }

    double Vehicle::GetOpacity() const {
        const auto *car = ResolveCar();
        return car ? car->opacity : 1.0;
    }

    int Vehicle::GetGear() const {
        const auto *car = ResolveCar();
        return car ? car->gear : 0;
    }

    int Vehicle::GetSeatCount() const {
        const auto *car = ResolveCar();
        return car ? car->seatCount : 0;
    }

    std::string Vehicle::ToString() const {
        const auto *car = ResolveCar();
        return car ? "Vehicle(" + std::to_string(GetId()) + ", " + car->model + ")" : "Vehicle(" + std::to_string(GetId()) + ", gone)";
    }

    v8pp::class_<Vehicle> &Vehicle::GetClass(v8::Isolate *isolate) {
        if (_class) {
            return *_class;
        }
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Vehicle>>(isolate, ClientCatalog(), "Vehicle", "A streamed server vehicle. Every value is the server state this client last received.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for a streamed vehicle; throws when the ID does not resolve to a vehicle."));
        cls.function("toString", &Vehicle::ToString, docs("string", {}, "Formats this vehicle for logging.", "The vehicle ID and model."));
        cls.property("model", &Vehicle::GetModel, property_docs("string", "Native model file, such as \"fordtl00.i3d\"."));
        cls.property("health", &Vehicle::GetHealth, property_docs("number", "Server health from 0 to 100."));
        cls.property("engineOn", &Vehicle::IsEngineOn, property_docs("boolean", "Whether the engine runs."));
        cls.property("sirenOn", &Vehicle::IsSirenOn, property_docs("boolean", "Whether the siren sounds."));
        cls.property("hornOn", &Vehicle::IsHornOn, property_docs("boolean", "Whether the horn sounds."));
        cls.property("fuel", &Vehicle::GetFuel, property_docs("number", "Fuel in the tank."));
        cls.property("opacity", &Vehicle::GetOpacity, property_docs("number", "Server-authored model opacity, 0 transparent and 1 opaque."));
        cls.property("gear", &Vehicle::GetGear, property_docs("number", "Current gear; 0 is neutral."));
        cls.property("seatCount", &Vehicle::GetSeatCount, property_docs("number", "Number of seats."));
        cls.prototype_function("getWorldTransform", &JS_VehicleGetWorldTransform,
            docs("{ position: { x: number; y: number; z: number }; rotation: { w: number; x: number; y: number; z: number } } | null", {},
                "Reads live native world position and full quaternion, including pitch and roll, in the same convention as Vehicle.spawn. Null while the native actor is absent."));
        cls.prototype_function("getOccupant", &JS_VehicleGetOccupant,
            docs("Player | null", {param("seat", "number", false, "Seat index; 0 is the driver.")}, "Returns the player in a seat.", "The player, or null for an empty seat."));
        cls.prototype_function("getOccupants", &JS_VehicleGetOccupants, docs("Player[]", {}, "Lists the seated players.", "Every streamed occupant in seat order."));
        return cls;
    }

    void Vehicle::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        GetClass(isolate).publish(global);
    }

    void RegisterLocalPlayer(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto context = isolate->GetCurrentContext();
        auto getter  = v8::FunctionTemplate::New(isolate, &JS_LocalPlayer)->GetFunction(context).ToLocalChecked();
        v8pp::set_global_accessor(isolate, global, ClientCatalog(), "LocalPlayer", getter,
            {.type = "Player | null", .description = "The player this client controls, or null before the server created it.", .readonly = true});
    }
} // namespace Mafia1Online::Scripting
