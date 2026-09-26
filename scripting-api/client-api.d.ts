// Mafia1Online client scripting API: the globals a resource's clientScripts
// see in the client's sandboxed V8. Maintained alongside the binding metadata
// registered into the "mafia1online-client" catalog. Server scripts use a different API with the
// same global names (Player, Vehicle, Pickup, World, Sound), so never load both
// declaration files into one project.

/// <reference types="@mafiahub/types" />

type Vector3 = Core.Vector3;
type PositionLike = Core.Vector3 | { x: number; y: number; z: number };
type RotationLike = { w: number; x: number; y: number; z: number };

/** Framework value types are constructors on the global object. */
declare const Vector2: typeof Core.Vector2;
declare const Vector3: typeof Core.Vector3;
declare const Vector4: typeof Core.Vector4;
declare const Quaternion: typeof Core.Quaternion;
declare const Color: typeof Core.Color;

/** A live replicated entity. The server remains authoritative over transforms. */
declare class Entity {
    /** Creates a handle for an existing entity; throws when the ID does not resolve. */
    constructor(id: number);
    /** Immutable network entity identifier. */
    readonly id: number;
    /** Current virtual-world identifier. */
    readonly virtualWorld: number;
    /** Last replicated position. A client assignment changes its local replica temporarily. */
    position: Vector3;
    /** Last replicated rotation. */
    get rotation(): Core.Quaternion;
    /** Accepts a quaternion or Euler angles in degrees. Client assignments are temporary. */
    set rotation(value: Core.Quaternion | Vector3);
    /** Read-only view of replicated key/value state. */
    readonly state: ClientStateBag;
    toString(): string;
}

/**
 * A streamed Mafia 1 player, local or remote. Every value is the server state
 * this client last received; a handle stops resolving once the player leaves.
 */
declare class Player extends Entity {
    /** Creates a handle for a streamed player; throws when the ID does not resolve to a player. */
    constructor(id: number);
    /** Server nickname. */
    readonly nickname: string;
    /** Server-selected native human model file, such as "Tommy.i3d"; empty after stream out. */
    readonly model: string;
    /** Server health from 0 to 100. */
    readonly health: number;
    /** Last replicated server-owned Free Ride balance; read only. */
    readonly money: number;
    /** Whether the player is spawned and alive. */
    readonly alive: boolean;
    /** Whether the player has a life in the current mission. */
    readonly spawned: boolean;
    /** Whether this is the player this client controls. */
    readonly isLocal: boolean;
    /** Mission generation of the current life. */
    readonly missionGeneration: number;
    /** Generation of the current life; it increases on every spawn. */
    readonly spawnGeneration: number;
    /** Returns the streamed vehicle the player sits in, or null on foot. */
    getVehicle(): Vehicle | null;
    /** Returns the seat index (0 is the driver), or -1 on foot. */
    getSeat(): number;
    /** Formats this player for logging: the player ID and nickname. */
    toString(): string;
    isNametagVisible(): boolean;
    isNametagHealthVisible(): boolean;
    getNametagText(): string;
    /** Packed 0xAARRGGBB. */
    getNametagColor(): number;
}

/** State keys written on the server and replicated to this client. */
interface ClientStateBag {
    get(key: string): any;
    has(key: string): boolean;
    keys(): string[];
    toObject(): Record<string, any>;
    /** Pass null to watch all keys. Returns an unsubscribe callback. */
    onChange(key: string | null, handler: (key: string, value: any, previous: any) => void): () => void;
}

/** Pickup values this client last received before its handle stopped resolving. */
interface ClientPickupSnapshot {
    readonly id: number;
    readonly missionGeneration: number;
    readonly weaponId: number;
    readonly position: { x: number; y: number; z: number };
    /** Radians. */
    readonly heading: number;
    readonly loaded: number;
    readonly reserve: number;
}

/** A streamed server vehicle. Every value is the server state this client last received. */
declare class Vehicle extends Entity {
    /** Creates a handle for a streamed vehicle; throws when the ID does not resolve to a vehicle. */
    constructor(id: number);
    /** Native model file, such as "fordtl00.i3d". */
    readonly model: string;
    /** Server health from 0 to 100. */
    readonly health: number;
    readonly engineOn: boolean;
    readonly sirenOn: boolean;
    readonly hornOn: boolean;
    /** Fuel in the tank. */
    readonly fuel: number;
    /** Replicated model opacity: 0 transparent, 1 opaque. */
    readonly opacity: number;
    /** Current gear; 0 is neutral. */
    readonly gear: number;
    readonly seatCount: number;
    /** Returns the player in a seat (0 is the driver), or null for an empty seat. */
    getOccupant(seat: number): Player | null;
    /** Lists the streamed occupants in seat order. */
    getOccupants(): Player[];
    toString(): string;
}

/**
 * A replicated weapon pickup in the loaded mission, created by a server
 * script or dropped by a player. Its fields are read-only client views.
 */
declare class Pickup extends Entity {
    /** Creates a handle for a streamed weapon pickup; throws if the ID is not a pickup. */
    constructor(id: number);
    /** Replicated world position; read only for pickups. */
    readonly position: Vector3;
    /** Facing derived from heading; read only for pickups. */
    get rotation(): Core.Quaternion;
    /** Stock weapon ID lying here. */
    readonly weaponId: number;
    /** Facing in radians. */
    readonly heading: number;
    /** Loaded rounds or throwable count. */
    readonly loaded: number;
    /** Reserve rounds. */
    readonly reserve: number;
    /** Server generation of the mission containing this pickup. */
    readonly missionGeneration: number;
    toString(): string;
}

/** Read-only replicated state of a managed stock mission door. */
declare class Door extends Entity {
    constructor(id: number);
    readonly position: Vector3;
    readonly frameName: string;
    readonly open: boolean;
    readonly locked: boolean;
    readonly enabled: boolean;
    readonly reverse: boolean;
    readonly openFraction: number;
    readonly missionGeneration: number;
    toString(): string;
}

interface ClientDoorSnapshot {
    id: number;
    missionGeneration: number;
    frameName: string;
    position: { x: number; y: number; z: number };
    open: boolean;
    locked: boolean;
    enabled: boolean;
    reverse: boolean;
    openFraction: number;
}

/** The player this client controls, or null before the server created it. */
declare const LocalPlayer: Player | null;

interface ClientWeatherInfo {
    /** Weather preset; default keeps the mission's own weather. */
    weather: "default" | "clear" | "rain" | "snow";
    /** Rain or snow intensity from 0 to 100, or null for the preset's own. */
    intensity: number | null;
    /** Whether the city's ambient music plays. */
    cityMusic: boolean;
}

/** Snapshot of a named frame in the loaded native mission scene. */
interface ClientMissionFrame {
    readonly position: Vector3;
    readonly direction: Vector3;
}

/**
 * Read-only view of the mission and the entities streamed to this client.
 * The server owns the world; change it from a server script.
 */
declare const World: {
    /** Lists the players streamed to this client, including the local one. */
    getPlayers(): Player[];
    /** Lists the vehicles streamed to this client. */
    getVehicles(): Vehicle[];
    /** Lists script-created and dropped weapon pickups in the loaded mission. */
    getPickups(): Pickup[];
    /** Lists server-managed mission doors for the loaded mission. */
    getDoors(): Door[];
    /** Returns the loaded stock mission, or null while none is loaded. */
    getMission(): string | null;
    /** Returns the server generation of the loaded mission, or 0 while none is loaded. */
    getMissionGeneration(): number;
    /** Whether the native mission is loaded. */
    isReady(): boolean;
    /** Returns the replicated weather and city music state. */
    getWeather(): ClientWeatherInfo;
    /** Reads the effective GAME_NIGHTMISSION flag, or null before a mission is ready. */
    getNightMode(): boolean | null;
    /** Reads a named mission frame, such as the Free Ride spawn anchor emeth_1. */
    getMissionFrame(name: string): ClientMissionFrame | null;
};

/**
 * The retail Mafia 1 HUD for this client. Every call returns false (or null)
 * while no mission is loaded, and the game clears the watch, score and
 * compass on every mission change.
 */
declare const Hud: {
    /**
     * Adds a line to the native HUD console, which keeps the last five lines for five seconds each.
     * @param text At most 127 characters are shown.
     * @param color 0xRRGGBB; defaults to white.
     */
    showMessage(text: string, color?: number): boolean;
    /**
     * Shows large white centred text, the race flash text, fading out over its last second.
     * @param durationSeconds Defaults to 3.
     */
    announce(text: string, durationSeconds?: number): boolean;
    /** Shows the mission watch running in real time from this clock time. */
    showWatch(hours: number, minutes: number, seconds?: number): boolean;
    /**
     * Shows the watch with its countdown wedge; `countdownEnd` fires when it runs out.
     * @param seconds Whole seconds; 0 removes the countdown.
     */
    startCountdown(seconds: number): boolean;
    /** Returns the seconds left, or null when no countdown runs. */
    getCountdown(): number | null;
    /** Hides the watch and stops its countdown without firing countdownEnd. */
    hideWatch(): void;
    /** Shows the freeride score counter with this value. */
    setScore(score: number): boolean;
    /** Adds to the score counter and shows it; returns the new score, or null while no mission is loaded. */
    addScore(points: number): number | null;
    /** Reads the native Free Ride score value, including while the counter is hidden; null before a mission loads. */
    getScore(): number | null;
    /** Returns whether the native Free Ride score counter is visible; null before a mission loads. */
    isScoreVisible(): boolean | null;
    /** Shows or hides the native Free Ride score counter without changing its value. */
    setScoreVisible(visible: boolean): boolean;
    /** Hides the counter without changing its value. */
    hideScore(): void;
    /** Points the native compass arrow at a fixed point, or at a streamed player or vehicle it follows. */
    setCompassTarget(target: PositionLike | Player | Vehicle): boolean;
    clearCompassTarget(): void;
};

/** The retail full-screen fade, drawn over the scene and the HUD. */
declare const Fade: {
    /**
     * Fades the screen to a color and holds it until Fade.in.
     * @param durationSeconds 0 is instant.
     * @param color 0xRRGGBB; defaults to black.
     */
    out(durationSeconds: number, color?: number): boolean;
    /** Fades from a color back to the scene. */
    in(durationSeconds: number, color?: number): boolean;
};

/** The retail game camera. */
declare const Camera: {
    /** Reads the active camera's effective field of view in degrees, or null before it exists. */
    getFov(): number | null;
    /** Sets the active camera's field of view in degrees (1–179). */
    setFov(degrees: number): boolean;
    /** Sets near and far clipping in world units (near 0.01–10; far above near and at most 5000). */
    setRange(nearClip: number, farClip: number): boolean;
    /** Freezes the camera at a world position looking along a normalized direction. A respawn or mission close restores the player camera. */
    lock(position: PositionLike, direction: PositionLike): boolean;
    /** Returns control to the player camera and refreshes the light cache. */
    unlock(): boolean;
    /**
     * Rolls the camera and the sky slowly from side to side, the retail boat swing. Mafia 1 has no camera shake.
     * @param intensity 0 to 100, as CAMERA_SETSWING takes it; 0 stops the swing.
     */
    setSwing(intensity: number): boolean;
};

/** Pixel projection of a world point. `null` means no loaded mission or camera. */
interface WorldProjection {
    /** Null when off screen or behind the camera. */
    x: number | null;
    /** Null when off screen or behind the camera. */
    y: number | null;
    onScreen: boolean;
    /** Static world geometry blocks the point when the check was requested. */
    occluded: boolean;
    visible: boolean;
}

/** Live local or borrowed world frame; getters return null after expiry. */
interface SceneFrame {
    readonly name: string;
    readonly id: number | null;
    readonly readOnly: boolean;
    isValid(): boolean;
    getWorldPosition(): { x: number; y: number; z: number } | null;
    getWorldDirection(): { x: number; y: number; z: number } | null;
    getRotation(): RotationLike | null;
    getScale(): { x: number; y: number; z: number } | null;
    setWorldPosition(position: PositionLike): boolean;
    setRotation(rotation: RotationLike): boolean;
    setDirection(direction: PositionLike): boolean;
    setScale(scale: number | PositionLike): boolean;
    setVisible(visible: boolean): boolean;
    destroy(): boolean;
}

interface CarCatalogEntry {
    /** Native base ID used by the stock car database and validated by the server. */
    id: number;
    /** Retail display name. */
    name: string;
    /** Stock color-zero .i3d filename. */
    model: string;
}

interface PreviewFrame {
    name: string;
    worldPosition: Vector3;
    rotation: RotationLike;
    scale: Vector3;
    visible: boolean;
}

/** Client-only scene frames; handles expire on mission unload or disconnect. */
declare const Scene: {
    /** Returns valid stock car IDs, display names and default-color model filenames from the loaded game. */
    getCarCatalog(): CarCatalogEntry[];
    /** Loads a stock .i3d into a private, client-only showroom scene. Up to eight previews may exist. */
    createPreview(model: string): number | null;
    /** Replaces a preview model; its existing model stays if loading fails. */
    setPreviewModel(handle: number, model: string): boolean;
    /** Sets pitch and roll in radians; Draw.preview supplies yaw per frame. */
    setPreviewTilt(handle: number, pitch: number, roll: number): boolean;
    /** Reads a named child frame; null if no frame exists or the preview expired. */
    getPreviewFrame(handle: number, name: string): PreviewFrame | null;
    /** Changes a named child frame only inside this preview. */
    setPreviewFrame(handle: number, name: string, changes: {
        worldPosition?: PositionLike;
        rotation?: RotationLike;
        scale?: number | PositionLike;
        visible?: boolean;
    }): boolean;
    /** Releases the private showroom scene and its native frames. */
    destroyPreview(handle: number): boolean;
    /** Loads a stock .i3d visual model. Returns null if missing or at the 128-frame cap. */
    createModel(filename: string): number | null;
    /** Checks that a stock model can safely be used as a native human. */
    canUseHumanModel(filename: string): boolean;
    /** Creates a non-rendering anchor frame. */
    createDummy(): number | null;
    /** Creates a local stationary C_entity. `solid` enables native collision on this client; the default is visual-only. */
    createHuman(model: string, position: PositionLike, direction?: PositionLike, solid?: boolean): number | null;
    /** Creates a local stationary C_entity and returns its live Frame handle. */
    createHumanFrame(model: string, position: PositionLike, direction?: PositionLike, solid?: boolean): SceneFrame | null;
    /** Plays a stock .i3d clip on a local human. Set loop for an ambient repeat. */
    playHumanAnimation(handle: number, filename: string, loop?: boolean): boolean;
    /** Restores a local human's stock breathing/idle animation. */
    setHumanIdle(handle: number): boolean;
    destroy(handle: number): boolean;
    setPosition(handle: number, position: PositionLike): boolean;
    /** Full quaternion rotation, in `{w,x,y,z}` order. */
    setRotation(handle: number, rotation: RotationLike): boolean;
    /** Points a frame forward; human directions stay horizontal. */
    setDirection(handle: number, direction: PositionLike): boolean;
    /** Uniform or per-axis scale, from 0.001 through 100. */
    setScale(handle: number, scale: number | PositionLike): boolean;
    setVisible(handle: number, visible: boolean): boolean;
    /** Screen coordinates are pixels; optional static-world occlusion check. */
    projectWorld(position: PositionLike, checkOcclusion?: boolean): WorldProjection | null;
};

/** Per-frame 2D overlay commands; call from Events.on("render"). Colors are 0xAARRGGBB. */
declare const Draw: {
    /** Draws a model preview in a screen-pixel rectangle before CEF composition. Yaw is radians; zoom is 0.5–2.5 (default 1). Call each render tick. */
    preview(handle: number, x: number, y: number, width: number, height: number, yaw?: number, zoom?: number): boolean;
    rect(x: number, y: number, width: number, height: number, argb: number): boolean;
    line(x1: number, y1: number, x2: number, y2: number, thickness: number, argb: number): boolean;
    circle(x: number, y: number, radius: number, argb: number): boolean;
    /** Centered native text; font is a stock face index 0 through 3 (default 3). */
    text(text: string, x: number, y: number, size: number, argb: number, font?: number): boolean;
    /** Centers a label at a world point. Static geometry occlusion is enabled by default. */
    worldText(text: string, position: PositionLike, size: number, argb: number, font?: number, checkOcclusion?: boolean): boolean;
};

/**
 * Sounds only this client hears. The game releases a sound once it has
 * played; a looping one plays until Sound.stop or the mission closes. Use the
 * server Sound class for sounds everyone hears.
 */
declare const Sound: {
    /**
     * Plays a non-positional sound.
     * @param wave Game sound file under the Sounds directory or its archives, such as "00_dog.wav".
     * @returns The sound id, or null when the file is missing or no mission is loaded.
     */
    play(wave: string, options?: { volume?: number; loop?: boolean }): number | null;
    /**
     * Plays a positional sound.
     * @param options Radius 1 to 200 (default 25), volume 0 to 1 (default 1) and looping (at most 32 loops).
     */
    playAt(wave: string, position: PositionLike, options?: { radius?: number; volume?: number; loop?: boolean }): number | null;
    /** Stops a sound; false while no mission is loaded. */
    stop(id: number): boolean;
};


interface ClientMissionInfo {
    /** Stock mission directory name. */
    mission: string;
    /** Server generation of this mission load. */
    missionGeneration: number;
}

/** Events from a web view owned by the resource. */
interface ClientBrowserEvent {
    viewId: number;
}
interface ClientBrowserURL extends ClientBrowserEvent {
    url: string;
}
interface ClientBrowserFrameURL extends ClientBrowserURL {
    isMainFrame: boolean;
}

/** Native events dispatched through Events.on, with their exact callback arguments. */
interface ClientEventMap {
    resourceStart: [resourceName: string];
    resourceStop: [resourceName: string];
    /** The framework chat line, before the chat shows it. */
    chatMessage: [message: { author: string; text: string; color: number }];
    /** Before a typed chat line is sent; returning false cancels it. */
    chatSend: [text: string];
    /** A replicated entity state key changed. */
    entityStateChange: [entity: Entity, key: string, value: any, previous: any];
    voiceStart: [];
    voiceStop: [];
    browserCreated: [event: ClientBrowserURL];
    browserLoadingStart: [event: ClientBrowserFrameURL];
    browserDocumentReady: [event: ClientBrowserURL];
    browserLoadingFailed: [event: ClientBrowserFrameURL & { description: string; errorCode: number }];
    browserNavigate: [event: ClientBrowserFrameURL & { blocked: boolean }];
    browserPopup: [event: ClientBrowserURL & { openerUrl: string }];
    browserCursorChange: [event: ClientBrowserEvent & { cursor: string; cursorType: number }];
    browserTooltip: [event: ClientBrowserEvent & { text: string }];
    browserInputFocusChange: [event: ClientBrowserEvent & { focused: boolean }];
    browserResourceBlocked: [event: ClientBrowserURL & { domain: string; reason: "cross-origin" | "invalid-url" | "host-filter" | "foreign-event" }];
    browserConsoleMessage: [event: ClientBrowserEvent & { message: string; source: string; line: number; severity: "debug" | "info" | "warning" | "error" | "fatal" }];
    browserOriginChange: [event: ClientBrowserURL & { origin: string }];
    /** After the native mission loaded and the server's weather and frame overrides were applied. */
    missionReady: [mission: ClientMissionInfo];
    /** While the native mission closes; local sounds and HUD state are cleared right after. */
    missionUnload: [mission: ClientMissionInfo];
    /** Submit Draw commands here; the queue is cleared before the next client update. */
    render: [];
    /** A player's replica reached this client, including the local player. */
    playerStreamIn: [player: Player];
    /** A player's replica has left this client; its handles no longer resolve. */
    playerStreamOut: [playerId: number];
    /** A streamed player started a new life. */
    playerSpawn: [player: Player];
    /** A streamed player's server health reached zero. */
    playerDeath: [player: Player];
    /** A weapon pickup for the loaded mission reached this client. */
    pickupStreamIn: [pickup: Pickup];
    /** A pickup left this client or its mission unloaded. The handle is gone; lastKnown is a plain snapshot. */
    pickupStreamOut: [pickupId: number, lastKnown: ClientPickupSnapshot];
    doorStreamIn: [door: Door];
    doorChange: [door: Door];
    doorStreamOut: [doorId: number, lastKnown: ClientDoorSnapshot];
    /** A countdown started with Hud.startCountdown ran out. */
    countdownEnd: [];
}

declare const Events: {
    /** Native client events, and events a server script sent with `player.emit(name, JSON.stringify(data))`, which arrive as the parsed data. */
    on<K extends keyof ClientEventMap>(eventName: K, handler: (...args: ClientEventMap[K]) => unknown): () => void;
    on(eventName: string, handler: (...args: any[]) => unknown): () => void;
    once(eventName: string, handler: (...args: any[]) => unknown): void;
    off(eventName: string, handler: (...args: any[]) => unknown): void;
    /** Emits to this client's own scripts. */
    emit(eventName: string, ...args: unknown[]): Promise<void>;
    emitTo(resourceName: string, eventName: string, ...args: unknown[]): Promise<void>;
    onLocal(eventName: string, handler: (...args: any[]) => unknown): void;
    emitLocal(eventName: string, ...args: unknown[]): Promise<void>;
    /**
     * Sends an event to the server, where `Events.onClient(eventName, (player, data) => ...)`
     * receives it with the sending Player. Non-string payloads are JSON-serialized;
     * string payloads are sent verbatim and must contain valid JSON text.
     */
    emitServer(eventName: string, payload?: unknown): void;
    listenerCount(eventName: string): number;
};

/** Local chat transport and visibility controls. */
declare const Chat: {
    send(text: string): void;
    setUIVisible(visible: boolean): void;
    isUIVisible(): boolean;
    open(): void;
    close(): void;
    isOpen(): boolean;
};

/** Each view belongs to the resource that created it. */
declare const Web: {
    createView(url: string, options?: { width?: number; height?: number; x?: number; y?: number; zIndex?: number; visible?: boolean; focus?: boolean }): number;
    destroyView(viewId: number): boolean;
    showView(viewId: number): boolean;
    hideView(viewId: number): boolean;
    focusView(viewId: number, focused?: boolean): boolean;
    isViewVisible(viewId: number): boolean;
    setViewOffscreen(viewId: number, offscreen?: boolean): boolean;
    isViewOffscreen(viewId: number): boolean;
    loadURL(viewId: number, url: string): boolean;
    resizeView(viewId: number, width: number, height: number): boolean;
    setViewPosition(viewId: number, x: number, y: number): boolean;
    on(viewId: number, eventName: string, handler: (...args: any[]) => void): void;
    off(viewId: number, eventName: string, handler?: (...args: any[]) => void): boolean;
    emit(viewId: number, eventName: string, payload?: unknown): boolean;
    getScreenSize(): { width: number; height: number };
};

/** Local proximity voice controls. */
declare const Voice: {
    setEnabled(enabled: boolean): void;
    isEnabled(): boolean;
    setVolume(volume: number): void;
    getVolume(): number;
    setHearingRange(range: number): void;
    getHearingRange(): number;
    getRange(): number;
    setPushToTalkKey(key: string): void;
    getPushToTalkKey(): string;
    setPushToTalkReleaseDelay(milliseconds: number): void;
    getPushToTalkReleaseDelay(): number;
    isTalking(): boolean;
    hasMicrophone(): boolean;
};

/** Settings for this client's view of other players' nametags. */
declare const Nametags: {
    setVisible(visible: boolean): void;
    isVisible(): boolean;
    setHealthVisible(visible: boolean): void;
    isHealthVisible(): boolean;
    setLabel(entityId: number, text: string, durationMs?: number, color?: number): void;
    clearLabel(entityId: number): void;
    clearLabels(): void;
};

/** Runtime side flags. */
declare const ExecutionEnvironment: {
    readonly isClient: boolean;
    readonly isServer: boolean;
};
