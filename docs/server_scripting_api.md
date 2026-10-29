# Server scripting API

Server resources run in Node.js. Mafia1Online adds `Player`, `Vehicle`,
`Pickup`, `Door`, `MapObject`, `Sound`, `Weapon`, and `World` to the framework scripting globals.
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

See [the Free Ride gamemode](../resources/freeride/server/main.js) for
mission loading, vehicle commands, respawning, and server to client events.

## API map

| Area | Entry points | Purpose |
| --- | --- | --- |
| Players | `World.getPlayers()`, `Player` | Lives, health, inventory, seats, chat, connection details. |
| Vehicles | `Vehicle.spawn()`, `World.getVehicles()` | Cars, occupancy, native damage, engine and physics commands; traffic cars the simulation host drives. |
| Pedestrians | `Pedestrian.getAll()`, `Pedestrian.setDensity()` | The city's walkers the simulation host runs around every player. |
| NPCs | `Npc.spawn()`, `Npc.getAll()`, `Npc.getInRange()` | Humans the host runs with the retail AI: gangsters, policemen, thrown-out traffic drivers, and scripted NPCs that follow, walk, drive and fight on command. |
| Police | `Police.getRecord()`, `Police.setWanted()` | Each player's retail crime record, wanted level and the officers it sends. |
| Taxi | `Taxi.setHailable()`, `Taxi.board()` | Walkers hailing players' cabs the retail way, and the NPC passengers they become. |
| Railways | `Railway.getAll()`, `Railway.getInRange()` | The mission's trams and metro trains, which the host runs and every player sees. |
| Bridges | `Bridge.getAll()`, `Bridge.get()` | The mission's drawbridges, which the host raises for its steamers and every player sees. |
| Gas station attendants | `Pumper.getAll()`, `Pumper.setPrice()` | The retail attendants, who fuel players' cars at the pumps for the price the gamemode sets. |
| Pickups and weapons | `Pickup.create()`, `Weapon` | World weapon items and the supported stock weapon catalog. |
| Mission doors | `Door.create()`, `Door.getAll()` | Lock, use, swing and paired-leaf state shared across clients. |
| Map objects | `MapObject.create()`, `MapObject.getAll()` | Script-placed models with collision that can move, for custom maps. |
| Fires and hoses | `Fire.create()`, `WaterJet.create()` | Fires that burn until water or a script puts them out, and the hoses and truck cannons that do it. |
| World | `World` | Missions, weather, music, scene frames, fire, explosions, world prop repair. |
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

The game's native Free Ride score is one shared counter on each client, and retail scripts can change it locally (the stock pump subtracts 500 when fueling; the attendant here does not). `Player.money` is the authoritative per-player balance; mirror it to the native score HUD with client `Hud.setScore(LocalPlayer.money)`. For priced fuel at the stock pumps use `Pumper.setPrice`, which debits `Player.money`; elsewhere debit with `trySpendMoney` and then call `Vehicle.setFuel`.

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
- `traffic`: `boolean`. True for a car of the city's traffic the simulation host spawned, also after a player took it (`vehicleTrafficTaken`).
- `trafficKind`: `"none" | "civil" | "police" | "mafia"`. What kind of traffic car it is; none for a script car.
- `hostDriven`: `boolean`. True while the simulation host drives it: its traffic driver or an NPC crew sits in it. Players only steal it or take a seat nobody holds.
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

Traffic cars are ordinary `Vehicle` handles the server creates for the
simulation host. While `hostDriven` is true their seats belong to the host's
crew: a player either steals the driver's seat (the driver is thrown out and
stays as a `driver` NPC) or takes a seat nobody holds, and the car becomes that
player's (`vehicleTrafficTaken`). Police traffic cars stay closed to players.

## Pedestrian

The city's walkers. The simulation host runs them with the retail AI around
every player; a walker that walks again after being retired is a new
pedestrian.

- `pedestrian.toString(): string` — Formats this pedestrian for logging. Returns: The pedestrian ID and model.
- `model`: `string`. Retail .i3d model filename.
- `position`: `Vector3`. Position the simulation host last reported.
- `heading`: `number`. Facing in radians.
- `alive`: `boolean`. False once it died; the corpse stays until it retires.
- `umbrella`: `boolean`. Whether it holds a rain umbrella; a hit drops it.
- `animation`: `number`. Retail traffic animation id; 6 is its walk.
- `walkStyle`: `number`. Retail walk style, 0 to 4.
- `pedestrian.remove(): boolean` — Asks the simulation host to retire it now; `pedestrianRetire` follows. Returns: False when it no longer exists or no host is attached.
- `Pedestrian.getAll(): Pedestrian[]`, `Pedestrian.getInRange(position, radius): Pedestrian[]`, `Pedestrian.getCount(): number` — The current pedestrians.
- `Pedestrian.setEnabled(enabled: boolean): boolean`, `Pedestrian.isEnabled(): boolean` — Turns pedestrian traffic on or off; off retires every pedestrian at once.
- `Pedestrian.setDensity(density: number): boolean`, `Pedestrian.getDensity(): number` — 0 to 1 of the retail count around each group of players; existing ones retire naturally.
- `Pedestrian.setCorpseLifetime(milliseconds: number): boolean`, `Pedestrian.getCorpseLifetime(): number` — How long a dead pedestrian stays, 0 to 3600000 (default 30000); 0 keeps it until every player is far away.

## Npc

An armed human the simulation host runs with the retail AI: a gangster
pedestrian someone hit, a policeman the police sent, or a traffic driver a
player threw out of its car. Every player sees it; its shots, hits and death
are validated like a player's.

- `npc.toString(): string` — Formats this NPC for logging. Returns: The NPC ID, kind and model.
- `kind`: `string`. `"gangster"`, `"police"` or `"driver"`.
- `model`: `string`. Retail .i3d model filename.
- `position`: `Vector3`. Position the simulation host last reported.
- `heading`: `number`. Facing in radians.
- `alive`: `boolean`. False once it died; the corpse stays until it retires.
- `health`: `number`. Health the simulation host last reported; a gangster starts at 200.
- `weapon`: `number`. Retail item id of the weapon in hand, 0 for none; 10 is the Thompson.
- `npc.getInventory(): Inventory | null` — The selected weapon and the held weapons the server records for it.
- `npc.getThrownFromVehicle(): Vehicle | null` — The car a `driver` NPC was thrown out of when a player stole it; null for another kind or once that car is gone.
- `npc.remove(): boolean` — Asks the simulation host to retire it now; `npcRetire` follows. This also removes a scripted NPC.
- `Npc.getAll(): Npc[]`, `Npc.getInRange(position, radius): Npc[]`, `Npc.getCount(): number` — The current NPCs.

### Scripted NPCs

`Npc.spawn` creates a retail `C_entity` on the simulation host that the
script gives tasks, the way a stock mission script drives its companions and
enemies. Each task runs the calls the retail entity script command makes, so
the retail AI does the walking, following, boarding, driving and fighting;
every player sees it like any other NPC. Only kind `"scripted"` NPCs take
tasks.

- `Npc.spawn(model: string, position, heading?: number, options?): Npc | null` — Spawns it. `options`: `health` (default 100), `behaviour`, `weapon`, `ammo`, `reserve` (a weapon to hold from the start) and `virtualWorld` (only players in that world see it; default every world). The handle works at once; `npcSpawn` follows once the host created it, `npcRetire` if it could not (an unknown model). Null when no simulation host is attached, no mission is loaded, 64 scripted NPCs exist or an argument is unusable.
- `behaviour`: `string`. How the retail AI rates others (`ai_logs::Event` by behaviour class; players count as guards, players on police duty (`Police.setOfficer`) as policemen):
  - `"neutral"` (default, reacts on hit): ignores players until one aims at or shoots it.
  - `"friendly"` (guard): every player and every friendly NPC is a friend; gangsters are enemies it fights on sight.
  - `"hostile"` (moves away, like the gangsters a hit pedestrian becomes): every player and friendly NPC is an enemy.
  - A player on police duty is a friend to `"neutral"` NPCs (which do not turn on a friend that hits them), unknown to `"friendly"` ones and neutral to `"hostile"` ones until one attacks the other.
- Every task method returns a task id (0 when refused) and replaces the task before it, which ends as `"cancelled"`. `npcTaskComplete` reports how each ends.
  - `npc.follow(target: Player | Npc, distance = 2): number` — `Entity_ActionFollow`: follows, runs to catch up and turns towards the target, until another task or the target dies or leaves. It resumes after a fight.
  - `npc.walkTo(position, { run?, radius?, direct? }): number` — `EntityMoveToFrame`: walks (or runs) over the mission's pedestrian paths (`check.bin`) to within `radius` (default 0.5). Completed on arrival; failed when the path is not found three times. A room the paths do not reach (a map-object interior, the paths' nodes being out of sight) has no way there: with `direct: true` it walks in a straight line instead, the way a retail entity steps along its script path (`AI_human_goto_target` once a frame, as `C_entity::scriptTick` does, with the entity's AI asleep as the script's `set_state SLEEP` leaves it). Every player sees it walk with its walk or run animation; walls and other actors stop it, and a walk that gains less than 0.3 in 3 seconds fails. Give a route of several stops as several walks.
  - `npc.enterVehicle(vehicle: Vehicle, seat = 0, { run? }): number` — `Entity_MoveToCar` to the door, then `Enemy_UseCar`'s `Use_Actor` within 3 of a nearly still car. The car must be a script's car with no player in it, not a traffic car: the simulation host drives it while a scripted NPC boards or sits in it, and players cannot get in meanwhile. Completed once seated. A passenger seat (1 and up) of any car a player drives works too: the player keeps driving it, the NPC walks to the door once the car stands nearly still and rides along, seated in that car on every client (`npc.exitVehicle()` gets it out the same way). The Taxi module boards its passengers so.
  - `npc.exitVehicle(): number` — `Enemy_UseCar` leave once the car is nearly still. Completed once out.
  - `npc.driveTo(position): number` — `Entity_CarMoveTo`: drives the car it sits in as driver over the road graph (`road.bin`) and stops there. Completed on arrival, failed when the retail driver gives up or is blocked.
  - `npc.attack(target: Player | Npc): number` — `Entity_ActionFire`: picks its best weapon, chases to its range and shoots (from a car seat it shoots out of the window). Completed when the target is dead.
  - `npc.stop(): number` — `Entity_ActionsClear`: drops every action. Completed at once.
- `npc.face(target: number | position): boolean` — Turns it on the spot to a heading in radians or towards a point (`ACTOR_SETDIR`). A task that moves it turns it again.
- `npc.giveWeapon(weaponId, ammo?, reserve?): boolean` — `HUMAN_ADDWEAPON` on its native inventory; the AI chooses what it holds. `getInventory()` shows it once the host reports it.
- `npc.setHealth(health): boolean` — Sets its health (above 0).
- `npc.playAnimation(name, { loop? }): boolean` — Plays a stock human clip, a bare `.i3d` name such as `"x barman stativ.i3d"`, the way a retail mission script's `PERSON_PLAYANIM` does (`C_human::Do_PlayAnim(name, simple, 1)`): looping by default, once with `loop: false`, after which it stands again. It plays on the simulation host and on every player's copy, also for players who see the NPC later (a one-shot clip only for those who saw it start). It ends the running task (`"cancelled"`), and any task stops the clip. A clip the game does not have shows nothing. `npc.stopAnimation()` stops it (`PERSON_STOPANIM`), and `npc.animation` reads the clip, an empty string for none.
- `npc.say(line: number): boolean` — Says a retail voice line (`C_human::NewScream(line)`: `tables/voices.def` group `line - 2`) in the NPC's voice on every player that streams it, positioned on its head and cutting off the line it was saying; later viewers do not replay it. `npc.voice` (0 to 39, `C_human` `m_uVoice`) is fixed at spawn: the `voice` spawn option, else retail's ranges (police 10-11, gangsters 12-16, thrown-out drivers 26-28, otherwise 12; women in retail traffic use 29-31). Ids the game uses: 6-8 a group event, 15-16 seeing a body, 21-23 finding someone, 33-36 ready to fight, 45-50 a fight spotted, 61-65 freezing, 66-69 panic, 70-74 searching, 75-78 giving up, 79-83 hit, 84 a strong leg hit, 85-91 pain, 92-93 hit while dying, 132-134 a car steal refused, 137-138 out of fuel, 139-144 police halt, 145-146 police grab, 147-149 police grab of an armed man, 150-151 a police car warning, 152-157 police notifying a crime, 161-167 thrown out of a car (reM `C_human.cpp`, `C_player.cpp` and the `C_entity` talk table). Other ids play whatever line that group holds; a voice with no sound for it says nothing.
- `npc.getTask(): { id, task } | null` — The task still running.
- `npc.getVehicle(): Vehicle | null` — The car it sits in.

A scripted NPC never retires by distance. A dead one stays for 60 seconds,
then goes (`npcRetire`); `npc.remove()` removes it earlier. `npcDamage` and
`npcDeath` fire for it like for any NPC. `npc.setVirtualWorld()` changes which
players see it; the host runs it in its one retail world, so it still meets
players of every world there.

```js
// A policeman walks to a cell door, turns to it and goes.
const officer = Npc.spawn("pol01.i3d", { x: -1890.5, y: -5.2, z: 18.1 }, Math.PI, { behaviour: "neutral", virtualWorld: 7 });
let walk = 0;
Events.on("npcSpawn", (npc) => {
    if (officer && npc.id === officer.id) walk = officer.walkTo({ x: -1884.0, y: -5.2, z: 24.6 });
});
Events.on("npcTaskComplete", (npc, info) => {
    if (!officer || npc.id !== officer.id || info.id !== walk) return;
    if (info.success) officer.face({ x: -1884.0, y: -5.2, z: 26.0 });
    setTimeout(() => officer.remove(), 5000);
});

// A companion that follows a player and fights for them.
const guard = Npc.spawn("frank.i3d", player.position, 0, { behaviour: "friendly", weapon: 10, reserve: 200 });
guard?.follow(player, 2.5);
```

Not supported yet: a companion that boards its leader's car by itself (it
walks to the car but does not board; `npc.enterVehicle(car, seat)` with a
passenger seat does), and turning a running NPC friendly or hostile after it
spawned.

```js
// A barman behind the counter, as the retail Morello's bar script has him:
// standing, then wiping the counter.
const barman = Npc.spawn("Luigi.i3d", { x: -1776.1, y: -5.44, z: 10.26 }, -3.07, { virtualWorld: 0 });
Events.on("npcSpawn", (npc) => {
    if (!barman || npc.id !== barman.id) return;
    barman.playAnimation("x barman stativ.i3d");
    setTimeout(() => barman.playAnimation("x barman stul.i3d", { loop: false }), 7000);
});
```

## Police

The retail police, which the simulation host runs for every player. Officers
come from police walkers and from the traffic's police cars, which also chase
players and set up roadblocks.

- `Police.getRecord(player: Player): PoliceRecord | null` — The player's crime record as the host last reported it; null when the player is not spawned.
- `Police.setWanted(player: Player, level: number): boolean` — Raises the record to 0 none, 1 minor, 2 arrest or 3 lethal, which sends the police the retail way, or lowers it (calling off the officers first). `playerWantedChange` follows. False when the player is not spawned, no simulation host is attached, or the police are off and the level would rise.
- `Police.clear(player: Player): boolean` — Settles the record and calls off every officer after the player.
- `Police.setEnabled(enabled: boolean): void`, `Police.isEnabled(): boolean` — Turns the police reaction to crimes on or off for everyone. While off, crimes raise nothing (no `playerCrime`), `Police.setWanted` cannot raise a level and a hit officer opens no new case; officers already on a case keep it until `Police.clear`.
- `Police.setOfficer(player: Player, officer: boolean): boolean`, `Police.isOfficer(player: Player): boolean` — Puts a player on police duty for the retail police, the way retail treats its own officers: the host keeps no crime record for them (retail records only its player and the people a mission script adds, never an officer), so no crime, wanted level, case, arrest, fine or car hunt touches them, and NPCs rate them as a policeman (the behaviour class retail gives its police walkers). A record they had is settled at once (`playerWantedChange` to 0) and `Police.setWanted` cannot raise it. `false` makes them a citizen again with a clean record. It lasts until changed or the player leaves; false only for an unknown player.
- `Police.sendBackup(player: Player): number` — Sends a police car with two officers after a wanted player, the retail reinforcements a rising wanted level brings (`C_shvestky::DalsiChalpiDoPartie`): it sets out from a road about 140 m away and hunts them as their level says (kill, arrest or pursue), and its officers join the NPCs as kind `police`. `policeBackup` answers whether it set out (the police pool needs two free officers). Returns the request id, or 0 when the player is not wanted, is an officer, the police are off or no simulation host is attached.

## Taxi

Retail taxi passengers. Single player lets the walkers hail the player's cab
once the mission script enables it (`taxidriver_enable`): in a taxi model at
20 m/s or less with no passenger aboard and nobody after them, a walker 20 to
90 m ahead within 36 degrees of the cab's heading starts waving
(`GeneratePassengerForTaxi`, 0x44f7d0, on a 0.5 to 2.1 s countdown), turns
to the cab and keeps waving (`MavaNaTaxi`, 0x451ff0); once the cab stands
still within 10 m it becomes a `system\taxi_pas.dat` passenger, boards the
back seat, shows the destination and pays fare plus fare times the time left
on arrival. The simulation host runs that hailing for every player a script
allows, with each player's own countdown, police record and taxi; the waves
show on every client. The script decides the rest.

- `Taxi.setHailable(player: Player, hailable: boolean): boolean`, `Taxi.isHailable(player): boolean` — Whether walkers may hail the player's cab (retail `EnableTaxiDriver`, per player). Retail's other conditions still apply on the host: the driver's seat of a taxi model (`taxi`, `fordapicktaxi`, `phantomtaxi` and retail's hot rods `hotrodp2/4/5`, `tbirdold`, `fordhot`, `blackdragon`), no passenger aboard, no officer after the player, a mission with `taxi_point` frames (both city maps; not the countryside). Off ends their waves. False for an unknown player.
- `Taxi.isAvailable(): boolean` — A simulation host runs the walkers, so hails can come at all.
- `Taxi.getHail(pedestrian): { player, vehicle, pickedUp } | null` — Whom a walker waves at.
- `Taxi.decline(pedestrian): boolean` — Ends the wave; the walker walks on.
- `Taxi.board(player: Player, pedestrian: Pedestrian, seat?: number): Npc | null` — Makes the walker hailing the player their passenger: a scripted NPC (`Npc.spawn` of the walker's model) takes its place where it stands on the host, players swap the walker for it where they see it, and it gets into the player's car (`npc.enterVehicle`) on retail's seat (seat 3 of a car with more than three seats, else seat 1, else the first free one). The player keeps driving. Null when the walker does not hail this player, the player does not drive a car with a free passenger seat or already has a passenger, or no NPC can be made.
- `Taxi.setDestination(passenger: Npc, position | null, radius = 15): boolean` — Where the ride goes. `taxiArrive` follows once the cab stands still (0.5 m/s or less) within the radius, as `taxi_pas.dat` waits for it.
- `Taxi.dropOff(passenger: Npc): boolean` — Ends the ride: the passenger gets out once the cab is nearly still, walks a few metres away and is removed once no player is within 90 m (at most a minute later).
- `Taxi.getPassenger(player): Npc | null`, `Taxi.getRide(passenger): TaxiRide | null` — The player's passenger (boarding or seated) and a ride's `player`, `vehicle`, `seat`, `phase` (`"boarding"`, `"riding"`, `"leaving"`, `"released"`), `destination`, `radius` and `arrived`.

The passenger also gets out without being told when the driver leaves the
driver's seat (`taxi_pas.dat`'s owner check; `taxiPassengerExit` reason
`driverLeft`). The events are `taxiHail`, `taxiHailEnd`, `taxiPickup`,
`taxiPassengerEnter`, `taxiArrive` and `taxiPassengerExit`.

```js
Events.on("taxiPickup", (player, pedestrian) => {
    const passenger = Taxi.board(player, pedestrian);
    if (passenger) Taxi.setDestination(passenger, { x: -189.3, y: 18.1, z: -690.5 });
});
Events.on("taxiArrive", (player, passenger) => {
    player.giveMoney(60);
    Taxi.dropOff(passenger);
});
```

## Railway

The mission's trams and metro trains (freeride has 38). The simulation host
runs them with the retail AI everywhere on the map and reports them ten times
a second (twice a second to players farther than 600 units); every player sees them where the host has them,
with the host's passengers. Players board and leave through the doors while a
railway stands at a station; everyone sees a rider aboard it. A railway is named by its load index, which stays the same for the mission
generation. A tram or train that runs into a player on foot knocks them down
(`playerDamage` with cause `railway`, then `railwayHitPlayer`).

- `Railway.getAll(): RailwayInfo[]` — Every railway the host has reported this mission, by load index.
- `Railway.get(index: number): RailwayInfo | null` — One railway; null before the host reported it.
- `Railway.getInRange(position, radius): RailwayInfo[]` — The railways whose body is within radius of position.
- `Railway.getCount(): number` — The number of reported railways.

```js
Events.on("railwayHitPlayer", (player, info) => {
    if (info.railway) console.log(`${player.nickname} was hit by railway ${info.railway.index} at ${info.railway.speed.toFixed(1)} u/s`);
});
```

## Bridge

The mission's drawbridges and the two steamers they open for. The simulation
host runs them with the retail AI: a steamer reaching a bridge turns its road
lights red, the bridge opens once no traffic car is on it, stays open while the
steamer passes and closes again. The host reports them five times a second and
on every phase change; every player sees the leaves, their collision, lights,
sounds and the steamers where the host has them, a few hundred milliseconds
later. A bridge is named by its load index. Read only.

- `Bridge.getAll(): BridgeInfo[]` — Every bridge the host has reported this mission, by load index.
- `Bridge.get(index: number): BridgeInfo | null` — One bridge; null before the host reported it.

```js
const raised = Bridge.getAll().filter((bridge) => bridge.raised);
if (raised.length) console.log(`Bridges ${raised.map((bridge) => bridge.index).join(", ")} are up`);
```

## Pumper

The mission's gas station attendants (retail `C_pumper`; the Free Ride scenes
have three, one per pump). The simulation host runs them with the retail AI
and every player sees the same attendant idle, smoke a cigarette and serve a
car, with his footsteps, the fuel loop, the cigarette props and his lines. A
driver who stops at a pump the retail way (fuel cap at the pump, slow, facing
the pump's direction) with the engine off and presses the exit key gets the
retail "use the pump" choice. The server checks that the player drives that
car, that the attendant is free, the policy and the price; then the
attendant greets, the car is aligned to the pump and locked with the camera on
him, he walks over, fuels, says he is done and walks back, and the car is
released. The server fills the network car's tank when his fuel animation
ends its pumping and takes the money then: every litre the tank lacks, or as
many as the player's money buys.

The attendant cannot be startled: cars and shots do not panic him, and a
blown-up pump leaves him standing. A service ends early (the attendant walks
back, the car is released) when the driver leaves the seat or dies, the car
goes, the simulation host leaves or three minutes pass.

- `Pumper.getAll(): PumperInfo[]` — Every attendant the host has reported this mission, by load index.
- `Pumper.get(index: number): PumperInfo | null` — One attendant; null before the host reported him.
- `Pumper.setEnabled(enabled: boolean): void` — While off, a request is refused (`pumperRefused`, reason `disabled`); a service under way finishes.
- `Pumper.isEnabled(): boolean` — True unless switched off.
- `Pumper.setPrice(pricePerLitre: number): void` — 0 (the default) to 10000. A player who cannot pay for one litre is refused (reason `money`).
- `Pumper.getPrice(): number` — The price per litre.

`PumperInfo` has `index`, `position`, `state` (`idle`, `smoking`, `greeting`,
`walking`, `fueling`, `turning`, `done`), `busy` (serving or walking back) and
`vehicle` (the car he serves, or null).

```js
Pumper.setPrice(3);
Events.on("vehicleRefuel", (vehicle, player, info) => {
    if (player) player.sendMessage(`The attendant put in ${info.litres.toFixed(1)} litres for $${info.cost}.`);
});
Events.on("pumperRefused", (player, vehicle, info) => {
    if (info.reason === "money") player.sendMessage(`A full tank costs $${info.cost}.`);
});
```

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

## MapObject

`MapObject` places a stock model in the current mission for every client,
like SA-MP's `CreateObject` and `MoveObject`. Players collide with it on foot,
in cars and with bullets, and so do the simulation host's NPCs and traffic.
Late joiners receive every object with its current pose and a running move.
An object lasts until `destroy()`, until the mission changes, or until the
resource that created it stops, so a restarted resource builds its map anew.

```js
const gate = MapObject.create("bar_fence.4ds", { x: -1200, y: 2, z: 300 }, { x: 0, y: Math.PI / 2, z: 0 });
gate.moveTo({ x: -1200, y: -1, z: 300 }, 1.5); // sink into the ground at 1.5 m/s
Events.on("mapObjectMoved", (object) => {
    if (object.id === gate.id) console.log("gate open");
});
```

- `MapObject.create(model, position, rotation?, options?): MapObject | null`.
  `model` is a file under `models\`, such as `"bar_fence.4ds"` (`.i3d` works
  too). `rotation` is a `Quaternion` or Euler angles in radians `{ x, y, z }`
  (y is up), applied as `glm::quat(euler)`. `options`:
  - `virtualWorld` (default `-1`, every world): only players in that world see
    and collide with it. Reads back as `4294967295` for every world.
  - `collision` (default `true`).
  - `scale`: a number or `{ x, y, z }`, 0.01 to 100 per axis.
  - `material`: a `tables\materials.def` id for collision built from the
    model's mesh (0 to 46, not glass 24 or crash frames 41); for example 4
    concrete, 13 wood, 19 metal structures, 27 wire fence, 38 brick.
  It returns null at the limit of 4096 objects. A model the client cannot load
  is logged there and shows nothing.
- `MapObject.getAll()` lists every object.
- `object.position` and `object.rotation` read the current pose, also while
  it moves. Assigning either teleports the object and stops a running move
  without `mapObjectMoved`. `object.virtualWorld` can be assigned (`-1` is
  every world).
- `object.moveTo(position, speed, rotation?)` moves it in a straight line at
  `speed` m/s (up to 1000), turning evenly to `rotation` on the way. A move
  that only turns takes `speed` as radians per second. A new move or a
  teleport replaces a running one. `mapObjectMoved(object)` fires on arrival.
- `object.stop()` halts it where it is; `object.moving` tells whether it moves.
- `object.model`, `object.collision` and `object.missionGeneration` are read
  only. `object.destroy()` removes it and its collision everywhere.

Collision comes from the game's own data where it can. When the loaded mission
places the same model and its `tree.klz` gives that placement collision, the
object takes those primitives (faces, boxes, plates, spheres, cylinders) with
their materials, so a lamp post keeps its cylinder and a traffic sign its box.
Otherwise every triangle of the model's mesh collides from both sides, in
concrete or the given `material`. Passing `material` always selects the mesh.
Collision follows a moving object, but a player standing on one is not carried
along; prefer slow moves for lifts. Only objects inside the mission's
collision grid (the freeride grid spans x -3797 to 2849 and z -2222 to 1600,
at any height) collide; one outside it is still drawn. AI path finding does
not know about map objects.

## Fire

`Fire` is a fire that burns until it is put out, unlike the one-shot
`World.createFire`. Every client within 250 units draws it from the retail
effects in `tables\effects.tbl`: a ring of flame emitters ("Fire" in the
centre with its smoke column, "Fire_no_smoke" around it) that the seed lays
out the same way everywhere within `radius` and up to `height` above the
position, one of the ten pooled retail fire lights (`C_game`'s
`SpawnFireFrameSlot`, kept lit while it burns and faded out as `C_game::Tick`
fades a retail fire's), the retail ignition sound and the `C_fire` loop
(sound table 21 and 20). The server owns its `intensity`: the lower it is,
the fewer flames burn; at 0 only "slow_smoke" is left for 40 seconds. A
water jet hitting it adds the retail steam ("smoke_parnik"). Players standing
in a burning fire take `damage` per second times the intensity, through the
same fire damage path as a Molotov. A fire lasts until destroyed, the mission
changes or the resource that created it stops; going out does not remove it.

```js
const fire = Fire.create({ x: -1157, y: -5.9, z: 280 }, { radius: 4, height: 5, toughness: 3, growth: 0.01 });
const hose = WaterJet.create(player);           // a hose in the player's hands
hose.aimAt(fire.position);                       // the water arcs onto the fire
Events.on("fireExtinguished", (out, info) => {
    if (out.id !== fire.id) return;
    for (const { player, share } of info.contributors) player?.giveMoney(Math.round(400 * share));
    setTimeout(() => out.destroy(), 20000);      // let it smoke a while
});
```

- `Fire.create(position, options?): Fire | null`. `options`: `radius` 0.5–15
  (default 3), `height` 0–12 (default 0; flames climb a facade), `intensity`
  0–1 (default 1), `damage` per second at full intensity 0–200 (default 20),
  `lifetime` in ms before it burns out by itself (0, the default, never),
  `growth` in intensity per second regained three seconds after the last
  water (default 0), `toughness` 0.1–20, how much harder than a 3-unit fire it
  is to put out (default 1), `attachTo`, a `Vehicle` it burns on and follows,
  and `virtualWorld` (default every world, `-1`). Null at 64 fires.
- `Fire.getAll()`, `Fire.getInRange(position, radius)` list fires, burning or out.
- `fire.intensity` (0–1, assignable: 0 puts it out with reason `"script"`,
  above 0 sets it alight again), `fire.radius` (assignable), `fire.position`
  (assignable; detaches it), `fire.height`, `fire.burning`, `fire.dousing`
  (water hit it in the last 1.5 s), `fire.vehicle`, `fire.missionGeneration`.
- `fire.douse(amount, player?)` takes intensity off, as an extinguisher or
  bucket would, credited to the player; returns what is left (-1 for a gone
  fire). `fire.extinguish()` puts it out at once. `fire.attachTo(vehicle | null)`.
- `fire.getContributors()` lists `{ player, amount, share }`, most first,
  where `share` sums to 1.
- `fire.destroy()` removes it, smoke and all, without an event.

`fireExtinguished(fire, { reason, contributors })` fires once the intensity
reaches 0: `reason` is `"doused"` (water or `douse`), `"script"` or
`"burnedOut"` (its lifetime ran out).

### WaterJet

A fire hose in a player's hands or a water cannon on a vehicle's roof. While
aimed, every client within 150 units draws the retail hose effect
"hadice" (effect 89: a hundred drops a second leaving its parent frame's x
axis at 5 m/s and falling with gravity) from a nozzle frame turned and scaled
so the drops fly at the jet's `pressure` on the lower ballistic arc that lands
on the target, with the retail hydrant's water loop (sound table 327). The
server douses every burning fire whose area (radius plus 1.5) holds the
target, within 45 units of the nozzle: 0.14 intensity a second at pressure
18, divided by the fire's toughness and by its radius over 3, credited to the
player holding the hose (or the vehicle jet's `player`).

- `WaterJet.create(source: Player | Vehicle, options?): WaterJet | null`.
  `options`: `pressure` 8–30 m/s (default 18; the arc reaches about
  pressure² / 9.8), `player` credited for a vehicle's jet, `virtualWorld`.
  It starts closed. Null at 32 jets.
- `jet.aimAt(position)` opens or turns it; `jet.stop()` closes it.
- `jet.active`, `jet.target` (null while closed), `jet.source`,
  `jet.pressure` (assignable), `jet.getNozzle()` (null while its holder is
  dead or gone), `jet.destroy()`. `WaterJet.getAll()` lists them.

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
- `World.getPropDamage(): PropDamageInfo` — Counts the world props broken (`broken`) or moved (`moved`) in this mission since it loaded or was last repaired. City cache traffic signs are not counted.
- `World.repairProps(): number` — Puts every broken phone booth, hydrant, trash bin, barrel and other breakable back up, with its collision, and every box, railing, mail box or can back where the mission placed it, for every client. Debris pieces and hydrant jets go. City cache traffic signs (`dopravni1B`–`8B`) are not synchronized and stay down on the client that knocked them over until the mission changes. Returns: How many props were broken or moved.
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
The Free Ride gamemode demonstrates both directions throughout, for example
in [`server/races.js`](../resources/freeride/server/races.js) and
[`client/hud.js`](../resources/freeride/client/hud.js).

Its [world events](../resources/freeride/server/world_events.js) show a
server-owned objective drawn entirely by
[client script](../resources/freeride/client/markers.js): the server
publishes the supply crate's position and checks each player's distance
itself before it creates native `Pickup.create` weapons and pays the finder.
Client-local models and labels cannot grant a reward on their own.

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
| `weaponDropped` | `[pickup: Pickup | null, player: Player | Npc | null, info: PickupEventInfo]` | Dispatched when a drop or a death leaves a weapon pickup in the world; a dead NPC's weapon names the NPC. |
| `pickupTaken` | `[pickup: Pickup | null, player: Player | null, info: PickupEventInfo]` | Dispatched after the server moved a pickup into a player's inventory. The pickup handle is null when nothing is left on the ground. |
| `mapObjectMoved` | `[object: MapObject]` | Dispatched after a MapObject.moveTo reached its target. A teleport, stop or new move does not dispatch it for the move it replaced. |
| `fireExtinguished` | `[fire: Fire, info: FireExtinguishedInfo]` | Dispatched once a fire's intensity reaches 0. The fire stays, smoking, until destroyed. |
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
| `vehicleTrafficTaken` | `[vehicle: Vehicle, player: Player, info: VehicleTrafficTakenInfo]` | A car the simulation host drove became the player's: the host stops driving it and the player's client simulates it. After a steal the player gets in with the next use, as in retail. |
| `vehicleStealRefused` | `[vehicle: Vehicle, player: Player]` | The server refused a player's steal of a traffic car (out of reach, not standing still, taken first, driver gone); a repeated try within 500 ms is refused without this event. The player's client already played the pull-out; the driver stays in the car. |
| `playerThrownOut` | `[player: Player, vehicle: Vehicle, info: PlayerThrownOutInfo]` | After `vehiclePlayerExited`, when a simulation host NPC pulled the player out of the seat. |
| `railwayHitPlayer` | `[player: Player, info: RailwayHitInfo]` | A tram or metro train knocked a player on foot down, after `playerDamage` (cause `railway`). |
| `vehicleRefuel` | `[vehicle: Vehicle, player: Player \| null, info: VehicleRefuelInfo]` | A gas station attendant's fuel animation ended its pumping: the server has set the fuel (`vehicleFuelChange`) and taken the money (`playerMoneyChange`). `info` has `pumper`, `litres` and `cost`. The car stays locked until he is back at his post. |
| `pumperRefused` | `[player: Player, vehicle: Vehicle, info: PumperRefusedInfo]` | The server refused a driver's "use the pump": `info.reason` is `disabled`, `unavailable` (no simulation host), `busy`, `full` (less than half a litre missing) or `money` (with `info.cost` for the full tank). Nothing is locked. |
| `pedestrianSpawn` | `[pedestrian: Pedestrian]` | The simulation host started a pedestrian near a player. |
| `pedestrianRetire` | `[pedestrian: Pedestrian]` | Just before a pedestrian leaves the world: far from every player, a corpse past its lifetime, `Pedestrian.remove` or `setEnabled(false)`. A mission change removes every pedestrian without this event. |
| `pedestrianDamage` | `[pedestrian: Pedestrian, info: PedestrianDamageInfo]` | After the simulation host applied a hit. |
| `pedestrianDeath` | `[pedestrian: Pedestrian, killer: Player \| Vehicle \| null]` | Once when a pedestrian dies; killer is the last attacker. |
| `npcSpawn` | `[npc: Npc]` | The simulation host started running an armed human: a gangster drawing a weapon, a policeman sent after a player, or a thrown-out traffic driver. |
| `npcRetire` | `[npc: Npc]` | Just before an NPC leaves the world: no player within 150 units, `Npc.remove` or the host leaving. A mission change removes every NPC without this event. |
| `npcDamage` | `[npc: Npc, info: NpcDamageInfo]` | After the simulation host applied a hit. |
| `npcDeath` | `[npc: Npc, killer: Player \| Npc \| Vehicle \| null]` | Once when an NPC dies; the weapon in its hand drops as a pickup (`weaponDropped`). |
| `npcTaskComplete` | `[npc: Npc, info: NpcTaskInfo]` | A scripted NPC's task ended: `info.id` (the id the task method returned), `info.task` (`"stop"`, `"follow"`, `"walkTo"`, `"enterVehicle"`, `"exitVehicle"`, `"driveTo"` or `"attack"`), `info.result` (`"completed"`, `"failed"` or `"cancelled"`) and `info.success`. |
| `playerCrime` | `[player: Player, info: PlayerCrimeInfo]` | The police manager recorded a new crime of a player; the wanted level may rise with it. |
| `playerWantedChange` | `[player: Player, info: PlayerWantedInfo]` | A player's wanted level changed: a crime, a script, an arrest, `Police.clear`, the officers losing the player, or a new simulation host taking over (to 0). |
| `policeBackup` | `[player: Player, info: { id: number, sent: boolean }]` | The simulation host answered a `Police.sendBackup` request for this player: `sent` is false when the pool had too few free officers, no road was far enough away, the player was no longer wanted, or the host or mission changed. |
| `taxiHail` | `[player: Player, pedestrian: Pedestrian, vehicle: Vehicle]` | A walker started waving at the cab of a player `Taxi.setHailable` allows. Every player sees the wave. |
| `taxiHailEnd` | `[player: Player, pedestrian: Pedestrian, info: { reason: string }]` | It stopped waving: `"boarded"` (`Taxi.board`), `"ended"` (`Taxi.decline`, a panic or hit, the cab drove off or its driver is wanted, the walker out of range, or 15 s after `taxiPickup` without a board) or `"gone"`. |
| `taxiPickup` | `[player: Player, pedestrian: Pedestrian, vehicle: Vehicle]` | Once, when the cab stands still within 10 m of the waving walker: retail's boarding moment. |
| `taxiPassengerEnter` | `[player: Player, passenger: Npc, vehicle: Vehicle]` | A `Taxi.board` passenger sat down in the player's car. |
| `taxiArrive` | `[player: Player, passenger: Npc, vehicle: Vehicle]` | Once, when the cab stands still within the radius of `Taxi.setDestination`'s point with the passenger aboard. |
| `taxiPassengerExit` | `[player: Player \| null, passenger: Npc \| null, vehicle: Vehicle \| null, info: { reason: string, boarded: boolean }]` | The ride ended: `reason` is `"dropOff"`, `"driverLeft"`, `"failed"` (never got in), `"died"` or `"gone"`; `boarded` whether it had sat down. |
| `playerArrest` | `[player: Player, officer: Npc, info: PlayerArrestInfo]` | An officer finished arresting or fining a player: both animations played to the end (an officer killed or called off before then caught nobody). Nothing else happens to the player; the script decides the outcome. The crime record is cleared. |
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
- `cause: "script" | "firearm" | "melee" | "explosion" | "fire" | "fall" | "drowning" | "vehicle" | "railway"` — Server damage path; `railway` is a tram or metro train running into the player on foot. Explosions and fires can carry a weapon ID when they came from a thrown weapon.
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

- `shooter: Player | Npc | null` — Player or simulation host NPC who fired, or null when they are gone.
- `damage: number` — Damage derived from the accepted shot.
- `position: { x: number; y: number; z: number }` — World-space impact position.
- `shotSequence: number` — Shooter's fire sequence; 0 for an NPC.
- `pelletIndex: number` — Pellet of that shot.

**VehiclePartInfo** — A loose part a vehicle shed.

- `debrisId: number` — Network ID of the temporary debris replica.
- `type: number` — 1 wheel, 2 bumper, 3 light, 4 license plate, 5 mirror, 6 wing, 7 door.
- `partIndex: number` — Native part index within the model.
- `position: { x: number; y: number; z: number }` — Where the part came off.

**VehicleTrafficTakenInfo** — How a player took a traffic car.

- `stolen: boolean` — True when the player pulled its driver out, who stays as a `driver` NPC; false for a seat nobody held.

**PlayerThrownOutInfo** — How an NPC threw a player out of a car.

- `seat: number` — Seat the player was thrown out of; 0 is the driver.
- `by: Npc` — The NPC that did it: a traffic driver taking its car back, a policeman before an arrest, or a gangster.

**RailwayInfo** — A tram or metro train as the simulation host last reported it.

- `index: number` — Load index within the mission, from 0.
- `position: { x: number; y: number; z: number }`, `direction: { x: number; y: number; z: number }` — The body.
- `speed: number` — Units per second.
- `state` — Retail railway state: `"moving"`, `"stationArrival"`, `"passengersLeaving"`, `"passengersEntering"`, `"stationWait"`, `"scheduleWait"`, `"departing"`, `"collisionBraking"`, `"stopPointBraking"`, `"semaphoreBraking"`, `"driverHit"` (a bullet hit its driver; the host lets it go on after 30 seconds), `"followingRailway"` or `"unknown"`.
- `wagons: RailwayWagonInfo[]` — Wagons behind the body, each with `position` and `direction`; empty for a tram, one for a freeride metro train.

**BridgeInfo** — A drawbridge as the simulation host last reported it.

- `index: number` — Load index within the mission, from 0.
- `position: { x: number; y: number; z: number }` — The bridge frame, one of its two leaves.
- `phase` — Retail bridge phase: `"closed"`, `"signaling"` (a steamer asked for it; the road lights are red), `"preparing"`, `"opening"`, `"open"`, `"closing"` or `"unknown"`.
- `angle: number` — Leaf angle in radians, 0 closed; the retail bridges open to 50 degrees.
- `raised: boolean` — Opening, open or closing. A player on foot who walks into the raised bridge dies, as in retail.

**RailwayHitInfo** — A railway running into a player.

- `railway: RailwayInfo | null` — The railway, as last reported.
- `damage: number` — Damage applied: retail (|speed| - 2.5) × 4.2.

**PedestrianDamageInfo** — A hit the simulation host applied to a pedestrian: `attacker: Player | Vehicle | null`, `hitType: number` (0 bullet, 1 hard knockdown, 2 melee, 3 explosion, 5 car impact), `damage`, `healthBefore`, `healthAfter`.

**NpcDamageInfo** — A hit the simulation host applied to an NPC: `attacker: Player | Npc | Vehicle | null`, `hitType: number` (0 bullet, 1 hard knockdown, 2 melee, 3 explosion, 8 car impact), `damage` (before the body part scale), `healthBefore`, `healthAfter`.

**PoliceRecord** — `level: number` (0 none, 1 minor, 2 arrest, 3 lethal), `crimes: string[]` (`"weaponSeen"`, `"firing"`, `"speeding"`, `"crashObject"`, `"crashCar"`, `"crashPedestrian"`, `"sidewalk"`, `"violence"`, `"redLight"`, `"carTheft"`, `"signDamage"`, `"assaultOnPolice"`, `"forcedArrest"`, `"policeCarRammed"`), `officers: number`.

**PlayerCrimeInfo** — `crime`: one of the PoliceRecord crime names. **PlayerWantedInfo** — `level`, `previous`. **PlayerArrestInfo** — `kind: "arrest" | "fine"`.

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
- **Mission changes** remove every vehicle, pickup, map object and sound and
  clear frame overrides; weather and city music carry over.
- **World props.** Breakables (phone booths, hydrants, trash bins, barrels and
  the other scene models a car, bullet, explosion or bat knocks down) and
  physical props (boxes, railings, mail boxes, cans) are the same for every
  player. The client where a prop broke reports it and the others break theirs
  the same way; a remote fuel pump bursts into flame and smoke there without
  damage, which only the client that broke it applies. A physical prop that a
  player (or the simulation host's traffic, NPCs and trains) sent somewhere new
  is placed there for everyone; one a remote player's copy pushed on another
  client goes back. Late joiners load the state without debris. The city cache
  traffic signs (`dopravni1B`–`8B`) are the exception: they fall only for the
  client that hit them and stay down until the mission changes. Each player has
  at most 256 breaks standing until a repair, and everyone together 2048 (the
  stock freeride scene has 1,044 breakables). Nothing repairs itself: call
  `World.repairProps()` (the Free Ride gamemode does it every few minutes and lets
  anyone call it once a minute). A mission change starts whole.

### Durable vehicle condition

`vehicle.saveState(): string` returns a versioned opaque JSON snapshot of the
vehicle's replicated durable condition. It includes full native damage, part
flags and deformation checkpoints as well as fuel and persistent presentation
state. Empty means the car is terminal, missing, or still awaiting complete native
reports. Store the string intact alongside `vehicle.model`.

`vehicle.restoreState(snapshot): boolean` applies it to a fresh, empty spawn of the
same model **before its first native damage report**. Spawn and restore in the same
server callback. Validation occurs before any mutation. Replication then makes the
controller apply the authoritative condition and deformation before reporting its
own state. Position/rotation come from `Vehicle.spawn`; velocity, pedals, occupants
and loose debris actors are not persisted. This API does not serialize unreplicated
native fields or arbitrary script state bags.
