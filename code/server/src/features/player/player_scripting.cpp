#include "player_scripting.h"

#include "core/server.h"
#include "features/car/vehicle_scripting.h"
#include "features/combat/weapon_scripting.h"
#include "features/script/script_events.h"
#include "features/script/script_runtime.h"
#include "features/world/world_scripting.h"
#include "shared/features/chat/text_policy.h"
#include "shared/features/player/player_entity.h"
#include "shared/scripting_catalog.h"

#include <core_modules.h>
#include <integrations/shared/rpc/emit_script_event.h>
#include <networking/network_peer.h>
#include <networking/rpc/client_identity.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace Mafia1Online::Scripting {
    std::unique_ptr<v8pp::class_<Player>> Player::_class;

    namespace {
        using Framework::Networking::Replication::NametagComponent;

        Player *SelfPlayer(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info.This());
        }

        Features::Combat::CombatService &Combat() {
            return GetServer().Combat();
        }

        const Shared::Combat::State *LivingCombat(uint64_t id) {
            const auto *state = Combat().GetState(id);
            return state && state->spawned && state->alive ? state : nullptr;
        }

        v8::Local<v8::Array> WeaponList(v8::Isolate *isolate, v8::Local<v8::Context> context, const Shared::Combat::State &state) {
            auto weapons   = v8::Array::New(isolate);
            uint32_t index = 0;
            for (uint32_t weaponId = 0; weaponId < Shared::Combat::kWeaponSlots; ++weaponId) {
                if (!Weapon::IsSupported(weaponId) || (state.inventoryMask & (1u << weaponId)) == 0) {
                    continue;
                }
                auto item = Weapon::Describe(isolate, context, weaponId).As<v8::Object>();
                SetField(isolate, context, item, "loaded", v8::Uint32::New(isolate, state.ammo[weaponId].loaded));
                SetField(isolate, context, item, "reserve", v8::Uint32::New(isolate, state.ammo[weaponId].reserve));
                weapons->Set(context, index++, item).Check();
            }
            return weapons;
        }

        bool ClearWeapons(uint64_t id) {
            const auto *state = LivingCombat(id);
            if (!state) {
                return false;
            }
            const uint32_t mask = state->inventoryMask;
            (void)Combat().SelectWeapon(id, 0);
            for (uint8_t weaponId = 0; weaponId < Shared::Combat::kWeaponSlots; ++weaponId) {
                if (mask & (1u << weaponId)) {
                    (void)Combat().RemoveWeapon(id, weaponId);
                }
            }
            return true;
        }

        void JS_SetNickname(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = SelfPlayer(info);
            if (!self || info.Length() != 1 || !info[0]->IsString()) {
                ScriptArgs::Throw(isolate, "Player.setNickname(nickname) expects text");
                return;
            }
            auto *player = self->ResolvePlayer();
            if (!player) {
                info.GetReturnValue().SetNull();
                return;
            }
            const std::string previous = player->nickname;
            const std::string applied  = GetServer().Players().SetNickname(self->GetId(), v8pp::from_v8<std::string>(isolate, info[0]));
            if (applied != previous) {
                Features::Script::EmitNicknameChange(self->GetId(), previous, applied);
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, applied));
        }

        void JS_SetModel(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = SelfPlayer(info);
            if (!self || info.Length() != 1 || !info[0]->IsString()) {
                ScriptArgs::Throw(isolate, "Player.setModel(model) expects an .i3d filename");
                return;
            }
            const std::string model = v8pp::from_v8<std::string>(isolate, info[0]);
            const std::string previous = self->GetModel();
            const bool changed = GetServer().Players().SetModel(self->GetId(), model);
            if (changed && previous != model) {
                Features::Script::EmitModelChange(self->GetId(), previous, model);
            }
            info.GetReturnValue().Set(changed);
        }

        void ReturnMoneyResult(const v8::FunctionCallbackInfo<v8::Value> &info, Player *self, uint32_t previous, bool accepted) {
            if (accepted) {
                if (auto *player = self->ResolvePlayer(); player && player->money != previous) {
                    Features::Script::EmitMoneyChange(self->GetId(), previous, player->money);
                }
            }
            info.GetReturnValue().Set(accepted);
        }

        void JS_SetMoney(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self = SelfPlayer(info);
            uint32_t amount = 0;
            if (!self || info.Length() != 1 || !ScriptArgs::ReadUInt(info[0], Shared::Entities::PlayerEntity::kMaxMoney, amount)) {
                ScriptArgs::Throw(isolate, "Player.setMoney(amount) expects an integer from 0 to 1000000000");
                return;
            }
            const auto *player = self->ResolvePlayer();
            const uint32_t previous = player ? player->money : 0;
            ReturnMoneyResult(info, self, previous, GetServer().Players().SetMoney(self->GetId(), amount));
        }

        void JS_GiveMoney(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self = SelfPlayer(info);
            const double amount = info.Length() == 1 && info[0]->IsNumber() ? info[0].As<v8::Number>()->Value() : std::numeric_limits<double>::quiet_NaN();
            if (!self || !std::isfinite(amount) || std::trunc(amount) != amount || std::abs(amount) > Shared::Entities::PlayerEntity::kMaxMoney) {
                ScriptArgs::Throw(isolate, "Player.giveMoney(amount) expects an integer from -1000000000 to 1000000000");
                return;
            }
            const auto *player = self->ResolvePlayer();
            const uint32_t previous = player ? player->money : 0;
            ReturnMoneyResult(info, self, previous, GetServer().Players().GiveMoney(self->GetId(), static_cast<int32_t>(amount)));
        }

        void JS_TrySpendMoney(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self = SelfPlayer(info);
            uint32_t cost = 0;
            if (!self || info.Length() != 1 || !ScriptArgs::ReadUInt(info[0], Shared::Entities::PlayerEntity::kMaxMoney, cost)) {
                ScriptArgs::Throw(isolate, "Player.trySpendMoney(cost) expects an integer from 0 to 1000000000");
                return;
            }
            const auto *player = self->ResolvePlayer();
            const uint32_t previous = player ? player->money : 0;
            ReturnMoneyResult(info, self, previous, GetServer().Players().TrySpendMoney(self->GetId(), cost));
        }

        // spawn and respawn: (position | x, y, z, heading?)
        void SpawnLife(const v8::FunctionCallbackInfo<v8::Value> &info, bool respawn) {
            auto *isolate = info.GetIsolate();
            auto *self    = SelfPlayer(info);
            glm::vec3 position;
            int next      = 0;
            float heading = 0.0f;
            if (!self || !ScriptArgs::ReadPosition(info, 0, position, next) || info.Length() > next + 1 || (info.Length() == next + 1 && !ScriptArgs::ReadFloat(info[next], heading))) {
                ScriptArgs::Throw(isolate, respawn ? "Player.respawn(position, heading?) expects a finite position and heading in radians"
                                                   : "Player.spawn(position, heading?) expects a finite position and heading in radians");
                return;
            }
            auto &server       = GetServer();
            const uint64_t id  = self->GetId();
            const auto *before = server.Players().FindByNetworkId(id);
            const uint64_t previousGeneration = before ? before->spawnGeneration : 0;
            const bool spawned = server.AllPlayersReady() &&
                                 (respawn ? server.Players().Respawn(id, position, heading, server.MissionGeneration()) : server.Players().Spawn(id, position, heading, server.MissionGeneration()));
            if (spawned) {
                // A new generation leaves the car; its old seat must not stay reserved.
                server.Cars().ClearOccupant(id, previousGeneration);
                server.Combat().OnPlayerSpawn(id);
                Features::Script::EmitPlayerStateEvent(server, respawn ? "playerRespawn" : "playerSpawn", *server.Players().FindByNetworkId(id));
            }
            info.GetReturnValue().Set(spawned);
        }

        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            SpawnLife(info, false);
        }

        void JS_Respawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            SpawnLife(info, true);
        }

        void JS_GetSuggestedSpawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = SelfPlayer(info);
            glm::vec3 position;
            if (!self || !GetServer().Players().SuggestedSpawnPosition(self->GetId(), position)) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(Framework::Scripting::Builtins::Vector3::GetClass(isolate).import_external(isolate, new Framework::Scripting::Builtins::Vector3(position)));
        }

        void JS_SendMessage(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto *self     = SelfPlayer(info);
            uint32_t color = 0;
            if (!self || (info.Length() != 1 && info.Length() != 2) || !info[0]->IsString() || (info.Length() == 2 && !ScriptArgs::ReadColor(info[1], color))) {
                ScriptArgs::Throw(isolate, "Player.sendMessage(text, color?) expects text and an optional 0xRRGGBB color");
                return;
            }
            info.GetReturnValue().Set(GetServer().SendNotice(self->GetId(), v8pp::from_v8<std::string>(isolate, info[0]), color));
        }

        void JS_SetNametag(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = SelfPlayer(info);
            if (!self || info.Length() != 1 || !info[0]->IsObject()) {
                ScriptArgs::Throw(isolate, "Player.setNametag({visible?, showHealth?, color?, text?}) expects an object");
                return;
            }
            auto context = isolate->GetCurrentContext();
            auto options = info[0].As<v8::Object>();
            auto *player = self->ResolvePlayer();
            if (!player) {
                info.GetReturnValue().Set(false);
                return;
            }
            auto nametag   = player->nametag;
            const auto get = [&](const char *name) {
                v8::Local<v8::Value> value;
                return options->Get(context, v8pp::to_v8(isolate, name)).ToLocal(&value) ? value : v8::Undefined(isolate).As<v8::Value>();
            };
            if (auto value = get("visible"); !value->IsUndefined()) {
                if (!value->IsBoolean()) {
                    ScriptArgs::Throw(isolate, "Player.setNametag: visible must be a boolean");
                    return;
                }
                nametag.Set(NametagComponent::Name, value->BooleanValue(isolate));
            }
            if (auto value = get("showHealth"); !value->IsUndefined()) {
                if (!value->IsBoolean()) {
                    ScriptArgs::Throw(isolate, "Player.setNametag: showHealth must be a boolean");
                    return;
                }
                nametag.Set(NametagComponent::Health, value->BooleanValue(isolate));
            }
            if (auto value = get("color"); !value->IsUndefined()) {
                uint32_t rgb = 0;
                if (!ScriptArgs::ReadUInt(value, 0xffffff, rgb)) {
                    ScriptArgs::Throw(isolate, "Player.setNametag: color must be 0xRRGGBB");
                    return;
                }
                nametag.color = 0xff000000u | rgb;
            }
            if (auto value = get("text"); !value->IsUndefined()) {
                if (!value->IsString()) {
                    ScriptArgs::Throw(isolate, "Player.setNametag: text must be a string");
                    return;
                }
                nametag.text = Shared::Chat::SanitizeLine(v8pp::from_v8<std::string>(isolate, value), Shared::Chat::kMaxNicknameCodePoints * 2);
            }
            player->nametag = nametag;
            info.GetReturnValue().Set(true);
        }

        void JS_GetVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self   = SelfPlayer(info);
            auto *player = self ? self->ResolvePlayer() : nullptr;
            const auto seat = player ? GetServer().Cars().SeatForPlayer(player->GetNetworkID(), player->spawnGeneration) : std::nullopt;
            info.GetReturnValue().Set(seat ? WrapVehicle(info.GetIsolate(), seat->carId) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_GetCameraTarget(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = SelfPlayer(info);
            const auto *player = self ? self->ResolvePlayer() : nullptr;
            auto *target = player && player->cameraTargetId ? GetServer().Players().FindByNetworkId(player->cameraTargetId) : nullptr;
            info.GetReturnValue().Set(target ? WrapPlayer(info.GetIsolate(), target->GetNetworkID()) : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_SetCameraTarget(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self = SelfPlayer(info);
            const uint64_t targetId = info.Length() == 1 && !info[0]->IsNull() ? ScriptArgs::ReadEntityId(isolate, info[0]) : 0;
            if (!self || info.Length() != 1 || (!info[0]->IsNull() && targetId == 0)) {
                ScriptArgs::Throw(isolate, "Player.setCameraTarget(target) expects a Player or null");
                return;
            }
            info.GetReturnValue().Set(GetServer().Players().SetCameraTarget(self->GetId(), targetId));
        }

        void JS_GetSeat(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self   = SelfPlayer(info);
            auto *player = self ? self->ResolvePlayer() : nullptr;
            const auto seat = player ? GetServer().Cars().SeatForPlayer(player->GetNetworkID(), player->spawnGeneration) : std::nullopt;
            info.GetReturnValue().Set(seat ? v8::Uint32::New(info.GetIsolate(), seat->seat).As<v8::Value>() : v8::Null(info.GetIsolate()).As<v8::Value>());
        }

        void JS_PutInVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate  = info.GetIsolate();
            auto *self     = SelfPlayer(info);
            const uint64_t carId = info.Length() >= 1 ? ScriptArgs::ReadEntityId(isolate, info[0]) : 0;
            uint32_t seat  = 0;
            if (!self || carId == 0 || info.Length() > 2 || (info.Length() == 2 && !ScriptArgs::ReadUInt(info[1], Shared::Entities::CarEntity::kMaxSeats - 1, seat))) {
                ScriptArgs::Throw(isolate, "Player.putInVehicle(vehicle, seat?) expects a Vehicle and a seat from 0 to 7");
                return;
            }
            info.GetReturnValue().Set(Vehicle::RecordSeatOutcome(carId, static_cast<uint8_t>(seat), self->GetId(), Shared::Entities::CarEntity::SeatResult::Entered));
        }

        void JS_RemoveFromVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self   = SelfPlayer(info);
            auto *player = self ? self->ResolvePlayer() : nullptr;
            const auto seat = player ? GetServer().Cars().SeatForPlayer(player->GetNetworkID(), player->spawnGeneration) : std::nullopt;
            info.GetReturnValue().Set(seat && Vehicle::RecordSeatOutcome(seat->carId, seat->seat, self->GetId(), Shared::Entities::CarEntity::SeatResult::Exited));
        }

        // giveWeapon(weaponId, loaded?, reserve?, equip?)
        void JS_GiveWeapon(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate     = info.GetIsolate();
            auto *self        = SelfPlayer(info);
            uint32_t weaponId = 0, loaded = 0, reserve = 0;
            const bool known  = info.Length() >= 1 && ScriptArgs::ReadUInt(info[0], Shared::Combat::kWeaponSlots - 1, weaponId) && Weapon::IsSupported(weaponId);
            if (!self || !known || info.Length() > 4 || (info.Length() >= 2 && !ScriptArgs::ReadUInt(info[1], std::numeric_limits<uint16_t>::max(), loaded)) ||
                (info.Length() >= 3 && !ScriptArgs::ReadUInt(info[2], std::numeric_limits<uint16_t>::max(), reserve)) || (info.Length() == 4 && !info[3]->IsBoolean())) {
                ScriptArgs::Throw(isolate, "Player.giveWeapon(weaponId, loaded?, reserve?, equip?) expects a supported weapon ID and ammunition from 0 to 65535");
                return;
            }
            if (info.Length() < 2) {
                loaded = Weapon::DefaultLoaded(static_cast<uint8_t>(weaponId));
            }
            bool given = Combat().GiveWeapon(self->GetId(), static_cast<uint8_t>(weaponId), static_cast<uint16_t>(loaded), static_cast<uint16_t>(reserve));
            if (given && info.Length() == 4 && info[3]->BooleanValue(isolate)) {
                given = Combat().SelectWeapon(self->GetId(), static_cast<uint8_t>(weaponId));
            }
            info.GetReturnValue().Set(given);
        }

        void JS_DropWeapon(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *self = SelfPlayer(info);
            uint32_t requested = 0;
            const bool explicitWeapon = info.Length() == 1 && !info[0]->IsUndefined();
            if (!self || info.Length() > 1 || (explicitWeapon &&
                (!ScriptArgs::ReadUInt(info[0], Shared::Combat::kWeaponSlots - 1, requested) || !Weapon::IsSupported(requested)))) {
                ScriptArgs::Throw(isolate, "Player.dropWeapon(weaponId?) expects a supported weapon ID when supplied");
                return;
            }
            const auto *state = LivingCombat(self->GetId());
            if (!state) {
                info.GetReturnValue().SetNull();
                return;
            }
            const uint8_t weaponId = explicitWeapon ? static_cast<uint8_t>(requested) : state->selectedWeapon;
            info.GetReturnValue().Set(WrapPickup(isolate, Combat().DropWeapon(self->GetId(), weaponId)));
        }

        void JS_GetWeapons(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate      = info.GetIsolate();
            auto *self         = SelfPlayer(info);
            const auto *combat = self ? Combat().GetState(self->GetId()) : nullptr;
            if (!combat || !combat->spawned) {
                info.GetReturnValue().Set(v8::Array::New(isolate));
                return;
            }
            info.GetReturnValue().Set(WeaponList(isolate, isolate->GetCurrentContext(), *combat));
        }

        void JS_GetInventory(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate      = info.GetIsolate();
            auto *self         = SelfPlayer(info);
            const auto *combat = self ? Combat().GetState(self->GetId()) : nullptr;
            if (!combat || !combat->spawned) {
                info.GetReturnValue().SetNull();
                return;
            }
            auto context   = isolate->GetCurrentContext();
            auto inventory = v8::Object::New(isolate);
            SetField(isolate, context, inventory, "selected", v8::Uint32::New(isolate, combat->selectedWeapon));
            SetField(isolate, context, inventory, "weapons", WeaponList(isolate, context, *combat));
            info.GetReturnValue().Set(inventory);
        }

        // setInventory([{weaponId, loaded?, reserve?}], selected?) replaces the
        // whole inventory; an entry without ammo gets the stock default.
        void JS_SetInventory(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate     = info.GetIsolate();
            auto *self        = SelfPlayer(info);
            uint32_t selected = 0;
            if (!self || info.Length() < 1 || info.Length() > 2 || !info[0]->IsArray() ||
                (info.Length() == 2 && !ScriptArgs::ReadUInt(info[1], Shared::Combat::kWeaponSlots - 1, selected))) {
                ScriptArgs::Throw(isolate, "Player.setInventory(weapons, selected?) expects an array of {weaponId, loaded?, reserve?} and an optional weapon ID");
                return;
            }
            auto context = isolate->GetCurrentContext();
            auto array   = info[0].As<v8::Array>();
            std::vector<Features::Combat::CombatService::InventoryEntry> entries;
            const auto field = [&](v8::Local<v8::Object> object, const char *name, uint32_t fallback, uint32_t max, uint32_t &out) {
                v8::Local<v8::Value> value;
                if (!object->Get(context, v8pp::to_v8(isolate, name)).ToLocal(&value) || value->IsUndefined()) {
                    out = fallback;
                    return true;
                }
                return ScriptArgs::ReadUInt(value, max, out);
            };
            for (uint32_t i = 0; i < array->Length(); ++i) {
                v8::Local<v8::Value> value;
                uint32_t weaponId = 0, loaded = 0, reserve = 0;
                if (!array->Get(context, i).ToLocal(&value) || !value->IsObject() || !field(value.As<v8::Object>(), "weaponId", 0xffffffffu, Shared::Combat::kWeaponSlots - 1, weaponId) ||
                    !Weapon::IsSupported(weaponId) || !field(value.As<v8::Object>(), "loaded", Weapon::DefaultLoaded(static_cast<uint8_t>(weaponId)), std::numeric_limits<uint16_t>::max(), loaded) ||
                    !field(value.As<v8::Object>(), "reserve", 0, std::numeric_limits<uint16_t>::max(), reserve)) {
                    ScriptArgs::Throw(isolate, "Player.setInventory: every entry needs a supported weaponId and ammunition from 0 to 65535");
                    return;
                }
                entries.push_back({static_cast<uint8_t>(weaponId), static_cast<uint16_t>(loaded), static_cast<uint16_t>(reserve)});
            }
            const auto selection = info.Length() == 2 ? std::optional<uint8_t>(static_cast<uint8_t>(selected)) : std::nullopt;
            info.GetReturnValue().Set(Combat().ReplaceInventory(self->GetId(), entries, selection));
        }

        // addItem(weaponId, loaded?, reserve?): grants one item, or adds to the
        // ammunition (a throwable's count) of a weapon already held.
        void JS_AddItem(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate     = info.GetIsolate();
            auto *self        = SelfPlayer(info);
            uint32_t weaponId = 0, loaded = 0, reserve = 0;
            const bool known  = info.Length() >= 1 && ScriptArgs::ReadUInt(info[0], Shared::Combat::kWeaponSlots - 1, weaponId) && Weapon::IsSupported(weaponId);
            if (!self || !known || info.Length() > 3 || (info.Length() >= 2 && !ScriptArgs::ReadUInt(info[1], std::numeric_limits<uint16_t>::max(), loaded)) ||
                (info.Length() == 3 && !ScriptArgs::ReadUInt(info[2], std::numeric_limits<uint16_t>::max(), reserve))) {
                ScriptArgs::Throw(isolate, "Player.addItem(weaponId, loaded?, reserve?) expects a supported weapon ID and ammunition from 0 to 65535");
                return;
            }
            if (info.Length() < 2) {
                loaded = Weapon::DefaultLoaded(static_cast<uint8_t>(weaponId));
            }
            const uint64_t id = self->GetId();
            const auto *state = LivingCombat(id);
            if (!state) {
                info.GetReturnValue().Set(false);
                return;
            }
            if (state->inventoryMask & (1u << weaponId)) {
                const auto &ammo = state->ammo[weaponId];
                loaded           = std::min<uint32_t>(ammo.loaded + loaded, std::numeric_limits<uint16_t>::max());
                reserve          = std::min<uint32_t>(ammo.reserve + reserve, std::numeric_limits<uint16_t>::max());
                info.GetReturnValue().Set(Combat().SetWeaponAmmo(id, static_cast<uint8_t>(weaponId), static_cast<uint16_t>(loaded), static_cast<uint16_t>(reserve)));
                return;
            }
            info.GetReturnValue().Set(Combat().GiveWeapon(id, static_cast<uint8_t>(weaponId), static_cast<uint16_t>(loaded), static_cast<uint16_t>(reserve)));
        }

        // removeItem(weaponId, count?): without a count removes the item; with
        // one takes that many rounds or throwables, reserve first, and removes
        // the item once nothing is left.
        void JS_RemoveItem(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate     = info.GetIsolate();
            auto *self        = SelfPlayer(info);
            uint32_t weaponId = 0, count = 0;
            if (!self || info.Length() < 1 || info.Length() > 2 || !ScriptArgs::ReadUInt(info[0], Shared::Combat::kWeaponSlots - 1, weaponId) ||
                (info.Length() == 2 && !ScriptArgs::ReadUInt(info[1], std::numeric_limits<uint32_t>::max(), count))) {
                ScriptArgs::Throw(isolate, "Player.removeItem(weaponId, count?) expects a weapon ID and an optional nonnegative count");
                return;
            }
            const uint64_t id = self->GetId();
            const auto *state = LivingCombat(id);
            if (!state || (state->inventoryMask & (1u << weaponId)) == 0) {
                info.GetReturnValue().Set(false);
                return;
            }
            const auto &ammo = state->ammo[weaponId];
            if (info.Length() == 2 && count < static_cast<uint32_t>(ammo.loaded) + ammo.reserve) {
                const uint32_t fromReserve = std::min<uint32_t>(count, ammo.reserve);
                const uint32_t fromLoaded  = count - fromReserve;
                info.GetReturnValue().Set(Combat().SetWeaponAmmo(id, static_cast<uint8_t>(weaponId), static_cast<uint16_t>(ammo.loaded - fromLoaded), static_cast<uint16_t>(ammo.reserve - fromReserve)));
                return;
            }
            if (state->selectedWeapon == weaponId) {
                (void)Combat().SelectWeapon(id, 0);
            }
            info.GetReturnValue().Set(Combat().RemoveWeapon(id, static_cast<uint8_t>(weaponId)));
        }

        void JS_GetCombatState(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate      = info.GetIsolate();
            auto *self         = SelfPlayer(info);
            const auto *combat = self ? Combat().GetState(self->GetId()) : nullptr;
            if (!combat) {
                info.GetReturnValue().SetNull();
                return;
            }
            auto context = isolate->GetCurrentContext();
            auto state   = v8::Object::New(isolate);
            SetField(isolate, context, state, "revision", v8::Uint32::New(isolate, combat->revision));
            SetField(isolate, context, state, "health", v8::Number::New(isolate, combat->health));
            SetField(isolate, context, state, "alive", v8::Boolean::New(isolate, combat->alive));
            SetField(isolate, context, state, "spawned", v8::Boolean::New(isolate, combat->spawned));
            SetField(isolate, context, state, "missionGeneration", v8::Number::New(isolate, static_cast<double>(combat->missionGeneration)));
            SetField(isolate, context, state, "spawnGeneration", v8::Number::New(isolate, static_cast<double>(combat->spawnGeneration)));
            SetField(isolate, context, state, "inventoryMask", v8::Uint32::New(isolate, combat->inventoryMask));
            SetField(isolate, context, state, "selectedWeapon", v8::Uint32::New(isolate, combat->selectedWeapon));
            SetField(isolate, context, state, "deathAnimation", v8::Uint32::New(isolate, combat->deathAnimation));
            SetField(isolate, context, state, "aiming", v8::Boolean::New(isolate, combat->aiming));
            SetField(isolate, context, state, "crouching", v8::Boolean::New(isolate, combat->crouching));
            SetField(isolate, context, state, "aimDirection", PlainVector(isolate, context, {combat->aimDirectionX, combat->aimDirectionY, combat->aimDirectionZ}));
            SetField(isolate, context, state, "poseTargetOffset", PlainVector(isolate, context, {combat->poseTargetOffsetX, combat->poseTargetOffsetY, combat->poseTargetOffsetZ}));
            auto ammo = v8::Array::New(isolate, Shared::Combat::kWeaponSlots);
            for (uint32_t i = 0; i < Shared::Combat::kWeaponSlots; ++i) {
                auto slot = v8::Object::New(isolate);
                SetField(isolate, context, slot, "loaded", v8::Uint32::New(isolate, combat->ammo[i].loaded));
                SetField(isolate, context, slot, "reserve", v8::Uint32::New(isolate, combat->ammo[i].reserve));
                ammo->Set(context, i, slot).Check();
            }
            SetField(isolate, context, state, "ammo", ammo);
            info.GetReturnValue().Set(state);
        }
    } // namespace

    Shared::Entities::PlayerEntity *Player::ResolvePlayer() const {
        return GetServer().Players().FindByNetworkId(_id);
    }

    uint64_t Player::ControllerGuid() const {
        const auto *player = ResolvePlayer();
        return player ? player->controllerGuid : 0;
    }

    std::string Player::GetNickname() const {
        const auto *player = ResolvePlayer();
        return player ? player->nickname : std::string {};
    }

    std::string Player::GetModel() const {
        const auto *player = ResolvePlayer();
        return player ? player->model : std::string {};
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
        return player && player->alive;
    }

    bool Player::IsSpawned() const {
        const auto *player = ResolvePlayer();
        return player && player->spawned;
    }

    double Player::GetMissionGeneration() const {
        const auto *player = ResolvePlayer();
        return player ? static_cast<double>(player->missionGeneration) : 0.0;
    }

    double Player::GetSpawnGeneration() const {
        const auto *player = ResolvePlayer();
        return player ? static_cast<double>(player->spawnGeneration) : 0.0;
    }

    Framework::Scripting::Builtins::Vector3 Player::GetPlayerPosition() const {
        const auto *player = ResolvePlayer();
        return player ? Framework::Scripting::Builtins::Vector3(player->position) : Framework::Scripting::Builtins::Vector3();
    }

    Framework::Scripting::Builtins::Quaternion Player::GetPlayerRotation() const {
        const auto *player = ResolvePlayer();
        return player ? Framework::Scripting::Builtins::Quaternion(player->rotation) : Framework::Scripting::Builtins::Quaternion();
    }

    int32_t Player::GetCurrentWeapon() const {
        const auto *combat = GetServer().Combat().GetState(_id);
        return combat && combat->spawned ? combat->selectedWeapon : 0;
    }

    bool Player::HasWeapon(uint32_t weaponId) const {
        const auto *combat = GetServer().Combat().GetState(_id);
        return weaponId < Shared::Combat::kWeaponSlots && combat && combat->spawned && (combat->inventoryMask & (1u << weaponId)) != 0;
    }

    bool Player::SetHealth(double health) {
        return std::isfinite(health) && GetServer().Combat().SetHealth(_id, static_cast<float>(health));
    }

    bool Player::Despawn() {
        auto &server = GetServer();
        if (auto *player = ResolvePlayer()) {
            server.Cars().ClearOccupant(_id, player->spawnGeneration);
        }
        const bool despawned = server.Players().Despawn(_id);
        if (despawned) {
            server.Combat().OnPlayerDespawn(_id);
            Features::Script::EmitPlayerStateEvent(server, "playerDespawn", *ResolvePlayer());
        }
        return despawned;
    }

    bool Player::RemoveWeapon(uint32_t weaponId) {
        return weaponId < Shared::Combat::kWeaponSlots && GetServer().Combat().RemoveWeapon(_id, static_cast<uint8_t>(weaponId));
    }

    bool Player::RemoveAllWeapons() {
        return ClearWeapons(_id);
    }

    bool Player::SetCurrentWeapon(uint32_t weaponId) {
        return weaponId < Shared::Combat::kWeaponSlots && GetServer().Combat().SelectWeapon(_id, static_cast<uint8_t>(weaponId));
    }

    bool Player::SetWeaponAmmo(uint32_t weaponId, uint32_t loaded, uint32_t reserve) {
        return weaponId < Shared::Combat::kWeaponSlots && loaded <= std::numeric_limits<uint16_t>::max() && reserve <= std::numeric_limits<uint16_t>::max() &&
               GetServer().Combat().SetWeaponAmmo(_id, static_cast<uint8_t>(weaponId), static_cast<uint16_t>(loaded), static_cast<uint16_t>(reserve));
    }

    bool Player::GiveAmmo(uint32_t weaponId, int32_t amount) {
        const auto *state = LivingCombat(_id);
        if (weaponId >= Shared::Combat::kWeaponSlots || !state || (state->inventoryMask & (1u << weaponId)) == 0) {
            return false;
        }
        const auto &ammo      = state->ammo[weaponId];
        const int64_t reserve = std::clamp<int64_t>(static_cast<int64_t>(ammo.reserve) + amount, 0, std::numeric_limits<uint16_t>::max());
        return GetServer().Combat().SetWeaponAmmo(_id, static_cast<uint8_t>(weaponId), ammo.loaded, static_cast<uint16_t>(reserve));
    }

    void Player::KickPlayer(const std::string &reason) {
        if (const uint64_t guid = ControllerGuid()) {
            Framework::CoreModules::GetNetworkPeer()->KickPlayer(MafiaNet::ToGuid(static_cast<MafiaNet::PeerGuid>(guid)),
                                                                 reason.empty() ? Framework::Networking::DisconnectionReason::KICKED : Framework::Networking::DisconnectionReason::KICKED_CUSTOM, reason);
        }
    }

    void Player::EmitToClient(const std::string &eventName, const std::string &payloadJson) {
        const uint64_t guid = ControllerGuid();
        if (eventName.empty() || guid == 0) {
            return;
        }
        Framework::Integrations::Shared::RPC::EmitScriptEvent event;
        event.FromParameters(eventName, payloadJson);
        Framework::CoreModules::GetNetworkPeer()->SendRPC(event, MafiaNet::ToGuid(static_cast<MafiaNet::PeerGuid>(guid)));
    }

    int Player::GetConnectionPing() const {
        const uint64_t guid = ControllerGuid();
        return guid ? Framework::CoreModules::GetNetworkPeer()->GetPing(MafiaNet::ToGuid(static_cast<MafiaNet::PeerGuid>(guid))) : -1;
    }

    std::string Player::GetConnectionAddress() const {
        const uint64_t guid = ControllerGuid();
        return guid ? Framework::CoreModules::GetNetworkPeer()->GetAddress(MafiaNet::ToGuid(static_cast<MafiaNet::PeerGuid>(guid))) : std::string {};
    }

    std::string Player::GetConnectionSteamId() const {
        const uint64_t guid = ControllerGuid();
        const auto *identity = guid ? Framework::CoreModules::GetNetworkPeer()->GetPeerIdentity(MafiaNet::ToGuid(static_cast<MafiaNet::PeerGuid>(guid))) : nullptr;
        return identity ? identity->steamId : std::string {};
    }

    std::string Player::GetConnectionDiscordId() const {
        const uint64_t guid = ControllerGuid();
        const auto *identity = guid ? Framework::CoreModules::GetNetworkPeer()->GetPeerIdentity(MafiaNet::ToGuid(static_cast<MafiaNet::PeerGuid>(guid))) : nullptr;
        return identity ? identity->discordId : std::string {};
    }

    std::string Player::GetConnectionHardwareId() const {
        const uint64_t guid = ControllerGuid();
        const auto *identity = guid ? Framework::CoreModules::GetNetworkPeer()->GetPeerIdentity(MafiaNet::ToGuid(static_cast<MafiaNet::PeerGuid>(guid))) : nullptr;
        return identity ? identity->hardwareId : std::string {};
    }

    void Player::SetNameVisible(bool visible) {
        if (auto *player = ResolvePlayer()) {
            player->nametag.Set(NametagComponent::Name, visible);
        }
    }

    void Player::SetHealthBarVisible(bool visible) {
        if (auto *player = ResolvePlayer()) {
            player->nametag.Set(NametagComponent::Health, visible);
        }
    }

    void Player::SetLabel(const std::string &text) {
        if (auto *player = ResolvePlayer()) {
            player->nametag.text = Shared::Chat::SanitizeLine(text, Shared::Chat::kMaxNicknameCodePoints * 2);
        }
    }

    void Player::SetLabelColor(uint32_t color) {
        if (auto *player = ResolvePlayer()) {
            player->nametag.color = color | 0xff000000u;
        }
    }

    std::string Player::ToString() const {
        std::ostringstream stream;
        stream << "Player{ id: " << _id << ", nickname: \"" << GetNickname() << "\" }";
        return stream.str();
    }

    v8pp::class_<Player> &Player::GetClass(v8::Isolate *isolate) {
        if (_class) {
            return *_class;
        }
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;
        // v8pp inherit<Player> requires the framework Player registered first.
        Framework::Scripting::Builtins::Player::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Player>>(isolate, ServerCatalog(), "Player",
            "A connected Mafia 1 player. The server owns its life, health, inventory and seat; every change goes through server validation and replicates to all clients.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Player>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for an existing connected player; it does not create a player."));
        cls.function("toString", &Player::ToString, docs("string", {}, "Formats this player for logging.", "The player ID and nickname."));

        cls.property("nickname", &Player::GetNickname, property_docs("string", "Sanitized, unique nickname."));
        cls.property("model", &Player::GetModel, property_docs("string", "Server-selected .i3d human model, retained across spawns and applied to every client."));
        cls.property("health", &Player::GetHealth, property_docs("number", "Server health from 0 to 100."));
        cls.property("money", &Player::GetMoney, property_docs("number", "Server-owned Free Ride balance from 0 to 1000000000. It survives respawns and mission changes until disconnect."));
        cls.property("alive", &Player::IsAlive, property_docs("boolean", "Whether the current life is alive."));
        cls.property("spawned", &Player::IsSpawned, property_docs("boolean", "Whether the player has a current life in the mission."));
        cls.property("missionGeneration", &Player::GetMissionGeneration, property_docs("number", "Mission generation of the current life."));
        cls.property("spawnGeneration", &Player::GetSpawnGeneration, property_docs("number", "Spawn generation; it increases with every spawn, respawn and despawn. Compare it before acting on a saved player."));
        cls.property("position", &Player::GetPlayerPosition, property_docs("Vector3", "Last position the player's client reported. Read only; use spawn or respawn to place a player."));
        cls.property("rotation", &Player::GetPlayerRotation, property_docs("Quaternion", "Last facing the player's client reported. Read only."));
        cls.property("ping", &Player::GetConnectionPing, property_docs("number", "Current round-trip latency in milliseconds, or -1 when unavailable."));
        cls.property("ip", &Player::GetConnectionAddress, property_docs("string", "Current remote network address, or an empty string when unavailable."));
        cls.property("steamId", &Player::GetConnectionSteamId, property_docs("string", "Client-announced Steam identifier, or an empty string when unavailable."));
        cls.property("discordId", &Player::GetConnectionDiscordId, property_docs("string", "Client-announced Discord identifier, or an empty string when unavailable."));
        cls.property("hardwareId", &Player::GetConnectionHardwareId, property_docs("string", "Framework hardware identifier, or an empty string when unavailable."));

        cls.function("kick", &Player::KickPlayer, docs("void", {param("reason", "string", true, "Reason shown to the player.")}, "Disconnects this player."));
        cls.function("emit", &Player::EmitToClient,
            docs("void", {param("eventName", "string", false, "Client event name."), param("payloadJson", "string", true, "JSON payload forwarded to the client.")}, "Emits a named script event to this player's client."));
        cls.function("getIP", &Player::GetConnectionAddress, docs("string", {}, "Returns this player's remote network address.", "Address, or an empty string when unavailable."));
        cls.prototype_function("setNickname", &JS_SetNickname,
            docs("string | null", {param("nickname", "string", false, "Requested nickname.")},
                "Applies the nickname policy: invalid characters are removed, whitespace collapsed, the name cut to 24 characters, and a taken name gets a numbered suffix. Fires playerNicknameChange when the name changes.",
                "The nickname actually used, or null when the player has left."));
        cls.prototype_function("setModel", &JS_SetModel,
            docs("boolean", {param("model", "string", false, "Stock human .i3d filename, 5 to 64 ASCII characters from A-Z, a-z, 0-9, underscore, dash, or dot.")},
                "Selects this player's human model on every client, including during a life. The choice persists across respawns and mission changes. Fires playerModelChange when it changes.",
                "False for an invalid filename or a disconnected player."));
        cls.prototype_function("setNametag", &JS_SetNametag,
            docs("boolean", {param("options", "{ visible?: boolean; showHealth?: boolean; color?: number; text?: string }", false, "Only the given fields change. color is 0xRRGGBB and also colors the name in chat; an empty text restores the nickname.")},
                "Changes this player's nametag.", "False when the player has left."));
        cls.function("setNametagVisible", &Player::SetNameVisible, docs("void", {param("visible", "boolean", false, "Whether other players see this player's name.")}, "Shows or hides this player's nametag."));
        cls.function("setNametagHealthVisible", &Player::SetHealthBarVisible, docs("void", {param("visible", "boolean", false, "Whether the health bar is shown.")}, "Shows or hides the health bar under this player's name."));
        cls.function("setNametagText", &Player::SetLabel, docs("void", {param("text", "string", false, "Label to show instead of the nickname; empty restores it.")}, "Overrides this player's nametag label."));
        cls.function("setNametagColor", &Player::SetLabelColor, docs("void", {param("color", "number", false, "Packed 0xAARRGGBB or 0xRRGGBB color; the alpha is always opaque.")}, "Tints this player's nametag and chat name."));
        cls.prototype_function("sendMessage", &JS_SendMessage,
            docs("boolean", {param("text", "string", false, "Notice text, 1 to 200 characters."), param("color", "number", true, "0xRRGGBB color.")}, "Sends a private chat notice to this player.", "False when the player has left or the text is invalid."));

        cls.prototype_function("spawn", &JS_Spawn,
            docs("boolean", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Spawn position; three numbers x, y, z are accepted too."), param("heading", "number", true, "Facing in radians around the vertical axis; defaults to 0.")},
                "Starts a new life with 100 health and an empty inventory once every client is mission ready. A seat held by the previous life is released and recorded as an exit. Fires playerSpawn.",
                "False before every client is ready, for an invalid position, or when the player has left."));
        cls.prototype_function("respawn", &JS_Respawn,
            docs("boolean", {param("position", "Vector3 | { x: number; y: number; z: number }", false, "Spawn position; three numbers x, y, z are accepted too."), param("heading", "number", true, "Facing in radians; defaults to 0.")},
                "Starts a new life after death, like spawn. The server never respawns anyone by itself. Fires playerRespawn.", "False when the life could not be started."));
        cls.function("despawn", &Player::Despawn, docs("boolean", {}, "Ends the current life and clears its seat. Fires playerDespawn.", "False when the player has left."));
        cls.prototype_function("getSuggestedSpawn", &JS_GetSuggestedSpawn,
            docs("Vector3 | null", {}, "Returns the native scene spawn hint the player's client reported for the current mission.", "The hint, or null before the client reported one."));
        cls.function("setHealth", &Player::SetHealth,
            docs("boolean", {param("health", "number", false, "Health from 0 to 100; values outside are clamped.")},
                "Sets server health, firing playerDamage, playerDeath or playerHealthChange. A dead player cannot be revived this way; use respawn.", "False when there is no living, spawned player."));
        cls.prototype_function("setMoney", &JS_SetMoney,
            docs("boolean", {param("amount", "number", false, "Whole-dollar balance from 0 to 1000000000.")}, "Sets this player's server balance. Fires playerMoneyChange when it changes.", "False if the player has disconnected."));
        cls.prototype_function("giveMoney", &JS_GiveMoney,
            docs("boolean", {param("amount", "number", false, "Signed whole-dollar change from -1000000000 to 1000000000.")},
                "Adds or removes money without exceeding the balance bounds. Fires playerMoneyChange when it changes.", "False if the result would be negative or above the maximum, or the player has disconnected."));
        cls.prototype_function("trySpendMoney", &JS_TrySpendMoney,
            docs("boolean", {param("cost", "number", false, "Whole-dollar cost from 0 to 1000000000.")},
                "Atomically checks and debits this player's server balance. Use the return value to grant shop goods. Fires playerMoneyChange on a nonzero purchase.",
                "False if the player cannot afford the cost or has disconnected."));

        cls.prototype_function("getVehicle", &JS_GetVehicle, docs("Vehicle | null", {}, "Returns the vehicle whose seat the current life holds.", "The vehicle, or null on foot."));
        cls.prototype_function("getCameraTarget", &JS_GetCameraTarget,
            docs("Player | null", {}, "Returns the player this client's camera follows when scripted, or null for the local camera.", "The connected target, or null."));
        cls.prototype_function("setCameraTarget", &JS_SetCameraTarget,
            docs("boolean", {param("target", "Player | null", false, "Connected player to follow, or null to restore the local camera.")},
                "Makes this player's camera follow the target on foot and switch to the target's car while seated. The choice persists across both players' respawns; a missing or despawned target temporarily falls back to the observer.",
                "False when the observer has left or the target is not connected in the same mission."));
        cls.prototype_function("getSeat", &JS_GetSeat, docs("number | null", {}, "Returns the seat the current life holds; 0 is the driver.", "The seat, or null on foot."));
        cls.prototype_function("putInVehicle", &JS_PutInVehicle,
            docs("boolean", {param("vehicle", "Vehicle", false, "Vehicle to seat the player in."), param("seat", "number", true, "Seat from 0 (driver) to 7; defaults to 0.")},
                "Records the player as seated through the server seat path, as if the native entry had completed; it does not play the door animation. Seat 0 makes this player's client the vehicle's simulation controller. Fires vehiclePlayerEntered.",
                "False for a dead or unspawned player, another mission's vehicle, or a seat that is taken or out of range."));
        cls.prototype_function("removeFromVehicle", &JS_RemoveFromVehicle,
            docs("boolean", {}, "Records the player's current seat as exited without the native exit animation. Fires vehiclePlayerExited.", "False when the player is not seated."));

        cls.prototype_function("giveWeapon", &JS_GiveWeapon,
            docs("boolean", {param("weaponId", "number", false, "Supported stock weapon, 2 through 15."), param("loaded", "number", true, "Loaded rounds, or the throwable count; defaults to a full magazine for a firearm and 1 otherwise. A firearm is capped to its magazine."),
                                param("reserve", "number", true, "Reserve rounds; defaults to 0."), param("equip", "boolean", true, "Select the weapon immediately.")},
                "Grants a weapon to a living player.", "False for a dead player, an unsupported weapon or a full inventory."));
        cls.function("giveAmmo", &Player::GiveAmmo,
            docs("boolean", {param("weaponId", "number", false, "A weapon the player holds."), param("amount", "number", false, "Reserve delta; a negative amount removes ammunition down to zero.")}, "Adds reserve ammunition to a held weapon.",
                "False when the weapon is not held."));
        cls.function("setWeaponAmmo", &Player::SetWeaponAmmo,
            docs("boolean", {param("weaponId", "number", false, "A weapon the player holds."), param("loaded", "number", false, "Loaded rounds; a firearm is capped to its magazine."), param("reserve", "number", false, "Reserve rounds.")},
                "Sets the ammunition of a held weapon; it does not grant the weapon.", "False when the weapon is not held."));
        cls.function("removeWeapon", &Player::RemoveWeapon, docs("boolean", {param("weaponId", "number", false, "Weapon to remove.")}, "Removes a held weapon.", "False when it was not held."));
        cls.prototype_function("dropWeapon", &JS_DropWeapon,
            docs("Pickup | null", {param("weaponId", "number", true, "Held weapon to drop; defaults to the selected weapon.")},
                "Moves a held weapon and its ammunition into a replicated pickup at the player's feet. Fires weaponDropped and playerWeaponDrop.",
                "The new pickup, or null if the player is dead, unspawned, seated, has no selected weapon, does not hold the requested weapon, or the pickup limit is full."));
        cls.function("removeAllWeapons", &Player::RemoveAllWeapons, docs("boolean", {}, "Holsters and removes every weapon of a living player.", "False for a dead or unspawned player."));
        cls.function("setCurrentWeapon", &Player::SetCurrentWeapon, docs("boolean", {param("weaponId", "number", false, "A held weapon, or 0 to holster.")}, "Selects a held weapon.", "False when the weapon is not held."));
        cls.function("getCurrentWeapon", &Player::GetCurrentWeapon, docs("number", {}, "Returns the selected weapon.", "The weapon ID, 0 when holstered or not spawned."));
        cls.function("hasWeapon", &Player::HasWeapon, docs("boolean", {param("weaponId", "number", false, "Weapon to test.")}, "Checks whether the current life holds a weapon.", "True when it is held."));
        cls.prototype_function("getWeapons", &JS_GetWeapons, docs("WeaponSlot[]", {}, "Lists the held weapons with their ammunition.", "The held weapons, empty when not spawned."));
        cls.prototype_function("getInventory", &JS_GetInventory, docs("Inventory | null", {}, "Returns the selected weapon and the held weapons.", "The inventory, or null when not spawned."));
        cls.prototype_function("setInventory", &JS_SetInventory,
            docs("boolean", {param("weapons", "{ weaponId: number; loaded?: number; reserve?: number }[]", false, "Complete new inventory, at most seven items. Missing ammunition gets the giveWeapon defaults."),
                                param("selected", "number", true, "Weapon to select afterwards.")},
                "Atomically replaces the whole inventory of a living player in one state revision. Without selected, the first entry becomes equipped; an empty list holsters. Duplicate weapons and more than seven items are rejected without changing the current inventory.",
                "False when the player is not alive, an item repeats, more than seven items are supplied, or selected is not in the new inventory."));
        cls.prototype_function("addItem", &JS_AddItem,
            docs("boolean", {param("weaponId", "number", false, "Supported weapon."), param("loaded", "number", true, "Rounds or count to add; defaults like giveWeapon."), param("reserve", "number", true, "Reserve rounds to add; defaults to 0.")},
                "Adds one item, or adds its ammunition or throwable count to a weapon already held.", "False for a dead player or a full inventory."));
        cls.prototype_function("removeItem", &JS_RemoveItem,
            docs("boolean", {param("weaponId", "number", false, "Held weapon."), param("count", "number", true, "Rounds or throwables to take, reserve first.")},
                "Without count removes the item; with count takes that much and removes the item once none is left.", "False when the weapon is not held."));
        cls.prototype_function("getCombatState", &JS_GetCombatState,
            docs("CombatState | null", {}, "Returns the server combat snapshot of this player.", "The snapshot, or null when the player has none."));
        return cls;
    }

    void Player::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &catalog = ServerCatalog();
        auto &slot    = catalog.data_type("WeaponSlot", "A held weapon and its ammunition.");
        slot.add_property("weaponId", "number", "Stock item identifier.");
        slot.add_property("name", "string", "Item name.");
        slot.add_property("kind", "\"melee\" | \"firearm\" | \"throwable\"", "How the item is used.");
        slot.add_property("magazine", "number", "Rounds per magazine for a firearm; 1 otherwise.");
        slot.add_property("loaded", "number", "Loaded rounds, or the throwable count.");
        slot.add_property("reserve", "number", "Reserve rounds.");
        auto &inventory = catalog.data_type("Inventory", "A player's weapons.");
        inventory.add_property("selected", "number", "Selected weapon; 0 is holstered.");
        inventory.add_property("weapons", "WeaponSlot[]", "Held weapons.");
        auto &combat = catalog.data_type("CombatState", "The server combat snapshot of a player life.");
        combat.add_property("revision", "number", "Server state revision.");
        combat.add_property("health", "number", "Server health.");
        combat.add_property("alive", "boolean", "Life status.");
        combat.add_property("spawned", "boolean", "Whether a life exists.");
        combat.add_property("missionGeneration", "number", "Mission generation of the life.");
        combat.add_property("spawnGeneration", "number", "Spawn generation of the life.");
        combat.add_property("inventoryMask", "number", "Bit n is set when weapon n is held.");
        combat.add_property("selectedWeapon", "number", "Selected weapon; 0 is holstered.");
        combat.add_property("deathAnimation", "number", "Death animation chosen for a dead life.");
        combat.add_property("aiming", "boolean", "Whether the player aims.");
        combat.add_property("crouching", "boolean", "Whether the native human crouches.");
        combat.add_property("aimDirection", "{ x: number; y: number; z: number }", "Unit aim direction.");
        combat.add_property("poseTargetOffset", "{ x: number; y: number; z: number }", "Camera-facing pose target relative to the player position. It updates while aiming, unaimed and unarmed; remote clients add it to the interpolated position to pose the neck and back.");
        combat.add_property("ammo", "{ loaded: number; reserve: number }[]", "Ammunition indexed by weapon ID.");

        auto &cls     = GetClass(isolate);
        auto context  = isolate->GetCurrentContext();
        global->Set(context, v8pp::to_v8(isolate, "Player"), cls.js_function_template()->GetFunction(context).ToLocalChecked()).Check();
    }
} // namespace Mafia1Online::Scripting
