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
        health.add_property("attacker", "Player | Npc | null", "Player or simulation host NPC the damage is attributed to, or null when the source is absent or has disconnected.");
        health.add_property("weaponId", "number | null", "Validated weapon used for this damage: 0 is fists, 5 is a Molotov, 15 is a grenade; null for script, fall, vehicle or unknown damage. Remains known if the attacker disconnects after throwing.");
        health.add_property("cause", "\"script\" | \"firearm\" | \"melee\" | \"explosion\" | \"fire\" | \"fall\" | \"drowning\" | \"vehicle\" | \"railway\"",
            "Server damage path that changed health; railway is a tram or metro train running into the player on foot. Explosions and fires can have a weaponId when they came from a thrown weapon.");
        health.add_property("oldHealth", "number", "Health before the change.");
        health.add_property("health", "number", "Health after the change.");
        health.add_property("amount", "number", "oldHealth minus health; negative for healing.");
        health.add_property("deathAnimation", "number", "Death animation chosen by the server for a fatal change, otherwise 0.");
        health.add_property("missionGeneration", "number", "Mission generation of the affected life.");
        health.add_property("spawnGeneration", "number", "Spawn generation of the affected life.");
        events.add_property("playerDamage", "[victim: Player, info: PlayerHealthInfo]", "Dispatched after an accepted health decrease.");
        events.add_property("playerDeath", "[victim: Player, killer: Player | Npc | null, info: PlayerHealthInfo]",
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
        events.add_property("weaponDropped", "[pickup: Pickup | null, player: Player | Npc | null, info: PickupEventInfo]",
            "Dispatched when a drop or a death leaves a weapon pickup in the world; a dead npc's weapon names the npc.");
        events.add_property("pickupTaken", "[pickup: Pickup | null, player: Player | null, info: PickupEventInfo]", "Dispatched after the server moved a pickup into a player's inventory. The pickup handle is null when nothing is left on the ground.");

        events.add_property("pedestrianSpawn", "[pedestrian: Pedestrian]", "Dispatched when the simulation host starts a pedestrian near a player.");
        events.add_property("pedestrianRetire", "[pedestrian: Pedestrian]", "Dispatched just before a pedestrian leaves the world: far from every player, a corpse past its lifetime, Pedestrian.remove or setEnabled(false). A mission change removes every pedestrian without this event.");
        auto &pedestrianDamage = catalog.data_type("PedestrianDamageInfo", "A hit the simulation host applied to a pedestrian.");
        pedestrianDamage.add_property("attacker", "Player | Vehicle | null", "The player or car that hit it, when there was one.");
        pedestrianDamage.add_property("hitType", "number", "Retail hit type: 0 bullet, 1 hard knockdown, 2 melee, 3 explosion, 5 car impact.");
        pedestrianDamage.add_property("damage", "number", "Damage of the hit.");
        pedestrianDamage.add_property("healthBefore", "number", "Health before the hit.");
        pedestrianDamage.add_property("healthAfter", "number", "Health after the hit; 0 or less means it died.");
        events.add_property("pedestrianDamage", "[pedestrian: Pedestrian, info: PedestrianDamageInfo]", "Dispatched after the simulation host applied a hit.");
        events.add_property("pedestrianDeath", "[pedestrian: Pedestrian, killer: Player | Vehicle | null]", "Dispatched once when a pedestrian dies; killer is the last attacker.");
        events.add_property("npcSpawn", "[npc: Npc]", "Dispatched when the simulation host starts running an armed human, such as a gangster pedestrian someone hit that draws its weapon or a police walker the police manager sends after a player.");
        events.add_property("npcRetire", "[npc: Npc]", "Dispatched just before an NPC leaves the world: no player within 150 units, Npc.remove or the host leaving. A mission change removes every NPC without this event.");
        auto &npcDamage = catalog.data_type("NpcDamageInfo", "A hit the simulation host applied to an NPC.");
        npcDamage.add_property("attacker", "Player | Npc | Vehicle | null", "Who hit it, when known.");
        npcDamage.add_property("hitType", "number", "Retail C_human hit type: 0 bullet, 1 hard knockdown, 2 melee, 3 explosion, 8 car impact.");
        npcDamage.add_property("damage", "number", "Damage of the hit before the body part scale.");
        npcDamage.add_property("healthBefore", "number", "Health before the hit.");
        npcDamage.add_property("healthAfter", "number", "Health after the hit; 0 or less means it died.");
        events.add_property("npcDamage", "[npc: Npc, info: NpcDamageInfo]", "Dispatched after the simulation host applied a hit.");
        events.add_property("npcDeath", "[npc: Npc, killer: Player | Npc | Vehicle | null]",
            "Dispatched once when an NPC dies; killer is the last attacker. The weapon in its hand drops as a pickup, as retail Do_WeaponDrop does (weaponDropped).");
        auto &record = catalog.data_type("PoliceRecord", "A player's retail police crime record on the simulation host.");
        record.add_property("level", "number", "Wanted level: 0 none, 1 minor (pursuit), 2 arrest, 3 lethal.");
        record.add_property("crimes", "(\"weaponSeen\" | \"firing\" | \"speeding\" | \"crashObject\" | \"crashCar\" | \"crashPedestrian\" | \"sidewalk\" | \"violence\" | \"redLight\" | \"carTheft\" | \"signDamage\" | \"assaultOnPolice\" | \"forcedArrest\" | \"policeCarRammed\")[]", "Crimes since the record was last settled.");
        record.add_property("officers", "number", "Officers assigned to the player.");
        auto &crime = catalog.data_type("PlayerCrimeInfo", "A crime the police noticed.");
        crime.add_property("crime", "\"weaponSeen\" | \"firing\" | \"speeding\" | \"crashObject\" | \"crashCar\" | \"crashPedestrian\" | \"sidewalk\" | \"violence\" | \"redLight\" | \"carTheft\" | \"signDamage\" | \"assaultOnPolice\" | \"forcedArrest\" | \"policeCarRammed\"", "What the player did: a weapon in hand, shots, speeding, a crash, driving on the sidewalk, fist fights, a red light, car theft, a damaged sign, assault on police, a scripted arrest or ramming a police car.");
        events.add_property("playerCrime", "[player: Player, info: PlayerCrimeInfo]",
            "Dispatched when the simulation host's police manager records a new crime of a player. The player's wanted level may rise with it (playerWantedChange).");
        auto &wanted = catalog.data_type("PlayerWantedInfo", "A wanted level change.");
        wanted.add_property("level", "number", "New level: 0 none, 1 minor, 2 arrest, 3 lethal.");
        wanted.add_property("previous", "number", "Level before the change.");
        events.add_property("playerWantedChange", "[player: Player, info: PlayerWantedInfo]",
            "Dispatched when a player's wanted level changes: the police raise it for a crime, a script sets it, an arrest or Police.clear settles it, the officers lose the player, "
            "or a new simulation host takes over (every record starts again at 0).");
        auto &arrest = catalog.data_type("PlayerArrestInfo", "How a police officer caught a player.");
        arrest.add_property("kind", "\"arrest\" | \"fine\"", "arrest: the retail arrest, which ends a single-player game; fine: the on-the-spot fine.");
        events.add_property("playerArrest", "[player: Player, officer: Npc, info: PlayerArrestInfo]",
            "Dispatched when a simulation host police officer finished arresting or fining a player. Nothing else happens to the player: the game goes on and the script decides the outcome. The player's crime record is cleared.");
        auto &taken = catalog.data_type("VehicleTrafficTakenInfo", "How a player took a car of the city's traffic.");
        taken.add_property("stolen", "boolean", "True when the player pulled its driver out, who stays as a driver NPC; false for a seat nobody held.");
        events.add_property("vehicleTrafficTaken", "[vehicle: Vehicle, player: Player, info: VehicleTrafficTakenInfo]",
            "Dispatched when a car the simulation host drove becomes the player's: the host stops driving it and the player's client simulates it. "
            "After a steal the player gets in with the next use, as in retail.");
        events.add_property("vehicleStealRefused", "[vehicle: Vehicle, player: Player]",
            "Dispatched when the server refused a player's steal of a traffic car: the car moved out of reach or was not standing still, another player took it first or its driver had left. "
            "A player's repeated try within 500 ms is refused without this event. "
            "The player's client already played the pull-out; the driver stays in the car.");
        auto &thrown = catalog.data_type("PlayerThrownOutInfo", "How an NPC threw a player out of a car.");
        thrown.add_property("seat", "number", "Seat the player was thrown out of; 0 is the driver.");
        thrown.add_property("by", "Npc", "The NPC that did it: a traffic driver taking its car back, a policeman before an arrest or a gangster.");
        events.add_property("playerThrownOut", "[player: Player, vehicle: Vehicle, info: PlayerThrownOutInfo]",
            "Dispatched after vehiclePlayerExited when a simulation host NPC pulled the player out of the seat (retail Do_ThrowCocotFromCar).");
        auto &wagon = catalog.data_type("RailwayWagonInfo", "One wagon behind a railway's body.");
        wagon.add_property("position", "{ x: number; y: number; z: number }", "Wagon position.");
        wagon.add_property("direction", "{ x: number; y: number; z: number }", "Unit direction of the wagon.");
        auto &railway = catalog.data_type("RailwayInfo", "A tram or metro train as the simulation host last reported it.");
        railway.add_property("index", "number", "Load index within the mission, from 0; stable for the mission generation.");
        railway.add_property("position", "{ x: number; y: number; z: number }", "Position of the body.");
        railway.add_property("direction", "{ x: number; y: number; z: number }", "Unit direction of the body.");
        railway.add_property("speed", "number", "Speed in units per second.");
        railway.add_property("state",
            "\"moving\" | \"stationArrival\" | \"passengersLeaving\" | \"passengersEntering\" | \"stationWait\" | \"scheduleWait\" | \"departing\" | \"collisionBraking\" | "
            "\"stopPointBraking\" | \"semaphoreBraking\" | \"driverHit\" | \"followingRailway\" | \"unknown\"",
            "Retail railway state. driverHit stops it after a bullet hit its driver; the host lets it go on after 30 seconds.");
        railway.add_property("wagons", "RailwayWagonInfo[]", "Wagons behind the body; empty for a tram, one for a freeride metro train.");
        auto &bridge = catalog.data_type("BridgeInfo", "A drawbridge as the simulation host last reported it.");
        bridge.add_property("index", "number", "Load index within the mission, from 0; stable for the mission generation.");
        bridge.add_property("position", "{ x: number; y: number; z: number }", "Position of the bridge frame, one of its two leaves.");
        bridge.add_property("phase", "\"closed\" | \"signaling\" | \"preparing\" | \"opening\" | \"open\" | \"closing\" | \"unknown\"",
            "Retail bridge phase. A steamer in its request zone moves a closed bridge to signaling (red road lights); it opens once no traffic car is on it.");
        bridge.add_property("angle", "number", "Leaf angle in radians, 0 closed; the retail bridges open to 50 degrees.");
        bridge.add_property("raised", "boolean", "True while opening, open or closing: the leaves are up, and a player on foot who walks into the raised bridge dies, as in retail.");
        auto &railwayHit = catalog.data_type("RailwayHitInfo", "A railway running into a player.");
        railwayHit.add_property("railway", "RailwayInfo | null", "The railway, as last reported.");
        railwayHit.add_property("damage", "number", "Damage applied: retail (|speed| - 2.5) * 4.2.");
        events.add_property("railwayHitPlayer", "[player: Player, info: RailwayHitInfo]",
            "Dispatched after a tram or metro train knocked a player on foot down, following playerDamage (cause railway).");
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
        hit.add_property("shooter", "Player | Npc | null", "Player or simulation host NPC who fired, or null when they are gone.");
        hit.add_property("damage", "number", "Damage derived from the accepted shot.");
        hit.add_property("position", "{ x: number; y: number; z: number }", "World-space impact position.");
        hit.add_property("shotSequence", "number", "Shooter's fire sequence; 0 for an NPC.");
        hit.add_property("pelletIndex", "number", "Pellet of that shot; 0 for an NPC.");
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
