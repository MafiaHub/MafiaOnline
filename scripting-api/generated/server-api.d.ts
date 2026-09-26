import type { EventHandler, MessageHandler, Unsubscribe } from "../shared.js";

declare global {
  /**
   * Native events dispatched through `Events.on`. Each property is the exact callback argument tuple for that event.
   */
  interface EventMap {
    /**
     * Dispatched after a resource entry point has run and immediately before the resource becomes running.
     */
    resourceStart: [resourceName: string];

    /**
     * Dispatched while a resource is stopping, before its stop callback, timers, exports and event handlers are cleaned up.
     */
    resourceStop: [resourceName: string];

    /**
     * Dispatched after the server console parses a command line that no built-in command handles.
     */
    consoleCommand: [command: string, args: string[]];

    /**
     * Dispatched after World.changeMission has reset the world and asked every client to load the new mission.
     */
    missionChange: [mission: MissionInfo];

    /**
     * Dispatched once each time every connected player has loaded the current mission generation. At least one player must be connected.
     */
    missionReady: [mission: MissionInfo];

    /**
     * Dispatched when a player's client has loaded the current mission.
     */
    playerMissionReady: [player: Player, info: MissionLoadInfo];

    /**
     * Dispatched when a player's client has unloaded its mission.
     */
    playerMissionUnloaded: [player: Player, info: MissionLoadInfo];

    /**
     * Dispatched when a player's client could not load the current mission.
     */
    playerMissionLoadFailed: [player: Player, info: MissionLoadInfo];

    /**
     * Dispatched after a player has connected and has a player handle. The player is not spawned yet.
     */
    playerConnect: [player: Player];

    /**
     * Dispatched while a player disconnects, before the player handle stops resolving.
     */
    playerDisconnect: [player: Player];

    /**
     * Dispatched when a player starts speaking on voice chat, including when no one is in earshot.
     */
    playerVoiceStart: [player: Player];

    /**
     * Dispatched shortly after a player's last voice frame reaches the server. A disconnect ends speech without this event.
     */
    playerVoiceStop: [player: Player];

    /**
     * Dispatched for an accepted, sanitized chat line after it was relayed.
     */
    playerChat: [player: Player, text: string];

    /**
     * Dispatched when a chat line starting with `/` passes the chat rate limit.
     */
    playerCommand: [player: Player, command: string, args: string[]];

    /**
     * Dispatched after Player.setNickname changed the name.
     */
    playerNicknameChange: [player: Player, oldNickname: string, nickname: string];

    /**
     * Dispatched after Player.setModel changed the human model.
     */
    playerModelChange: [player: Player, oldModel: string, model: string];

    /**
     * Dispatched after the server changes a player's Free Ride balance.
     */
    playerMoneyChange: [player: Player, oldMoney: number, money: number];

    /**
     * Dispatched after a player uses a mission door or a script changes its open target. Player is null for script changes.
     */
    doorStateChange: [door: Door, player: Player | null];

    /**
     * Dispatched after a script changes a mission door lock.
     */
    doorLockChange: [door: Door, player: null];

    /**
     * Dispatched after a script enables or disables native door use.
     */
    doorInteractionChange: [door: Door, player: null];

    /**
     * Dispatched after Player.spawn started a new life.
     */
    playerSpawn: [player: Player];

    /**
     * Dispatched after Player.respawn started a new life.
     */
    playerRespawn: [player: Player];

    /**
     * Dispatched after Player.despawn ended the current life.
     */
    playerDespawn: [player: Player];

    /**
     * Dispatched after an accepted health decrease.
     */
    playerDamage: [victim: Player, info: PlayerHealthInfo];

    /**
     * Dispatched after a fatal health decrease, following playerDamage. The killer is null when no connected attacker owns the damage; info.weaponId identifies the validated weapon when known. Death never respawns a player by itself; call Player.respawn.
     */
    playerDeath: [victim: Player, killer: Player | null, info: PlayerHealthInfo];

    /**
     * Dispatched after an accepted health increase.
     */
    playerHealthChange: [player: Player, info: PlayerHealthInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerWeaponEquip: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerAimChange: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerWeaponReload: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerWeaponFire: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerWeaponThrow: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerWeaponThrowStart: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerWeaponThrowRelease: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerWeaponThrowCancel: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerWeaponMeleeStart: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerWeaponMelee: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after the server accepted this controller action.
     */
    playerWeaponMeleeCancel: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched after a controller drop or Player.dropWeapon updates inventory. Death drops only emit weaponDropped.
     */
    playerWeaponDrop: [player: Player, info: WeaponActionInfo];

    /**
     * Dispatched when a drop or a death leaves a weapon pickup in the world.
     */
    weaponDropped: [pickup: Pickup | null, player: Player | null, info: PickupEventInfo];

    /**
     * Dispatched after the server moved a pickup into a player's inventory. The pickup handle is null when nothing is left on the ground.
     */
    pickupTaken: [pickup: Pickup | null, player: Player | null, info: PickupEventInfo];

    /**
     * Dispatched after Vehicle.spawn created a vehicle.
     */
    vehicleSpawn: [vehicle: Vehicle];

    /**
     * Dispatched by Vehicle.destroy immediately before the vehicle is removed. A mission change removes every vehicle without this event.
     */
    vehicleDestroy: [vehicle: Vehicle];

    /**
     * Dispatched after the matching vehicle state changed, from its simulation controller or a script.
     */
    vehicleEngineChange: [vehicle: Vehicle];

    /**
     * Dispatched after the matching vehicle state changed, from its simulation controller or a script.
     */
    vehicleFuelChange: [vehicle: Vehicle];

    /**
     * Dispatched after the matching vehicle state changed, from its simulation controller or a script.
     */
    vehicleLightsChange: [vehicle: Vehicle];

    /**
     * Dispatched after the matching vehicle state changed, from its simulation controller or a script.
     */
    vehicleHornChange: [vehicle: Vehicle];

    /**
     * Dispatched after the matching vehicle state changed, from its simulation controller or a script.
     */
    vehicleSirenChange: [vehicle: Vehicle];

    /**
     * Dispatched after the matching vehicle state changed, from its simulation controller or a script.
     */
    vehicleGearChange: [vehicle: Vehicle];

    /**
     * Dispatched after the matching vehicle state changed, from its simulation controller or a script.
     */
    vehicleDamageState: [vehicle: Vehicle];

    /**
     * Dispatched when the native per-part damage snapshot changes.
     */
    vehicleDamage: [vehicle: Vehicle, damage: VehicleDamage | null];

    /**
     * Dispatched after Vehicle.setOpacity changes the server-owned opacity.
     */
    vehicleOpacityChange: [vehicle: Vehicle];

    /**
     * Dispatched after Vehicle.repair starts a native damage and deformation reset on every client; a fresh damage snapshot follows from the simulation controller.
     */
    vehicleRepair: [vehicle: Vehicle];

    /**
     * Dispatched once a vehicle exploded (1), sank (2) or left the map (3). Occupants die through server combat first.
     */
    vehicleTerminal: [vehicle: Vehicle, state: number];

    /**
     * Dispatched when a pellet hit is replayed to the vehicle's simulation controller, before the resulting vehicleDamage.
     */
    vehicleHit: [vehicle: Vehicle, info: VehicleHitInfo];

    /**
     * Dispatched when the server accepts a native loose part from the vehicle's controller.
     */
    vehiclePartDetached: [vehicle: Vehicle, info: VehiclePartInfo];

    /**
     * Dispatched when the server accepts the start of a native enter or steal.
     */
    vehiclePlayerEntering: [vehicle: Vehicle, player: Player, info: VehicleSeatInfo];

    /**
     * Dispatched when a player sits in a seat, natively or through Player.putInVehicle.
     */
    vehiclePlayerEntered: [vehicle: Vehicle, player: Player, info: VehicleSeatInfo];

    /**
     * Dispatched when a player leaves a seat, including by death, respawn or removeFromVehicle.
     */
    vehiclePlayerExited: [vehicle: Vehicle, player: Player, info: VehicleSeatInfo];

    /**
     * Dispatched when a native exit was blocked.
     */
    vehiclePlayerExitBlocked: [vehicle: Vehicle, player: Player, info: VehicleSeatInfo];

    /**
     * Dispatched when one key of an entity's state changes: on the server when a script writes it, on a client when the write arrives. `value` is undefined when the key was removed and `previous` is undefined when it held nothing before, so a stored null stays distinguishable from an absent key. The entity is whatever the game's WrapScriptEntity answers, and the base Entity handle by default.
     */
    entityStateChange: [entity: Entity, key: string, value: any, previous: any];
  }

  /** Names of native events available in this scripting environment. */
  type EventName = keyof EventMap;

  /**
   * The server's current stock mission.
   */
  interface MissionInfo {
    /**
     * Stock mission directory name.
     */
    mission: string;

    /**
     * Generation of this mission load; it increases on every change, including a reload of the same mission.
     */
    missionGeneration: number;
  }

  /**
   * A client's report about loading a mission generation.
   */
  interface MissionLoadInfo {
    /**
     * Generation the report refers to.
     */
    missionGeneration: number;

    /**
     * 0 unloaded, 1 ready, 2 scene failure, 3 collision failure, 4 game initialization failure.
     */
    state: number;
  }

  /**
   * An accepted change of a player's server health.
   */
  interface PlayerHealthInfo {
    /**
     * Player the damage is attributed to, or null when the source is absent or has disconnected.
     */
    attacker: Player | null;

    /**
     * Validated weapon used for this damage: 0 is fists, 5 is a Molotov, 15 is a grenade; null for script, fall, vehicle or unknown damage. Remains known if the attacker disconnects after throwing.
     */
    weaponId: number | null;

    /**
     * Server damage path that changed health. Explosions and fires can have a weaponId when they came from a thrown weapon.
     */
    cause: "script" | "firearm" | "melee" | "explosion" | "fire" | "fall" | "drowning" | "vehicle";

    /**
     * Health before the change.
     */
    oldHealth: number;

    /**
     * Health after the change.
     */
    health: number;

    /**
     * oldHealth minus health; negative for healing.
     */
    amount: number;

    /**
     * Death animation chosen by the server for a fatal change, otherwise 0.
     */
    deathAnimation: number;

    /**
     * Mission generation of the affected life.
     */
    missionGeneration: number;

    /**
     * Spawn generation of the affected life.
     */
    spawnGeneration: number;
  }

  /**
   * A weapon action the server accepted from a player's client.
   */
  interface WeaponActionInfo {
    /**
     * Weapon the action used.
     */
    weaponId: number;

    /**
     * Selected weapon after the action; 0 is holstered.
     */
    selectedWeapon: number;

    /**
     * Bit n is set when weapon n is held.
     */
    inventoryMask: number;

    /**
     * Loaded ammunition of weaponId after the action.
     */
    loaded: number;

    /**
     * Reserve ammunition of weaponId after the action.
     */
    reserve: number;

    /**
     * Whether the player is aiming.
     */
    aiming: boolean;

    /**
     * Unit aim or throw direction.
     */
    direction: { x: number; y: number; z: number };

    /**
     * The targeted player the client suggested for a shot; confirmed damage arrives as playerDamage.
     */
    target: Player | null;

    /**
     * Server combat state revision.
     */
    revision: number;

    /**
     * Native combo index (0 to 3), or -1 for a heavy finisher on a melee release.
     */
    meleeIndex: number;

    /**
     * Measured local press duration on a melee release, in milliseconds.
     */
    meleeHoldMs: number;

    /**
     * Native grenade or Molotov charge duration on a throw release, capped at 2000 milliseconds; zero for other actions.
     */
    throwHoldMs: number;
  }

  /**
   * A weapon pickup transition.
   */
  interface PickupEventInfo {
    /**
     * Network ID of the pickup, which may no longer exist.
     */
    pickupId: number;

    /**
     * Weapon of the pickup.
     */
    weaponId: number;

    /**
     * Where the pickup lies, or null once it is gone.
     */
    position: { x: number; y: number; z: number } | null;
  }

  /**
   * An accepted firearm pellet against a vehicle.
   */
  interface VehicleHitInfo {
    /**
     * Player who fired, or null when they have left.
     */
    shooter: Player | null;

    /**
     * Damage derived from the accepted shot.
     */
    damage: number;

    /**
     * World-space impact position.
     */
    position: { x: number; y: number; z: number };

    /**
     * Shooter's fire sequence.
     */
    shotSequence: number;

    /**
     * Pellet of that shot.
     */
    pelletIndex: number;
  }

  /**
   * A loose part a vehicle shed.
   */
  interface VehiclePartInfo {
    /**
     * Network ID of the temporary debris replica.
     */
    debrisId: number;

    /**
     * 1 wheel, 2 bumper, 3 light, 4 license plate, 5 mirror, 6 wing, 7 door.
     */
    type: number;

    /**
     * Native part index within the model.
     */
    partIndex: number;

    /**
     * Where the part came off.
     */
    position: { x: number; y: number; z: number };
  }

  /**
   * An accepted seat transition.
   */
  interface VehicleSeatInfo {
    /**
     * Seat index; 0 is the driver.
     */
    seat: number;

    /**
     * True when the player pulled the previous driver out.
     */
    stolen: boolean;

    /**
     * Server seat sequence of the transition.
     */
    serverSequence: number;
  }

  /**
   * A held weapon and its ammunition.
   */
  interface WeaponSlot {
    /**
     * Stock item identifier.
     */
    weaponId: number;

    /**
     * Item name.
     */
    name: string;

    /**
     * How the item is used.
     */
    kind: "melee" | "firearm" | "throwable";

    /**
     * Rounds per magazine for a firearm; 1 otherwise.
     */
    magazine: number;

    /**
     * Loaded rounds, or the throwable count.
     */
    loaded: number;

    /**
     * Reserve rounds.
     */
    reserve: number;
  }

  /**
   * A player's weapons.
   */
  interface Inventory {
    /**
     * Selected weapon; 0 is holstered.
     */
    selected: number;

    /**
     * Held weapons.
     */
    weapons: WeaponSlot[];
  }

  /**
   * The server combat snapshot of a player life.
   */
  interface CombatState {
    /**
     * Server state revision.
     */
    revision: number;

    /**
     * Server health.
     */
    health: number;

    /**
     * Life status.
     */
    alive: boolean;

    /**
     * Whether a life exists.
     */
    spawned: boolean;

    /**
     * Mission generation of the life.
     */
    missionGeneration: number;

    /**
     * Spawn generation of the life.
     */
    spawnGeneration: number;

    /**
     * Bit n is set when weapon n is held.
     */
    inventoryMask: number;

    /**
     * Selected weapon; 0 is holstered.
     */
    selectedWeapon: number;

    /**
     * Death animation chosen for a dead life.
     */
    deathAnimation: number;

    /**
     * Whether the player aims.
     */
    aiming: boolean;

    /**
     * Whether the native human crouches.
     */
    crouching: boolean;

    /**
     * Unit aim direction.
     */
    aimDirection: { x: number; y: number; z: number };

    /**
     * Camera-facing pose target relative to the player position. It updates while aiming, unaimed and unarmed; remote clients add it to the interpolated position to pose the neck and back.
     */
    poseTargetOffset: { x: number; y: number; z: number };

    /**
     * Ammunition indexed by weapon ID.
     */
    ammo: { loaded: number; reserve: number }[];
  }

  /**
   * A connected Mafia 1 player. The server owns its life, health, inventory and seat; every change goes through server validation and replicates to all clients.
   */
  class Player {
    /**
     * Creates a handle for an existing connected player; it does not create a player.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Sanitized, unique nickname.
     */
    readonly nickname: string;

    /**
     * Server-selected .i3d human model, retained across spawns and applied to every client.
     */
    readonly model: string;

    /**
     * Server health from 0 to 100.
     */
    readonly health: number;

    /**
     * Server-owned Free Ride balance from 0 to 1000000000. It survives respawns and mission changes until disconnect.
     */
    readonly money: number;

    /**
     * Whether the current life is alive.
     */
    readonly alive: boolean;

    /**
     * Whether the player has a current life in the mission.
     */
    readonly spawned: boolean;

    /**
     * Mission generation of the current life.
     */
    readonly missionGeneration: number;

    /**
     * Spawn generation; it increases with every spawn, respawn and despawn. Compare it before acting on a saved player.
     */
    readonly spawnGeneration: number;

    /**
     * Last position the player's client reported. Read only; use spawn or respawn to place a player.
     */
    readonly position: Vector3;

    /**
     * Last facing the player's client reported. Read only.
     */
    readonly rotation: Quaternion;

    /**
     * Current round-trip latency in milliseconds, or -1 when unavailable.
     */
    readonly ping: number;

    /**
     * Current remote network address, or an empty string when unavailable.
     */
    readonly ip: string;

    /**
     * Client-announced Steam identifier, or an empty string when unavailable.
     */
    readonly steamId: string;

    /**
     * Client-announced Discord identifier, or an empty string when unavailable.
     */
    readonly discordId: string;

    /**
     * Framework hardware identifier, or an empty string when unavailable.
     */
    readonly hardwareId: string;

    /**
     * Formats this player for logging.
     * @returns The player ID and nickname.
     */
    toString(): string;

    /**
     * Disconnects this player.
     * @param reason Reason shown to the player.
     */
    kick(reason?: string): void;

    /**
     * Emits a named script event to this player's client.
     * @param eventName Client event name.
     * @param payloadJson JSON payload forwarded to the client.
     */
    emit(eventName: string, payloadJson?: string): void;

    /**
     * Returns this player's remote network address.
     * @returns Address, or an empty string when unavailable.
     */
    getIP(): string;

    /**
     * Applies the nickname policy: invalid characters are removed, whitespace collapsed, the name cut to 24 characters, and a taken name gets a numbered suffix. Fires playerNicknameChange when the name changes.
     * @param nickname Requested nickname.
     * @returns The nickname actually used, or null when the player has left.
     */
    setNickname(nickname: string): string | null;

    /**
     * Selects this player's human model on every client, including during a life. The choice persists across respawns and mission changes. Fires playerModelChange when it changes.
     * @param model Stock human .i3d filename, 5 to 64 ASCII characters from A-Z, a-z, 0-9, underscore, dash, or dot.
     * @returns False for an invalid filename or a disconnected player.
     */
    setModel(model: string): boolean;

    /**
     * Changes this player's nametag.
     * @param options Only the given fields change. color is 0xRRGGBB and also colors the name in chat; an empty text restores the nickname.
     * @returns False when the player has left.
     */
    setNametag(options: { visible?: boolean; showHealth?: boolean; color?: number; text?: string }): boolean;

    /**
     * Shows or hides this player's nametag.
     * @param visible Whether other players see this player's name.
     */
    setNametagVisible(visible: boolean): void;

    /**
     * Shows or hides the health bar under this player's name.
     * @param visible Whether the health bar is shown.
     */
    setNametagHealthVisible(visible: boolean): void;

    /**
     * Overrides this player's nametag label.
     * @param text Label to show instead of the nickname; empty restores it.
     */
    setNametagText(text: string): void;

    /**
     * Tints this player's nametag and chat name.
     * @param color Packed 0xAARRGGBB or 0xRRGGBB color; the alpha is always opaque.
     */
    setNametagColor(color: number): void;

    /**
     * Sends a private chat notice to this player.
     * @param text Notice text, 1 to 200 characters.
     * @param color 0xRRGGBB color.
     * @returns False when the player has left or the text is invalid.
     */
    sendMessage(text: string, color?: number): boolean;

    /**
     * Starts a new life with 100 health and an empty inventory once every client is mission ready. A seat held by the previous life is released and recorded as an exit. Fires playerSpawn.
     * @param position Spawn position; three numbers x, y, z are accepted too.
     * @param heading Facing in radians around the vertical axis; defaults to 0.
     * @returns False before every client is ready, for an invalid position, or when the player has left.
     */
    spawn(position: Vector3 | { x: number; y: number; z: number }, heading?: number): boolean;

    /**
     * Starts a new life after death, like spawn. The server never respawns anyone by itself. Fires playerRespawn.
     * @param position Spawn position; three numbers x, y, z are accepted too.
     * @param heading Facing in radians; defaults to 0.
     * @returns False when the life could not be started.
     */
    respawn(position: Vector3 | { x: number; y: number; z: number }, heading?: number): boolean;

    /**
     * Ends the current life and clears its seat. Fires playerDespawn.
     * @returns False when the player has left.
     */
    despawn(): boolean;

    /**
     * Returns the native scene spawn hint the player's client reported for the current mission.
     * @returns The hint, or null before the client reported one.
     */
    getSuggestedSpawn(): Vector3 | null;

    /**
     * Sets server health, firing playerDamage, playerDeath or playerHealthChange. A dead player cannot be revived this way; use respawn.
     * @param health Health from 0 to 100; values outside are clamped.
     * @returns False when there is no living, spawned player.
     */
    setHealth(health: number): boolean;

    /**
     * Sets this player's server balance. Fires playerMoneyChange when it changes.
     * @param amount Whole-dollar balance from 0 to 1000000000.
     * @returns False if the player has disconnected.
     */
    setMoney(amount: number): boolean;

    /**
     * Adds or removes money without exceeding the balance bounds. Fires playerMoneyChange when it changes.
     * @param amount Signed whole-dollar change from -1000000000 to 1000000000.
     * @returns False if the result would be negative or above the maximum, or the player has disconnected.
     */
    giveMoney(amount: number): boolean;

    /**
     * Atomically checks and debits this player's server balance. Use the return value to grant shop goods. Fires playerMoneyChange on a nonzero purchase.
     * @param cost Whole-dollar cost from 0 to 1000000000.
     * @returns False if the player cannot afford the cost or has disconnected.
     */
    trySpendMoney(cost: number): boolean;

    /**
     * Returns the vehicle whose seat the current life holds.
     * @returns The vehicle, or null on foot.
     */
    getVehicle(): Vehicle | null;

    /**
     * Returns the player this client's camera follows when scripted, or null for the local camera.
     * @returns The connected target, or null.
     */
    getCameraTarget(): Player | null;

    /**
     * Makes this player's camera follow the target on foot and switch to the target's car while seated. The choice persists across both players' respawns; a missing or despawned target temporarily falls back to the observer.
     * @param target Connected player to follow, or null to restore the local camera.
     * @returns False when the observer has left or the target is not connected in the same mission.
     */
    setCameraTarget(target: Player | null): boolean;

    /**
     * Returns the seat the current life holds; 0 is the driver.
     * @returns The seat, or null on foot.
     */
    getSeat(): number | null;

    /**
     * Records the player as seated through the server seat path, as if the native entry had completed; it does not play the door animation. Seat 0 makes this player's client the vehicle's simulation controller. Fires vehiclePlayerEntered.
     * @param vehicle Vehicle to seat the player in.
     * @param seat Seat from 0 (driver) to 7; defaults to 0.
     * @returns False for a dead or unspawned player, another mission's vehicle, or a seat that is taken or out of range.
     */
    putInVehicle(vehicle: Vehicle, seat?: number): boolean;

    /**
     * Records the player's current seat as exited without the native exit animation. Fires vehiclePlayerExited.
     * @returns False when the player is not seated.
     */
    removeFromVehicle(): boolean;

    /**
     * Grants a weapon to a living player.
     * @param weaponId Supported stock weapon, 2 through 15.
     * @param loaded Loaded rounds, or the throwable count; defaults to a full magazine for a firearm and 1 otherwise. A firearm is capped to its magazine.
     * @param reserve Reserve rounds; defaults to 0.
     * @param equip Select the weapon immediately.
     * @returns False for a dead player, an unsupported weapon or a full inventory.
     */
    giveWeapon(weaponId: number, loaded?: number, reserve?: number, equip?: boolean): boolean;

    /**
     * Adds reserve ammunition to a held weapon.
     * @param weaponId A weapon the player holds.
     * @param amount Reserve delta; a negative amount removes ammunition down to zero.
     * @returns False when the weapon is not held.
     */
    giveAmmo(weaponId: number, amount: number): boolean;

    /**
     * Sets the ammunition of a held weapon; it does not grant the weapon.
     * @param weaponId A weapon the player holds.
     * @param loaded Loaded rounds; a firearm is capped to its magazine.
     * @param reserve Reserve rounds.
     * @returns False when the weapon is not held.
     */
    setWeaponAmmo(weaponId: number, loaded: number, reserve: number): boolean;

    /**
     * Removes a held weapon.
     * @param weaponId Weapon to remove.
     * @returns False when it was not held.
     */
    removeWeapon(weaponId: number): boolean;

    /**
     * Moves a held weapon and its ammunition into a replicated pickup at the player's feet. Fires weaponDropped and playerWeaponDrop.
     * @param weaponId Held weapon to drop; defaults to the selected weapon.
     * @returns The new pickup, or null if the player is dead, unspawned, seated, has no selected weapon, does not hold the requested weapon, or the pickup limit is full.
     */
    dropWeapon(weaponId?: number): Pickup | null;

    /**
     * Holsters and removes every weapon of a living player.
     * @returns False for a dead or unspawned player.
     */
    removeAllWeapons(): boolean;

    /**
     * Selects a held weapon.
     * @param weaponId A held weapon, or 0 to holster.
     * @returns False when the weapon is not held.
     */
    setCurrentWeapon(weaponId: number): boolean;

    /**
     * Returns the selected weapon.
     * @returns The weapon ID, 0 when holstered or not spawned.
     */
    getCurrentWeapon(): number;

    /**
     * Checks whether the current life holds a weapon.
     * @param weaponId Weapon to test.
     * @returns True when it is held.
     */
    hasWeapon(weaponId: number): boolean;

    /**
     * Lists the held weapons with their ammunition.
     * @returns The held weapons, empty when not spawned.
     */
    getWeapons(): WeaponSlot[];

    /**
     * Returns the selected weapon and the held weapons.
     * @returns The inventory, or null when not spawned.
     */
    getInventory(): Inventory | null;

    /**
     * Atomically replaces the whole inventory of a living player in one state revision. Without selected, the first entry becomes equipped; an empty list holsters. Duplicate weapons and more than seven items are rejected without changing the current inventory.
     * @param weapons Complete new inventory, at most seven items. Missing ammunition gets the giveWeapon defaults.
     * @param selected Weapon to select afterwards.
     * @returns False when the player is not alive, an item repeats, more than seven items are supplied, or selected is not in the new inventory.
     */
    setInventory(weapons: { weaponId: number; loaded?: number; reserve?: number }[], selected?: number): boolean;

    /**
     * Adds one item, or adds its ammunition or throwable count to a weapon already held.
     * @param weaponId Supported weapon.
     * @param loaded Rounds or count to add; defaults like giveWeapon.
     * @param reserve Reserve rounds to add; defaults to 0.
     * @returns False for a dead player or a full inventory.
     */
    addItem(weaponId: number, loaded?: number, reserve?: number): boolean;

    /**
     * Without count removes the item; with count takes that much and removes the item once none is left.
     * @param weaponId Held weapon.
     * @param count Rounds or throwables to take, reserve first.
     * @returns False when the weapon is not held.
     */
    removeItem(weaponId: number, count?: number): boolean;

    /**
     * Returns the server combat snapshot of this player.
     * @returns The snapshot, or null when the player has none.
     */
    getCombatState(): CombatState | null;
  }

  interface Player extends BasePlayer {}

  /**
   * The simulation controller's native per-part damage snapshot. Indexes are native model indexes.
   */
  interface VehicleDamage {
    /**
     * Snapshot revision.
     */
    revision: number;

    /**
     * Engine health.
     */
    engineHealth: number;

    /**
     * Native engine damage power.
     */
    engineDamagePower: number;

    /**
     * Whether the engine is destroyed.
     */
    engineDestroyed: boolean;

    /**
     * Gearbox health.
     */
    gearboxHealth: number;

    /**
     * Body damage.
     */
    bodyDamage: number;

    /**
     * Fuel tank health.
     */
    fuelTankHealth: number;

    /**
     * Whether the car burns.
     */
    burning: boolean;

    /**
     * Per-light damage.
     */
    lights: { flags: number; damage: number }[];

    /**
     * Per-zone deformation; flag 1 is broken glass or a part.
     */
    zones: { flags: number; crackLevel: number }[];

    /**
     * Per-wheel state; 0x80000000 flat, 0x40000000 detached, 0x400 broken.
     */
    wheels: { flags: number; health: number; deformAngle: number }[];
  }

  /**
   * A server car. The server owns engine, damage, seats and terminal state; the client chosen as simulation controller reports native physics and damage.
   */
  class Vehicle {
    /**
     * Creates a handle for an existing vehicle; use Vehicle.spawn to create one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Stock .i3d model filename.
     */
    readonly model: string;

    /**
     * Last reported position. Read only; use setTransform.
     */
    readonly position: Vector3;

    /**
     * Last reported rotation. Read only; use setTransform.
     */
    readonly rotation: Quaternion;

    /**
     * Linear velocity in units per second.
     */
    readonly velocity: Vector3;

    /**
     * Angular velocity.
     */
    readonly angularVelocity: Vector3;

    /**
     * Steering in native mapped angle units.
     */
    readonly steeringInput: number;

    /**
     * Fuel amount.
     */
    readonly fuel: number;

    /**
     * Tank capacity the controller reported.
     */
    readonly fuelTankCapacity: number;

    /**
     * Engine rotations, owner telemetry.
     */
    readonly engineRotations: number;

    /**
     * Current gear, owner telemetry.
     */
    readonly gear: number;

    /**
     * Highest gear of the model.
     */
    readonly maximumGear: number;

    /**
     * Light bits: left indicator 0x1, right indicator 0x2, headlights 0x80, brake active 0x100, brake check 0x800, reverse active 0x1000, reverse check 0x8000, master lights 0x10000.
     */
    readonly lightState: number;

    /**
     * Whether the horn sounds.
     */
    readonly hornOn: boolean;

    /**
     * Whether the server-owned siren is on.
     */
    readonly sirenOn: boolean;

    /**
     * Whether the engine runs.
     */
    readonly engineOn: boolean;

    /**
     * 0xRRGGBB color of the radar marker.
     */
    readonly radarColor: number;

    /**
     * Server-authored visual opacity, 0 transparent and 1 opaque.
     */
    readonly opacity: number;

    /**
     * Increments when repair resets native damage and deformation.
     */
    readonly repairRevision: number;

    /**
     * Whether the vehicle is drawn on every player's radar.
     */
    readonly radarVisible: boolean;

    /**
     * Whether the native speed limiter is on.
     */
    readonly speedLimited: boolean;

    /**
     * Resolved throttle from 0 to 1.
     */
    readonly powerInput: number;

    /**
     * Resolved brake from 0 to 1.
     */
    readonly brakeInput: number;

    /**
     * Resolved handbrake from 0 to 1.
     */
    readonly handbrakeInput: number;

    /**
     * False until a controller reported native state; setFuel, setLights and setHorn need it.
     */
    readonly dynamicsValid: boolean;

    /**
     * Script metadata health set by setDamage.
     */
    readonly health: number;

    /**
     * Script metadata damage flags.
     */
    readonly damageFlags: number;

    /**
     * Script metadata detached part flags.
     */
    readonly detachedParts: number;

    /**
     * Whether a native damage snapshot exists.
     */
    readonly nativeDamageValid: boolean;

    /**
     * Revision of the native damage snapshot.
     */
    readonly nativeDamageRevision: number;

    /**
     * Revision of the accepted deformation checkpoint.
     */
    readonly meshRevision: number;

    /**
     * Number of seats, 1 to 8.
     */
    readonly seatCount: number;

    /**
     * 0 active, 1 exploded, 2 submerged, 3 out of bounds.
     */
    readonly terminalState: number;

    /**
     * Mission generation the vehicle belongs to.
     */
    readonly missionGeneration: number;

    /**
     * Revision of the engine state.
     */
    readonly engineRevision: number;

    /**
     * Revision of the last script fuel, lights or horn command.
     */
    readonly dynamicsCommandRevision: number;

    /**
     * Sequence of the terminal transition.
     */
    readonly terminalSequence: number;

    /**
     * Sequence of the last accepted seat transition.
     */
    readonly seatSequence: number;

    /**
     * Formats this vehicle for logging.
     * @returns The vehicle ID and model.
     */
    toString(): string;

    /**
     * Fires vehicleDestroy, then removes the vehicle from every client.
     * @returns False when it no longer exists.
     */
    destroy(): boolean;

    /**
     * Returns the player whose client simulates this vehicle.
     * @returns The controller, or null.
     */
    getController(): Player | null;

    /**
     * Returns the current-life occupant of seat 0.
     * @returns The driver, or null.
     */
    getDriver(): Player | null;

    /**
     * Returns the current-life occupant of a seat.
     * @param seat Seat from 0 to 7.
     * @returns The occupant, or null.
     */
    getOccupant(seat: number): Player | null;

    /**
     * Returns the occupant of every seat.
     * @returns One entry per seat, null when empty.
     */
    getOccupants(): (Player | null)[];

    /**
     * Returns each seat's door target, from 0 closed to 1 open.
     * @returns One entry per seat.
     */
    getDoors(): number[];

    /**
     * Returns the controller's native damage snapshot.
     * @returns The snapshot, or null before the first report or while repair awaits a fresh report.
     */
    getDamageState(): VehicleDamage | null;

    /**
     * Sets the authoritative pose; the simulation controller applies it before it reports again.
     * @param position New position; three numbers are accepted too.
     * @param rotation Heading in radians or a quaternion; keeps the current rotation when omitted.
     * @param velocity Velocity after the teleport; defaults to zero.
     * @returns False when the vehicle no longer exists.
     */
    setTransform(position: Vector3 | { x: number; y: number; z: number }, rotation?: number | Quaternion, velocity?: Vector3 | { x: number; y: number; z: number }): boolean;

    /**
     * Starts or stops the engine. Fires vehicleEngineChange.
     * @param on Engine state.
     * @returns False for a terminal vehicle.
     */
    setEngine(on: boolean): boolean;

    /**
     * Sets the fuel; the controller applies and acknowledges it. After the first native tank report, only this server call may increase fuel.
     * @param fuel Fuel from 0 to fuelTankCapacity.
     * @returns False before native dynamics are available or when out of range.
     */
    setFuel(fuel: number): boolean;

    /**
     * Sets the semantic light bits; native blink phase stays game controlled.
     * @param lightState Light bits, see lightState.
     * @returns False before native dynamics are available or for unsupported bits.
     */
    setLights(lightState: number): boolean;

    /**
     * Sounds or silences the horn.
     * @param on Horn state.
     * @returns False before native dynamics are available.
     */
    setHorn(on: boolean): boolean;

    /**
     * Switches the siren sound and light bar. Drivers toggle it with K.
     * @param on Siren state.
     * @returns False for a terminal vehicle.
     */
    setSiren(on: boolean): boolean;

    /**
     * Shows or hides this vehicle on every radar.
     * @param visible Whether every player's radar shows this vehicle.
     * @param color 0xRRGGBB marker color; defaults to white.
     * @returns False when the vehicle no longer exists.
     */
    setRadarMarker(visible: boolean, color?: number): boolean;

    /**
     * Changes the car model's opacity on every client. Fires vehicleOpacityChange.
     * @param opacity Visual opacity from 0 transparent to 1 opaque.
     * @returns False for a terminal vehicle or a value outside 0 to 1.
     */
    setOpacity(opacity: number): boolean;

    /**
     * Repairs native engine, gearbox, body, fuel tank, lights, attached wheels and deform meshes without resetting position or seats. Fires vehicleRepair.
     * @returns False for a terminal vehicle. Loose debris actors remain until their own lifetime ends.
     */
    repair(): boolean;

    /**
     * Changes the seat count if the removed seats are empty.
     * @param count Seats from 1 to 8.
     * @returns False when a removed seat is taken.
     */
    setSeatCount(count: number): boolean;

    /**
     * Sets script metadata only; it does not deform the car. Fires vehicleDamageState.
     * @param health Metadata health.
     * @param damageFlags Metadata flags.
     * @param detachedParts Metadata part flags.
     * @returns False when the vehicle no longer exists.
     */
    setDamage(health: number, damageFlags: number, detachedParts: number): boolean;

    /**
     * Authors a native damage revision the simulation controller applies. Fires vehicleDamage.
     * @param engineHealth Engine health; zero lets the car burn and explode.
     * @param gearboxHealth Gearbox health.
     * @param bodyDamage Body damage.
     * @param fuelTankHealth Fuel tank health.
     * @returns False before the first native damage snapshot or for out-of-range values.
     */
    setMechanicalDamage(engineHealth: number, gearboxHealth: number, bodyDamage: number, fuelTankHealth: number): boolean;

    /**
     * Ends the vehicle; an explosion kills current occupants through server combat first. Fires vehicleTerminal.
     * @param state 1 exploded, 2 submerged, 3 out of bounds.
     * @returns False when already terminal.
     */
    setTerminalState(state: number): boolean;

    /**
     * Same as setTerminalState(1).
     * @returns False when already terminal.
     */
    explode(): boolean;

    /**
     * Records an administrative seat result without native animation and fires the matching seat event. Entering or stealing seat 0 makes the player's client the simulation controller.
     * @param seat Seat from 0 to 7.
     * @param player Living player of this mission.
     * @param result 1 entered, 2 stolen, 3 exited, 4 exit blocked.
     * @returns False when the result is not valid now.
     */
    recordSeatOutcome(seat: number, player: Player, result: number): boolean;

    /**
     * Creates a car for the current mission. Fires vehicleSpawn.
     * @param model Stock .i3d model filename, for example thunderbird00.i3d.
     * @param position Spawn position; three numbers are accepted too.
     * @param rotation Heading in radians or a quaternion; defaults to 0.
     * @param controller Mission-ready player whose client simulates the car first; defaults to the first ready player.
     * @returns The vehicle, or null before every client is mission ready.
     */
    static spawn(model: string, position: Vector3 | { x: number; y: number; z: number }, rotation?: number | Quaternion, controller?: Player): Vehicle | null;
  }

  interface Vehicle extends Entity {}

  /**
   * A server-owned weapon in the world: a script pickup, a dropped weapon or a dead player's weapon. The use key asks the server, which checks reach, life and inventory.
   */
  class Pickup {
    /**
     * Creates a handle for an existing pickup; use Pickup.create to place one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Weapon lying here.
     */
    readonly weaponId: number;

    /**
     * Facing in radians.
     */
    readonly heading: number;

    /**
     * Loaded rounds or throwable count.
     */
    readonly loaded: number;

    /**
     * Reserve rounds.
     */
    readonly reserve: number;

    /**
     * Formats this pickup for logging.
     * @returns The pickup ID and weapon.
     */
    toString(): string;

    /**
     * Removes the pickup from every client.
     * @returns False when it no longer exists.
     */
    destroy(): boolean;

    /**
     * Places a weapon pickup in the current mission. A mission change removes every pickup.
     * @param weaponId Supported weapon, 2 through 15.
     * @param position Where it lies; three numbers are accepted too.
     * @param heading Facing in radians; defaults to 0.
     * @param loaded Loaded rounds or count, up to 1000; defaults to a full magazine for a firearm and 1 otherwise.
     * @param reserve Reserve rounds, up to 1000; defaults to 0.
     * @param lifetimeMs Removal delay; 0 or omitted keeps it until taken or destroyed.
     * @returns The pickup, or null when the limit of 256 is reached.
     */
    static create(weaponId: number, position: Vector3 | { x: number; y: number; z: number }, heading?: number, loaded?: number, reserve?: number, lifetimeMs?: number): Pickup | null;

    /**
     * Lists every pickup.
     * @returns The current pickups.
     */
    static getAll(): Pickup[];
  }

  interface Pickup extends Entity {}

  /**
   * A server-owned mission door. Native use opens or closes it for everyone; its lock, swing direction, paired leaf and pose survive reconnects until the mission changes.
   */
  class Door {
    /**
     * Creates a handle for a replicated mission door.
     * @param id Door network ID.
     */
    constructor(id: number);

    /**
     * Name of the root door frame in the mission.
     */
    readonly frameName: string;

    /**
     * Target open state.
     */
    readonly open: boolean;

    /**
     * Locks native use and script opening.
     */
    locked: boolean;

    /**
     * Whether the native use prompt can operate this door.
     */
    enabled: boolean;

    /**
     * Current swing side of the root leaf.
     */
    readonly reverse: boolean;

    /**
     * Target open fraction from 0 to 1.
     */
    readonly openFraction: number;

    /**
     * Animates the door open, including its paired leaf. Fires doorStateChange.
     * @param reverse Root leaf swing side.
     * @param pairedReverse Paired leaf swing side; use the opposite value for outward double doors.
     */
    openDoor(reverse: boolean, pairedReverse: boolean): boolean;

    /**
     * Animates the door closed, including its paired leaf. Fires doorStateChange.
     */
    closeDoor(): boolean;

    /**
     * Snaps both leaves to a partial or full pose and replicates it.
     * @param fraction 0 to 1.
     * @param reverse Root swing side.
     * @param pairedReverse Paired leaf swing side.
     */
    setOpenFraction(fraction: number, reverse: boolean, pairedReverse: boolean): boolean;

    /**
     * Stops managing this mission door until its next native use.
     */
    destroy(): boolean;

    /**
     * Formats the door ID and frame name.
     */
    toString(): string;

    /**
     * Registers a stock mission door for server control. Native use also registers nearby doors automatically.
     * @param frameName Root door frame name.
     * @param position World hinge position for server reach checks.
     */
    static create(frameName: string, position: Vector3 | { x: number; y: number; z: number }): Door | null;

    /**
     * Finds a registered door by name.
     * @param frameName Root frame name.
     */
    static get(frameName: string): Door | null;

    /**
     * Lists registered mission doors.
     */
    static getAll(): Door[];
  }

  interface Door extends Entity {}

  /**
   * A stock Mafia 1 weapon the server accepts.
   */
  interface WeaponInfo {
    /**
     * Stock item identifier, 2 through 15.
     */
    weaponId: number;

    /**
     * Item name.
     */
    name: string;

    /**
     * How the item is used.
     */
    kind: "melee" | "firearm" | "throwable";

    /**
     * Rounds per magazine for a firearm; 1 otherwise.
     */
    magazine: number;
  }

  /**
   * Catalog of the stock weapons the server accepts. 2 is the knuckle duster, 5 the Molotov and 15 the grenade.
   */
  const Weapon: {
    /**
     * Lists every weapon the server accepts.
     * @returns The supported weapons in ID order.
     */
    list(): WeaponInfo[];

    /**
     * Describes one weapon.
     * @param weaponId Stock item identifier.
     * @returns The weapon, or null when the server does not accept it.
     */
    get(weaponId: number): WeaponInfo | null;
  };

  /**
   * A replicated positional sound. Every client in range builds its own native sound for it and hears it the same way; a late joiner hears it too. A mission change removes every sound.
   */
  class Sound {
    /**
     * Creates a handle for an existing sound; use Sound.create to place one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Game sound file, such as "00_dog.wav".
     */
    readonly wave: string;

    /**
     * Volume from 0 to 1.
     */
    volume: number;

    /**
     * Audible radius from 1 to 200 units.
     */
    radius: number;

    /**
     * Whether the sound plays; a disabled sound keeps its place.
     */
    enabled: boolean;

    /**
     * Formats this sound for logging.
     * @returns The sound ID and wave.
     */
    toString(): string;

    /**
     * Makes the sound follow a player or vehicle.
     * @param entity Entity to follow, or null to stay where it is.
     * @returns False when the sound was destroyed.
     */
    attachTo(entity: Player | Vehicle | null): boolean;

    /**
     * Returns the followed entity.
     * @returns The entity, or null for a fixed sound.
     */
    getAttached(): Player | Vehicle | null;

    /**
     * Removes the sound for everyone.
     * @returns False when it was already gone.
     */
    destroy(): boolean;

    /**
     * Places a looping positional sound.
     * @param wave Game sound file under the Sounds directory or its archives.
     * @param position Where the sound plays.
     * @param options Radius 1 to 200 (default 25), volume 0 to 1 (default 1) and an entity to follow.
     * @returns The sound, or null when the limit of 64 sounds is reached.
     */
    static create(wave: string, position: Vector3 | { x: number; y: number; z: number }, options?: { radius?: number; volume?: number; attachTo?: Player | Vehicle }): Sound | null;

    /**
     * Plays a sound once for every client currently in the mission.
     * @param wave Game sound file.
     * @param position Where the sound plays.
     * @param options Radius and volume as for create.
     */
    static play(wave: string, position: Vector3 | { x: number; y: number; z: number }, options?: { radius?: number; volume?: number }): void;
  }

  interface Sound extends Entity {}

  /**
   * The replicated weather.
   */
  interface WeatherInfo {
    /**
     * Weather preset; default keeps the mission's own weather.
     */
    weather: "default" | "clear" | "rain" | "snow";

    /**
     * Rain or snow intensity from 0 to 100, or null for the preset's own.
     */
    intensity: number | null;
  }

  /**
   * The shared Mafia 1 world: mission, collections, weather, scene frames, city music and effects everyone sees.
   */
  const World: {
    /**
     * Returns the current stock mission.
     * @returns Stock mission directory name.
     */
    getMission(): string;

    /**
     * Returns the current mission generation.
     * @returns It increases on every change, including a reload.
     */
    getMissionGeneration(): number;

    /**
     * Resets the world and asks every client to load the mission. Fires missionChange.
     * @param mission Stock mission name, such as "freeride".
     * @returns False for an unknown mission.
     */
    changeMission(mission: string): boolean;

    /**
     * Whether every connected player has loaded the current mission.
     * @returns True when all are ready.
     */
    isReady(): boolean;

    /**
     * Lists the connected players.
     * @returns Every connected player.
     */
    getPlayers(): Player[];

    /**
     * Lists the server vehicles.
     * @returns Every vehicle.
     */
    getVehicles(): Vehicle[];

    /**
     * Lists the weapon pickups.
     * @returns Every pickup.
     */
    getPickups(): Pickup[];

    /**
     * Lists the registered native mission doors.
     * @returns Every managed door in the current mission.
     */
    getDoors(): Door[];

    /**
     * Changes the weather for everyone, including late joiners; it carries over mission changes.
     * @param weather Weather preset.
     * @param intensity Rain or snow intensity from 0 to 100.
     */
    setWeather(weather: "default" | "clear" | "rain" | "snow", intensity?: number): void;

    /**
     * Returns the replicated weather.
     * @returns The weather preset and intensity.
     */
    getWeather(): WeatherInfo;

    /**
     * Turns the city music on or off for everyone.
     * @param enabled Whether the city's ambient music plays.
     */
    setCityMusic(enabled: boolean): void;

    /**
     * Whether the city music plays.
     * @returns True when enabled.
     */
    isCityMusicEnabled(): boolean;

    /**
     * Replicates the native GAME_NIGHTMISSION flag to every client, including late joiners. It controls night behavior such as vehicle lights; it does not change the sky or time of day.
     * @param enabled True or false overrides the mission's night flag; null restores the mission's own flag.
     */
    setNightMode(enabled: boolean | null): void;

    /**
     * Returns the server's night-mode override.
     * @returns Null when each mission script controls its own flag.
     */
    getNightModeOverride(): boolean | null;

    /**
     * Shows or hides a scene object for everyone, including late joiners, until the mission changes.
     * @param name Name of a static frame in the current mission's scene.
     * @param visible Whether it shows.
     * @returns False when 128 frames are already overridden.
     */
    setFrameVisible(name: string, visible: boolean): boolean;

    /**
     * Restores every overridden frame to the scene's own visibility.
     */
    resetFrames(): void;

    /**
     * Applies FRM_SETALPHA to a supported visual frame for everyone in this mission, including late joiners.
     * @param name Named static visual frame in the current mission's scene.
     * @param opacity Opacity from 0 (transparent) to 1 (opaque).
     * @returns False when 128 opacity overrides already exist.
     */
    setFrameOpacity(name: string, opacity: number): boolean;

    /**
     * Restores the scene's original opacity for one or all overridden frames.
     * @param name One frame to restore; omit to restore every overridden frame.
     */
    resetFrameOpacity(name?: string): void;

    /**
     * Explodes for every client. The server applies player damage and fires playerDamage and playerDeath; clients damage cars and objects.
     * @param position Explosion centre.
     * @param options Radius up to 30 (default 15) and damage up to 1000 (default 400). Damage 0 is a visual explosion.
     */
    createExplosion(position: Vector3 | { x: number; y: number; z: number }, options?: { radius?: number; damage?: number }): void;

    /**
     * Starts a fire every client sees; the server burns players standing in it.
     * @param position Fire position.
     * @param options Duration 500 to 60000 ms (default 5000), radius up to 10 (default 2.5) and damage per second up to 200 (default 50).
     */
    createFire(position: Vector3 | { x: number; y: number; z: number }, options?: { duration?: number; radius?: number; damage?: number }): void;
  };

  /**
   * Mutable two-dimensional vector exposed as the global Vector2.
   */
  class Vector2 {
    /**
     * Creates a vector from two numeric components.
     * @param x Initial X component.
     * @param y Initial Y component.
     */
    constructor(x: number, y: number);

    /**
     * Mutable X component.
     */
    x: number;

    /**
     * Mutable Y component.
     */
    y: number;

    /**
     * Read-only Euclidean magnitude of this vector.
     */
    readonly length: number;

    /**
     * Read-only squared magnitude, avoiding a square-root calculation.
     */
    readonly lengthSquared: number;

    /**
     * Adds another vector to this vector in place.
     * @param other Vector to add component-wise.
     * @returns This mutated vector for chaining.
     */
    add(other: Vector2): this;

    /**
     * Subtracts another vector from this vector in place.
     * @param other Vector to subtract component-wise.
     * @returns This mutated vector for chaining.
     */
    sub(other: Vector2): this;

    /**
     * Multiplies this vector by a scalar in place.
     * @param scalar Multiplier applied to both components.
     * @returns This mutated vector for chaining.
     */
    mul(scalar: number): this;

    /**
     * Divides this vector by a scalar in place.
     * @param scalar Divisor applied to both components; must be non-zero.
     * @returns This mutated vector for chaining.
     */
    div(scalar: number): this;

    /**
     * Computes the dot product without changing either vector.
     * @param other Vector used for the dot product.
     * @returns Scalar dot product.
     */
    dot(other: Vector2): number;

    /**
     * Normalizes this vector in place; a zero vector remains unchanged.
     * @returns This mutated vector for chaining.
     */
    normalize(): this;

    /**
     * Linearly interpolates this vector toward a target in place.
     * @param target Destination vector.
     * @param t Interpolation factor; 0 keeps the current value and 1 reaches target.
     * @returns This mutated vector for chaining.
     */
    lerp(target: Vector2, t: number): this;

    /**
     * Replaces both components in place.
     * @param x Replacement X component.
     * @param y Replacement Y component.
     * @returns This mutated vector for chaining.
     */
    set(x: number, y: number): this;

    /**
     * Computes Euclidean distance to another vector.
     * @param other Vector to measure from this vector.
     * @returns Distance between the two vectors.
     */
    distance(other: Vector2): number;

    /**
     * Creates an independent copy of this vector.
     * @returns New vector with the same components.
     */
    clone(): Vector2;

    /**
     * Formats this vector for logging and debugging.
     * @returns Text in Vector2(x, y) form.
     */
    toString(): string;

    /**
     * Converts this vector to a plain object for JSON.stringify.
     * @returns Object containing the current components.
     */
    toJSON(): { x: number; y: number };

    /**
     * Creates a vector whose components are zero.
     * @returns New Vector2(0, 0).
     */
    static zero(): Vector2;

    /**
     * Creates a vector whose components are one.
     * @returns New Vector2(1, 1).
     */
    static one(): Vector2;
  }

  /**
   * Mutable three-dimensional vector exposed as the global Vector3 for positions, directions, and Euler angles.
   */
  class Vector3 {
    /**
     * Creates a vector from three numeric components.
     * @param x Initial X component.
     * @param y Initial Y component.
     * @param z Initial Z component.
     */
    constructor(x: number, y: number, z: number);

    /**
     * Mutable X component.
     */
    x: number;

    /**
     * Mutable Y component.
     */
    y: number;

    /**
     * Mutable Z component.
     */
    z: number;

    /**
     * Read-only Euclidean magnitude of this vector.
     */
    readonly length: number;

    /**
     * Read-only squared magnitude, avoiding a square-root calculation.
     */
    readonly lengthSquared: number;

    /**
     * Adds another vector to this vector in place.
     * @param other Vector to add component-wise.
     * @returns This mutated vector for chaining.
     */
    add(other: Vector3): this;

    /**
     * Subtracts another vector from this vector in place.
     * @param other Vector to subtract component-wise.
     * @returns This mutated vector for chaining.
     */
    sub(other: Vector3): this;

    /**
     * Multiplies this vector by a scalar in place.
     * @param scalar Multiplier applied to every component.
     * @returns This mutated vector for chaining.
     */
    mul(scalar: number): this;

    /**
     * Divides this vector by a scalar in place.
     * @param scalar Divisor applied to every component; must be non-zero.
     * @returns This mutated vector for chaining.
     */
    div(scalar: number): this;

    /**
     * Computes the dot product without changing either vector.
     * @param other Vector used for the dot product.
     * @returns Scalar dot product.
     */
    dot(other: Vector3): number;

    /**
     * Replaces this vector with its cross product against another vector.
     * @param other Second vector in the cross product.
     * @returns This mutated perpendicular vector for chaining.
     */
    cross(other: Vector3): this;

    /**
     * Normalizes this vector in place; a zero vector remains unchanged.
     * @returns This mutated vector for chaining.
     */
    normalize(): this;

    /**
     * Linearly interpolates this vector toward a target in place.
     * @param target Destination vector.
     * @param t Interpolation factor; 0 keeps the current value and 1 reaches target.
     * @returns This mutated vector for chaining.
     */
    lerp(target: Vector3, t: number): this;

    /**
     * Replaces all components in place.
     * @param x Replacement X component.
     * @param y Replacement Y component.
     * @param z Replacement Z component.
     * @returns This mutated vector for chaining.
     */
    set(x: number, y: number, z: number): this;

    /**
     * Computes Euclidean distance to another vector.
     * @param other Vector to measure from this vector.
     * @returns Distance between the two vectors.
     */
    distance(other: Vector3): number;

    /**
     * Creates an independent copy of this vector.
     * @returns New vector with the same components.
     */
    clone(): Vector3;

    /**
     * Formats this vector for logging and debugging.
     * @returns Text in Vector3(x, y, z) form.
     */
    toString(): string;

    /**
     * Converts this vector to a plain object for JSON.stringify.
     * @returns Object containing the current components.
     */
    toJSON(): { x: number; y: number; z: number };

    /**
     * Creates a zero vector.
     * @returns New Vector3(0, 0, 0).
     */
    static zero(): Vector3;

    /**
     * Creates a vector whose components are one.
     * @returns New Vector3(1, 1, 1).
     */
    static one(): Vector3;

    /**
     * Creates the framework's positive-Y unit direction.
     * @returns New Vector3(0, 1, 0).
     */
    static up(): Vector3;

    /**
     * Creates the framework's positive-Z unit direction.
     * @returns New Vector3(0, 0, 1).
     */
    static forward(): Vector3;

    /**
     * Creates the framework's positive-X unit direction.
     * @returns New Vector3(1, 0, 0).
     */
    static right(): Vector3;
  }

  /**
   * Mutable four-dimensional vector exposed as the global Vector4.
   */
  class Vector4 {
    /**
     * Creates a vector from four numeric components.
     * @param x Initial X component.
     * @param y Initial Y component.
     * @param z Initial Z component.
     * @param w Initial W component.
     */
    constructor(x: number, y: number, z: number, w: number);

    /**
     * Mutable X component.
     */
    x: number;

    /**
     * Mutable Y component.
     */
    y: number;

    /**
     * Mutable Z component.
     */
    z: number;

    /**
     * Mutable W component.
     */
    w: number;

    /**
     * Read-only Euclidean magnitude of this vector.
     */
    readonly length: number;

    /**
     * Read-only squared magnitude, avoiding a square-root calculation.
     */
    readonly lengthSquared: number;

    /**
     * Adds another vector to this vector in place.
     * @param other Vector to add component-wise.
     * @returns This mutated vector for chaining.
     */
    add(other: Vector4): this;

    /**
     * Subtracts another vector from this vector in place.
     * @param other Vector to subtract component-wise.
     * @returns This mutated vector for chaining.
     */
    sub(other: Vector4): this;

    /**
     * Multiplies this vector by a scalar in place.
     * @param scalar Multiplier applied to every component.
     * @returns This mutated vector for chaining.
     */
    mul(scalar: number): this;

    /**
     * Divides this vector by a scalar in place.
     * @param scalar Divisor applied to every component; must be non-zero.
     * @returns This mutated vector for chaining.
     */
    div(scalar: number): this;

    /**
     * Computes the dot product without changing either vector.
     * @param other Vector used for the dot product.
     * @returns Scalar dot product.
     */
    dot(other: Vector4): number;

    /**
     * Computes Euclidean distance to another vector.
     * @param other Vector to measure from this vector.
     * @returns Distance between the two vectors.
     */
    distance(other: Vector4): number;

    /**
     * Normalizes this vector in place; a zero vector remains unchanged.
     * @returns This mutated vector for chaining.
     */
    normalize(): this;

    /**
     * Linearly interpolates this vector toward a target in place.
     * @param target Destination vector.
     * @param t Interpolation factor; 0 keeps the current value and 1 reaches target.
     * @returns This mutated vector for chaining.
     */
    lerp(target: Vector4, t: number): this;

    /**
     * Replaces all components in place.
     * @param x Replacement X component.
     * @param y Replacement Y component.
     * @param z Replacement Z component.
     * @param w Replacement W component.
     * @returns This mutated vector for chaining.
     */
    set(x: number, y: number, z: number, w: number): this;

    /**
     * Creates an independent copy of this vector.
     * @returns New vector with the same components.
     */
    clone(): Vector4;

    /**
     * Formats this vector for logging and debugging.
     * @returns Text in Vector4(x, y, z, w) form.
     */
    toString(): string;

    /**
     * Converts this vector to a plain object for JSON.stringify.
     * @returns Object containing the current components.
     */
    toJSON(): { x: number; y: number; z: number; w: number };

    /**
     * Creates a zero vector.
     * @returns New Vector4(0, 0, 0, 0).
     */
    static zero(): Vector4;

    /**
     * Creates a vector whose components are one.
     * @returns New Vector4(1, 1, 1, 1).
     */
    static one(): Vector4;
  }

  /**
   * Mutable quaternion exposed as the global Quaternion for three-dimensional rotations. Components are scalar-first (w, x, y, z), matching GLM — not the x, y, z, w order some libraries use.
   */
  class Quaternion {
    /**
     * Creates a quaternion in scalar-first w, x, y, z component order. Watch the trap: the scalar w is the first argument, unlike the x, y, z, w order used by some other libraries.
     * @param w Initial scalar component (comes first — this is not x, y, z, w order).
     * @param x Initial X imaginary component.
     * @param y Initial Y imaginary component.
     * @param z Initial Z imaginary component.
     */
    constructor(w: number, x: number, y: number, z: number);

    /**
     * Mutable scalar component.
     */
    w: number;

    /**
     * Mutable X imaginary component.
     */
    x: number;

    /**
     * Mutable Y imaginary component.
     */
    y: number;

    /**
     * Mutable Z imaginary component.
     */
    z: number;

    /**
     * Read-only magnitude (norm) of this quaternion; 1 for a unit rotation.
     */
    readonly length: number;

    /**
     * Read-only squared magnitude, avoiding a square-root calculation.
     */
    readonly lengthSquared: number;

    /**
     * Composes this rotation with another quaternion in place.
     * @param other Rotation composed after this quaternion.
     * @returns This mutated quaternion for chaining.
     */
    mul(other: Quaternion): this;

    /**
     * Normalizes this quaternion in place; a zero quaternion becomes the identity rotation instead of NaN.
     * @returns This unit quaternion for chaining.
     */
    normalize(): this;

    /**
     * Computes the conjugate without changing this quaternion.
     * @returns New conjugated quaternion.
     */
    conjugate(): Quaternion;

    /**
     * Computes the inverse rotation without changing this quaternion.
     * @returns New inverse quaternion.
     */
    inverse(): Quaternion;

    /**
     * Spherically interpolates this quaternion toward a target in place.
     * @param target Destination rotation.
     * @param t Interpolation factor; 0 keeps the current rotation and 1 reaches target.
     * @returns This mutated quaternion for chaining.
     */
    slerp(target: Quaternion, t: number): this;

    /**
     * Computes the quaternion dot product.
     * @param other Quaternion used for the dot product.
     * @returns Scalar dot product.
     */
    dot(other: Quaternion): number;

    /**
     * Applies this rotation to a vector without mutating either value.
     * @param vector Vector to rotate.
     * @returns New rotated vector.
     */
    rotateVector(vector: Vector3): Vector3;

    /**
     * Converts this rotation to Euler angles in radians.
     * @returns Pitch, yaw, and roll as a Vector3.
     */
    toEuler(): Vector3;

    /**
     * Replaces all quaternion components in place.
     * @param w Replacement scalar component.
     * @param x Replacement X imaginary component.
     * @param y Replacement Y imaginary component.
     * @param z Replacement Z imaginary component.
     * @returns This mutated quaternion for chaining.
     */
    set(w: number, x: number, y: number, z: number): this;

    /**
     * Creates an independent copy of this quaternion.
     * @returns New quaternion with the same components.
     */
    clone(): Quaternion;

    /**
     * Formats this quaternion for logging and debugging.
     * @returns Text in Quaternion(w, x, y, z) form.
     */
    toString(): string;

    /**
     * Converts this quaternion to a plain object for JSON.stringify.
     * @returns Object containing the current components.
     */
    toJSON(): { w: number; x: number; y: number; z: number };

    /**
     * Creates the identity rotation.
     * @returns New Quaternion(1, 0, 0, 0).
     */
    static identity(): Quaternion;

    /**
     * Creates a rotation from Euler angles.
     * @param pitch Pitch angle in radians.
     * @param yaw Yaw angle in radians.
     * @param roll Roll angle in radians.
     * @returns New rotation quaternion.
     */
    static fromEuler(pitch: number, yaw: number, roll: number): Quaternion;

    /**
     * Creates a rotation around an axis.
     * @param axis Rotation axis; it is normalized internally.
     * @param angle Rotation angle in radians.
     * @returns New axis-angle rotation quaternion.
     */
    static fromAxisAngle(axis: Vector3, angle: number): Quaternion;
  }

  /**
   * Mutable RGBA color exposed as the global Color, with components stored from 0 to 1.
   */
  class Color {
    /**
     * Creates a color from normalized RGBA components.
     * @param r Initial red component from 0 to 1.
     * @param g Initial green component from 0 to 1.
     * @param b Initial blue component from 0 to 1.
     * @param a Optional alpha component from 0 to 1; defaults to 1.
     */
    constructor(r: number, g: number, b: number, a?: number);

    /**
     * Mutable normalized red component.
     */
    r: number;

    /**
     * Mutable normalized green component.
     */
    g: number;

    /**
     * Mutable normalized blue component.
     */
    b: number;

    /**
     * Mutable normalized alpha component.
     */
    a: number;

    /**
     * Linearly interpolates every component toward a target in place.
     * @param target Destination color.
     * @param t Interpolation factor; 0 keeps this color and 1 reaches target.
     * @returns This mutated color for chaining.
     */
    lerp(target: Color, t: number): this;

    /**
     * Replaces this color's normalized components in place.
     * @param r Replacement red component from 0 to 1.
     * @param g Replacement green component from 0 to 1.
     * @param b Replacement blue component from 0 to 1.
     * @param a Optional replacement alpha; defaults to 1 when omitted.
     * @returns This mutated color for chaining.
     */
    set(r: number, g: number, b: number, a?: number): this;

    /**
     * Creates an independent copy of this color.
     * @returns New color with the same components.
     */
    clone(): Color;

    /**
     * Converts normalized components to a hexadecimal CSS-style string.
     * @param includeAlpha Whether to append the alpha byte; defaults to false.
     * @returns Lowercase #rrggbb or #rrggbbaa string.
     */
    toHex(includeAlpha?: boolean): string;

    /**
     * Formats this color for logging and debugging.
     * @returns Text in Color(r, g, b, a) form.
     */
    toString(): string;

    /**
     * Converts this color to a plain object for JSON.stringify.
     * @returns Object containing the current normalized components.
     */
    toJSON(): { r: number; g: number; b: number; a: number };

    /**
     * Parses a hexadecimal color string.
     * @param hex Hexadecimal color in #RRGGBB or #RRGGBBAA form; the leading # is optional. Three- and four-digit shorthands are not supported.
     * @returns Parsed color, or opaque white when the input is invalid.
     */
    static fromHex(hex: string): Color;

    /**
     * Creates a normalized color from byte components.
     * @param r Red byte from 0 to 255.
     * @param g Green byte from 0 to 255.
     * @param b Blue byte from 0 to 255.
     * @param a Optional alpha byte from 0 to 255; defaults to 255.
     * @returns New normalized color.
     */
    static fromRGB(r: number, g: number, b: number, a?: number): Color;

    /**
     * Creates opaque white.
     * @returns New Color(1, 1, 1, 1).
     */
    static white(): Color;

    /**
     * Creates opaque black.
     * @returns New Color(0, 0, 0, 1).
     */
    static black(): Color;

    /**
     * Creates opaque red.
     * @returns New Color(1, 0, 0, 1).
     */
    static red(): Color;

    /**
     * Creates opaque green.
     * @returns New Color(0, 1, 0, 1).
     */
    static green(): Color;

    /**
     * Creates opaque blue.
     * @returns New Color(0, 0, 1, 1).
     */
    static blue(): Color;

    /**
     * Creates opaque yellow.
     * @returns New Color(1, 1, 0, 1).
     */
    static yellow(): Color;

    /**
     * Creates opaque cyan.
     * @returns New Color(0, 1, 1, 1).
     */
    static cyan(): Color;

    /**
     * Creates opaque magenta.
     * @returns New Color(1, 0, 1, 1).
     */
    static magenta(): Color;

    /**
     * Creates fully transparent black.
     * @returns New Color(0, 0, 0, 0).
     */
    static transparent(): Color;
  }

  /**
   * Asynchronous resource event bus exposed as the global Events.
   */
  const Events: {
    /**
     * Registers a persistent handler in the shared event namespace.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     * @returns Function that removes this exact subscription.
     */
    on(eventName: string, handler: EventHandler): Unsubscribe;

    /**
     * Registers a handler that is removed before its first invocation.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    once(eventName: string, handler: EventHandler): void;

    /**
     * Removes a matching handler owned by the calling resource.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    off(eventName: string, handler: EventHandler): void;

    /**
     * Invokes every shared handler and waits for all synchronous and asynchronous results.
     * @param eventName Shared event name.
     * @param args Arguments delivered to every matching handler.
     * @returns Promise rejected with an AggregateError when one or more handlers fail.
     */
    emit(eventName: string, args?: unknown[]): Promise<void>;

    /**
     * Invokes matching handlers belonging only to one resource.
     * @param resourceName Destination running resource.
     * @param eventName Shared event name.
     * @param args Arguments delivered to matching handlers owned by the destination.
     * @returns Promise rejected when one or more destination handlers fail.
     */
    emitTo(resourceName: string, eventName: string, args?: unknown[]): Promise<void>;

    /**
     * Registers a handler in the calling resource's private local-event namespace.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    onLocal(eventName: string, handler: EventHandler): void;

    /**
     * Emits an event only within the calling resource.
     * @param eventName Private local event name.
     * @param args Arguments delivered only to handlers owned by the calling resource.
     * @returns Promise rejected when one or more local handlers fail.
     */
    emitLocal(eventName: string, args?: unknown[]): Promise<void>;

    /**
     * Counts persistent and one-shot shared handlers across resources.
     * @param eventName Shared event name to inspect.
     * @returns Number of matching handlers.
     */
    listenerCount(eventName: string): number;

    /**
     * Registers a persistent server handler for events originating from clients; this namespace is isolated from native events.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     * @returns Function that removes this exact subscription.
     */
    onClient(eventName: string, handler: EventHandler): Unsubscribe;

    /**
     * Registers a one-shot server handler for a client-originated event.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    onceClient(eventName: string, handler: EventHandler): void;

    /**
     * Removes a matching client-originated event handler owned by the calling resource.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    offClient(eventName: string, handler: EventHandler): void;

    /**
     * Broadcasts a named event to every connected client's shared Events handlers.
     * @param eventName Client shared-event name.
     * @param payload Optional string payload sent verbatim; other values are JSON-serialized.
     */
    emitAllClients(eventName: string, payload?: unknown): void;
  };

  /**
   * Typed request and notification channel between local resources through the global Messages object.
   */
  const Messages: {
    /**
     * Registers or replaces a message handler owned by the calling resource.
     * @param messageType Message type unique within the receiving resource.
     * @param handler Handler invoked with the payload and a reply callback; the reply is ignored for notifications.
     */
    handle(messageType: string, handler: MessageHandler): void;

    /**
     * Sends a request to another local resource and waits for its handler to call reply.
     * @param resourceName Destination running resource.
     * @param messageType Handler type registered by the destination.
     * @param payload Optional payload delivered to the handler.
     * @returns Promise resolved with the reply value or rejected when delivery or handling fails.
     */
    request(resourceName: string, messageType: string, payload?: unknown): Promise<unknown>;

    /**
     * Sends a fire-and-forget notification to another local resource.
     * @param resourceName Destination running resource.
     * @param messageType Handler type registered by the destination.
     * @param payload Optional payload delivered to the handler.
     */
    send(resourceName: string, messageType: string, payload?: unknown): void;
  };

  /**
   * Bulk access to values exported by another running resource through the global Imports object.
   */
  const Imports: {
    /**
     * Builds an object containing every currently registered export from another resource; undeclared dependencies produce a warning.
     * @param resourceName Name of the running resource whose exports should be read.
     * @returns Object keyed by export name, or an empty object when the resource has no exports.
     */
    get(resourceName: string): Record<string, unknown>;
  };

  /**
   * Registration and lookup of cross-resource values through the global Exports object.
   */
  const Exports: {
    /**
     * Registers a value from the calling resource for use by dependent resources.
     * @param name Export name, preferably declared in the current resource manifest.
     * @param value JavaScript value or function retained by the current resource.
     * @returns True when the value was registered.
     */
    register(name: string, value: unknown): boolean;

    /**
     * Reads one registered export from another running resource; undeclared dependencies produce a warning.
     * @param resourceName Name of the running resource that owns the export.
     * @param exportName Registered export name.
     * @returns The exported value.
     */
    get(resourceName: string, exportName: string): unknown;
  };

  /**
   * Resource-aware console that routes output through the Framework logger.
   */
  const console: {
    /**
     * Writes an informational log entry prefixed with the current resource name.
     * @param values Values formatted and joined with spaces.
     */
    log(values?: unknown[]): void;

    /**
     * Alias of console.log for informational output.
     * @param values Values formatted and joined with spaces.
     */
    info(values?: unknown[]): void;

    /**
     * Writes a warning log entry prefixed with the current resource name.
     * @param values Values formatted and joined with spaces.
     */
    warn(values?: unknown[]): void;

    /**
     * Writes an error log entry prefixed with the current resource name.
     * @param values Values formatted and joined with spaces.
     */
    error(values?: unknown[]): void;

    /**
     * Writes a debug log entry prefixed with the current resource name.
     * @param values Values formatted and joined with spaces.
     */
    debug(values?: unknown[]): void;
  };

  /**
   * Runtime-side flags exposed as the global ExecutionEnvironment.
   */
  const ExecutionEnvironment: {
    /**
     * True in the sandboxed client scripting runtime.
     */
    readonly isClient: boolean;

    /**
     * True in the authoritative server scripting runtime.
     */
    readonly isServer: boolean;
  };

  /**
   * Server-side delivery of structured chat messages to connected clients.
   */
  const Chat: {
    /**
     * Broadcasts a structured chat message to all clients.
     * @param text Message body broadcast to every connected client.
     * @param options Optional author label and color: a Color or a packed 0xRRGGBBAA integer.
     */
    sendToAll(text: string, options?: { author?: string; color?: number | Color }): void;

    /**
     * Sends a structured chat message to one player's owning connection.
     * @param player Player-owned entity identifying the destination connection.
     * @param text Message body sent to the player.
     * @param options Optional author label and color: a Color or a packed 0xRRGGBBAA integer.
     */
    sendToPlayer(player: Entity, text: string, options?: { author?: string; color?: number | Color }): void;
  };

  /**
   * Server-side proximity voice rules: how far voice carries, and who may talk to or hear whom.
   */
  const Voice: {
    /**
     * Sets how far voice carries for talkers with no override of their own. Connected clients are told, so their playback fades out at the same distance.
     * @param range Audibility radius in world units; values <= 0 restore the default of 25.
     */
    setRange(range: number): void;

    /**
     * Reads the server-wide proximity range.
     * @returns Radius in world units.
     */
    getRange(): number;

    /**
     * Overrides how far one player's voice carries, for whisper and shout modes.
     * @param player Player whose voice carries the given distance.
     * @param range Audibility radius in world units; values <= 0 return them to the server-wide range.
     */
    setPlayerRange(player: Entity, range: number): void;

    /**
     * Reads how far a player's voice carries, with the server-wide default already resolved.
     * @param player Player to query.
     * @returns Radius in world units.
     */
    getPlayerRange(player: Entity): number;

    /**
     * Server-wide mute: a muted player's voice reaches nobody.
     * @param player Player to mute or unmute.
     * @param muted True to stop their voice reaching anyone.
     */
    setPlayerMuted(player: Entity, muted: boolean): void;

    /**
     * Checks the server-wide mute flag.
     * @param player Player to query.
     * @returns True when the player's voice reaches nobody.
     */
    isPlayerMuted(player: Entity): boolean;

    /**
     * Server-wide deafen: a deaf player receives nobody.
     * @param player Player to deafen or undeafen.
     * @param deaf True to stop them receiving anyone's voice.
     */
    setPlayerDeaf(player: Entity, deaf: boolean): void;

    /**
     * Checks the server-wide deafen flag.
     * @param player Player to query.
     * @returns True when the player receives nobody's voice.
     */
    isPlayerDeaf(player: Entity): boolean;

    /**
     * One-way mute between two players, enforced by the server rather than the client.
     * @param listener Player who stops hearing the target.
     * @param target Player the listener stops hearing.
     * @param muted True to mute, false to restore.
     */
    setLocalMute(listener: Entity, target: Entity, muted: boolean): void;

    /**
     * Checks a one-way mute.
     * @param listener Player doing the muting.
     * @param target Player being muted.
     * @returns True when the listener does not receive the target.
     */
    isLocallyMuted(listener: Entity, target: Entity): boolean;

    /**
     * Checks whether a player left voice chat enabled in their own client settings. A preference, not a permission: use setPlayerMuted or setPlayerDeaf to enforce anything.
     * @param player Player to query.
     * @returns True unless the player turned voice chat off.
     */
    isPlayerVoiceEnabled(player: Entity): boolean;

    /**
     * Checks whether a player is speaking right now. Tracks speech rather than the push-to-talk key: silence is dropped before it reaches the server, and the state clears shortly after the last frame. The same signal raises the playerVoiceStart and playerVoiceStop events.
     * @param player Player to query.
     * @returns True while the player's voice is reaching the server.
     */
    isPlayerTalking(player: Entity): boolean;
  };

  /**
   * Base handle for a live replicated network entity.
   */
  class Entity {
    /**
     * Creates a script wrapper for an existing entity with this ID; it does not spawn an entity.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Immutable network entity identifier.
     */
    readonly id: number;

    /**
     * Current virtual-world identifier.
     */
    readonly virtualWorld: number;

    /**
     * Authoritative world-space position; assignment forces replicated state.
     */
    position: Vector3;

    /**
     * Authoritative rotation; reads return a quaternion and assignments accept a quaternion or Euler angles in degrees.
     */
    rotation: Quaternion | Vector3;

    /**
     * Arbitrary key/value state carried by this entity. The server writes it and every client that can see the entity receives it; see StateBag.
     */
    readonly state: StateBag;

    /**
     * Formats this entity handle for logging and debugging.
     * @returns Text containing the network entity ID.
     */
    toString(): string;

    /**
     * Moves this entity into another virtual world.
     * @param world Virtual-world identifier used to partition replication and visibility.
     */
    setVirtualWorld(world: number): void;

    /**
     * Restricts replication of this entity to one owning connection while preserving normal range and visibility checks.
     * @param player Player-owned entity whose connection should exclusively receive this entity, or null to clear the restriction.
     */
    setVisibleTo(player: Entity | null): void;
  }

  /**
   * Arbitrary key/value state attached to one replicated entity, reached as `entity.state`. Keys set on the server replicate to every client that can currently see the entity.
   */
  class StateBag {
    /**
     * Reads one key from this entity's state.
     * @param key Key to read.
     * @returns The stored value, or undefined when the key is not set.
     */
    get(key: string): any;

    /**
     * Checks whether this entity's state holds a key.
     * @param key Key to test.
     * @returns True when the key is set.
     */
    has(key: string): boolean;

    /**
     * Lists the keys this entity's state holds, sorted.
     * @returns Every key currently set, in ascending order.
     */
    keys(): string[];

    /**
     * Copies this entity's whole state into a plain object.
     * @returns Every key and value currently set. The copy does not track later changes.
     */
    toObject(): Record<string, any>;

    /**
     * Watches this entity's state. The filter is applied before the handler runs, so a listener watching one key of one entity is not woken by unrelated changes. The subscription is dropped when the registering resource stops.
     * @param key Only report this key, or null to report every key of this entity.
     * @param handler Called as (key: string, value: any, previous: any) with the changed key, its new value and the value before it. `value` is undefined when the key was removed and `previous` is undefined when it held nothing.
     * @returns A zero-argument function that cancels this subscription.
     */
    onChange(key: string | null, handler: Function): Function;

    /**
     * Writes one key of this entity's state and replicates it to whoever the scope names. Throws when a limit is reached.
     * @param key Key to write; at most 64 UTF-8 bytes.
     * @param value Value to store. Booleans, numbers and strings travel as themselves; anything else is serialized as JSON. At most 4096 bytes.
     * @param options Who the key reaches: every client that can see the entity (the default), only its owning client, or nobody -- server-side storage that never goes on the wire.
     * @returns True when the value changed, false when it was already stored.
     */
    set(key: string, value: any, options?: { scope?: 'broadcast' | 'owner' | 'server' }): boolean;

    /**
     * Removes one key from this entity's state, telling every client that held it.
     * @param key Key to drop.
     * @returns True when the key was set and has been removed.
     */
    remove(key: string): boolean;
  }

  /**
   * Framework-owned base handle for a connected player, extended by each game or mod.
   */
  class BasePlayer {
    /**
     * Creates a wrapper for an existing connected player with this ID; it does not connect or spawn a player.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Authenticated Steam identifier, or an empty string when unavailable.
     */
    readonly steamId: string;

    /**
     * Authenticated Discord identifier, or an empty string when unavailable.
     */
    readonly discordId: string;

    /**
     * Framework hardware identifier, or an empty string when unavailable.
     */
    readonly hardwareId: string;

    /**
     * Current round-trip latency in milliseconds, or -1 when unavailable.
     */
    readonly ping: number;

    /**
     * Current remote network address, or an empty string when unavailable.
     */
    readonly ip: string;

    /**
     * Formats this player handle for logging and debugging.
     * @returns Text containing the player's network entity ID.
     */
    toString(): string;

    /**
     * Checks whether this player's nametag is shown to other players.
     * @returns True unless the nametag was hidden; false also when this game has no nametags.
     */
    isNametagVisible(): boolean;

    /**
     * Checks whether the health bar under this player's nametag is shown.
     * @returns True unless the health bar was hidden; false also when this game has no nametags.
     */
    isNametagHealthVisible(): boolean;

    /**
     * Reads this player's nametag text override.
     * @returns The override, or an empty string when the player's own name is drawn.
     */
    getNametagText(): string;

    /**
     * Reads this player's nametag color.
     * @returns Packed 0xAARRGGBB color; opaque white when untinted.
     */
    getNametagColor(): number;

    /**
     * Disconnects this player from the server.
     * @param reason Optional reason shown to the disconnected player; omitting it uses the generic kicked reason.
     */
    kick(reason?: string): void;

    /**
     * Emits a named script event to this player's client connection.
     * @param eventName Client event name.
     * @param payloadJson Optional JSON payload forwarded verbatim to the owning client.
     */
    emit(eventName: string, payloadJson?: string): void;

    /**
     * Returns this player's current remote network address.
     * @returns Address string, or an empty string when the player or peer is unavailable.
     */
    getIP(): string;

    /**
     * Shows or hides the name over this player's head for every other player. The health bar has its own switch, and each player can still hide all nametags locally.
     * @param visible True to show this player's nametag to everyone, false to hide it.
     */
    setNametagVisible(visible: boolean): void;

    /**
     * Shows or hides the health bar under this player's nametag, leaving the name itself alone.
     * @param visible True to show the health bar under this player's name, false to hide it.
     */
    setNametagHealthVisible(visible: boolean): void;

    /**
     * Overrides the text drawn on this player's nametag.
     * @param text Text to show instead of the player's name; empty or omitted restores the name.
     */
    setNametagText(text?: string): void;

    /**
     * Tints the text on this player's nametag.
     * @param color Packed 0xAARRGGBB color.
     */
    setNametagColor(color: number): void;
  }

  interface BasePlayer extends Entity {}

  /**
   * Asynchronous resource event bus. Native event names and callback payloads are inferred from {@link EventMap}.
   * Script-defined event names remain supported by the string overloads.
   */
  interface EventBus {
    on<K extends EventName>(eventName: K, handler: (...args: EventMap[K]) => unknown | Promise<unknown>): () => void;
    on(eventName: string, handler: (...args: unknown[]) => unknown | Promise<unknown>): () => void;
    once<K extends EventName>(eventName: K, handler: (...args: EventMap[K]) => unknown | Promise<unknown>): void;
    once(eventName: string, handler: (...args: unknown[]) => unknown | Promise<unknown>): void;
    off<K extends EventName>(eventName: K, handler: (...args: EventMap[K]) => unknown | Promise<unknown>): void;
    off(eventName: string, handler: (...args: unknown[]) => unknown | Promise<unknown>): void;
    emit(eventName: string, ...args: unknown[]): Promise<void>;
    emitTo(resourceName: string, eventName: string, ...args: unknown[]): Promise<void>;
    onLocal(eventName: string, handler: (...args: unknown[]) => unknown | Promise<unknown>): void;
    emitLocal(eventName: string, ...args: unknown[]): Promise<void>;
    onClient(eventName: string, handler: (...args: unknown[]) => unknown | Promise<unknown>): () => void;
    onceClient(eventName: string, handler: (...args: unknown[]) => unknown | Promise<unknown>): void;
    offClient(eventName: string, handler: (...args: unknown[]) => unknown | Promise<unknown>): void;
    listenerCount(eventName: string): number;
  }

  namespace Core {
    /** Framework event bus for native and resource-defined events. */
    const Events: EventBus;
  }
}

export {};
