#include "script_events.h"

#include "script_runtime.h"

#include "core/server.h"
#include "features/car/vehicle_scripting.h"
#include "features/combat/combat_service.h"
#include "features/combat/weapon_scripting.h"
#include "features/door/door_scripting.h"
#include "features/pickup/pickup_scripting.h"
#include "features/player/player_scripting.h"
#include "features/world/world_scripting.h"
#include "shared/features/car/car_debris_entity.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/car/car_hit_report.h"
#include "shared/features/car/seat_action.h"
#include "shared/features/combat/combat_protocol.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/world/mission_load_result.h"
#include "shared/scripting_catalog.h"

#include <logging/logger.h>
#include <scripting/builtins/entity.h>
#include <scripting/scripting_catalog.h>
#include <scripting/node_engine.h>

#include <v8pp/convert.hpp>
#include <v8pp/metadata.hpp>

namespace Mafia1Online::Features::Script {
    namespace {
        using namespace Mafia1Online::Scripting;

        v8::Local<v8::Value> Number(v8::Isolate *isolate, double value) {
            return v8::Number::New(isolate, value);
        }

        const char *DamageCauseName(Combat::DamageCause cause) {
            switch (cause) {
            case Combat::DamageCause::Script: return "script";
            case Combat::DamageCause::Firearm: return "firearm";
            case Combat::DamageCause::Melee: return "melee";
            case Combat::DamageCause::Explosion: return "explosion";
            case Combat::DamageCause::Fire: return "fire";
            case Combat::DamageCause::Fall: return "fall";
            case Combat::DamageCause::Drowning: return "drowning";
            case Combat::DamageCause::Vehicle: return "vehicle";
            }
            return "unknown";
        }

        v8::Local<v8::Object> MissionInfo(v8::Isolate *isolate, v8::Local<v8::Context> context, Core::Server &server) {
            auto info = v8::Object::New(isolate);
            SetField(isolate, context, info, "mission", v8pp::to_v8(isolate, server.Mission()));
            SetField(isolate, context, info, "missionGeneration", Number(isolate, static_cast<double>(server.MissionGeneration())));
            return info;
        }
    } // namespace

    void RegisterScripting(Framework::Scripting::Engine *engine, Core::Server &server) {
        SetServer(&server);
        auto *node    = static_cast<Framework::Scripting::NodeEngine *>(engine);
        auto *isolate = node->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        auto context = node->GetContext();
        v8::Context::Scope contextScope(context);
        auto global = context->Global();
        RegisterServerEventMetadata();
        Framework::Scripting::Builtins::Entity::Register(isolate, global);
        Scripting::Player::Register(isolate, global);
        Scripting::Vehicle::Register(isolate, global);
        Scripting::Pickup::Register(isolate, global);
        Scripting::Door::Register(isolate, global);
        Scripting::Weapon::Register(isolate, global);
        Scripting::Sound::Register(isolate, global);
        Scripting::World::Register(isolate, global);
#ifndef NDEBUG
        auto &catalog = ServerCatalog();
        Framework::Scripting::MergeScriptingCatalog(catalog, v8pp::metadata::catalog("framework-server"));
        const auto output = MetadataOutputPath("server");
        if (!output.empty() && !v8pp::metadata::write_json_file(catalog, output.string())) {
            Framework::Logging::GetLogger("Scripting")->error("Failed to export Mafia1Online server scripting API metadata");
        }
#endif
    }

    void EmitPlayerEvent(Core::Server &, const char *name, uint64_t playerId) {
        EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) { arguments.push_back(WrapPlayer(isolate, playerId)); });
    }

    void EmitDoorEvent(Core::Server &, const char *name, Shared::Entities::DoorEntity &door, uint64_t playerId) {
        EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) {
            arguments.push_back(WrapDoor(isolate, door.GetNetworkID()));
            arguments.push_back(WrapPlayer(isolate, playerId));
        });
    }

    void EmitPlayerChat(Core::Server &, uint64_t playerId, const std::string &text) {
        EmitEvent("playerChat", [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) {
            arguments.push_back(WrapPlayer(isolate, playerId));
            arguments.push_back(v8pp::to_v8(isolate, text));
        });
    }

    void EmitPlayerCommand(Core::Server &, uint64_t playerId, const std::string &command, const std::vector<std::string> &args) {
        EmitEvent("playerCommand", [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
            auto list = v8::Array::New(isolate, static_cast<int>(args.size()));
            for (size_t i = 0; i < args.size(); ++i) {
                list->Set(context, static_cast<uint32_t>(i), v8pp::to_v8(isolate, args[i])).Check();
            }
            arguments.push_back(WrapPlayer(isolate, playerId));
            arguments.push_back(v8pp::to_v8(isolate, command));
            arguments.push_back(list);
        });
    }

    void EmitNicknameChange(uint64_t playerId, const std::string &oldNickname, const std::string &nickname) {
        EmitEvent("playerNicknameChange", [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) {
            arguments.push_back(WrapPlayer(isolate, playerId));
            arguments.push_back(v8pp::to_v8(isolate, oldNickname));
            arguments.push_back(v8pp::to_v8(isolate, nickname));
        });
    }

    void EmitModelChange(uint64_t playerId, const std::string &oldModel, const std::string &model) {
        EmitEvent("playerModelChange", [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) {
            arguments.push_back(WrapPlayer(isolate, playerId));
            arguments.push_back(v8pp::to_v8(isolate, oldModel));
            arguments.push_back(v8pp::to_v8(isolate, model));
        });
    }

    void EmitMoneyChange(uint64_t playerId, uint32_t oldMoney, uint32_t money) {
        EmitEvent("playerMoneyChange", [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) {
            arguments.push_back(WrapPlayer(isolate, playerId));
            arguments.push_back(v8::Uint32::New(isolate, oldMoney));
            arguments.push_back(v8::Uint32::New(isolate, money));
        });
    }

    void EmitSeatEvent(Core::Server &, const Shared::Car::SeatEvent &event) {
        using Action     = Shared::Car::SeatAction;
        const char *name = nullptr;
        switch (event.action) {
        case Action::EnterBegin:
        case Action::StealBegin: name = "vehiclePlayerEntering"; break;
        case Action::Enter:
        case Action::Steal: name = "vehiclePlayerEntered"; break;
        case Action::Exit: name = "vehiclePlayerExited"; break;
        case Action::ExitBlocked: name = "vehiclePlayerExitBlocked"; break;
        default: return;
        }
        EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
            auto info = v8::Object::New(isolate);
            SetField(isolate, context, info, "seat", v8::Uint32::New(isolate, event.seat));
            SetField(isolate, context, info, "stolen", v8::Boolean::New(isolate, event.action == Action::Steal || event.action == Action::StealBegin));
            SetField(isolate, context, info, "serverSequence", Number(isolate, static_cast<double>(event.serverSequence)));
            arguments.push_back(WrapVehicle(isolate, event.carId));
            arguments.push_back(WrapPlayer(isolate, event.playerId));
            arguments.push_back(info);
        });
    }

    void EmitCombatActionEvent(Core::Server &server, const Shared::Combat::Event &event) {
        using Action     = Shared::Combat::Action;
        const char *name = nullptr;
        switch (event.action) {
        case Action::Equip: name = "playerWeaponEquip"; break;
        case Action::Aim: name = "playerAimChange"; break;
        case Action::Drop: name = "playerWeaponDrop"; break;
        case Action::Reload: name = "playerWeaponReload"; break;
        case Action::Fire: name = "playerWeaponFire"; break;
        case Action::Throw: name = "playerWeaponThrow"; break;
        case Action::ThrowStart: name = "playerWeaponThrowStart"; break;
        case Action::ThrowRelease: name = "playerWeaponThrowRelease"; break;
        case Action::ThrowCancel: name = "playerWeaponThrowCancel"; break;
        case Action::MeleeStart: name = "playerWeaponMeleeStart"; break;
        case Action::Melee: name = "playerWeaponMelee"; break;
        case Action::MeleeCancel: name = "playerWeaponMeleeCancel"; break;
        default: return;
        }
        const auto *state = server.Combat().GetState(event.networkId);
        if (!state || state->missionGeneration != event.missionGeneration || state->spawnGeneration != event.spawnGeneration) {
            return;
        }
        const auto combat = *state;
        EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
            const auto &ammo = combat.ammo[event.weaponId];
            auto info        = v8::Object::New(isolate);
            SetField(isolate, context, info, "weaponId", v8::Uint32::New(isolate, event.weaponId));
            SetField(isolate, context, info, "selectedWeapon", v8::Uint32::New(isolate, combat.selectedWeapon));
            SetField(isolate, context, info, "inventoryMask", v8::Uint32::New(isolate, combat.inventoryMask));
            SetField(isolate, context, info, "loaded", v8::Uint32::New(isolate, ammo.loaded));
            SetField(isolate, context, info, "reserve", v8::Uint32::New(isolate, ammo.reserve));
            SetField(isolate, context, info, "aiming", v8::Boolean::New(isolate, combat.aiming));
            SetField(isolate, context, info, "direction", PlainVector(isolate, context, {event.directionX, event.directionY, event.directionZ}));
            SetField(isolate, context, info, "target", WrapPlayer(isolate, event.targetNetworkId));
            SetField(isolate, context, info, "revision", v8::Uint32::New(isolate, event.revision));
            SetField(isolate, context, info, "meleeIndex", v8::Integer::New(isolate, event.meleeIndex));
            SetField(isolate, context, info, "meleeHoldMs", v8::Uint32::New(isolate, event.meleeHoldMs));
            SetField(isolate, context, info, "throwHoldMs", v8::Uint32::New(isolate, event.throwHoldMs));
            arguments.push_back(WrapPlayer(isolate, event.networkId));
            arguments.push_back(info);
        });
    }

    void EmitCarEvent(Core::Server &, const char *name, Shared::Entities::CarEntity &car) {
        const std::string_view event(name);
        EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
            arguments.push_back(WrapVehicle(isolate, car.GetNetworkID()));
            if (event == "vehicleDamage") {
                arguments.push_back(car.nativeDamageValid ? Scripting::Vehicle::DamageObject(isolate, context, car).As<v8::Value>() : v8::Null(isolate).As<v8::Value>());
            }
            else if (event == "vehicleTerminal") {
                arguments.push_back(v8::Uint32::New(isolate, static_cast<uint32_t>(car.terminalState)));
            }
        });
    }

    void EmitCarHitEvent(Core::Server &, const Shared::Car::HitReport &hit, float damage) {
        EmitEvent("vehicleHit", [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
            auto info = v8::Object::New(isolate);
            SetField(isolate, context, info, "shooter", WrapPlayer(isolate, hit.shooterId));
            SetField(isolate, context, info, "damage", Number(isolate, damage));
            SetField(isolate, context, info, "position", PlainVector(isolate, context, {hit.hitX, hit.hitY, hit.hitZ}));
            SetField(isolate, context, info, "shotSequence", v8::Uint32::New(isolate, hit.shotSequence));
            SetField(isolate, context, info, "pelletIndex", v8::Uint32::New(isolate, hit.pelletIndex));
            arguments.push_back(WrapVehicle(isolate, hit.carId));
            arguments.push_back(info);
        });
    }

    void EmitPickupEvent(Core::Server &server, const char *name, uint64_t pickupId, uint64_t playerId) {
        const auto *pickup     = server.Pickups().Find(pickupId);
        const uint8_t weaponId = pickup ? pickup->weaponId : 0;
        const auto position    = pickup ? std::optional<glm::vec3>(pickup->position) : std::nullopt;
        EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
            auto info = v8::Object::New(isolate);
            SetField(isolate, context, info, "pickupId", Number(isolate, static_cast<double>(pickupId)));
            SetField(isolate, context, info, "weaponId", v8::Uint32::New(isolate, weaponId));
            SetField(isolate, context, info, "position", position ? PlainVector(isolate, context, *position).As<v8::Value>() : v8::Null(isolate).As<v8::Value>());
            arguments.push_back(WrapPickup(isolate, pickupId));
            arguments.push_back(WrapPlayer(isolate, playerId));
            arguments.push_back(info);
        });
    }

    void EmitCarDebrisEvent(Core::Server &, const char *name, Shared::Entities::CarDebrisEntity &debris) {
        EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
            auto info = v8::Object::New(isolate);
            SetField(isolate, context, info, "debrisId", Number(isolate, static_cast<double>(debris.GetNetworkID())));
            SetField(isolate, context, info, "type", v8::Uint32::New(isolate, debris.type));
            SetField(isolate, context, info, "partIndex", v8::Uint32::New(isolate, debris.partIndex));
            SetField(isolate, context, info, "position", PlainVector(isolate, context, debris.position));
            arguments.push_back(WrapVehicle(isolate, debris.carId));
            arguments.push_back(info);
        });
    }

    void EmitPlayerStateEvent(Core::Server &, const char *name, Shared::Entities::PlayerEntity &player) {
        const uint64_t id = player.GetNetworkID();
        EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context>, EventArguments &arguments) { arguments.push_back(WrapPlayer(isolate, id)); });
    }

    void EmitPlayerHealthEvent(Core::Server &server, uint64_t playerId, uint64_t sourceId, std::optional<uint8_t> weaponId,
                               Combat::DamageCause cause, float oldHealth, float newHealth, uint16_t deathAnimation) {
        auto *player = server.Players().FindByNetworkId(playerId);
        if (!player || oldHealth == newHealth) {
            return;
        }
        const uint64_t missionGeneration = player->missionGeneration;
        const uint64_t spawnGeneration   = player->spawnGeneration;
        const auto emit                  = [&](const char *name, bool withKiller) {
            EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
                auto info = v8::Object::New(isolate);
                SetField(isolate, context, info, "attacker", WrapPlayer(isolate, sourceId));
                SetField(isolate, context, info, "weaponId", weaponId ? v8::Uint32::New(isolate, *weaponId).As<v8::Value>() : v8::Null(isolate).As<v8::Value>());
                SetField(isolate, context, info, "cause", v8pp::to_v8(isolate, DamageCauseName(cause)));
                SetField(isolate, context, info, "oldHealth", Number(isolate, oldHealth));
                SetField(isolate, context, info, "health", Number(isolate, newHealth));
                SetField(isolate, context, info, "amount", Number(isolate, oldHealth - newHealth));
                SetField(isolate, context, info, "deathAnimation", v8::Uint32::New(isolate, deathAnimation));
                SetField(isolate, context, info, "missionGeneration", Number(isolate, static_cast<double>(missionGeneration)));
                SetField(isolate, context, info, "spawnGeneration", Number(isolate, static_cast<double>(spawnGeneration)));
                arguments.push_back(WrapPlayer(isolate, playerId));
                if (withKiller) {
                    arguments.push_back(WrapPlayer(isolate, sourceId));
                }
                arguments.push_back(info);
            });
        };
        emit(newHealth < oldHealth ? "playerDamage" : "playerHealthChange", false);
        if (newHealth <= 0.0f && oldHealth > 0.0f) {
            emit("playerDeath", true);
        }
    }

    void EmitMissionEvent(Core::Server &server, const char *name) {
        EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) { arguments.push_back(MissionInfo(isolate, context, server)); });
    }

    void EmitMissionLoadEvent(Core::Server &, uint64_t playerId, uint64_t generation, uint8_t state) {
        using State      = Shared::World::MissionLoadState;
        const char *name = state == static_cast<uint8_t>(State::Ready) ? "playerMissionReady" : state == static_cast<uint8_t>(State::Unloaded) ? "playerMissionUnloaded" : "playerMissionLoadFailed";
        EmitEvent(name, [&](v8::Isolate *isolate, v8::Local<v8::Context> context, EventArguments &arguments) {
            auto info = v8::Object::New(isolate);
            SetField(isolate, context, info, "missionGeneration", Number(isolate, static_cast<double>(generation)));
            SetField(isolate, context, info, "state", v8::Uint32::New(isolate, state));
            arguments.push_back(WrapPlayer(isolate, playerId));
            arguments.push_back(info);
        });
    }
} // namespace Mafia1Online::Features::Script
