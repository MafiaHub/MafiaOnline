#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Framework::Scripting {
    class Engine;
}

namespace Mafia1Online::Core {
    class Server;
}
namespace Mafia1Online::Features::Combat {
    enum class DamageCause : uint8_t;
}
namespace Mafia1Online::Shared::Car {
    struct SeatEvent;
    struct HitReport;
}
namespace Mafia1Online::Shared::Combat {
    struct Event;
}
namespace Mafia1Online::Shared::Entities {
    class CarEntity;
    class CarDebrisEntity;
    class PlayerEntity;
    class DoorEntity;
} // namespace Mafia1Online::Shared::Entities

// Native server events. The payloads are documented in
// shared/scripting_catalog.h, which must change together with them.
namespace Mafia1Online::Features::Script {
    void RegisterScripting(Framework::Scripting::Engine *engine, Core::Server &server);

    void EmitPlayerEvent(Core::Server &server, const char *name, uint64_t playerId);
    void EmitDoorEvent(Core::Server &server, const char *name, Shared::Entities::DoorEntity &door, uint64_t playerId);
    void EmitPlayerChat(Core::Server &server, uint64_t playerId, const std::string &text);
    void EmitPlayerCommand(Core::Server &server, uint64_t playerId, const std::string &command, const std::vector<std::string> &args);
    void EmitNicknameChange(uint64_t playerId, const std::string &oldNickname, const std::string &nickname);
    void EmitModelChange(uint64_t playerId, const std::string &oldModel, const std::string &model);
    void EmitMoneyChange(uint64_t playerId, uint32_t oldMoney, uint32_t money);
    void EmitSeatEvent(Core::Server &server, const Shared::Car::SeatEvent &event);
    void EmitCombatActionEvent(Core::Server &server, const Shared::Combat::Event &event);
    // name(vehicle); vehicleDamage adds the native damage snapshot and
    // vehicleTerminal the terminal state.
    void EmitCarEvent(Core::Server &server, const char *name, Shared::Entities::CarEntity &car);
    void EmitCarHitEvent(Core::Server &server, const Shared::Car::HitReport &hit, float damage);
    void EmitPickupEvent(Core::Server &server, const char *name, uint64_t pickupId, uint64_t playerId);
    void EmitCarDebrisEvent(Core::Server &server, const char *name, Shared::Entities::CarDebrisEntity &debris);
    void EmitPlayerStateEvent(Core::Server &server, const char *name, Shared::Entities::PlayerEntity &player);
    void EmitPlayerHealthEvent(Core::Server &server, uint64_t playerId, uint64_t sourceId, std::optional<uint8_t> weaponId,
                               Combat::DamageCause cause, float oldHealth, float newHealth, uint16_t deathAnimation);
    void EmitMissionEvent(Core::Server &server, const char *name);
    void EmitMissionLoadEvent(Core::Server &server, uint64_t playerId, uint64_t generation, uint8_t state);
} // namespace Mafia1Online::Features::Script
