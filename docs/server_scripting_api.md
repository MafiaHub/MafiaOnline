# Server scripting API

Server resources run in Node.js. Mafia1Online adds `Player`, `Vehicle`,
`Pickup`, `Door`, `Sound`, `Weapon`, and `World` to the framework scripting globals.
Use these bindings for authoritative game state. Client resources handle
presentation such as HUD, fade, camera and local sounds; see the
[client API](client_scripting_api.md).

For editor completion, load [server-api.d.ts](../scripting-api/server-api.d.ts)
with your server resource. The [generated reference](../scripting-api/generated/server-api.d.ts)
documents every registered binding, including framework globals and parameter
details, from the runtime metadata catalog. This guide covers the workflow and
the Mafia1Online methods in one place.

## Start here

```js
// server/main.js
Events.on("playerMissionReady", (player) => {
    if (!World.isReady() || player.spawned) return;
    const position = player.getSuggestedSpawn();
    if (position && player.spawn(position)) {
        player.giveWeapon(6, 6, 36, true);
    }
});

Events.on("playerCommand", (player, command) => {
    if (command !== "hello") return;
    player.sendMessage(`Hello, ${player.nickname}`);
});
```

See [the sample gamemode](../resources/sample-gamemode/server/main.js) for
mission loading, vehicle commands, respawning, and server to client events.

## API map

| Area | Entry points | Purpose |
| --- | --- | --- |
| Players | `World.getPlayers()`, `Player` | Lives, health, inventory, seats, chat, connection details. |
| Vehicles | `Vehicle.spawn()`, `World.getVehicles()` | Cars, occupancy, native damage, engine and physics commands. |
| Pickups and weapons | `Pickup.create()`, `Weapon` | World weapon items and the supported stock weapon catalog. |
| Mission doors | `Door.create()`, `Door.getAll()` | Lock, use, swing and paired-leaf state shared across clients. |
| World | `World` | Missions, weather, music, scene frames, fire, explosions. |
| Positional audio | `Sound.create()`, `Sound.play()` | Persistent or one-shot world sounds. |
| Events | `Events.on()`, `Events.onClient()` | Native transitions and client-originated messages. |

## Lifetimes and replication

Handles resolve their network ID on each call. Once an entity is destroyed, a
saved handle no longer resolves; methods generally return `false` or `null`.
Constructors such as `new Vehicle(id)` wrap an existing entity and do not
spawn one. A player's `spawnGeneration` identifies a particular life, while
`World.getMissionGeneration()` identifies a mission load. Save both when a
timer or Promise may act after death, respawn, or a mission change.

The server validates life, inventory, seat, vehicle and world operations, then
replicates accepted state to clients, including late joiners. Inherited
`Entity.position`, `Entity.rotation` and `Entity.setVirtualWorld()` are
framework controls. Prefer Mafia1Online operations such as `Vehicle.setTransform()`
and `Player.spawn()` for game entities because those also apply the game's
authority rules. A mission change removes vehicles, pickups, sounds, and frame
overrides; weather and city music persist.

The signatures below use `Vector3` and `Quaternion` from the framework.
Position parameters also accept a plain `{ x, y, z }` object or three numeric
coordinates. Heading values are radians. Invalid argument shapes throw a
`TypeError`; a valid request rejected by game state usually returns `false`
or `null`.

## Framework entity state

Every game entity inherits `id`, `virtualWorld`, `state`, and `toString()` from
`Entity`. The server can use `entity.setVisibleTo(player)` to restrict
streaming to one connection, and `entity.setVisibleTo(null)` to clear that
restriction. `entity.setVirtualWorld(id)` changes the replication world.

`entity.state` is a replicated key/value store:

```js
vehicle.state.set("ownerName", player.nickname);
const ownerName = vehicle.state.get("ownerName");
const unsubscribe = vehicle.state.onChange("ownerName", (key, value, previous) => {
    console.log(key, previous, "->", value);
});
```

`state.has(key)`, `keys()`, `toObject()`, and `remove(key)` are also available.
`state.set(key, value, { scope })` accepts `"broadcast"` (default), `"owner"`,
or `"server"`. The server raises `entityStateChange` with
`(entity, key, value, previous)`; removed or previously absent values are
`undefined`.

## Framework globals

The framework also registers the following globals in server resources. Their
complete member signatures appear in the [generated reference](../scripting-api/generated/server-api.d.ts).

| Global | Use |
| --- | --- |
| `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Color` | Math and color values accepted by bindings. |
| `Messages` | Resource to resource request and response messages. |
| `Imports`, `Exports` | Public functions shared between resources. |
| `console` | Server resource logging. |
| `ExecutionEnvironment` | Details about the current script environment. |
| `Voice` | Server voice channel and player speech controls. |
| `Entity`, `StateBag`, `Player` | Network handles and replicated per-entity state. Mafia1Online extends `Player`. |
| `Chat` | Server chat controls. |
| `Events` | Resource, native, and client event subscriptions. |

`Player`, `Vehicle`, `Pickup`, and `Sound` extend framework entity handles.
Their constructors take an existing network ID. For gameplay changes, prefer
the game methods described below.

## Player

`Player` inherits `Entity` and the framework nametag readers
`isNametagVisible()`, `isNametagHealthVisible()`, `getNametagText()`, and
`getNametagColor()`. Connection properties and methods refer to the currently
connected peer; an empty string or `-1` means the value is unavailable.

- `player.toString(): string` — Formats this player for logging. Returns: The player ID and nickname.
- `nickname`: `string`. Sanitized, unique nickname.
- `model`: `string`. Server-selected stock human .i3d model, retained across spawns and mission changes.
- `health`: `number`. Server health from 0 to 100.
- `money`: `number`. Server-owned Free Ride balance, from 0 to 1,000,000,000. It survives death, respawn, and mission changes; it ends when the player disconnects.
- `alive`: `boolean`. Whether the current life is alive.
- `spawned`: `boolean`. Whether the player has a current life in the mission.
- `missionGeneration`: `number`. Mission generation of the current life.
- `spawnGeneration`: `number`. Spawn generation; it increases with every spawn, respawn and despawn. Compare it before acting on a saved player.
- `position`: `Vector3`. Last position the player's client reported. Read only; use spawn or respawn to place a player.
- `rotation`: `Quaternion`. Last facing the player's client reported. Read only.
- `ping`: `number`. Current round-trip latency in milliseconds, or -1 when unavailable.
- `ip`: `string`. Current remote network address, or an empty string when unavailable.
- `steamId`: `string`. Client-announced Steam identifier, or an empty string when unavailable.
- `discordId`: `string`. Client-announced Discord identifier, or an empty string when unavailable.
- `hardwareId`: `string`. Framework hardware identifier, or an empty string when unavailable.
- `player.kick(reason?: string): void` — Disconnects this player.
- `player.emit(eventName: string, payloadJson?: string): void` — Emits a named script event to this player's client.
- `player.getIP(): string` — Returns this player's remote network address. Returns: Address, or an empty string when unavailable.
- `player.setNickname(nickname: string): string | null` — Applies the nickname policy: invalid characters are removed, whitespace collapsed, the name cut to 24 characters, and a taken name gets a numbered suffix. Fires playerNicknameChange when the name changes. Returns: The nickname actually used, or null when the player has left.
- `player.setModel(model: string): boolean` — Changes the player's human model on every client, including during a life. Accepts a 5–64 byte `.i3d` filename containing only ASCII letters, digits, underscores, dashes, or dots. Fires `playerModelChange` when the value changes. The choice persists through respawns and mission changes. Returns false for an invalid filename or disconnected player. The filename must exist in the stock game assets for clients to display it.
- `player.setNametag(options: { visible?: boolean; showHealth?: boolean; color?: number; text?: string }): boolean` — Changes this player's nametag. Returns: False when the player has left.
- `player.setNametagVisible(visible: boolean): void` — Shows or hides this player's nametag.
- `player.setNametagHealthVisible(visible: boolean): void` — Shows or hides the health bar under this player's name.
- `player.setNametagText(text: string): void` — Overrides this player's nametag label.
- `player.setNametagColor(color: number): void` — Tints this player's nametag and chat name.
- `player.sendMessage(text: string, color?: number): boolean` — Sends a private chat notice to this player. Returns: False when the player has left or the text is invalid.
- `player.spawn(position: Vector3 | { x: number; y: number; z: number }, heading?: number): boolean` — Starts a new life with 100 health and an empty inventory once every client is mission ready. A seat held by the previous life is released and recorded as an exit. Fires playerSpawn. Returns: False before every client is ready, for an invalid position, or when the player has left.
- `player.respawn(position: Vector3 | { x: number; y: number; z: number }, heading?: number): boolean` — Starts a new life after death, like spawn. The server never respawns anyone by itself. Fires playerRespawn. Returns: False when the life could not be started.
- `player.despawn(): boolean` — Ends the current life and clears its seat. Fires playerDespawn. Returns: False when the player has left.
- `player.getSuggestedSpawn(): Vector3 | null` — Returns the native scene spawn hint the player's client reported for the current mission. Returns: The hint, or null before the client reported one.
- `player.setHealth(health: number): boolean` — Sets server health, firing playerDamage, playerDeath or playerHealthChange. A dead player cannot be revived this way; use respawn. Returns: False when there is no living, spawned player.
- `player.setMoney(amount: number): boolean` — Sets a whole-dollar balance. Fires `playerMoneyChange` when it changes.
- `player.giveMoney(amount: number): boolean` — Adds a signed whole-dollar amount. Returns false if the result would fall outside 0 to 1,000,000,000.
- `player.trySpendMoney(cost: number): boolean` — Checks and debits an affordable whole-dollar cost in one server call. Grant goods only if it returns true. A nonzero debit fires `playerMoneyChange`.
- `player.getCameraTarget(): Player | null` — Returns the connected player selected for this player's camera, or null for the local camera.
- `player.setCameraTarget(target: Player | null): boolean` — Follows a connected player's pedestrian camera and switches to that player's car while seated. Pass null to restore the observer's own camera. Returns false if the observer has left or the target is disconnected or in another mission.
- `player.getVehicle(): Vehicle | null` — Returns the vehicle whose seat the current life holds. Returns: The vehicle, or null on foot.
- `player.getSeat(): number | null` — Returns the seat the current life holds; 0 is the driver. Returns: The seat, or null on foot.
- `player.putInVehicle(vehicle: Vehicle, seat?: number): boolean` — Records the player as seated through the server seat path, as if the native entry had completed; it does not play the door animation. Seat 0 makes this player's client the vehicle's simulation controller. Fires vehiclePlayerEntered. Returns: False for a dead or unspawned player, another mission's vehicle, or a seat that is taken or out of range.
- `player.removeFromVehicle(): boolean` — Records the player's current seat as exited without the native exit animation. Fires vehiclePlayerExited. Returns: False when the player is not seated.
- `player.giveWeapon(weaponId: number, loaded?: number, reserve?: number, equip?: boolean): boolean` — Grants a weapon to a living player. Returns: False for a dead player, an unsupported weapon or a full inventory.
- `player.dropWeapon(weaponId?: number): Pickup | null` — Moves a held weapon and its ammunition into a replicated pickup at the player's feet. Omit `weaponId` to drop the selected weapon. Returns null if the player cannot drop it or the pickup limit is full. Emits `playerWeaponDrop` and `weaponDropped` after the server updates inventory.
- `player.giveAmmo(weaponId: number, amount: number): boolean` — Adds reserve ammunition to a held weapon. Returns: False when the weapon is not held.
- `player.setWeaponAmmo(weaponId: number, loaded: number, reserve: number): boolean` — Sets the ammunition of a held weapon; it does not grant the weapon. Returns: False when the weapon is not held.
- `player.removeWeapon(weaponId: number): boolean` — Removes a held weapon. Returns: False when it was not held.
- `player.removeAllWeapons(): boolean` — Holsters and removes every weapon of a living player. Returns: False for a dead or unspawned player.
- `player.setCurrentWeapon(weaponId: number): boolean` — Selects a held weapon. Returns: False when the weapon is not held.
- `player.getCurrentWeapon(): number` — Returns the selected weapon. Returns: The weapon ID, 0 when holstered or not spawned.
- `player.hasWeapon(weaponId: number): boolean` — Checks whether the current life holds a weapon. Returns: True when it is held.
- `player.getWeapons(): WeaponSlot[]` — Lists the held weapons with their ammunition. Returns: The held weapons, empty when not spawned.
- `player.getInventory(): Inventory | null` — Returns the selected weapon and the held weapons. Returns: The inventory, or null when not spawned.
- `player.setInventory(weapons: { weaponId: number; loaded?: number; reserve?: number }[], selected?: number): boolean` — Atomically replaces the whole inventory in one combat state revision. Native inventory permits one hand weapon, five small slots, and one coat slot for a large weapon. Without `selected`, the first item is equipped; an empty list holsters. Invalid selections, duplicates, or an unrepresentable slot layout leave the old inventory intact.
- `player.addItem(weaponId: number, loaded?: number, reserve?: number): boolean` — Adds one item, or adds its ammunition or throwable count to a weapon already held. Returns: False for a dead player or a full inventory.
- `player.removeItem(weaponId: number, count?: number): boolean` — Without count removes the item; with count takes that much and removes the item once none is left. Returns: False when the weapon is not held.
- `player.getCombatState(): CombatState | null` — Returns the server combat snapshot of this player. Returns: The snapshot, or null when the player has none.

Use `trySpendMoney` for purchases: it checks affordability and debits in one server call. A shop must validate the player's location and requested item before calling it. The balance is an integer in whole dollars and has no persistence beyond the connection unless the gamemode stores and restores it.

```js
const price = 75;
if (player.alive && player.trySpendMoney(price)) {
    if (!player.giveWeapon(7, 7, 21)) player.giveMoney(price); // Refund if inventory is full.
}
```

The game's native Free Ride score is one shared counter on each client, and retail scripts can change it locally (the stock pump subtracts 500 when fueling). `Player.money` is the authoritative per-player balance; mirror it to the native score HUD with client `Hud.setScore(LocalPlayer.money)`. For priced fuel, debit with `trySpendMoney` on the server and then call `Vehicle.setFuel` rather than treating the retail score deduction as a purchase.

The camera target is a server-owned choice per observer. It remains selected
across the observer's respawn and the target's despawn/respawn. While the
target has no living native ped, the client shows the observer's own camera;
the remote view resumes when the target spawns again. A disconnected target
clears the choice. The local player continues to control only their own ped.
Player and car replicas are always streamed in this server, so camera follow
does not have a distance limit.

`CombatState.poseTargetOffset` is the camera-facing target relative to the
player's world position. It updates while unaimed or unarmed too. Remote
clients add it to the interpolated player position to pose the neck and back.
`CombatState.crouching` reports the native crouch state.

## Vehicle

- `vehicle.toString(): string` — Formats this vehicle for logging. Returns: The vehicle ID and model.
- `model`: `string`. Stock .i3d model filename.
- `position`: `Vector3`. Last reported position. Read only; use setTransform.
- `rotation`: `Quaternion`. Last reported rotation. Read only; use setTransform.
- `velocity`: `Vector3`. Linear velocity in units per second.
- `angularVelocity`: `Vector3`. Angular velocity.
- `steeringInput`: `number`. Steering in native mapped angle units.
- `fuel`: `number`. Fuel amount.
- `fuelTankCapacity`: `number`. Tank capacity the controller reported.
- `engineRotations`: `number`. Engine rotations, owner telemetry.
- `gear`: `number`. Current gear, owner telemetry.
- `maximumGear`: `number`. Highest gear of the model.
- `lightState`: `number`. Light bits: left indicator 0x1, right indicator 0x2, headlights 0x80, brake active 0x100, brake check 0x800, reverse active 0x1000, reverse check 0x8000, master lights 0x10000.
- `hornOn`: `boolean`. Whether the horn sounds.
- `sirenOn`: `boolean`. Whether the server-owned siren is on.
- `engineOn`: `boolean`. Whether the engine runs.
- `radarColor`: `number`. 0xRRGGBB color of the radar marker.
- `opacity`: `number`. Server-authored model opacity, 0 transparent and 1 opaque.
- `repairRevision`: `number`. Increments when repair resets native damage and deformation.
- `radarVisible`: `boolean`. Whether the vehicle is drawn on every player's radar.
- `speedLimited`: `boolean`. Whether the native speed limiter is on.
- `powerInput`: `number`. Resolved throttle from 0 to 1.
- `brakeInput`: `number`. Resolved brake from 0 to 1.
- `handbrakeInput`: `number`. Resolved handbrake from 0 to 1.
- `dynamicsValid`: `boolean`. False until a controller reported native state; setFuel, setLights and setHorn need it.
- `health`: `number`. Script metadata health set by setDamage.
- `damageFlags`: `number`. Script metadata damage flags.
- `detachedParts`: `number`. Script metadata detached part flags.
- `nativeDamageValid`: `boolean`. Whether a native damage snapshot exists.
- `nativeDamageRevision`: `number`. Revision of the native damage snapshot.
- `meshRevision`: `number`. Revision of the accepted deformation checkpoint.
- `seatCount`: `number`. Number of seats, 1 to 8.
- `terminalState`: `number`. 0 active, 1 exploded, 2 native water contact, 3 fall volume or native invalid fall.
- `missionGeneration`: `number`. Mission generation the vehicle belongs to.
- `engineRevision`: `number`. Revision of the engine state.
- `dynamicsCommandRevision`: `number`. Revision of the last script fuel, lights or horn command.
- `terminalSequence`: `number`. Sequence of the terminal transition.
- `seatSequence`: `number`. Sequence of the last accepted seat transition.
- `vehicle.destroy(): boolean` — Fires vehicleDestroy, then removes the vehicle from every client. Returns: False when it no longer exists.
- `vehicle.getController(): Player | null` — Returns the player whose client simulates this vehicle. Returns: The controller, or null.
- `vehicle.getDriver(): Player | null` — Returns the current-life occupant of seat 0. Returns: The driver, or null.
- `vehicle.getOccupant(seat: number): Player | null` — Returns the current-life occupant of a seat. Returns: The occupant, or null.
- `vehicle.getOccupants(): (Player | null)[]` — Returns the occupant of every seat. Returns: One entry per seat, null when empty.
- `vehicle.getDoors(): number[]` — Returns each seat's door target, from 0 closed to 1 open. Returns: One entry per seat.
- `vehicle.getDamageState(): VehicleDamage | null` — Returns the controller's native damage snapshot. Returns: The snapshot, or null before the first report or while repair awaits a fresh report.
- `vehicle.setTransform(position: Vector3 | { x: number; y: number; z: number }, rotation?: number | Quaternion, velocity?: Vector3 | { x: number; y: number; z: number }): boolean` — Sets the authoritative pose; the simulation controller applies it before it reports again. Returns: False when the vehicle no longer exists.
- `vehicle.setEngine(on: boolean): boolean` — Starts or stops the engine. Fires vehicleEngineChange. Returns: False for a terminal vehicle.
- `vehicle.setFuel(fuel: number): boolean` — Sets the fuel; the controller applies and acknowledges it. After the first native tank report, only this server call may increase fuel; controller movement reports can consume it. Returns: False before native dynamics are available or when out of range.
- `vehicle.setLights(lightState: number): boolean` — Sets the semantic light bits; native blink phase stays game controlled. Returns: False before native dynamics are available or for unsupported bits.
- `vehicle.setHorn(on: boolean): boolean` — Sounds or silences the horn. Returns: False before native dynamics are available.
- `vehicle.setSiren(on: boolean): boolean` — Switches the siren sound and light bar. Drivers toggle it with K. Returns: False for a terminal vehicle.
- `vehicle.setRadarMarker(visible: boolean, color?: number): boolean` — Shows or hides this vehicle on every radar. Returns: False when the vehicle no longer exists.
- `vehicle.setOpacity(opacity: number): boolean` — Changes the car model's opacity on every client; 0 is transparent and 1 is opaque. Fires `vehicleOpacityChange`. Returns false outside that range or for a terminal car.
- `vehicle.repair(): boolean` — Repairs the native engine, gearbox, body, fuel tank, lights, attached wheels, and deform meshes without moving the car or changing seats. Fires `vehicleRepair`. Returns false for a terminal car. Existing loose debris actors remain until their own lifetime ends.
- `vehicle.setSeatCount(count: number): boolean` — Changes the seat count if the removed seats are empty. Returns: False when a removed seat is taken.
- `vehicle.setDamage(health: number, damageFlags: number, detachedParts: number): boolean` — Sets script metadata only; it does not deform the car. Fires vehicleDamageState. Returns: False when the vehicle no longer exists.
- `vehicle.setMechanicalDamage(engineHealth: number, gearboxHealth: number, bodyDamage: number, fuelTankHealth: number): boolean` — Authors a native damage revision the simulation controller applies. Fires vehicleDamage. Returns: False before the first native damage snapshot or for out-of-range values.
- `vehicle.setTerminalState(state: number): boolean` — Marks the vehicle terminal and fires vehicleTerminal. Explosion occupants die before the event; water and fall occupants die after it. Scripts decide when to destroy it. Returns: False when already terminal.
- `vehicle.explode(): boolean` — Same as setTerminalState(1). Returns: False when already terminal.
- `vehicle.recordSeatOutcome(seat: number, player: Player, result: number): boolean` — Records an administrative seat result without native animation and fires the matching seat event. Entering or stealing seat 0 makes the player's client the simulation controller. Returns: False when the result is not valid now.
- `Vehicle.spawn(model: string, position: Vector3 | { x: number; y: number; z: number }, rotation?: number | Quaternion, controller?: Player): Vehicle | null` — Creates a car for the current mission. Fires vehicleSpawn. Returns: The vehicle, or null before every client is mission ready.

`repair()` invalidates the previous native damage and mesh checkpoints. Each
client restores its initialized car through the retail damage/deform reset
paths, and the simulation controller then reports a fresh durable snapshot.
`getDamageState()` may briefly return null while that report is pending.
Repair does not move or upright a car; retail `CAR_REPAIR` does that and also
resets seats and motion, so it is not used here.

## Pickup

- `pickup.toString(): string` — Formats this pickup for logging. Returns: The pickup ID and weapon.
- `weaponId`: `number`. Weapon lying here.
- `heading`: `number`. Facing in radians.
- `loaded`: `number`. Loaded rounds or throwable count.
- `reserve`: `number`. Reserve rounds.
- `pickup.destroy(): boolean` — Removes the pickup from every client. Returns: False when it no longer exists.
- `Pickup.create(weaponId: number, position: Vector3 | { x: number; y: number; z: number }, heading?: number, loaded?: number, reserve?: number, lifetimeMs?: number): Pickup | null` — Places a weapon pickup in the current mission. A mission change removes every pickup. Returns: The pickup, or null when the limit of 256 is reached.
- `Pickup.getAll(): Pickup[]` — Lists every pickup. Returns: The current pickups.

To drop the equipped weapon, use `player.dropWeapon()`. Pass a held weapon ID to
drop an item from the inventory without equipping it first. The server creates
the pickup with its current loaded and reserve ammunition, then removes that
weapon from the inventory in one accepted transition. If pickup creation fails,
the weapon stays in the inventory.

```js
const pickup = player.dropWeapon();
if (pickup) {
    console.log(`${player.nickname} dropped ${Weapon.get(pickup.weaponId)?.name} at pickup ${pickup.id}`);
}

Events.on("weaponDropped", (pickup, owner, info) => {
    console.log(`${owner?.nickname ?? "Unknown"} dropped weapon ${info.weaponId}`);
});
```

`playerWeaponDrop` reports script and controller drops after inventory changes;
`weaponDropped` also reports the selected weapon dropped on death. Script-made
pickups from `Pickup.create` do not raise either drop event.

## Weapon

- `Weapon.list(): WeaponInfo[]` — Lists every weapon the server accepts. Returns: The supported weapons in ID order.
- `Weapon.get(weaponId: number): WeaponInfo | null` — Describes one weapon. Returns: The weapon, or null when the server does not accept it.

## Door

`Door` wraps a stock `C_door` actor in the current mission. When a nearby
player uses a native door, the server registers it automatically and sends its
open target, lock, use state and swing direction to every client. The root and
its linked leaf move together for double doors. A reconnecting client receives
the current target state. A mission change removes these handles.

- `Door.create(frameName, position): Door | null` registers a known root door
  frame before anyone uses it. `position` is its hinge position and supplies
  the server's reach check. Registration returns an existing door with that
  name if one exists; it returns null at the 1024-door limit.
- `Door.get(frameName)` and `Door.getAll()` find registered mission doors.
- `door.openDoor(reverse, pairedReverse)` opens the root and linked leaf with
  independent swing directions. `door.closeDoor()` animates both closed.
- `door.setOpenFraction(fraction, reverse, pairedReverse)` immediately places
  both leaves at a fraction from 0 to 1. A partial pose stays in place until
  the next open or close command.
- `door.locked` prevents native and script opening; closing still works.
  `door.enabled` controls the native use prompt separately from the lock.
- `door.open`, `door.reverse`, `door.openFraction`, and `door.frameName` expose
  the target state. `door.destroy()` stops managing the door until the next
  native use.

`doorStateChange(door, player)` fires after a native or script transition;
`player` is null for a script call. `doorLockChange` and
`doorInteractionChange` fire after scripts change those properties. Native
door names and positions come from the loaded mission, so a script that
pre-registers them should use the exact root frame name and hinge position.

```js
Events.on("doorStateChange", (door, player) => {
    console.log(`${player?.nickname ?? "Script"} ${door.open ? "opened" : "closed"} ${door.frameName}`);
});
```

## World

- `World.getMission(): string` — Returns the current mission. Returns: Stock or detected mod mission directory name.
- `World.getMissionGeneration(): number` — Returns the current mission generation. Returns: It increases on every change, including a reload.
- `World.changeMission(mission: string): boolean` — Resets the world and asks every client to load the mission. Fires missionChange. Returns: False for an unknown mission.
- `World.isReady(): boolean` — Whether every connected player has loaded the current mission. Returns: True when all are ready.
- `World.getPlayers(): Player[]` — Lists the connected players. Returns: Every connected player.
- `World.getVehicles(): Vehicle[]` — Lists the server vehicles. Returns: Every vehicle.
- `World.getPickups(): Pickup[]` — Lists the weapon pickups. Returns: Every pickup.
- `World.getDoors(): Door[]` — Lists the server-managed mission doors.
- `World.setWeather(weather: "default" | "clear" | "rain" | "snow", intensity?: number): void` — Changes the weather for everyone, including late joiners; it carries over mission changes.
- `World.getWeather(): WeatherInfo` — Returns the replicated weather. Returns: The weather preset and intensity.
- `World.setCityMusic(enabled: boolean): void` — Turns the city music on or off for everyone.
- `World.isCityMusicEnabled(): boolean` — Whether the city music plays. Returns: True when enabled.
- `World.setNightMode(enabled: boolean | null): void` — Overrides the native GAME_NIGHTMISSION flag for everyone, including late joiners. Null restores each mission's own flag. This affects night behavior such as vehicle lights, not the sky or clock.
- `World.getNightModeOverride(): boolean | null` — Returns the server override, or null while mission scripts own the night flag.
- `World.setFrameVisible(name: string, visible: boolean): boolean` — Shows or hides a scene object for everyone, including late joiners, until the mission changes. Returns: False when 128 frames are already overridden.
- `World.resetFrames(): void` — Restores every overridden frame to the scene's own visibility.
- `World.setFrameOpacity(name: string, opacity: number): boolean` — Applies `FRM_SETALPHA` to a named static visual frame for everyone in this mission, including late joiners. Opacity is 0–1; returns false at the 128-frame limit.
- `World.resetFrameOpacity(name?: string): void` — Restores the original opacity for one overridden frame or all of them. Mission changes clear these overrides.
- `World.createExplosion(position: Vector3 | { x: number; y: number; z: number }, options?: { radius?: number; damage?: number }): void` — Explodes for every client. The server applies player damage and fires playerDamage and playerDeath; clients damage cars and objects.
- `World.createFire(position: Vector3 | { x: number; y: number; z: number }, options?: { duration?: number; radius?: number; damage?: number }): void` — Starts a fire every client sees; the server burns players standing in it.

## Sound

- `sound.toString(): string` — Formats this sound for logging. Returns: The sound ID and wave.
- `wave`: `string`. Game sound file, such as "00_dog.wav".
- `volume`: `number`. Volume from 0 to 1.
- `radius`: `number`. Audible radius from 1 to 200 units.
- `enabled`: `boolean`. Whether the sound plays; a disabled sound keeps its place.
- `sound.attachTo(entity: Player | Vehicle | null): boolean` — Makes the sound follow a player or vehicle. Returns: False when the sound was destroyed.
- `sound.getAttached(): Player | Vehicle | null` — Returns the followed entity. Returns: The entity, or null for a fixed sound.
- `sound.destroy(): boolean` — Removes the sound for everyone. Returns: False when it was already gone.
- `Sound.create(wave: string, position: Vector3 | { x: number; y: number; z: number }, options?: { radius?: number; volume?: number; attachTo?: Player | Vehicle }): Sound | null` — Places a looping positional sound. Returns: The sound, or null when the limit of 64 sounds is reached.
- `Sound.play(wave: string, position: Vector3 | { x: number; y: number; z: number }, options?: { radius?: number; volume?: number }): void` — Plays a sound once for every client currently in the mission.

## Events

`Events.on(name, handler)` subscribes to a native event or a shared resource
event and returns an unsubscribe function. `once` handles only the next event;
`off` removes a handler. `emit(name, ...args)` and
`emitTo(resourceName, name, ...args)` return Promises that settle after the
receivers. `onLocal` and `emitLocal` stay within the calling resource.

Client messages are isolated from native events. Register them with
`Events.onClient(name, (player, data) => {})`; the player is the sender.
`onceClient` and `offClient` manage that separate table. A client calls
`Events.emitServer(name, payload)` to send the message. The server can call
`player.emit(name, JSON.stringify(payload))` for one client or
`Events.emitAllClients(name, payload)` for all clients. The bridge sends a
string payload verbatim and receivers parse it as JSON, so a string must
already contain valid JSON text. Objects, arrays, and numbers passed to
`emitAllClients` are serialized for you. `player.emit` always expects a JSON
string.

```js
Events.onClient("shop:buy", (player, request) => {
    if (!player.alive || !request || typeof request.itemId !== "number") return;
    player.emit("shop:result", JSON.stringify({ accepted: true }));
});
```

The matching client script sends the request and receives the reply:

```js
Events.on("shop:result", (result) => {
    Hud.showMessage(result.accepted ? "Purchase accepted" : "Purchase declined");
});
Events.emitServer("shop:buy", { itemId: 6 });
```

Use `Events.emitAllClients("shop:stock", { itemId: 6, remaining: 3 })`
when every client should receive an update. Client requests are untrusted;
the server handler must validate the sender and payload before changing state.
The sample gamemode demonstrates both directions in
[`server/main.js`](../resources/sample-gamemode/server/main.js) and
[`client/main.js`](../resources/sample-gamemode/client/main.js).

The [visual pickup demo](../resources/sample-gamemode/server/visual_pickups.js)
shows a server-owned collectible drawn entirely by
[client script](../resources/sample-gamemode/client/visual_pickups.js): the
server publishes its position and active state, and validates mission
generation, life, and distance before awarding ammo. Client-local models and
labels cannot grant a reward on their own. Use `Pickup.create` instead when
the item should be a native weapon pickup with the game's use interaction.

The table lists native server event arguments. `Events.onClient` event names
are defined by your resources.

| Event | Arguments | When |
| --- | --- | --- |
| `resourceStart` | `[resourceName: string]` | Dispatched after a resource entry point has run and immediately before the resource becomes running. |
| `resourceStop` | `[resourceName: string]` | Dispatched while a resource is stopping, before its stop callback, timers, exports and event handlers are cleaned up. |
| `consoleCommand` | `[command: string, args: string[]]` | Dispatched after the server console parses a command line that no built-in command handles. |
| `entityStateChange` | `[entity: Entity, key: string, value: any, previous: any]` | A server script changed one entity state key. Removed and previously absent values are `undefined`. |
| `missionChange` | `[mission: MissionInfo]` | Dispatched after World.changeMission has reset the world and asked every client to load the new mission. |
| `missionReady` | `[mission: MissionInfo]` | Dispatched once each time every connected player has loaded the current mission generation. At least one player must be connected. |
| `playerMissionReady` | `[player: Player, info: MissionLoadInfo]` | Dispatched when a player's client has loaded the current mission. |
| `playerMissionUnloaded` | `[player: Player, info: MissionLoadInfo]` | Dispatched when a player's client has unloaded its mission. |
| `playerMissionLoadFailed` | `[player: Player, info: MissionLoadInfo]` | Dispatched when a player's client could not load the current mission. |
| `playerConnect` | `[player: Player]` | Dispatched after a player has connected and has a player handle. The player is not spawned yet. |
| `playerDisconnect` | `[player: Player]` | Dispatched while a player disconnects, before the player handle stops resolving. |
| `playerChat` | `[player: Player, text: string]` | Dispatched for an accepted, sanitized chat line after it was relayed. |
| `playerCommand` | `[player: Player, command: string, args: string[]]` | Dispatched when a chat line starting with `/` passes the chat rate limit. |
| `playerNicknameChange` | `[player: Player, oldNickname: string, nickname: string]` | Dispatched after Player.setNickname changed the name. |
| `playerModelChange` | `[player: Player, oldModel: string, model: string]` | Dispatched after Player.setModel changed the model. |
| `playerMoneyChange` | `[player: Player, oldMoney: number, money: number]` | Dispatched after the server changes the player's balance. |
| `playerSpawn` | `[player: Player]` | Dispatched after Player.spawn started a new life. |
| `playerRespawn` | `[player: Player]` | Dispatched after Player.respawn started a new life. |
| `playerDespawn` | `[player: Player]` | Dispatched after Player.despawn ended the current life. |
| `playerDamage` | `[victim: Player, info: PlayerHealthInfo]` | Dispatched after an accepted health decrease. |
| `playerDeath` | `[victim: Player, killer: Player | null, info: PlayerHealthInfo]` | Dispatched after a fatal health decrease, following playerDamage. The killer is null when no connected attacker owns the damage; info.weaponId identifies the validated weapon when known. Death never respawns a player by itself; call Player.respawn. |
| `playerHealthChange` | `[player: Player, info: PlayerHealthInfo]` | Dispatched after an accepted health increase. |
| `playerVoiceStart` | `[player: Player]` | The player's voice relay started receiving speech. |
| `playerVoiceStop` | `[player: Player]` | The player's voice relay stopped receiving speech. |
| `weaponDropped` | `[pickup: Pickup | null, player: Player | null, info: PickupEventInfo]` | Dispatched when a drop or a death leaves a weapon pickup in the world. |
| `pickupTaken` | `[pickup: Pickup | null, player: Player | null, info: PickupEventInfo]` | Dispatched after the server moved a pickup into a player's inventory. The pickup handle is null when nothing is left on the ground. |
| `vehicleSpawn` | `[vehicle: Vehicle]` | Dispatched after Vehicle.spawn created a vehicle. |
| `vehicleDestroy` | `[vehicle: Vehicle]` | Dispatched immediately before Vehicle.destroy removes the vehicle. A mission change removes every vehicle without this event. |
| `vehicleDamage` | `[vehicle: Vehicle, damage: VehicleDamage | null]` | Dispatched when the native per-part damage snapshot changes. |
| `vehicleOpacityChange` | `[vehicle: Vehicle]` | Dispatched after `Vehicle.setOpacity` changes the server-owned opacity. |
| `vehicleRepair` | `[vehicle: Vehicle]` | Dispatched after `Vehicle.repair` starts a native damage and deformation reset. |
| `vehicleTerminal` | `[vehicle: Vehicle, state: number]` | Dispatched when a vehicle explodes (1), its native body hits water (2), or it enters a fall volume or native invalid-fall state (3). Water and fall occupants die through server combat immediately after this event; explosion occupants die before it. Scripts decide when to destroy the car. |
| `vehicleHit` | `[vehicle: Vehicle, info: VehicleHitInfo]` | Dispatched when a pellet hit is replayed to the vehicle's simulation controller, before the resulting vehicleDamage. |
| `vehiclePartDetached` | `[vehicle: Vehicle, info: VehiclePartInfo]` | Dispatched when the server accepts a native loose part from the vehicle's controller. |
| `vehiclePlayerEntering` | `[vehicle: Vehicle, player: Player, info: VehicleSeatInfo]` | Dispatched when the server accepts the start of a native enter or steal. |
| `vehiclePlayerEntered` | `[vehicle: Vehicle, player: Player, info: VehicleSeatInfo]` | Dispatched when a player sits in a seat, natively or through Player.putInVehicle. |
| `vehiclePlayerExited` | `[vehicle: Vehicle, player: Player, info: VehicleSeatInfo]` | Dispatched when a player leaves a seat, including by death, respawn or removeFromVehicle. |
| `vehiclePlayerExitBlocked` | `[vehicle: Vehicle, player: Player, info: VehicleSeatInfo]` | Dispatched when a script records an administrative blocked exit; native blocked exits use seat transfer or the retail emergency exit. |
| `vehiclePlayerSeatChanged` | `[vehicle: Vehicle, player: Player, info: VehicleSeatInfo]` | Dispatched after a blocked-side exit moves the player into the paired seat. `info.fromSeat` is the prior seat. |
| `playerWeaponEquip` | `[player: Player, info: WeaponActionInfo]` | Dispatched after the server accepted this controller action. |
| `playerAimChange` | `[player: Player, info: WeaponActionInfo]` | Dispatched after the server accepted this controller action. |
| `playerWeaponDrop` | `[player: Player, info: WeaponActionInfo]` | Dispatched after the server accepted this controller action. |
| `playerWeaponReload` | `[player: Player, info: WeaponActionInfo]` | Dispatched after the server accepted this controller action. |
| `playerWeaponFire` | `[player: Player, info: WeaponActionInfo]` | Dispatched after the server accepted this controller action. |
| `playerWeaponThrow` | `[player: Player, info: WeaponActionInfo]` | Dispatched after the server accepted this controller action. |
| `playerWeaponThrowStart` | `[player: Player, info: WeaponActionInfo]` | The native grenade or Molotov windup began. |
| `playerWeaponThrowRelease` | `[player: Player, info: WeaponActionInfo]` | The charge was released into the throw animation; `throwHoldMs` gives its duration. |
| `playerWeaponThrowCancel` | `[player: Player, info: WeaponActionInfo]` | A press ended before the minimum charge. |
| `playerWeaponMeleeStart` | `[player: Player, info: WeaponActionInfo]` | The native melee press began; bat windup starts while held. |
| `playerWeaponMelee` | `[player: Player, info: WeaponActionInfo]` | The native melee release played. `meleeIndex` and `meleeHoldMs` describe the local attack. |
| `playerWeaponMeleeCancel` | `[player: Player, info: WeaponActionInfo]` | The native melee press ended without an attack. |
| `vehicleEngineChange` | `[vehicle: Vehicle]` | Dispatched after the matching vehicle state changed, from its simulation controller or a script. |
| `vehicleFuelChange` | `[vehicle: Vehicle]` | Dispatched after the matching vehicle state changed, from its simulation controller or a script. |
| `vehicleLightsChange` | `[vehicle: Vehicle]` | Dispatched after the matching vehicle state changed, from its simulation controller or a script. |
| `vehicleHornChange` | `[vehicle: Vehicle]` | Dispatched after the matching vehicle state changed, from its simulation controller or a script. |
| `vehicleSirenChange` | `[vehicle: Vehicle]` | Dispatched after the matching vehicle state changed, from its simulation controller or a script. |
| `vehicleGearChange` | `[vehicle: Vehicle]` | Dispatched after the matching vehicle state changed, from its simulation controller or a script. |
| `vehicleDamageState` | `[vehicle: Vehicle]` | Dispatched after the matching vehicle state changed, from its simulation controller or a script. |

**MissionInfo** — The server's current mission.

- `mission: string` — Stock or detected mod mission directory name.
- `missionGeneration: number` — Generation of this mission load; it increases on every change, including a reload of the same mission.

**MissionLoadInfo** — A client's report about loading a mission generation.

- `missionGeneration: number` — Generation the report refers to.
- `state: number` — 0 unloaded, 1 ready, 2 scene failure, 3 collision failure, 4 game initialization failure.

**PlayerHealthInfo** — An accepted change of a player's server health.

- `attacker: Player | null` — Player the damage is attributed to, or null when the source is absent or has disconnected.
- `weaponId: number | null` — Validated weapon used for this damage: 0 is fists, 5 is a Molotov, 15 is a grenade. Null for script, fall, vehicle or unknown damage; remains known if the attacker disconnects after throwing.
- `cause: "script" | "firearm" | "melee" | "explosion" | "fire" | "fall" | "drowning" | "vehicle"` — Server damage path. Explosions and fires can carry a weapon ID when they came from a thrown weapon.
- `oldHealth: number` — Health before the change.
- `health: number` — Health after the change.
- `amount: number` — oldHealth minus health; negative for healing.
- `deathAnimation: number` — Death animation chosen by the server for a fatal change, otherwise 0.
- `missionGeneration: number` — Mission generation of the affected life.
- `spawnGeneration: number` — Spawn generation of the affected life.

**WeaponActionInfo** — A weapon action the server accepted from a player's client.

- `weaponId: number` — Weapon the action used.
- `selectedWeapon: number` — Selected weapon after the action; 0 is holstered.
- `inventoryMask: number` — Bit n is set when weapon n is held.
- `loaded: number` — Loaded ammunition of weaponId after the action.
- `reserve: number` — Reserve ammunition of weaponId after the action.
- `aiming: boolean` — Whether the player is aiming.
- `direction: { x: number; y: number; z: number }` — Unit aim or throw direction.
- `target: Player | null` — The targeted player the client suggested for a shot; confirmed damage arrives as playerDamage.
- `revision: number` — Server combat state revision.
- `meleeIndex: number` — Native combo index 0–3, or -1 for a heavy finisher on a melee release.
- `meleeHoldMs: number` — Measured local press duration on a melee release, in milliseconds.
- `throwHoldMs: number` — Native grenade or Molotov charge duration on a throw release, capped at 2000 milliseconds; zero for other actions.

**CombatState** — A player's current server combat snapshot from
`player.getCombatState()`.

- `revision: number` — Snapshot revision.
- `health: number`, `alive: boolean`, `spawned: boolean` — Life state.
- `missionGeneration: number`, `spawnGeneration: number` — Mission and life identity.
- `inventoryMask: number`, `selectedWeapon: number`, `ammo: { loaded: number; reserve: number }[]` — Inventory state, with ammunition indexed by weapon ID.
- `deathAnimation: number`, `aiming: boolean`, `crouching: boolean` — Native pose and death state.
- `aimDirection: { x: number; y: number; z: number }` — Accepted aim direction.
- `poseTargetOffset: { x: number; y: number; z: number }` — Camera-facing target relative to player position; also updated outside weapon aim.

**PickupEventInfo** — A weapon pickup transition.

- `pickupId: number` — Network ID of the pickup, which may no longer exist.
- `weaponId: number` — Weapon of the pickup.
- `position: { x: number; y: number; z: number } | null` — Where the pickup lies, or null once it is gone.

**VehicleHitInfo** — An accepted firearm pellet against a vehicle.

- `shooter: Player | null` — Player who fired, or null when they have left.
- `damage: number` — Damage derived from the accepted shot.
- `position: { x: number; y: number; z: number }` — World-space impact position.
- `shotSequence: number` — Shooter's fire sequence.
- `pelletIndex: number` — Pellet of that shot.

**VehiclePartInfo** — A loose part a vehicle shed.

- `debrisId: number` — Network ID of the temporary debris replica.
- `type: number` — 1 wheel, 2 bumper, 3 light, 4 license plate, 5 mirror, 6 wing, 7 door.
- `partIndex: number` — Native part index within the model.
- `position: { x: number; y: number; z: number }` — Where the part came off.

**VehicleSeatInfo** — An accepted seat transition.

- `seat: number` — Seat index; 0 is the driver.
- `fromSeat: number | null` — Previous seat for a native climb across the car; otherwise null.
- `stolen: boolean` — True when the player pulled the previous driver out.
- `serverSequence: number` — Server seat sequence of the transition.

## Behaviour notes

- **Death and respawn.** Death never respawns anyone. `playerDamage` is followed
  by `playerDeath` for a fatal change; call `player.respawn(position)` when the
  gamemode decides. A new life starts with 100 health and an empty inventory.
- **Vehicle simulation controller.** The driver's client simulates a car; with
  no driver a passenger's, and an empty car follows the nearest player. Seat
  results come from native entries, steals and exits and are authoritative on
  the server; a steal evicts the victim as soon as it starts.
- **Chat and nicknames.** Nicknames are sanitized and made unique; chat is rate
  limited per player. Lines starting with `/` raise `playerCommand` instead of
  being relayed.
- **Pickups.** Dropped and dead players' weapons become pickups for two
  minutes; scripts create their own with `Pickup.create`.
- **Mission changes** remove every vehicle, pickup and sound and clear frame
  overrides; weather and city music carry over.
