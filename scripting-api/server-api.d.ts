// Mafia1Online server globals for Node.js resources. Load this file separately
// from client-api.d.ts: several globals have different server and client APIs.
/// <reference types="@mafiahub/types" />

type ServerVector3 = Core.Vector3;
type ServerPosition = ServerVector3 | { x: number; y: number; z: number };
type ServerQuaternion = Core.Quaternion;

/** Framework math constructors are installed as globals at runtime. */
declare const Vector2: typeof Core.Vector2;
declare const Vector3: typeof Core.Vector3;
declare const Vector4: typeof Core.Vector4;
declare const Quaternion: typeof Core.Quaternion;
declare const Color: typeof Core.Color;

/** Server-side proximity voice controls. */
declare const Voice: {
    setRange(range: number): void;
    getRange(): number;
    setPlayerRange(player: Entity, range: number): void;
    getPlayerRange(player: Entity): number;
    setPlayerMuted(player: Entity, muted: boolean): void;
    isPlayerMuted(player: Entity): boolean;
    setPlayerDeaf(player: Entity, deaf: boolean): void;
    isPlayerDeaf(player: Entity): boolean;
    setLocalMute(listener: Entity, target: Entity, muted: boolean): void;
    isLocallyMuted(listener: Entity, target: Entity): boolean;
    isPlayerVoiceEnabled(player: Entity): boolean;
    isPlayerTalking(player: Entity): boolean;
};

declare const Chat: {
    sendToAll(text: string, options?: { author?: string; color?: number | Core.Color }): void;
    sendToPlayer(player: Entity, text: string, options?: { author?: string; color?: number | Core.Color }): void;
};

declare const ExecutionEnvironment: {
    readonly isClient: boolean;
    readonly isServer: boolean;
};

/** Per-entity state replicated to clients that can see the entity. */
interface StateBag {
    get(key: string): any;
    has(key: string): boolean;
    keys(): string[];
    toObject(): Record<string, any>;
    onChange(key: string | null, handler: (key: string, value: any, previous: any) => unknown): () => void;
    set(key: string, value: any, options?: { scope?: "broadcast" | "owner" | "server" }): boolean;
    remove(key: string): boolean;
}

/** A live replicated entity handle. Constructing it never creates an entity. */
declare class Entity {
    constructor(id: number);
    readonly id: number;
    readonly virtualWorld: number;
    position: ServerVector3;
    rotation: ServerQuaternion | ServerVector3;
    readonly state: StateBag;
    setVirtualWorld(world: number): void;
    setVisibleTo(player: Entity | null): void;
    toString(): string;
}

interface WeaponInfo {
    weaponId: number;
    name: string;
    kind: "melee" | "firearm" | "throwable";
    magazine: number;
}

interface WeaponSlot extends WeaponInfo {
    loaded: number;
    reserve: number;
}

interface Inventory {
    selected: number;
    weapons: WeaponSlot[];
}

interface CombatState {
    revision: number;
    health: number;
    alive: boolean;
    spawned: boolean;
    missionGeneration: number;
    spawnGeneration: number;
    inventoryMask: number;
    selectedWeapon: number;
    deathAnimation: number;
    aiming: boolean;
    crouching: boolean;
    aimDirection: { x: number; y: number; z: number };
    /** Camera-facing target relative to the player's world position, updated even when unarmed or unaimed. */
    poseTargetOffset: { x: number; y: number; z: number };
    ammo: { loaded: number; reserve: number }[];
}

/** A connected player. Life, health, inventory, and seats are server owned. */
declare class Player extends Entity {
    constructor(id: number);
    readonly nickname: string;
    /** Selected stock human .i3d model, retained across spawns. */
    readonly model: string;
    readonly health: number;
    /** Server-owned Free Ride balance, 0 to 1,000,000,000. Persists across lives and map changes. */
    readonly money: number;
    readonly alive: boolean;
    readonly spawned: boolean;
    readonly missionGeneration: number;
    readonly spawnGeneration: number;
    readonly position: ServerVector3;
    readonly rotation: ServerQuaternion;
    readonly ping: number;
    readonly ip: string;
    readonly steamId: string;
    readonly discordId: string;
    readonly hardwareId: string;
    kick(reason?: string): void;
    /** Send a client event. The payload must contain valid JSON text; use JSON.stringify for an object. */
    emit(eventName: string, payloadJson?: string): void;
    getIP(): string;
    setNickname(nickname: string): string | null;
    /** Changes this player's model on every client, including during a life. */
    setModel(model: string): boolean;
    setNametag(options: { visible?: boolean; showHealth?: boolean; color?: number; text?: string }): boolean;
    setNametagVisible(visible: boolean): void;
    setNametagHealthVisible(visible: boolean): void;
    setNametagText(text: string): void;
    setNametagColor(color: number): void;
    isNametagVisible(): boolean;
    isNametagHealthVisible(): boolean;
    getNametagText(): string;
    getNametagColor(): number;
    sendMessage(text: string, color?: number): boolean;
    /** Starts a new life only when every client is mission ready. */
    spawn(position: ServerPosition, heading?: number): boolean;
    spawn(x: number, y: number, z: number, heading?: number): boolean;
    respawn(position: ServerPosition, heading?: number): boolean;
    respawn(x: number, y: number, z: number, heading?: number): boolean;
    despawn(): boolean;
    getSuggestedSpawn(): ServerVector3 | null;
    setHealth(health: number): boolean;
    /** Sets the whole-dollar balance. Throws for non-integers or values outside 0..1,000,000,000. */
    setMoney(amount: number): boolean;
    /** Adds a signed whole-dollar amount; returns false if the balance would exceed its bounds. */
    giveMoney(amount: number): boolean;
    /** Atomically debits an affordable cost. Grant shop goods only when this returns true. */
    trySpendMoney(cost: number): boolean;
    /** Connected target the camera was instructed to follow, or null for local control. */
    getCameraTarget(): Player | null;
    /** Follows a player's pedestrian/car camera; null restores this player's own camera. */
    setCameraTarget(target: Player | null): boolean;
    getVehicle(): Vehicle | null;
    /** Seat 0 is the driver; null means on foot. */
    getSeat(): number | null;
    putInVehicle(vehicle: Vehicle, seat?: number): boolean;
    removeFromVehicle(): boolean;
    giveWeapon(weaponId: number, loaded?: number, reserve?: number, equip?: boolean): boolean;
    /** Adds a signed amount to reserve ammunition. */
    giveAmmo(weaponId: number, amount: number): boolean;
    setWeaponAmmo(weaponId: number, loaded: number, reserve: number): boolean;
    removeWeapon(weaponId: number): boolean;
    /** Drop a held weapon and its ammunition into a synced world pickup. Defaults to the selected weapon. */
    dropWeapon(weaponId?: number): Pickup | null;
    removeAllWeapons(): boolean;
    setCurrentWeapon(weaponId: number): boolean;
    getCurrentWeapon(): number;
    hasWeapon(weaponId: number): boolean;
    getWeapons(): WeaponSlot[];
    getInventory(): Inventory | null;
    /** Replaces all weapons in one revision. Duplicates, >7 items, and an unheld selection fail without changing the inventory. */
    setInventory(weapons: { weaponId: number; loaded?: number; reserve?: number }[], selected?: number): boolean;
    addItem(weaponId: number, loaded?: number, reserve?: number): boolean;
    removeItem(weaponId: number, count?: number): boolean;
    getCombatState(): CombatState | null;
    toString(): string;
}

interface VehicleDamage {
    revision: number;
    engineHealth: number;
    engineDamagePower: number;
    engineDestroyed: boolean;
    gearboxHealth: number;
    bodyDamage: number;
    fuelTankHealth: number;
    burning: boolean;
    lights: { flags: number; damage: number }[];
    zones: { flags: number; crackLevel: number }[];
    wheels: { flags: number; health: number; deformAngle: number }[];
}

/** A server vehicle. The selected client's native simulation reports physics and damage. */
declare class Vehicle extends Entity {
    constructor(id: number);
    readonly model: string;
    readonly position: ServerVector3;
    readonly rotation: ServerQuaternion;
    readonly velocity: ServerVector3;
    readonly angularVelocity: ServerVector3;
    readonly steeringInput: number;
    readonly fuel: number;
    readonly fuelTankCapacity: number;
    readonly engineRotations: number;
    readonly gear: number;
    readonly maximumGear: number;
    readonly lightState: number;
    readonly hornOn: boolean;
    readonly sirenOn: boolean;
    readonly engineOn: boolean;
    readonly radarColor: number;
    /** Replicated model opacity: 0 transparent, 1 opaque. */
    readonly opacity: number;
    /** Increments whenever repair starts a fresh native damage baseline. */
    readonly repairRevision: number;
    readonly radarVisible: boolean;
    readonly speedLimited: boolean;
    readonly powerInput: number;
    readonly brakeInput: number;
    readonly handbrakeInput: number;
    readonly dynamicsValid: boolean;
    readonly health: number;
    readonly damageFlags: number;
    readonly detachedParts: number;
    readonly nativeDamageValid: boolean;
    readonly nativeDamageRevision: number;
    readonly meshRevision: number;
    readonly seatCount: number;
    readonly terminalState: number;
    readonly missionGeneration: number;
    readonly engineRevision: number;
    readonly dynamicsCommandRevision: number;
    readonly terminalSequence: number;
    readonly seatSequence: number;
    destroy(): boolean;
    getController(): Player | null;
    getDriver(): Player | null;
    getOccupant(seat: number): Player | null;
    getOccupants(): (Player | null)[];
    getDoors(): number[];
    /** Returns null before the first native report and while repair awaits a new report. */
    getDamageState(): VehicleDamage | null;
    setTransform(position: ServerPosition, rotation?: number | ServerQuaternion, velocity?: ServerPosition): boolean;
    setTransform(x: number, y: number, z: number, rotation?: number | ServerQuaternion, velocity?: ServerPosition): boolean;
    setEngine(on: boolean): boolean;
    /** After the first native tank report, only this server method may increase fuel. */
    setFuel(fuel: number): boolean;
    setLights(lightState: number): boolean;
    setHorn(on: boolean): boolean;
    setSiren(on: boolean): boolean;
    setRadarMarker(visible: boolean, color?: number): boolean;
    setOpacity(opacity: number): boolean;
    /** Repairs native damage and deformation without resetting position or seats. Loose debris remains. */
    repair(): boolean;
    /** Opaque durable condition snapshot, or empty before full native reports. */
    saveState(): string;
    /** Restore into a fresh, empty spawn of the same model. */
    restoreState(snapshot: string): boolean;
    setSeatCount(count: number): boolean;
    /** Script metadata only; use setMechanicalDamage for native damage. */
    setDamage(health: number, damageFlags: number, detachedParts: number): boolean;
    setMechanicalDamage(engineHealth: number, gearboxHealth: number, bodyDamage: number, fuelTankHealth: number): boolean;
    setTerminalState(state: 1 | 2 | 3): boolean;
    explode(): boolean;
    recordSeatOutcome(seat: number, player: Player, result: 1 | 2 | 3 | 4): boolean;
    toString(): string;
    static spawn(model: string, position: ServerPosition, rotation?: number | ServerQuaternion, controller?: Player): Vehicle | null;
    static spawn(model: string, x: number, y: number, z: number, rotation?: number | ServerQuaternion, controller?: Player): Vehicle | null;
}

declare class Pickup extends Entity {
    constructor(id: number);
    readonly weaponId: number;
    readonly heading: number;
    readonly loaded: number;
    readonly reserve: number;
    destroy(): boolean;
    toString(): string;
    static create(weaponId: number, position: ServerPosition, heading?: number, loaded?: number, reserve?: number, lifetimeMs?: number): Pickup | null;
    static create(weaponId: number, x: number, y: number, z: number, heading?: number, loaded?: number, reserve?: number, lifetimeMs?: number): Pickup | null;
    static getAll(): Pickup[];
}

/** A stock mission C_door shared across clients. A mission change removes its server handle. */
declare class Door extends Entity {
    constructor(id: number);
    readonly frameName: string;
    readonly open: boolean;
    locked: boolean;
    enabled: boolean;
    readonly reverse: boolean;
    readonly openFraction: number;
    /** Animates both leaves open. Supply each leaf's swing side independently. */
    openDoor(reverse: boolean, pairedReverse: boolean): boolean;
    closeDoor(): boolean;
    /** Snaps both leaves to a fraction from 0 to 1. */
    setOpenFraction(fraction: number, reverse: boolean, pairedReverse: boolean): boolean;
    destroy(): boolean;
    toString(): string;
    /** Registers a named native mission door for reach-checked server control. Native use also registers nearby doors automatically. */
    static create(frameName: string, position: ServerPosition): Door | null;
    static get(frameName: string): Door | null;
    static getAll(): Door[];
}

declare const Weapon: {
    list(): WeaponInfo[];
    get(weaponId: number): WeaponInfo | null;
};

interface WeatherInfo {
    weather: "default" | "clear" | "rain" | "snow";
    intensity: number | null;
}

declare const World: {
    getMission(): string;
    getMissionGeneration(): number;
    changeMission(mission: string): boolean;
    isReady(): boolean;
    getPlayers(): Player[];
    getVehicles(): Vehicle[];
    getPickups(): Pickup[];
    getDoors(): Door[];
    setWeather(weather: WeatherInfo["weather"], intensity?: number): void;
    getWeather(): WeatherInfo;
    setCityMusic(enabled: boolean): void;
    isCityMusicEnabled(): boolean;
    /** Overrides the native GAME_NIGHTMISSION flag for all clients; null restores each mission's own flag. */
    setNightMode(enabled: boolean | null): void;
    /** Returns the server override, or null while mission scripts own the flag. */
    getNightModeOverride(): boolean | null;
    setFrameVisible(name: string, visible: boolean): boolean;
    resetFrames(): void;
    /** Sets 0–1 opacity on a named static visual in this mission; returns false at the 128-frame limit. */
    setFrameOpacity(name: string, opacity: number): boolean;
    /** Restores one frame's original opacity, or all overridden frames when omitted. */
    resetFrameOpacity(name?: string): void;
    createExplosion(position: ServerPosition, options?: { radius?: number; damage?: number }): void;
    createExplosion(x: number, y: number, z: number, options?: { radius?: number; damage?: number }): void;
    createFire(position: ServerPosition, options?: { duration?: number; radius?: number; damage?: number }): void;
    createFire(x: number, y: number, z: number, options?: { duration?: number; radius?: number; damage?: number }): void;
};

declare class Sound extends Entity {
    constructor(id: number);
    readonly wave: string;
    volume: number;
    radius: number;
    enabled: boolean;
    attachTo(entity: Player | Vehicle | null): boolean;
    getAttached(): Player | Vehicle | null;
    destroy(): boolean;
    toString(): string;
    static create(wave: string, position: ServerPosition, options?: { radius?: number; volume?: number; attachTo?: Player | Vehicle }): Sound | null;
    static create(wave: string, x: number, y: number, z: number, options?: { radius?: number; volume?: number; attachTo?: Player | Vehicle }): Sound | null;
    static play(wave: string, position: ServerPosition, options?: { radius?: number; volume?: number }): void;
    static play(wave: string, x: number, y: number, z: number, options?: { radius?: number; volume?: number }): void;
}

interface MissionInfo { mission: string; missionGeneration: number; }
interface MissionLoadInfo { missionGeneration: number; state: number; }
interface PlayerHealthInfo {
    attacker: Player | null;
    weaponId: number | null;
    cause: "script" | "firearm" | "melee" | "explosion" | "fire" | "fall" | "drowning" | "vehicle";
    oldHealth: number;
    health: number;
    amount: number;
    deathAnimation: number;
    missionGeneration: number;
    spawnGeneration: number;
}
interface WeaponActionInfo {
    weaponId: number;
    selectedWeapon: number;
    inventoryMask: number;
    loaded: number;
    reserve: number;
    aiming: boolean;
    direction: { x: number; y: number; z: number };
    target: Player | null;
    revision: number;
    meleeIndex: number;
    meleeHoldMs: number;
    throwHoldMs: number;
}
interface PickupEventInfo { pickupId: number; weaponId: number; position: { x: number; y: number; z: number } | null; }
interface VehicleHitInfo { shooter: Player | null; damage: number; position: { x: number; y: number; z: number }; shotSequence: number; pelletIndex: number; }
interface VehiclePartInfo { debrisId: number; type: number; partIndex: number; position: { x: number; y: number; z: number }; }
interface VehicleSeatInfo { seat: number; fromSeat: number | null; stolen: boolean; serverSequence: number; }

/** Native server events. Client events use the separate Events.onClient table. */
interface ServerEventMap {
    resourceStart: [resourceName: string];
    resourceStop: [resourceName: string];
    consoleCommand: [command: string, args: string[]];
    entityStateChange: [entity: Entity, key: string, value: any, previous: any];
    missionChange: [mission: MissionInfo];
    missionReady: [mission: MissionInfo];
    playerMissionReady: [player: Player, info: MissionLoadInfo];
    playerMissionUnloaded: [player: Player, info: MissionLoadInfo];
    playerMissionLoadFailed: [player: Player, info: MissionLoadInfo];
    playerConnect: [player: Player];
    playerDisconnect: [player: Player];
    playerChat: [player: Player, text: string];
    playerCommand: [player: Player, command: string, args: string[]];
    playerNicknameChange: [player: Player, oldNickname: string, nickname: string];
    playerModelChange: [player: Player, oldModel: string, model: string];
    playerMoneyChange: [player: Player, oldMoney: number, money: number];
    doorStateChange: [door: Door, player: Player | null];
    doorLockChange: [door: Door, player: null];
    doorInteractionChange: [door: Door, player: null];
    playerSpawn: [player: Player];
    playerRespawn: [player: Player];
    playerDespawn: [player: Player];
    playerDamage: [victim: Player, info: PlayerHealthInfo];
    playerDeath: [victim: Player, killer: Player | null, info: PlayerHealthInfo];
    playerHealthChange: [player: Player, info: PlayerHealthInfo];
    playerVoiceStart: [player: Player];
    playerVoiceStop: [player: Player];
    playerWeaponEquip: [player: Player, info: WeaponActionInfo];
    playerAimChange: [player: Player, info: WeaponActionInfo];
    playerWeaponDrop: [player: Player, info: WeaponActionInfo];
    playerWeaponReload: [player: Player, info: WeaponActionInfo];
    playerWeaponFire: [player: Player, info: WeaponActionInfo];
    playerWeaponThrow: [player: Player, info: WeaponActionInfo];
    playerWeaponThrowStart: [player: Player, info: WeaponActionInfo];
    playerWeaponThrowRelease: [player: Player, info: WeaponActionInfo];
    playerWeaponThrowCancel: [player: Player, info: WeaponActionInfo];
    playerWeaponMeleeStart: [player: Player, info: WeaponActionInfo];
    playerWeaponMelee: [player: Player, info: WeaponActionInfo];
    playerWeaponMeleeCancel: [player: Player, info: WeaponActionInfo];
    weaponDropped: [pickup: Pickup | null, player: Player | null, info: PickupEventInfo];
    pickupTaken: [pickup: Pickup | null, player: Player | null, info: PickupEventInfo];
    vehicleSpawn: [vehicle: Vehicle];
    vehicleDestroy: [vehicle: Vehicle];
    vehicleDamage: [vehicle: Vehicle, damage: VehicleDamage | null];
    vehicleOpacityChange: [vehicle: Vehicle];
    vehicleRepair: [vehicle: Vehicle];
    vehicleTerminal: [vehicle: Vehicle, state: number];
    vehicleHit: [vehicle: Vehicle, info: VehicleHitInfo];
    vehiclePartDetached: [vehicle: Vehicle, info: VehiclePartInfo];
    vehiclePlayerEntering: [vehicle: Vehicle, player: Player, info: VehicleSeatInfo];
    vehiclePlayerEntered: [vehicle: Vehicle, player: Player, info: VehicleSeatInfo];
    vehiclePlayerExited: [vehicle: Vehicle, player: Player, info: VehicleSeatInfo];
    vehiclePlayerExitBlocked: [vehicle: Vehicle, player: Player, info: VehicleSeatInfo];
    vehiclePlayerSeatChanged: [vehicle: Vehicle, player: Player, info: VehicleSeatInfo];
    vehicleEngineChange: [vehicle: Vehicle];
    vehicleFuelChange: [vehicle: Vehicle];
    vehicleLightsChange: [vehicle: Vehicle];
    vehicleHornChange: [vehicle: Vehicle];
    vehicleSirenChange: [vehicle: Vehicle];
    vehicleGearChange: [vehicle: Vehicle];
    vehicleDamageState: [vehicle: Vehicle];
}

declare const Events: {
    on<K extends keyof ServerEventMap>(eventName: K, handler: (...args: ServerEventMap[K]) => unknown): () => void;
    on(eventName: string, handler: (...args: any[]) => unknown): () => void;
    once(eventName: string, handler: (...args: any[]) => unknown): void;
    off(eventName: string, handler: (...args: any[]) => unknown): void;
    emit(eventName: string, ...args: unknown[]): Promise<void>;
    emitTo(resourceName: string, eventName: string, ...args: unknown[]): Promise<void>;
    onLocal(eventName: string, handler: (...args: any[]) => unknown): void;
    emitLocal(eventName: string, ...args: unknown[]): Promise<void>;
    listenerCount(eventName: string): number;
    /** Isolated receiver for Events.emitServer calls from clients. */
    onClient(eventName: string, handler: (player: Player, data: any) => unknown): () => void;
    onceClient(eventName: string, handler: (player: Player, data: any) => unknown): void;
    offClient(eventName: string, handler: (player: Player, data: any) => unknown): void;
    /** Broadcast to clients. Strings are sent verbatim and must contain valid JSON; other values are serialized. */
    emitAllClients(eventName: string, payload?: unknown): void;
};
