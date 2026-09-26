#include <utils/safe_win32.h>

#include "gameplay_menu_service.h"

#include "features/web_ui/web_ui_service.h"
#include "features/world/world_service.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/input/native_input.h>
#include <mafia1/sdk/menu/native_gameplay_menu.h>
#include <mafia1/sdk/player/native_human.h>
#include <mafia1/sdk/seat/native_seat.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <span>

namespace Mafia1Online::Features::GameplayMenu {
    namespace {
        using namespace SDK::Menu;
        using Inventory = SDK::Player::NativeInventory;
        using Item      = SDK::Player::NativeGameItem;

        std::string Text(uint32_t id) {
            const char *text = GameplayText(id);
            const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
            if (length <= 1)
                return {};
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), length);
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), length - 1, nullptr, 0, nullptr, nullptr);
            std::string result(static_cast<size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), length - 1, result.data(), bytes, nullptr, nullptr);
            return result;
        }

        template <typename F>
        void InventoryItems(Inventory &inventory, F &&visit) {
            visit(inventory._selected, 0u, 0u);
            for (uint32_t i = 0; i < std::size(inventory._weapons); ++i) visit(inventory._weapons[i], 1u, i);
            visit(inventory._coatWeapon, 2u, 0u);
            for (uint32_t i = 0; i < std::min<uint32_t>(inventory._itemCount, std::size(inventory._items)); ++i) visit(inventory._items[i], 3u, i);
        }
    } // namespace

    void GameplayMenuService::Install(World::WorldService &world, WebUi::WebUiService &ui) {
        _world = &world;
        _ui    = &ui;
    }

    void GameplayMenuService::Reset() {
        if (_ui)
            _ui->CloseGameplayMenu();
        _openId  = 0;
        _player  = {};
        _vehicle = 0;
        _mission = 0;
        _spawn   = 0;
        _choices.clear();
        _path.clear();
        _automaticPath.clear();
        _replay.clear();
        _replayIndex = 0;
        _injected    = false;
        _failed      = false;
        // Release the opening/confirmation key before the next interaction.
        _waitForRelease = true;
    }

    bool GameplayMenuService::CaptureContext() {
        auto *player = SDK::Player::CurrentPlayer();
        if (!_world || !_world->IsReady() || !player)
            return false;
        _player           = _world->NativeObjects().FindByNative(player);
        auto *replication = Framework::CoreModules::GetReplication();
        auto *state       = replication ? replication->GetEntity<Shared::Entities::PlayerEntity>(_player.networkId) : nullptr;
        if (!state || !state->spawned || !state->alive)
            return false;
        const auto *human = reinterpret_cast<const SDK::Seat::NativeHuman *>(player);
        _vehicle          = reinterpret_cast<uintptr_t>(human->usedActorEnter);
        _seat             = human->seatId;
        _vehicleEngine    = human->usedActorEnter && human->usedActorEnter->GetType() == SDK::Player::NativeActor::Type::Car && static_cast<const SDK::Car::NativeCar *>(human->usedActorEnter)->EngineOn();
        _mission          = _world->LoadedMissionGeneration();
        _spawn            = state->spawnGeneration;
        return true;
    }

    bool GameplayMenuService::ContextValid() const {
        if (!_world || !_world->IsReady() || _mission != _world->LoadedMissionGeneration() || !_player.object || _world->NativeObjects().Resolve(_player) != SDK::Player::CurrentPlayer())
            return false;
        auto *replication = Framework::CoreModules::GetReplication();
        auto *state       = replication ? replication->GetEntity<Shared::Entities::PlayerEntity>(_player.networkId) : nullptr;
        if (!state || !state->spawned || !state->alive || state->spawnGeneration != _spawn)
            return false;
        const auto *human = static_cast<const SDK::Seat::NativeHuman *>(_player.object);
        if (human->actor.IsDead() || reinterpret_cast<uintptr_t>(human->usedActorEnter) != _vehicle || human->seatId != _seat)
            return false;
        if (_kind == Kind::Interaction && human->usedActorEnter && human->usedActorEnter->GetType() == SDK::Player::NativeActor::Type::Car && static_cast<const SDK::Car::NativeCar *>(human->usedActorEnter)->EngineOn() != _vehicleEngine)
            return false;
        return true;
    }

    bool GameplayMenuService::Matches(const Choice &a, const Choice &b) const {
        return a.itemId == b.itemId && a.action == b.action && a.slot == b.slot && a.index == b.index && a.object == b.object && a.source == b.source && a.target.networkId == b.target.networkId && a.target.generation == b.target.generation;
    }

    std::vector<GameplayMenuService::Row> GameplayMenuService::ReadRows(SDK::Menu::NativeMenu *base, Kind kind) const {
        std::vector<Row> rows;
        const auto inventoryRow = [&](Inventory &inventory, Item &item, uint32_t slot, uint32_t index, bool own) {
            if (!item.itemId)
                return;
            Choice choice;
            choice.itemId = item.itemId;
            choice.slot   = slot;
            choice.index  = index;
            choice.source = reinterpret_cast<uintptr_t>(&inventory);
            choice.label  = Text(3500 + item.itemId);
            if (choice.label.empty() || choice.label == " ")
                choice.label = "Item " + std::to_string(item.itemId);
            if (SDK::Player::StockItemFlags(item.itemId) & SDK::Player::kFirearmItemFlag) {
                choice.detail = std::to_string(std::max(0, item.ammoLoaded)) + " / " + std::to_string(std::max(0, item.ammoReserve));
            }
            if (own && slot == 0)
                choice.detail = "In hand" + (choice.detail.empty() ? std::string() : " · " + choice.detail);
            choice.canSelect = !own || slot != 3 || !(SDK::Player::StockItemFlags(item.itemId) & SDK::Player::kNoAutoSelectItemFlag);
            choice.canDrop   = own && slot != 4 && choice.canSelect;
            if (!own) {
                // Corpse inventories are C_human::m_Inventory, verified +0x480.
                choice.target = _world->NativeObjects().FindByNative(reinterpret_cast<std::byte *>(&inventory) - offsetof(SDK::Player::NativeHuman, _inventory));
            }
            rows.push_back({std::move(choice), &item});
        };
        if (kind == Kind::Inventory) {
            auto &inventory = *reinterpret_cast<NativeInventoryMenu *>(base)->inventory;
            InventoryItems(inventory, [&](Item &item, uint32_t slot, uint32_t index) {
                inventoryRow(inventory, item, slot, index, true);
            });
            inventoryRow(inventory, inventory._pickupItem, 4, 0, true);
        }
        else {
            auto *menu = reinterpret_cast<NativePickupMenu *>(base);
            for (auto *item = menu->gameItems->begin; item != menu->gameItems->end; ++item) {
                Choice choice;
                choice.itemId = item->itemId;
                choice.action = item->itemId == 1 ? item->ammoLoaded : 0;
                choice.object = reinterpret_cast<uintptr_t>(item->usingObject);
                if (auto *object = item->usingObject) {
                    choice.source = reinterpret_cast<uintptr_t>(object->actor ? static_cast<void *>(object->actor) : object->frame);
                    choice.target = _world->NativeObjects().FindByNative(object->actor);
                }
                else {
                    const auto *human = reinterpret_cast<const SDK::Seat::NativeHuman *>(SDK::Player::CurrentPlayer());
                    choice.source     = human ? reinterpret_cast<uintptr_t>(human->usedActorEnter) : 0;
                }
                choice.label = Text(item->itemId == 1 ? item->ammoLoaded : 3500 + item->itemId);
                if (choice.label.empty() || choice.label == " ")
                    choice.label = "Use item " + std::to_string(item->itemId);
                rows.push_back({std::move(choice), item});
            }
            for (auto *inventory : menu->inventories->Items()) {
                // Retail BuildEnabledItemIdList actually collects addresses,
                // not IDs: LEA at 0x609be0/0x609bff/0x609c58. Preserve its order.
                InventoryItems(*inventory, [&](Item &item, uint32_t slot, uint32_t index) {
                    inventoryRow(*inventory, item, slot, index, false);
                });
            }
        }
        return rows;
    }

    uint32_t GameplayMenuService::Execute(SDK::Menu::NativeMenu *base, Kind kind, const std::vector<Row> &rows, size_t index, bool drop) {
        const auto &row = rows[index];
        if (kind == Kind::Inventory) {
            auto *menu = reinterpret_cast<NativeInventoryMenu *>(base);
            if (drop)
                menu->Drop(static_cast<Item *>(row.item));
            else
                menu->Select(row.choice.index, row.choice.slot);
            return kInventoryCancelled;
        }
        std::vector<void *> addresses;
        for (const auto &entry : rows) addresses.push_back(entry.item);
        return reinterpret_cast<NativePickupMenu *>(base)->Select(addresses, static_cast<uint32_t>(index));
    }

    void GameplayMenuService::Open(Kind kind, const std::vector<Row> &rows) {
        if (!CaptureContext())
            return;
        _kind = kind;
        if (_injected)
            _path.assign(_replay.begin(), _replay.begin() + _replayIndex);
        else
            _path = kind == Kind::Interaction ? _automaticPath : std::vector<Step>();
        _choices.clear();
        nlohmann::json choices = nlohmann::json::array();
        for (const auto &row : rows) {
            if (_choices.size() == 63)
                break;
            choices.push_back({{"label", row.choice.label}, {"detail", row.choice.detail}, {"canSelect", row.choice.canSelect}, {"canDrop", row.choice.canDrop}});
            _choices.push_back(row.choice);
        }
        _openId = ++_nextId;
        if (!_ui->OpenGameplayMenu(_openId, {{"id", _openId}, {"kind", kind == Kind::Inventory ? "inventory" : "interaction"}, {"title", kind == Kind::Inventory ? "Inventory" : "Choose an action"}, {"choices", choices}})) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Gameplay menu unavailable; cancelled without pausing the mission");
            Reset();
        }
        else {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Gameplay menu {} opened with {} choices", _openId, _choices.size());
        }
    }

    std::optional<uint32_t> GameplayMenuService::HandleMenu(SDK::Menu::NativeMenu *menu) {
        if (!_world || !menu)
            return std::nullopt;
        const bool inventory = menu->_vtable == reinterpret_cast<void *>(kInventoryVtable);
        if (!inventory && menu->_vtable != reinterpret_cast<void *>(kItemPickupVtable))
            return std::nullopt;
        const auto kind = inventory ? Kind::Inventory : Kind::Interaction;
        uint32_t result = inventory ? kInventoryCancelled : kPickupCancelled;
        if (!_world->IsReady()) {
            menu->Destroy();
            return result;
        }
        const auto rows = ReadRows(menu, kind);
        if (_injected && !_failed && _replayIndex < _replay.size()) {
            const auto &step = _replay[_replayIndex];
            const auto it    = std::find_if(rows.begin(), rows.end(), [&](const Row &row) {
                return Matches(step.choice, row.choice);
            });
            if (ContextValid() && step.kind == kind && it != rows.end() && it->choice.canSelect && (!step.choice.drop || it->choice.canDrop)) {
                const bool drop = step.choice.drop;
                ++_replayIndex;
                result = Execute(menu, kind, rows, static_cast<size_t>(it - rows.begin()), drop);
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Gameplay menu selection applied: item {}, action {}, drop {}", step.choice.itemId, step.choice.action, drop);
            }
            else {
                _failed = true;
            }
        }
        else if (!_failed && !_openId) {
            // Single native interactions already execute immediately in retail.
            // Never enter ExecuteMenu's loop, even when CEF is unavailable.
            if (!inventory && rows.size() == 1 && _replay.empty() && !(SDK::Core::Mission::Get()->Game()->_gameFlags & 2u)) {
                // A single corpse-use action can immediately open a second
                // menu. Its later response must replay this outer step too.
                _automaticPath.push_back({kind, rows[0].choice});
                result = Execute(menu, kind, rows, 0, false);
            }
            else if (inventory || !rows.empty())
                Open(kind, rows);
        }
        menu->Destroy();
        // The caller's remaining AI must not fire using the click that opened
        // or selected a menu. This leaves physics and the human's AI ticking.
        if (inventory || _openId || rows.size() != 1 || _injected) {
            ClearInput(&SDK::Input::Instance());
            _waitForRelease = true;
        }
        return result;
    }

    void GameplayMenuService::ClearInput(void *input) const {
        for (bool pressed : {false, true}) {
            float *state    = nullptr;
            const int count = SDK::Input::GetState(input, &state, pressed);
            if (count > 0)
                std::fill_n(state, count, 0.0f);
        }
    }

    void GameplayMenuService::FilterInput(void *input) {
        if (!_world || !_world->IsReady())
            return;
        if (!_replay.empty() && !_injected) {
            if (!ContextValid() || _ui->HidesKeyboard()) {
                Reset();
                return;
            }
            ClearInput(input);
            const int control = _replay.front().kind == Kind::Inventory ? kInventoryControl : kInteractControl;
            for (bool pressed : {false, true}) {
                float *state    = nullptr;
                const int count = SDK::Input::GetState(input, &state, pressed);
                if (control < count)
                    state[control] = 1.0f;
            }
            _injected = true;
        }
        else if (_openId) {
            ClearInput(input);
        }
        else if (_waitForRelease) {
            float *state    = nullptr;
            const int count = SDK::Input::GetState(input, &state, false);
            bool held       = false;
            for (const int control : {kInventoryControl, kInteractControl, kAlternateInteractControl, kFireControl, kAlternateFireControl}) {
                if (control < count)
                    held |= state[control] != 0.0f;
            }
            _waitForRelease = held;
            for (bool pressed : {false, true}) {
                const int size = SDK::Input::GetState(input, &state, pressed);
                for (const int control : {kInventoryControl, kInteractControl, kAlternateInteractControl, kFireControl, kAlternateFireControl})
                    if (control < size)
                        state[control] = 0.0f;
            }
        }
    }

    bool GameplayMenuService::FilterNearObjects(SDK::World::NativeItemVector &items) {
        if (!_injected || _replay.empty() || _replay.front().kind != Kind::Interaction)
            return false;
        // During a selection the nearest pickup must not silently replace the
        // chosen door/seat with a PickupRequest before we see its fresh menu.
        const auto &wanted = _replay.front().choice;
        bool found         = false;
        for (auto *item = items.begin; item != items.end; ++item) {
            found |= reinterpret_cast<uintptr_t>(item->usingObject) == wanted.object && item->itemId == wanted.itemId && (item->itemId != 1 || item->ammoLoaded == wanted.action);
        }
        if (!found || !ContextValid())
            items.end = items.begin;
        return true;
    }

    bool GameplayMenuService::AllowsNativeUse(const void *human) const {
        // If a fuel pump disappears while its chooser is open, retail falls
        // back to leaving the car without opening a menu. A replay may use an
        // actor only after its requested menu choice was actually matched.
        return human != _player.object || !_injected || _replay.empty() || _replay.front().kind != Kind::Interaction || (!_failed && _replayIndex == _replay.size());
    }

    void GameplayMenuService::Update() {
        if (!_ui)
            return;
        _automaticPath.clear();
        if (auto response = _ui->TakeGameplayMenuResponse(); response && response->id == _openId && response->index < _choices.size() && ContextValid()) {
            auto choice = _choices[response->index];
            if (choice.canSelect && (!response->drop || choice.canDrop)) {
                choice.drop = response->drop;
                _replay     = _path;
                _replay.push_back({_kind, std::move(choice)});
                _replayIndex = 0;
                _openId      = 0;
            }
        }
        if (_injected) {
            _replay.clear();
            _replayIndex = 0;
            _injected    = false;
            _failed      = false;
        }
        if (_openId && (!_ui->HasGameplayMenu(_openId) || !ContextValid()))
            Reset();
        if (_openId) {
            for (const auto &choice : _choices) {
                if (choice.target.networkId && !_world->NativeObjects().Resolve(choice.target)) {
                    Reset();
                    break;
                }
            }
        }
    }
} // namespace Mafia1Online::Features::GameplayMenu
