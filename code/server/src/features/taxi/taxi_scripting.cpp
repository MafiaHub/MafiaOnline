#include "taxi_scripting.h"

#include "core/server.h"
#include "features/script/script_runtime.h"
#include "shared/features/car/car_entity.h"
#include "shared/scripting_catalog.h"

#include <scripting/engine.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <optional>

namespace Mafia1Online::Scripting {
    namespace {
        using Features::Taxi::TaxiService;

        uint64_t ReadId(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return info.Length() > index ? ScriptArgs::ReadEntityId(info.GetIsolate(), info[index]) : 0;
        }

        // Taxi.setHailable(player, hailable)
        void JS_SetHailable(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const uint64_t id = ReadId(info, 0);
            if (!id || info.Length() != 2 || !info[1]->IsBoolean()) {
                ScriptArgs::Throw(info.GetIsolate(), "Taxi.setHailable(player, hailable) expects a Player and a boolean");
                return;
            }
            info.GetReturnValue().Set(GetServer().Taxi().SetHailable(id, info[1]->BooleanValue(info.GetIsolate())));
        }

        void JS_IsHailable(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const uint64_t id = ReadId(info, 0);
            if (!id || info.Length() != 1) {
                ScriptArgs::Throw(info.GetIsolate(), "Taxi.isHailable(player) expects a Player");
                return;
            }
            info.GetReturnValue().Set(GetServer().Taxi().IsHailable(id));
        }

        void JS_IsAvailable(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(GetServer().Taxi().Available());
        }

        // Taxi.getHail(pedestrian)
        void JS_GetHail(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            const uint64_t id = ReadId(info, 0);
            if (!id || info.Length() != 1) {
                ScriptArgs::Throw(isolate, "Taxi.getHail(pedestrian) expects a Pedestrian");
                return;
            }
            const auto hail = GetServer().Taxi().FindHail(id);
            if (!hail) {
                info.GetReturnValue().SetNull();
                return;
            }
            auto context = isolate->GetCurrentContext();
            auto object = v8::Object::New(isolate);
            SetField(isolate, context, object, "player", WrapPlayer(isolate, hail->playerId));
            SetField(isolate, context, object, "vehicle", WrapVehicle(isolate, hail->carId));
            SetField(isolate, context, object, "pickedUp", v8::Boolean::New(isolate, hail->pickedUp));
            info.GetReturnValue().Set(object);
        }

        void JS_Decline(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const uint64_t id = ReadId(info, 0);
            if (!id || info.Length() != 1) {
                ScriptArgs::Throw(info.GetIsolate(), "Taxi.decline(pedestrian) expects a Pedestrian");
                return;
            }
            info.GetReturnValue().Set(GetServer().Taxi().Decline(id));
        }

        // Taxi.board(player, pedestrian, seat?)
        void JS_Board(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            const uint64_t playerId = ReadId(info, 0);
            const uint64_t pedestrianId = ReadId(info, 1);
            uint32_t seat = 0;
            const bool seatGiven = info.Length() == 3 && !info[2]->IsUndefined();
            if (!playerId || !pedestrianId || info.Length() > 3 || (seatGiven && (!ScriptArgs::ReadUInt(info[2], Shared::Entities::CarEntity::kMaxSeats - 1, seat) || seat == 0))) {
                ScriptArgs::Throw(isolate, "Taxi.board(player, pedestrian, seat?) expects a Player, the Pedestrian hailing them and a passenger seat from 1");
                return;
            }
            auto *module = GetServer().GetScriptingModule();
            std::string owner = module && module->GetEngine() ? module->GetEngine()->GetResourceForIsolate(isolate) : std::string();
            const uint64_t npcId =
                GetServer().Taxi().Board(playerId, pedestrianId, seatGiven ? std::optional<uint8_t>(static_cast<uint8_t>(seat)) : std::nullopt, std::move(owner));
            info.GetReturnValue().Set(npcId ? WrapNpc(isolate, npcId) : v8::Null(isolate).As<v8::Value>());
        }

        // Taxi.setDestination(passenger, position | null, radius?)
        void JS_SetDestination(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            constexpr const char *kUsage = "Taxi.setDestination(passenger, position | null, radius?) expects a passenger Npc, a finite position or null and a radius above 0";
            const uint64_t npcId = ReadId(info, 0);
            float radius = TaxiService::kArriveRadius;
            std::optional<glm::vec3> destination;
            int next = 2;
            if (!npcId || info.Length() < 2) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            if (!info[1]->IsNull()) {
                glm::vec3 position;
                if (!ScriptArgs::ReadPosition(info, 1, position, next)) {
                    ScriptArgs::Throw(isolate, kUsage);
                    return;
                }
                destination = position;
            }
            if (info.Length() > next + 1 || (info.Length() == next + 1 && !info[next]->IsUndefined() && (!ScriptArgs::ReadFloat(info[next], radius) || !(radius > 0.0f)))) {
                ScriptArgs::Throw(isolate, kUsage);
                return;
            }
            info.GetReturnValue().Set(GetServer().Taxi().SetDestination(npcId, destination, radius));
        }

        void JS_DropOff(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const uint64_t npcId = ReadId(info, 0);
            if (!npcId || info.Length() != 1) {
                ScriptArgs::Throw(info.GetIsolate(), "Taxi.dropOff(passenger) expects a passenger Npc");
                return;
            }
            info.GetReturnValue().Set(GetServer().Taxi().DropOff(npcId));
        }

        void JS_GetPassenger(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            const uint64_t playerId = ReadId(info, 0);
            if (!playerId || info.Length() != 1) {
                ScriptArgs::Throw(isolate, "Taxi.getPassenger(player) expects a Player");
                return;
            }
            const uint64_t npcId = GetServer().Taxi().PassengerOf(playerId);
            info.GetReturnValue().Set(npcId ? WrapNpc(isolate, npcId) : v8::Null(isolate).As<v8::Value>());
        }

        void JS_GetRide(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            const uint64_t npcId = ReadId(info, 0);
            if (!npcId || info.Length() != 1) {
                ScriptArgs::Throw(isolate, "Taxi.getRide(passenger) expects an Npc");
                return;
            }
            const auto ride = GetServer().Taxi().FindRide(npcId);
            if (!ride) {
                info.GetReturnValue().SetNull();
                return;
            }
            auto context = isolate->GetCurrentContext();
            auto object = v8::Object::New(isolate);
            SetField(isolate, context, object, "player", WrapPlayer(isolate, ride->playerId));
            SetField(isolate, context, object, "vehicle", WrapVehicle(isolate, ride->carId));
            SetField(isolate, context, object, "seat", v8::Uint32::New(isolate, ride->seat));
            SetField(isolate, context, object, "phase", v8pp::to_v8(isolate, ride->phase));
            SetField(isolate, context, object, "destination", ride->destination ? PlainVector(isolate, context, *ride->destination).As<v8::Value>() : v8::Null(isolate).As<v8::Value>());
            SetField(isolate, context, object, "radius", v8::Number::New(isolate, ride->radius));
            SetField(isolate, context, object, "arrived", v8::Boolean::New(isolate, ride->arrived));
            info.GetReturnValue().Set(object);
        }
    } // namespace

    Features::Taxi::TaxiService::Events Taxi::Events() {
        Features::Taxi::TaxiService::Events events;
        events.hail = [](uint64_t playerId, uint64_t pedestrianId, uint64_t carId) {
            EmitEvent("taxiHail", [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) {
                arguments.push_back(WrapPlayer(isolate, playerId));
                arguments.push_back(WrapPedestrian(isolate, pedestrianId));
                arguments.push_back(WrapVehicle(isolate, carId));
            });
        };
        events.hailEnd = [](uint64_t playerId, uint64_t pedestrianId, const char *reason) {
            EmitEvent("taxiHailEnd", [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
                auto info = v8::Object::New(isolate);
                SetField(isolate, context, info, "reason", v8pp::to_v8(isolate, reason));
                arguments.push_back(WrapPlayer(isolate, playerId));
                arguments.push_back(WrapPedestrian(isolate, pedestrianId));
                arguments.push_back(info);
            });
        };
        events.pickup = [](uint64_t playerId, uint64_t pedestrianId, uint64_t carId) {
            EmitEvent("taxiPickup", [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) {
                arguments.push_back(WrapPlayer(isolate, playerId));
                arguments.push_back(WrapPedestrian(isolate, pedestrianId));
                arguments.push_back(WrapVehicle(isolate, carId));
            });
        };
        events.enter = [](uint64_t playerId, uint64_t npcId, uint64_t carId) {
            EmitEvent("taxiPassengerEnter", [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) {
                arguments.push_back(WrapPlayer(isolate, playerId));
                arguments.push_back(WrapNpc(isolate, npcId));
                arguments.push_back(WrapVehicle(isolate, carId));
            });
        };
        events.arrive = [](uint64_t playerId, uint64_t npcId, uint64_t carId) {
            EmitEvent("taxiArrive", [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) {
                arguments.push_back(WrapPlayer(isolate, playerId));
                arguments.push_back(WrapNpc(isolate, npcId));
                arguments.push_back(WrapVehicle(isolate, carId));
            });
        };
        events.exit = [](uint64_t playerId, uint64_t npcId, uint64_t carId, TaxiService::ExitReason reason, bool boarded) {
            EmitEvent("taxiPassengerExit", [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
                auto info = v8::Object::New(isolate);
                SetField(isolate, context, info, "reason", v8pp::to_v8(isolate, TaxiService::ExitReasonName(reason)));
                SetField(isolate, context, info, "boarded", v8::Boolean::New(isolate, boarded));
                arguments.push_back(WrapPlayer(isolate, playerId));
                arguments.push_back(WrapNpc(isolate, npcId));
                arguments.push_back(WrapVehicle(isolate, carId));
                arguments.push_back(info);
            });
        };
        return events;
    }

    void Taxi::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        auto &catalog = ServerCatalog();
        auto &hail = catalog.data_type("TaxiHail", "A walker waving at a player's taxi.");
        hail.add_property("player", "Player | null", "The driver it waves at.");
        hail.add_property("vehicle", "Vehicle | null", "Their taxi.");
        hail.add_property("pickedUp", "boolean", "The taxi stopped by it (taxiPickup).");
        auto &ride = catalog.data_type("TaxiRide", "A taxi passenger's ride.");
        ride.add_property("player", "Player | null", "The driver.");
        ride.add_property("vehicle", "Vehicle | null", "The taxi.");
        ride.add_property("seat", "number", "The passenger's seat.");
        ride.add_property("phase", "\"boarding\" | \"riding\" | \"leaving\" | \"released\"", "Walking to the door, seated, getting out, or out and walking off.");
        ride.add_property("destination", "{ x: number; y: number; z: number } | null", "Taxi.setDestination's point.");
        ride.add_property("radius", "number", "How close the taxi must stop.");
        ride.add_property("arrived", "boolean", "The taxi stopped there (taxiArrive).");
        v8pp::module taxi(isolate, catalog, "Taxi",
            "Retail taxi passengers. The simulation host runs single player's hailing for every player Taxi.setHailable allows: in a taxi model "
            "(taxi, fordapicktaxi, phantomtaxi and retail's hot rods) at 20 m/s or less with no passenger, a walker 20 to 90 m ahead waves "
            "(taxiHail), turns to the cab and keeps waving; when the taxi stops within 10 m (taxiPickup) a script boards it (Taxi.board). It becomes an "
            "NPC passenger that gets into the back, rides on every client, and gets out when Taxi.dropOff says so or the driver leaves the driver's seat.");
        taxi.function("setHailable", &JS_SetHailable,
            docs("boolean", {param("player", "Player", false, "A connected player."), param("hailable", "boolean", false, "Whether walkers may hail their taxi.")},
                "Lets walkers hail the player's taxi, as the retail mission script's taxidriver_enable does for single player's player. Off ends their waves. "
                "It lasts until changed or the player leaves.",
                "False when the player is unknown."));
        taxi.function("isHailable", &JS_IsHailable, docs("boolean", {param("player", "Player", false, "A player.")}, "Whether walkers may hail the player's taxi.", "The setting."));
        taxi.function("isAvailable", &JS_IsAvailable,
            docs("boolean", {}, "Whether a simulation host runs the walkers, so hails can come at all.", "False without a simulation host or a loaded mission."));
        taxi.function("getHail", &JS_GetHail,
            docs("TaxiHail | null", {param("pedestrian", "Pedestrian", false, "A walker.")}, "Returns whom the walker waves at.", "The hail, or null when it does not wave."));
        taxi.function("decline", &JS_Decline,
            docs("boolean", {param("pedestrian", "Pedestrian", false, "A waving walker.")}, "Ends the walker's wave; it walks on. taxiHailEnd follows.",
                "False when it does not wave, already boards or no host is attached."));
        taxi.function("board", &JS_Board,
            docs("Npc | null",
                {param("player", "Player", false, "The driver the walker hails."), param("pedestrian", "Pedestrian", false, "The waving walker."),
                    param("seat", "number", true, "A free passenger seat; by default retail's: seat 3 of a car with more than three seats, else seat 1, else the first free one.")},
                "Makes the walker the player's passenger: a scripted NPC takes its place where it stands (players swap the walker for it) and gets into the "
                "player's car, which the player keeps driving. taxiPassengerEnter follows once seated; taxiPassengerExit if it never sits down.",
                "The passenger NPC, or null when the walker does not hail this player, the player does not drive a car with a free passenger seat, already "
                "has a passenger, or no NPC can be made."));
        taxi.function("setDestination", &JS_SetDestination,
            docs("boolean",
                {param("passenger", "Npc", false, "A passenger from Taxi.board."), param("position", "Vector3 | { x: number; y: number; z: number } | null", false, "Where the ride goes, or null for nowhere."),
                    param("radius", "number", true, "How close the taxi must stop; 15 by default, as taxi_pas.dat.")},
                "Sets where the ride goes: taxiArrive follows once the taxi stands still (0.5 m/s or less) within the radius.", "False when it is not a passenger still aboard."));
        taxi.function("dropOff", &JS_DropOff,
            docs("boolean", {param("passenger", "Npc", false, "A passenger from Taxi.board.")},
                "Ends the ride: the passenger gets out once the car is nearly still and walks off, and is removed once out of sight. taxiPassengerExit follows.",
                "False when it is not a passenger or already leaves."));
        taxi.function("getPassenger", &JS_GetPassenger,
            docs("Npc | null", {param("player", "Player", false, "A player.")}, "Returns the player's passenger, boarding or seated.", "The passenger, or null."));
        taxi.function("getRide", &JS_GetRide,
            docs("TaxiRide | null", {param("passenger", "Npc", false, "An NPC.")}, "Returns a passenger's ride.", "The ride, or null when it is no taxi passenger."));
        taxi.publish(global);
    }
} // namespace Mafia1Online::Scripting
