#pragma once

#include <v8pp/metadata.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace Mafia1Online::Scripting {
    inline v8pp::metadata::registry &ServerCatalog() {
        return v8pp::metadata::catalog("mafia1online-server");
    }

    inline v8pp::metadata::registry &ClientCatalog() {
        return v8pp::metadata::catalog("mafia1online-client");
    }

    // Canonical Linux builds run inside a /workspace container, while their
    // binaries run from the host checkout. Resolve the checkout at runtime so
    // a debug build never writes to the container-only configured path.
    inline std::filesystem::path MetadataOutputPath(std::string_view target) {
        std::error_code error;
        auto directory = std::filesystem::current_path(error);
        if (error) {
            return {};
        }
        const auto filename = std::string(target) + "-api.json";
        for (;;) {
            for (const auto *relative : {"scripting-api/generated", "code/projects/mafia1online/scripting-api/generated"}) {
                const auto candidate = directory / relative;
                if (std::filesystem::is_directory(candidate, error)) {
                    return candidate / filename;
                }
                error.clear();
            }
            const auto parent = directory.parent_path();
            if (parent == directory) {
                return {};
            }
            directory = parent;
        }
    }

    // The native client events. Keep this in step with the Emit calls in
    // client/src/features/script.
    inline void RegisterClientEventMetadata() {
        auto &catalog = ClientCatalog();
        auto &events  = catalog.data_type("EventMap",
             "Native events dispatched through `Events.on`. Each property is the exact callback argument tuple for that event. An event the server sends with `Player.emit` arrives as `[data: unknown]`.");
        events.add_property("resourceStart", "[resourceName: string]", "Dispatched after a resource entry point has run and immediately before the resource becomes running.");
        events.add_property("resourceStop", "[resourceName: string]", "Dispatched while a resource is stopping, before its stop callback, timers, exports and event handlers are cleaned up.");
        auto &chat = catalog.data_type("ChatMessageEvent", "A chat message received from the server.");
        chat.add_property("author", "string", "Display name supplied by the server.");
        chat.add_property("text", "string", "Message body.");
        chat.add_property("color", "number", "Packed message color.");
        events.add_property("chatMessage", "[message: ChatMessageEvent]", "Dispatched before a server chat message is added to the built-in chat overlay.");
        events.add_property("chatSend", "[text: string]", "Dispatched synchronously when the local player submits a chat line. Return false to keep the line client-side. Chat.send bypasses this event; asynchronous handlers cannot block it.");
        events.add_property("voiceStart", "[]", "Dispatched when the local player starts speaking on voice chat. A held push-to-talk key with no speech does not trigger it.");
        events.add_property("voiceStop", "[]", "Dispatched when the local player stops speaking.");
        auto &mission = catalog.data_type("ClientMissionInfo", "The mission this client has loaded.");
        mission.add_property("mission", "string", "Stock or detected mod mission directory name.");
        mission.add_property("missionGeneration", "number", "Server generation of this mission load.");
        events.add_property("missionReady", "[mission: ClientMissionInfo]", "Dispatched once the native mission has loaded, after the server's weather and frame overrides were applied.");
        events.add_property("missionUnload", "[mission: ClientMissionInfo]", "Dispatched while the native mission closes. Local sounds and HUD state are cleared right after.");
        events.add_property("render", "[]", "Dispatched once per client update while a mission is loaded. Submit Draw commands here; the queue is cleared before the next update.");
        events.add_property("playerStreamIn", "[player: Player]", "Dispatched when a player's replica reaches this client, including the local player.");
        events.add_property("playerStreamOut", "[playerId: number]", "Dispatched once a player's replica has left this client; its handles no longer resolve.");
        events.add_property("playerSpawn", "[player: Player]", "Dispatched when a streamed player starts a new life.");
        events.add_property("playerDeath", "[player: Player]", "Dispatched when a streamed player's server health reaches zero.");
        auto &pickupSnapshot = catalog.data_type("ClientPickupSnapshot", "The last pickup values this client received before the pickup streamed out. These values remain available after its handle stops resolving.");
        pickupSnapshot.add_property("id", "number", "Network entity ID of the pickup.");
        pickupSnapshot.add_property("missionGeneration", "number", "Mission generation this pickup belonged to.");
        pickupSnapshot.add_property("weaponId", "number", "Weapon this pickup held at the last replicated update.");
        pickupSnapshot.add_property("position", "{ x: number; y: number; z: number }", "Last replicated world position.");
        pickupSnapshot.add_property("heading", "number", "Last replicated heading in radians.");
        pickupSnapshot.add_property("loaded", "number", "Last replicated loaded rounds or throwable count.");
        pickupSnapshot.add_property("reserve", "number", "Last replicated reserve rounds.");
        events.add_property("pickupStreamIn", "[pickup: Pickup]", "Dispatched when a weapon pickup for the loaded mission reaches this client.");
        events.add_property("pickupStreamOut", "[pickupId: number, lastKnown: ClientPickupSnapshot]", "Dispatched when a weapon pickup leaves this client or its mission unloads. The handle no longer resolves; lastKnown is a plain snapshot from the last replicated update.");
        auto &doorSnapshot = catalog.data_type("ClientDoorSnapshot", "Last known door state before a mission door streams out.");
        doorSnapshot.add_property("id", "number", "Network ID of the door.");
        doorSnapshot.add_property("missionGeneration", "number", "Mission generation containing the door.");
        doorSnapshot.add_property("frameName", "string", "Root door frame name.");
        doorSnapshot.add_property("position", "{ x: number; y: number; z: number }", "Last replicated hinge position.");
        doorSnapshot.add_property("open", "boolean", "Last server target open state.");
        doorSnapshot.add_property("locked", "boolean", "Last lock state.");
        doorSnapshot.add_property("enabled", "boolean", "Last native use state.");
        doorSnapshot.add_property("reverse", "boolean", "Last root swing side.");
        doorSnapshot.add_property("openFraction", "number", "Last open fraction from 0 to 1.");
        events.add_property("doorStreamIn", "[door: Door]", "Dispatched when a mission door replica reaches this client.");
        events.add_property("doorChange", "[door: Door]", "Dispatched after a replicated mission door's target, lock or native use state changes.");
        events.add_property("doorStreamOut", "[doorId: number, lastKnown: ClientDoorSnapshot]", "Dispatched when a mission door replica leaves or its mission unloads.");
        events.add_property("countdownEnd", "[]", "Dispatched when a countdown started with Hud.startCountdown runs out.");
    }

    // The native server events and their exact callback arguments. Keep this
    // in step with the Emit functions in server/src/features/script.
    inline void RegisterServerEventMetadata() {
        auto &catalog = ServerCatalog();
        auto &events  = catalog.data_type("EventMap",
             "Native events dispatched through `Events.on`. Each property is the exact callback argument tuple for that event.");
        events.add_property("resourceStart", "[resourceName: string]", "Dispatched after a resource entry point has run and immediately before the resource becomes running.");
        events.add_property("resourceStop", "[resourceName: string]", "Dispatched while a resource is stopping, before its stop callback, timers, exports and event handlers are cleaned up.");
        events.add_property("consoleCommand", "[command: string, args: string[]]", "Dispatched after the server console parses a command line that no built-in command handles.");

        auto &mission = catalog.data_type("MissionInfo", "The server's current mission.");
        mission.add_property("mission", "string", "Stock or detected mod mission directory name.");
        mission.add_property("missionGeneration", "number", "Generation of this mission load; it increases on every change, including a reload of the same mission.");
        events.add_property("missionChange", "[mission: MissionInfo]", "Dispatched after World.changeMission has reset the world and asked every client to load the new mission.");
        events.add_property("missionReady", "[mission: MissionInfo]", "Dispatched once each time every connected player has loaded the current mission generation. At least one player must be connected.");

        auto &load = catalog.data_type("MissionLoadInfo", "A client's report about loading a mission generation.");
        load.add_property("missionGeneration", "number", "Generation the report refers to.");
        load.add_property("state", "number", "0 unloaded, 1 ready, 2 scene failure, 3 collision failure, 4 game initialization failure.");
        events.add_property("playerMissionReady", "[player: Player, info: MissionLoadInfo]", "Dispatched when a player's client has loaded the current mission.");
        events.add_property("playerMissionUnloaded", "[player: Player, info: MissionLoadInfo]", "Dispatched when a player's client has unloaded its mission.");
        events.add_property("playerMissionLoadFailed", "[player: Player, info: MissionLoadInfo]", "Dispatched when a player's client could not load the current mission.");

        events.add_property("playerConnect", "[player: Player]", "Dispatched after a player has connected and has a player handle. The player is not spawned yet.");
        events.add_property("playerDisconnect", "[player: Player]", "Dispatched while a player disconnects, before the player handle stops resolving.");
        events.add_property("playerVoiceStart", "[player: Player]", "Dispatched when a player starts speaking on voice chat, including when no one is in earshot.");
        events.add_property("playerVoiceStop", "[player: Player]", "Dispatched shortly after a player's last voice frame reaches the server. A disconnect ends speech without this event.");
        events.add_property("playerChat", "[player: Player, text: string]", "Dispatched for an accepted, sanitized chat line after it was relayed.");
        events.add_property("playerCommand", "[player: Player, command: string, args: string[]]", "Dispatched when a chat line starting with `/` passes the chat rate limit.");
        events.add_property("playerNicknameChange", "[player: Player, oldNickname: string, nickname: string]", "Dispatched after Player.setNickname changed the name.");
        events.add_property("playerModelChange", "[player: Player, oldModel: string, model: string]", "Dispatched after Player.setModel changed the human model.");
        events.add_property("playerMoneyChange", "[player: Player, oldMoney: number, money: number]", "Dispatched after the server changes a player's Free Ride balance.");
        events.add_property("doorStateChange", "[door: Door, player: Player | null]", "Dispatched after a player uses a mission door or a script changes its open target. Player is null for script changes.");
        events.add_property("doorLockChange", "[door: Door, player: null]", "Dispatched after a script changes a mission door lock.");
        events.add_property("doorInteractionChange", "[door: Door, player: null]", "Dispatched after a script enables or disables native door use.");
        events.add_property("playerSpawn", "[player: Player]", "Dispatched after Player.spawn started a new life.");
        events.add_property("playerRespawn", "[player: Player]", "Dispatched after Player.respawn started a new life.");
        events.add_property("playerDespawn", "[player: Player]", "Dispatched after Player.despawn ended the current life.");

        auto &health = catalog.data_type("PlayerHealthInfo", "An accepted change of a player's server health.");
        health.add_property("attacker", "Player | null", "Player the damage is attributed to, or null when the source is absent or has disconnected.");
        health.add_property("weaponId", "number | null", "Validated weapon used for this damage: 0 is fists, 5 is a Molotov, 15 is a grenade; null for script, fall, vehicle or unknown damage. Remains known if the attacker disconnects after throwing.");
        health.add_property("cause", "\"script\" | \"firearm\" | \"melee\" | \"explosion\" | \"fire\" | \"fall\" | \"drowning\" | \"vehicle\"", "Server damage path that changed health. Explosions and fires can have a weaponId when they came from a thrown weapon.");
        health.add_property("oldHealth", "number", "Health before the change.");
        health.add_property("health", "number", "Health after the change.");
        health.add_property("amount", "number", "oldHealth minus health; negative for healing.");
        health.add_property("deathAnimation", "number", "Death animation chosen by the server for a fatal change, otherwise 0.");
        health.add_property("missionGeneration", "number", "Mission generation of the affected life.");
        health.add_property("spawnGeneration", "number", "Spawn generation of the affected life.");
        events.add_property("playerDamage", "[victim: Player, info: PlayerHealthInfo]", "Dispatched after an accepted health decrease.");
        events.add_property("playerDeath", "[victim: Player, killer: Player | null, info: PlayerHealthInfo]",
            "Dispatched after a fatal health decrease, following playerDamage. The killer is null when no connected attacker owns the damage; info.weaponId identifies the validated weapon when known. Death never respawns a player by itself; call Player.respawn.");
        events.add_property("playerHealthChange", "[player: Player, info: PlayerHealthInfo]", "Dispatched after an accepted health increase.");

        auto &action = catalog.data_type("WeaponActionInfo", "A weapon action the server accepted from a player's client.");
        action.add_property("weaponId", "number", "Weapon the action used.");
        action.add_property("selectedWeapon", "number", "Selected weapon after the action; 0 is holstered.");
        action.add_property("inventoryMask", "number", "Bit n is set when weapon n is held.");
        action.add_property("loaded", "number", "Loaded ammunition of weaponId after the action.");
        action.add_property("reserve", "number", "Reserve ammunition of weaponId after the action.");
        action.add_property("aiming", "boolean", "Whether the player is aiming.");
        action.add_property("direction", "{ x: number; y: number; z: number }", "Unit aim or throw direction.");
        action.add_property("target", "Player | null", "The targeted player the client suggested for a shot; confirmed damage arrives as playerDamage.");
        action.add_property("revision", "number", "Server combat state revision.");
        action.add_property("meleeIndex", "number", "Native combo index (0 to 3), or -1 for a heavy finisher on a melee release.");
        action.add_property("meleeHoldMs", "number", "Measured local press duration on a melee release, in milliseconds.");
        action.add_property("throwHoldMs", "number", "Native grenade or Molotov charge duration on a throw release, capped at 2000 milliseconds; zero for other actions.");
        for (const char *name : {"playerWeaponEquip", "playerAimChange", "playerWeaponReload", "playerWeaponFire", "playerWeaponThrow", "playerWeaponThrowStart", "playerWeaponThrowRelease", "playerWeaponThrowCancel", "playerWeaponMeleeStart", "playerWeaponMelee", "playerWeaponMeleeCancel"}) {
            events.add_property(name, "[player: Player, info: WeaponActionInfo]", "Dispatched after the server accepted this controller action.");
        }
        events.add_property("playerWeaponDrop", "[player: Player, info: WeaponActionInfo]", "Dispatched after a controller drop or Player.dropWeapon updates inventory. Death drops only emit weaponDropped.");

        auto &pickup = catalog.data_type("PickupEventInfo", "A weapon pickup transition.");
        pickup.add_property("pickupId", "number", "Network ID of the pickup, which may no longer exist.");
        pickup.add_property("weaponId", "number", "Weapon of the pickup.");
        pickup.add_property("position", "{ x: number; y: number; z: number } | null", "Where the pickup lies, or null once it is gone.");
        events.add_property("weaponDropped", "[pickup: Pickup | null, player: Player | null, info: PickupEventInfo]", "Dispatched when a drop or a death leaves a weapon pickup in the world.");
        events.add_property("pickupTaken", "[pickup: Pickup | null, player: Player | null, info: PickupEventInfo]", "Dispatched after the server moved a pickup into a player's inventory. The pickup handle is null when nothing is left on the ground.");

        events.add_property("vehicleSpawn", "[vehicle: Vehicle]", "Dispatched after Vehicle.spawn created a vehicle.");
        events.add_property("vehicleDestroy", "[vehicle: Vehicle]", "Dispatched immediately before Vehicle.destroy removes the vehicle. A mission change removes every vehicle without this event.");
        for (const char *name : {"vehicleEngineChange", "vehicleFuelChange", "vehicleLightsChange", "vehicleHornChange", "vehicleSirenChange", "vehicleGearChange", "vehicleDamageState"}) {
            events.add_property(name, "[vehicle: Vehicle]", "Dispatched after the matching vehicle state changed, from its simulation controller or a script.");
        }
        events.add_property("vehicleDamage", "[vehicle: Vehicle, damage: VehicleDamage | null]", "Dispatched when the native per-part damage snapshot changes.");
        events.add_property("vehicleOpacityChange", "[vehicle: Vehicle]", "Dispatched after Vehicle.setOpacity changes the server-owned opacity.");
        events.add_property("vehicleRepair", "[vehicle: Vehicle]", "Dispatched after Vehicle.repair starts a native damage and deformation reset on every client; a fresh damage snapshot follows from the simulation controller.");
        events.add_property("vehicleTerminal", "[vehicle: Vehicle, state: number]", "Dispatched when a vehicle explodes (1), its native body hits water (2), or it enters a fall volume or native invalid-fall state (3). Water and fall occupants die through server combat immediately after this event; explosion occupants die before it. Scripts decide when to destroy the car.");

        auto &hit = catalog.data_type("VehicleHitInfo", "An accepted firearm pellet against a vehicle.");
        hit.add_property("shooter", "Player | null", "Player who fired, or null when they have left.");
        hit.add_property("damage", "number", "Damage derived from the accepted shot.");
        hit.add_property("position", "{ x: number; y: number; z: number }", "World-space impact position.");
        hit.add_property("shotSequence", "number", "Shooter's fire sequence.");
        hit.add_property("pelletIndex", "number", "Pellet of that shot.");
        events.add_property("vehicleHit", "[vehicle: Vehicle, info: VehicleHitInfo]", "Dispatched when a pellet hit is replayed to the vehicle's simulation controller, before the resulting vehicleDamage.");

        auto &part = catalog.data_type("VehiclePartInfo", "A loose part a vehicle shed.");
        part.add_property("debrisId", "number", "Network ID of the temporary debris replica.");
        part.add_property("type", "number", "1 wheel, 2 bumper, 3 light, 4 license plate, 5 mirror, 6 wing, 7 door.");
        part.add_property("partIndex", "number", "Native part index within the model.");
        part.add_property("position", "{ x: number; y: number; z: number }", "Where the part came off.");
        events.add_property("vehiclePartDetached", "[vehicle: Vehicle, info: VehiclePartInfo]", "Dispatched when the server accepts a native loose part from the vehicle's controller.");

        auto &seat = catalog.data_type("VehicleSeatInfo", "An accepted seat transition.");
        seat.add_property("seat", "number", "Seat index; 0 is the driver.");
        seat.add_property("fromSeat", "number | null", "Previous seat for a native climb across the car; otherwise null.");
        seat.add_property("stolen", "boolean", "True when the player pulled the previous driver out.");
        seat.add_property("serverSequence", "number", "Server seat sequence of the transition.");
        events.add_property("vehiclePlayerEntering", "[vehicle: Vehicle, player: Player, info: VehicleSeatInfo]", "Dispatched when the server accepts the start of a native enter or steal.");
        events.add_property("vehiclePlayerEntered", "[vehicle: Vehicle, player: Player, info: VehicleSeatInfo]", "Dispatched when a player sits in a seat, natively or through Player.putInVehicle.");
        events.add_property("vehiclePlayerExited", "[vehicle: Vehicle, player: Player, info: VehicleSeatInfo]", "Dispatched when a player leaves a seat, including by death, respawn or removeFromVehicle.");
        events.add_property("vehiclePlayerExitBlocked", "[vehicle: Vehicle, player: Player, info: VehicleSeatInfo]", "Dispatched when a script records an administrative blocked exit; native blocked exits use seat transfer or the retail emergency exit.");
        events.add_property("vehiclePlayerSeatChanged", "[vehicle: Vehicle, player: Player, info: VehicleSeatInfo]", "Dispatched after a blocked-side exit moves the player into the paired seat. info.fromSeat is the prior seat.");
    }
} // namespace Mafia1Online::Scripting
