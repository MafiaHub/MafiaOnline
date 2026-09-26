import type { EventHandler, KeyHandler, MessageHandler, Unsubscribe, WebEventHandler } from "../shared.js";

declare global {
  /**
   * Native events dispatched through `Events.on`. Each property is the exact callback argument tuple for that event. An event the server sends with `Player.emit` arrives as `[data: unknown]`.
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
     * Dispatched before a server chat message is added to the built-in chat overlay.
     */
    chatMessage: [message: ChatMessageEvent];

    /**
     * Dispatched synchronously when the local player submits a chat line. Return false to keep the line client-side. Chat.send bypasses this event; asynchronous handlers cannot block it.
     */
    chatSend: [text: string];

    /**
     * Dispatched when the local player starts speaking on voice chat. A held push-to-talk key with no speech does not trigger it.
     */
    voiceStart: [];

    /**
     * Dispatched when the local player stops speaking.
     */
    voiceStop: [];

    /**
     * Dispatched once the native mission has loaded, after the server's weather and frame overrides were applied.
     */
    missionReady: [mission: ClientMissionInfo];

    /**
     * Dispatched while the native mission closes. Local sounds and HUD state are cleared right after.
     */
    missionUnload: [mission: ClientMissionInfo];

    /**
     * Dispatched once per client update while a mission is loaded. Submit Draw commands here; the queue is cleared before the next update.
     */
    render: [];

    /**
     * Dispatched when a player's replica reaches this client, including the local player.
     */
    playerStreamIn: [player: Player];

    /**
     * Dispatched once a player's replica has left this client; its handles no longer resolve.
     */
    playerStreamOut: [playerId: number];

    /**
     * Dispatched when a streamed player starts a new life.
     */
    playerSpawn: [player: Player];

    /**
     * Dispatched when a streamed player's server health reaches zero.
     */
    playerDeath: [player: Player];

    /**
     * Dispatched when a weapon pickup for the loaded mission reaches this client.
     */
    pickupStreamIn: [pickup: Pickup];

    /**
     * Dispatched when a weapon pickup leaves this client or its mission unloads. The handle no longer resolves; lastKnown is a plain snapshot from the last replicated update.
     */
    pickupStreamOut: [pickupId: number, lastKnown: ClientPickupSnapshot];

    /**
     * Dispatched when a mission door replica reaches this client.
     */
    doorStreamIn: [door: Door];

    /**
     * Dispatched after a replicated mission door's target, lock or native use state changes.
     */
    doorChange: [door: Door];

    /**
     * Dispatched when a mission door replica leaves or its mission unloads.
     */
    doorStreamOut: [doorId: number, lastKnown: ClientDoorSnapshot];

    /**
     * Dispatched when a countdown started with Hud.startCountdown runs out.
     */
    countdownEnd: [];

    /**
     * Dispatched when one key of an entity's state changes: on the server when a script writes it, on a client when the write arrives. `value` is undefined when the key was removed and `previous` is undefined when it held nothing before, so a stored null stays distinguishable from an absent key. The entity is whatever the game's WrapScriptEntity answers, and the base Entity handle by default.
     */
    entityStateChange: [entity: Entity, key: string, value: any, previous: any];

    /**
     * Dispatched once per view, before any navigation, to the resource that created it.
     */
    browserCreated: [event: BrowserCreatedEvent];

    /**
     * Dispatched when any frame of an owned view begins loading.
     */
    browserLoadingStart: [event: BrowserLoadingStartEvent];

    /**
     * Dispatched when an owned view's main document is ready; the earliest point at which the page can receive Web.emit.
     */
    browserDocumentReady: [event: BrowserDocumentReadyEvent];

    /**
     * Dispatched when a frame of an owned view fails to load.
     */
    browserLoadingFailed: [event: BrowserLoadingFailedEvent];

    /**
     * Dispatched for every navigation an owned view is asked to perform, refused or not.
     */
    browserNavigate: [event: BrowserNavigateEvent];

    /**
     * Dispatched when an owned view blocks a popup; windowless views cannot host one, so handle the URL yourself.
     */
    browserPopup: [event: BrowserPopupEvent];

    /**
     * Dispatched when an owned view's requested cursor shape changes.
     */
    browserCursorChange: [event: BrowserCursorChangeEvent];

    /**
     * Dispatched when an owned view requests a tooltip; windowless rendering draws none, so the script must.
     */
    browserTooltip: [event: BrowserTooltipEvent];

    /**
     * Dispatched when focus enters or leaves an editable element of an owned view; use it to stop routing keys to the game.
     */
    browserInputFocusChange: [event: BrowserInputFocusChangeEvent];

    /**
     * Dispatched when an owned view rejects a navigation or a page event from outside its locked origin.
     */
    browserResourceBlocked: [event: BrowserResourceBlockedEvent];

    /**
     * Dispatched for console output of an owned view; the framework logs these regardless.
     */
    browserConsoleMessage: [event: BrowserConsoleMessageEvent];

    /**
     * Dispatched on view creation and whenever Web.loadURL re-locks an owned view to a different origin.
     */
    browserOriginChange: [event: BrowserOriginChangeEvent];
  }

  /** Names of native events available in this scripting environment. */
  type EventName = keyof EventMap;

  /**
   * A chat message received from the server.
   */
  interface ChatMessageEvent {
    /**
     * Display name supplied by the server.
     */
    author: string;

    /**
     * Message body.
     */
    text: string;

    /**
     * Packed message color.
     */
    color: number;
  }

  /**
   * The mission this client has loaded.
   */
  interface ClientMissionInfo {
    /**
     * Stock or detected mod mission directory name.
     */
    mission: string;

    /**
     * Server generation of this mission load.
     */
    missionGeneration: number;
  }

  /**
   * The last pickup values this client received before the pickup streamed out. These values remain available after its handle stops resolving.
   */
  interface ClientPickupSnapshot {
    /**
     * Network entity ID of the pickup.
     */
    id: number;

    /**
     * Mission generation this pickup belonged to.
     */
    missionGeneration: number;

    /**
     * Weapon this pickup held at the last replicated update.
     */
    weaponId: number;

    /**
     * Last replicated world position.
     */
    position: { x: number; y: number; z: number };

    /**
     * Last replicated heading in radians.
     */
    heading: number;

    /**
     * Last replicated loaded rounds or throwable count.
     */
    loaded: number;

    /**
     * Last replicated reserve rounds.
     */
    reserve: number;
  }

  /**
   * Last known door state before a mission door streams out.
   */
  interface ClientDoorSnapshot {
    /**
     * Network ID of the door.
     */
    id: number;

    /**
     * Mission generation containing the door.
     */
    missionGeneration: number;

    /**
     * Root door frame name.
     */
    frameName: string;

    /**
     * Last replicated hinge position.
     */
    position: { x: number; y: number; z: number };

    /**
     * Last server target open state.
     */
    open: boolean;

    /**
     * Last lock state.
     */
    locked: boolean;

    /**
     * Last native use state.
     */
    enabled: boolean;

    /**
     * Last root swing side.
     */
    reverse: boolean;

    /**
     * Last open fraction from 0 to 1.
     */
    openFraction: number;
  }

  /**
   * A streamed Mafia 1 player, local or remote. Every value is the server state this client last received; a handle stops resolving once the player leaves.
   */
  class Player {
    /**
     * Creates a handle for a streamed player; throws when the ID does not resolve to a player.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Server nickname.
     */
    readonly nickname: string;

    /**
     * Server-selected native human model file, such as "Tommy.i3d"; empty after the player streams out.
     */
    readonly model: string;

    /**
     * Server health from 0 to 100.
     */
    readonly health: number;

    /**
     * Last replicated server-owned Free Ride balance. Read only, including for the local player.
     */
    readonly money: number;

    /**
     * Whether the player is spawned and alive.
     */
    readonly alive: boolean;

    /**
     * Whether the player has a life in the current mission.
     */
    readonly spawned: boolean;

    /**
     * Whether this is the player this client controls.
     */
    readonly isLocal: boolean;

    /**
     * Mission generation of the current life.
     */
    readonly missionGeneration: number;

    /**
     * Generation of the current life; it increases on every spawn.
     */
    readonly spawnGeneration: number;

    /**
     * Formats this player for logging.
     * @returns The player ID and nickname.
     */
    toString(): string;

    /**
     * Returns the streamed vehicle the player sits in.
     * @returns The vehicle, or null on foot.
     */
    getVehicle(): Vehicle | null;

    /**
     * Returns the seat the player occupies.
     * @returns The seat index (0 is the driver), or -1 on foot.
     */
    getSeat(): number;
  }

  interface Player extends BasePlayer {}

  /**
   * A streamed server vehicle. Every value is the server state this client last received.
   */
  class Vehicle {
    /**
     * Creates a handle for a streamed vehicle; throws when the ID does not resolve to a vehicle.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Native model file, such as "fordtl00.i3d".
     */
    readonly model: string;

    /**
     * Server health from 0 to 100.
     */
    readonly health: number;

    /**
     * Whether the engine runs.
     */
    readonly engineOn: boolean;

    /**
     * Whether the siren sounds.
     */
    readonly sirenOn: boolean;

    /**
     * Whether the horn sounds.
     */
    readonly hornOn: boolean;

    /**
     * Fuel in the tank.
     */
    readonly fuel: number;

    /**
     * Server-authored model opacity, 0 transparent and 1 opaque.
     */
    readonly opacity: number;

    /**
     * Current gear; 0 is neutral.
     */
    readonly gear: number;

    /**
     * Number of seats.
     */
    readonly seatCount: number;

    /**
     * Formats this vehicle for logging.
     * @returns The vehicle ID and model.
     */
    toString(): string;

    /**
     * Returns the player in a seat.
     * @param seat Seat index; 0 is the driver.
     * @returns The player, or null for an empty seat.
     */
    getOccupant(seat: number): Player | null;

    /**
     * Lists the seated players.
     * @returns Every streamed occupant in seat order.
     */
    getOccupants(): Player[];
  }

  interface Vehicle extends Entity {}

  /**
   * A weapon pickup streamed from the server. Script-created pickups and weapons dropped by players use the same handle; values are the latest replicated state.
   */
  class Pickup {
    /**
     * Creates a handle for a streamed weapon pickup; throws when the ID does not resolve to a pickup.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Replicated pickup position. Read only on the client.
     */
    readonly position: Vector3;

    /**
     * Facing derived from heading. Read only on the client.
     */
    readonly rotation: Quaternion;

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
     * Server generation of the mission containing this pickup.
     */
    readonly missionGeneration: number;

    /**
     * Formats this pickup for logging.
     * @returns The pickup ID and weapon, or gone when it has streamed out.
     */
    toString(): string;
  }

  interface Pickup extends Entity {}

  /**
   * Read-only view of a server-managed mission door, including its paired leaf's shared target state.
   */
  class Door {
    /**
     * Creates a handle for a streamed mission door.
     * @param id Door network ID.
     */
    constructor(id: number);

    /**
     * Replicated hinge position, read only on the client.
     */
    readonly position: Vector3;

    /**
     * Name of the root door frame.
     */
    readonly frameName: string;

    /**
     * Server target open state.
     */
    readonly open: boolean;

    /**
     * Whether native opening is locked.
     */
    readonly locked: boolean;

    /**
     * Whether native use is enabled.
     */
    readonly enabled: boolean;

    /**
     * Swing side of the root leaf.
     */
    readonly reverse: boolean;

    /**
     * Target open fraction from 0 to 1.
     */
    readonly openFraction: number;

    /**
     * Mission generation this door belongs to.
     */
    readonly missionGeneration: number;

    /**
     * Formats the door ID and frame name.
     */
    toString(): string;
  }

  interface Door extends Entity {}

  /**
   * The weather the server replicated to this client.
   */
  interface ClientWeatherInfo {
    /**
     * Weather preset; default keeps the mission's own weather.
     */
    weather: "default" | "clear" | "rain" | "snow";

    /**
     * Rain or snow intensity from 0 to 100, or null for the preset's own.
     */
    intensity: number | null;

    /**
     * Whether the city's ambient music plays.
     */
    cityMusic: boolean;
  }

  /**
   * A named frame from the loaded native mission scene. Its vectors are snapshots.
   */
  interface ClientMissionFrame {
    /**
     * World position of the named frame.
     */
    position: Vector3;

    /**
     * World forward direction of the named frame.
     */
    direction: Vector3;
  }

  /**
   * Read-only view of the mission and the entities streamed to this client. The server owns the world; change it from a server script.
   */
  const World: {
    /**
     * Lists the players streamed to this client, including the local one.
     * @returns Every streamed player.
     */
    getPlayers(): Player[];

    /**
     * Lists the vehicles streamed to this client.
     * @returns Every streamed vehicle.
     */
    getVehicles(): Vehicle[];

    /**
     * Lists the weapon pickups streamed into the loaded mission.
     * @returns Script-created and dropped pickups in this mission; empty while no mission is loaded.
     */
    getPickups(): Pickup[];

    /**
     * Lists the mission doors the server is synchronizing.
     * @returns Empty before a mission has loaded.
     */
    getDoors(): Door[];

    /**
     * Returns the loaded stock mission.
     * @returns The mission name, or null while none is loaded.
     */
    getMission(): string | null;

    /**
     * Returns the server generation of the loaded mission.
     * @returns The generation, or 0 while none is loaded.
     */
    getMissionGeneration(): number;

    /**
     * Whether the native mission is loaded.
     * @returns True while a mission runs.
     */
    isReady(): boolean;

    /**
     * Returns the replicated weather and city music state.
     * @returns The weather preset, intensity and city music flag.
     */
    getWeather(): ClientWeatherInfo;

    /**
     * Reads the loaded native mission's effective night flag.
     * @returns Null before the mission is ready.
     */
    getNightMode(): boolean | null;

    /**
     * Reads a named frame's world position and direction from the loaded mission.
     * @param name Native scene frame name, such as the Free Ride spawn anchor emeth_1.
     * @returns A snapshot, or null when the frame or mission is unavailable.
     */
    getMissionFrame(name: string): ClientMissionFrame | null;

    /**
     * Finds a borrowed, read-only native mission frame. Its getters resolve fresh values on every call.
     * @param name Native scene frame name, such as emeth_1.
     * @returns A live Frame handle, or null when the frame or mission is unavailable.
     */
    findWorldFrame(name: string): Frame | null;
  };

  /**
   * The retail Mafia 1 HUD for this client. Every call returns false while no mission is loaded, and the game clears the watch, score and compass on every mission change.
   */
  const Hud: {
    /**
     * Adds a line to the native HUD console, which keeps the last five lines for five seconds each.
     * @param text Message line; at most 127 characters are shown.
     * @param color 0xRRGGBB color; defaults to white.
     * @returns False while no mission is loaded.
     */
    showMessage(text: string, color?: number): boolean;

    /**
     * Shows large white centred text, the race flash text, fading out over its last second.
     * @param text Announcement; at most 127 characters are shown.
     * @param durationSeconds How long it stays; defaults to 3.
     * @returns False while no mission is loaded.
     */
    announce(text: string, durationSeconds?: number): boolean;

    /**
     * Shows the mission watch running in real time from this clock time.
     * @param hours Clock hours from 0 to 23.
     * @param minutes Clock minutes from 0 to 59.
     * @param seconds Clock seconds from 0 to 59.
     * @returns False while no mission is loaded.
     */
    showWatch(hours: number, minutes: number, seconds?: number): boolean;

    /**
     * Shows the watch with its countdown wedge. The countdownEnd event fires when it runs out.
     * @param seconds Countdown length in whole seconds; 0 removes the countdown.
     * @returns False while no mission is loaded.
     */
    startCountdown(seconds: number): boolean;

    /**
     * Returns the seconds left on the countdown.
     * @returns The remaining seconds, or null when no countdown runs.
     */
    getCountdown(): number | null;

    /**
     * Hides the watch and stops its countdown without firing countdownEnd.
     */
    hideWatch(): void;

    /**
     * Shows the freeride score counter with this value.
     * @param score Score to show.
     * @returns False while no mission is loaded.
     */
    setScore(score: number): boolean;

    /**
     * Adds to the score counter and shows it.
     * @param points Points to add; negative subtracts.
     * @returns The new score, or null while no mission is loaded.
     */
    addScore(points: number): number | null;

    /**
     * Reads the native Free Ride score value, even while its counter is hidden. Retail scripts may change it locally.
     * @returns The score, or null while no mission is loaded.
     */
    getScore(): number | null;

    /**
     * Reads whether the native Free Ride score counter is shown.
     * @returns True or false, or null while no mission is loaded.
     */
    isScoreVisible(): boolean | null;

    /**
     * Shows or hides the native Free Ride score counter without changing its value.
     * @param visible Whether to show the existing score value.
     * @returns False while no mission is loaded.
     */
    setScoreVisible(visible: boolean): boolean;

    /**
     * Hides the score counter without changing its value.
     */
    hideScore(): void;

    /**
     * Points the native compass arrow at a target.
     * @param target A fixed point, or a streamed player or vehicle the arrow follows.
     * @returns False while no mission is loaded or the entity is not streamed.
     */
    setCompassTarget(target: Vector3 | { x: number; y: number; z: number } | Player | Vehicle): boolean;

    /**
     * Hides the compass arrow.
     */
    clearCompassTarget(): void;
  };

  /**
   * The retail full-screen fade, drawn over the scene and the HUD.
   */
  const Fade: {
    /**
     * Fades the screen to a color and holds it until Fade.in.
     * @param durationSeconds Fade duration; 0 is instant.
     * @param color 0xRRGGBB color to fade to; defaults to black.
     * @returns False while no mission is loaded.
     */
    out(durationSeconds: number, color?: number): boolean;

    /**
     * Fades from a color back to the scene.
     * @param durationSeconds Fade duration; 0 is instant.
     * @param color 0xRRGGBB color to fade from; defaults to black.
     * @returns False while no mission is loaded.
     */
    "in"(durationSeconds: number, color?: number): boolean;
  };

  /**
   * The retail game camera.
   */
  const Camera: {
    /**
     * Rolls the camera and the sky slowly from side to side, the retail boat swing. Mafia 1 has no camera shake.
     * @param intensity Roll strength from 0 to 100, as the retail CAMERA_SETSWING script command takes it; 0 stops the swing.
     * @returns False while no mission is loaded.
     */
    setSwing(intensity: number): boolean;

    /**
     * Returns the active camera's effective field of view in degrees.
     * @returns Null while no active mission camera exists.
     */
    getFov(): number | null;

    /**
     * Sets the active camera's field of view through the retail widescreen-aware CAMERA_SETFOV path.
     * @param degrees Field of view from 1 to 179 degrees.
     * @returns False while no active mission camera exists. Camera modes may subsequently update the value.
     */
    setFov(degrees: number): boolean;

    /**
     * Sets the active camera's clip planes with the retail CAMERA_SETRANGE setter.
     * @param nearClip Near clip plane in world units, 0.01 to 10.
     * @param farClip Far clip plane above nearClip and at most 5000 world units.
     * @returns False while no active mission camera exists. Camera modes may subsequently update the values.
     */
    setRange(nearClip: number, farClip: number): boolean;

    /**
     * Locks the camera at a world position and direction, as CAMERA_LOCK does with a frame. Call Camera.unlock to resume following the player.
     * @param position Fixed camera position in world coordinates.
     * @param direction Nonzero forward direction; normalized before use.
     * @param roll Bank around the forward direction in radians; defaults to zero.
     * @returns False while no active mission camera exists or direction is zero. A new local life or mission close restores the player camera.
     */
    lock(position: Vector3 | { x: number; y: number; z: number }, direction: Vector3 | { x: number; y: number; z: number }, roll?: number): boolean;

    /**
     * Returns from a camera lock to the previous player camera mode and refreshes the light cache.
     * @returns False while no mission is loaded.
     */
    unlock(): boolean;
  };

  /**
   * Sounds only this client hears. The game releases a sound once it has played; a looping one plays until Sound.stop or the mission closes. Use the server Sound class for sounds everyone hears.
   */
  const Sound: {
    /**
     * Plays a non-positional sound.
     * @param wave Game sound file under the Sounds directory or its archives, such as "00_dog.wav".
     * @param options Volume 0 to 1 (default 1) and whether it loops (at most 32 loops).
     * @returns The sound id, or null when the file is missing or no mission is loaded.
     */
    play(wave: string, options?: { volume?: number; loop?: boolean }): number | null;

    /**
     * Plays a positional sound.
     * @param wave Game sound file.
     * @param position Where the sound plays.
     * @param options Radius 1 to 200 (default 25), volume 0 to 1 and looping.
     * @returns The sound id, or null when the file is missing or no mission is loaded.
     */
    playAt(wave: string, position: Vector3 | { x: number; y: number; z: number }, options?: { radius?: number; volume?: number; loop?: boolean }): number | null;

    /**
     * Stops a sound.
     * @param id Id from Sound.play or Sound.playAt.
     * @returns False while no mission is loaded.
     */
    stop(id: number): boolean;
  };

  /**
   * Live handle to a native scene frame in the loaded mission. Named world frames are borrowed and read-only; local frames belong to their creating resource. Handles expire on mission unload or resource stop.
   */
  interface Frame {
    /**
     * Frame name at the time this handle was created.
     */
    readonly name: string;

    /**
     * Legacy numeric ID for an owned local frame; null for a borrowed mission frame.
     */
    readonly id: number | null;

    /**
     * True for a named mission frame; its setters and destroy return false.
     */
    readonly readOnly: boolean;

    /**
     * Checks whether the frame still resolves in this mission and resource.
     * @returns False after mission unload, resource stop, local destruction, or a removed named frame.
     */
    isValid(): boolean;

    /**
     * Reads the current world position through the native world matrix.
     * @returns Null after this handle expires.
     */
    getWorldPosition(): Vector3 | null;

    /**
     * Reads the current normalized world forward direction.
     * @returns Null after this handle expires.
     */
    getWorldDirection(): Vector3 | null;

    /**
     * Reads the frame's local quaternion rotation.
     * @returns Null after this handle expires.
     */
    getRotation(): { w: number; x: number; y: number; z: number } | null;

    /**
     * Reads the frame's local scale.
     * @returns Null after this handle expires.
     */
    getScale(): Vector3 | null;

    /**
     * Moves an owned local frame in world space; borrowed mission frames are read-only.
     * @returns False for a read-only or expired frame.
     */
    setWorldPosition(position: Vector3 | { x: number; y: number; z: number }): boolean;

    /**
     * Sets an owned local frame's local quaternion rotation.
     * @returns False for a read-only or expired frame.
     */
    setRotation(rotation: { w: number; x: number; y: number; z: number }): boolean;

    /**
     * Points an owned local frame forward; human directions remain horizontal.
     * @returns False for a read-only or expired frame.
     */
    setDirection(direction: Vector3 | { x: number; y: number; z: number }): boolean;

    /**
     * Sets an owned model or dummy frame's local scale. Native human actor scale is fixed.
     * @returns False for a read-only, human, or expired frame.
     */
    setScale(scale: number | Vector3 | { x: number; y: number; z: number }): boolean;

    /**
     * Shows or hides an owned local frame.
     * @returns False for a read-only or expired frame.
     */
    setVisible(visible: boolean): boolean;

    /**
     * Releases an owned local frame.
     * @returns False for a borrowed or expired frame.
     */
    destroy(): boolean;
  }

  /**
   * Client-local mission frames for visual effects. Handles do not replicate and are invalid after mission unload.
   */
  const Scene: {
    /**
     * Returns valid stock car IDs, display names and color-zero model filenames from the live native car database. Empty before initialization.
     */
    getCarCatalog(): CarCatalogEntry[];

    /**
     * Loads a stock .i3d into a private client-only showroom scene for 2D rendering. Returns null if unavailable or eight previews already exist.
     */
    createPreview(model: string): number | null;

    /**
     * Changes a preview's stock model; the old model stays if loading fails.
     */
    setPreviewModel(handle: number, model: string): boolean;

    /**
     * Sets preview pitch and roll in radians. Draw.preview supplies yaw and zoom per frame.
     */
    setPreviewTilt(handle: number, pitch: number, roll: number): boolean;

    /**
     * Returns a snapshot of a named child frame in the preview model, or null if unavailable.
     */
    getPreviewFrame(handle: number, name: string): PreviewFrame | null;

    /**
     * Changes a named child frame in the private preview scene. It never edits mission or replicated frames; invalid names or expired previews return false.
     */
    setPreviewFrame(handle: number, name: string, changes: { worldPosition?: Vector3 | { x: number; y: number; z: number }; rotation?: { w: number; x: number; y: number; z: number }; scale?: number | Vector3 | { x: number; y: number; z: number }; visible?: boolean }): boolean;

    /**
     * Releases a private showroom scene and its native frames.
     */
    destroyPreview(handle: number): boolean;

    /**
     * Loads a stock .i3d as a non-interactive local model. Returns null if unavailable or the 128-frame cap is reached.
     */
    createModel(filename: string): number | null;

    /**
     * Checks that the stock model loads and has the complete skeleton required by native human collision and animation.
     * @param filename Bare stock human .i3d filename.
     * @returns False for a missing or non-human model, or before mission load.
     */
    canUseHumanModel(filename: string): boolean;

    /**
     * Creates a local dummy frame; it has no mesh or collision.
     */
    createDummy(): number | null;

    /**
     * Loads a stock .i3d as a local model and returns a live Frame object.
     */
    createModelFrame(filename: string): Frame | null;

    /**
     * Creates an invisible local dummy and returns a live Frame object.
     */
    createDummyFrame(): Frame | null;

    /**
     * Creates a local human and returns its live Frame. Use frame.id with Scene.playHumanAnimation or Scene.setHumanIdle.
     * @param solid Native collision on this client; defaults to false.
     */
    createHumanFrame(model: string, position: Vector3 | { x: number; y: number; z: number }, direction?: Vector3 | { x: number; y: number; z: number }, solid?: boolean): Frame | null;

    /**
     * Wraps an existing owned numeric local frame handle in the common Frame interface.
     */
    getFrame(handle: number): Frame | null;

    /**
     * Creates a stationary local C_entity with a validated stock human model. Native animation still ticks. Up to 24 local humans; no network replication or server-side interaction.
     * @param solid Native collision on this client; defaults to false.
     */
    createHuman(model: string, position: Vector3 | { x: number; y: number; z: number }, direction?: Vector3 | { x: number; y: number; z: number }, solid?: boolean): number | null;

    /**
     * Plays a stock .i3d clip on a local human; loop defaults to false. Returns false for a missing animation or expired handle.
     */
    playHumanAnimation(handle: number, filename: string, loop?: boolean): boolean;

    /**
     * Stops a local human clip and restores the stock idle animation.
     */
    setHumanIdle(handle: number): boolean;

    /**
     * Releases a local frame and invalidates its handle.
     */
    destroy(handle: number): boolean;

    /**
     * Sets a frame's world position.
     */
    setPosition(handle: number, position: Vector3 | { x: number; y: number; z: number }): boolean;

    /**
     * Sets a frame's full rotation as a normalized quaternion.
     */
    setRotation(handle: number, rotation: { w: number; x: number; y: number; z: number }): boolean;

    /**
     * Points a frame forward; human directions stay horizontal.
     */
    setDirection(handle: number, direction: Vector3 | { x: number; y: number; z: number }): boolean;

    /**
     * Sets uniform or per-axis scale.
     */
    setScale(handle: number, scale: number | Vector3 | { x: number; y: number; z: number }): boolean;

    /**
     * Toggles a local frame.
     */
    setVisible(handle: number, visible: boolean): boolean;

    /**
     * Projects a point into screen pixels. With occlusion enabled, static world geometry blocks visibility.
     */
    projectWorld(position: Vector3 | { x: number; y: number; z: number }, checkOcclusion?: boolean): WorldProjection | null;
  };

  /**
   * One spawnable stock car from the loaded game's highest-priority carindex.def table.
   */
  interface CarCatalogEntry {
    /**
     * Native base car ID accepted by the server's stock car catalog.
     */
    id: number;

    /**
     * Retail display name decoded to UTF-8 from the game's native code page.
     */
    name: string;

    /**
     * Stock color-zero .i3d filename for preview and vehicle spawning.
     */
    model: string;
  }

  /**
   * Snapshot of a named child in a private model preview. Look it up again after replacing the model.
   */
  interface PreviewFrame {
    /**
     * Native child frame name.
     */
    name: string;

    /**
     * Current world position inside the private showroom scene.
     */
    worldPosition: Vector3;

    /**
     * Current local quaternion.
     */
    rotation: { w: number; x: number; y: number; z: number };

    /**
     * Current local scale.
     */
    scale: Vector3;

    /**
     * Native frame visibility flag.
     */
    visible: boolean;
  }

  /**
   * Projection of a world point into the current game viewport.
   */
  interface WorldProjection {
    /**
     * Screen pixel X, or null when off screen or behind the camera.
     */
    x: number | null;

    /**
     * Screen pixel Y, or null when off screen or behind the camera.
     */
    y: number | null;

    /**
     * Point is in front of the camera and inside the viewport.
     */
    onScreen: boolean;

    /**
     * Static world geometry blocks the point when occlusion was requested.
     */
    occluded: boolean;

    /**
     * onScreen and not occluded.
     */
    visible: boolean;
  }

  /**
   * Per-frame native overlay primitives in screen pixels. Call from Events.on('render'). Colors are 0xAARRGGBB.
   */
  const Draw: {
    /**
     * Queues a filled rectangle.
     */
    rect(x: number, y: number, width: number, height: number, argb: number): boolean;

    /**
     * Queues a line.
     */
    line(x1: number, y1: number, x2: number, y2: number, thickness: number, argb: number): boolean;

    /**
     * Queues a filled circle.
     */
    circle(x: number, y: number, radius: number, argb: number): boolean;

    /**
     * Queues centered native text. Font indices 0..3 are the game's built-in faces.
     */
    text(text: string, x: number, y: number, size: number, argb: number, font?: number): boolean;

    /**
     * Projects and queues centered text only when on screen and not blocked by static world geometry.
     */
    worldText(text: string, position: Vector3 | { x: number; y: number; z: number }, size: number, argb: number, font?: number, checkOcclusion?: boolean): boolean;

    /**
     * Queues a stock 3D model for a screen-pixel rectangle. Draws after the game scene and before CEF, clearing the viewport to a dark background. Call each Events.render tick.
     * @param yaw Radians; 0 faces the model's native forward orientation.
     * @param zoom 0.5 to 2.5; 1 is stock framing, larger is closer.
     */
    preview(handle: number, x: number, y: number, width: number, height: number, yaw?: number, zoom?: number): boolean;
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
     * Sends a named event from this client to the server's isolated onClient handlers.
     * @param eventName Server-side client-event name.
     * @param payload Optional string payload sent verbatim; other values are JSON-serialized.
     */
    emitServer(eventName: string, payload?: unknown): void;
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
   * Client-only, resource-owned CEF web-view API exposed as the global Web.
   */
  const Web: {
    /**
     * Creates an origin-locked web view owned by the calling resource.
     * @param url Resource-relative URL or allowed absolute URL loaded into the view.
     * @param options Optional initial pixel geometry, stacking, visibility, and focus settings.
     * @returns Numeric view ID used by the remaining Web methods.
     */
    createView(url: string, options?: { width?: number; height?: number; x?: number; y?: number; zIndex?: number; visible?: boolean; focus?: boolean }): number;

    /**
     * Destroys an owned view and removes all of its script event handlers.
     * @param viewId Owned view identifier.
     * @returns True when a view was destroyed.
     */
    destroyView(viewId: number): boolean;

    /**
     * Makes an owned view visible.
     * @param viewId Owned view identifier.
     * @returns True when the view exists and was updated.
     */
    showView(viewId: number): boolean;

    /**
     * Hides an owned view.
     * @param viewId Owned view identifier.
     * @returns True when the view exists and was updated.
     */
    hideView(viewId: number): boolean;

    /**
     * Changes input focus for an owned view.
     * @param viewId Owned view identifier.
     * @param focused Whether the view captures keyboard and mouse input; defaults to true.
     * @returns True when the view exists and focus was updated.
     */
    focusView(viewId: number, focused?: boolean): boolean;

    /**
     * Checks whether an owned view is currently visible.
     * @param viewId Owned view identifier.
     * @returns False for missing or unowned views.
     */
    isViewVisible(viewId: number): boolean;

    /**
     * Keeps a hidden view painting so something else can sample it, such as a render target drawing the page onto world geometry. A view that is merely hidden stops painting and the sampled picture freezes.
     * @param viewId Owned view identifier.
     * @param offscreen Whether the page keeps painting without being drawn on screen; defaults to true.
     * @returns True when the view exists and was updated.
     */
    setViewOffscreen(viewId: number, offscreen?: boolean): boolean;

    /**
     * Checks whether an owned view keeps painting while hidden.
     * @param viewId Owned view identifier.
     * @returns False for missing or unowned views.
     */
    isViewOffscreen(viewId: number): boolean;

    /**
     * Navigates a view and replaces its allowed origin with the new URL's origin.
     * @param viewId Owned view identifier.
     * @param url New resource-relative or allowed absolute URL.
     * @returns True when navigation was requested.
     */
    loadURL(viewId: number, url: string): boolean;

    /**
     * Resizes an owned view's viewport.
     * @param viewId Owned view identifier.
     * @param width New viewport width in pixels.
     * @param height New viewport height in pixels.
     * @returns True when the view exists and was resized.
     */
    resizeView(viewId: number, width: number, height: number): boolean;

    /**
     * Moves an owned view on screen.
     * @param viewId Owned view identifier.
     * @param x New horizontal screen position in pixels.
     * @param y New vertical screen position in pixels.
     * @returns True when the view exists and was moved.
     */
    setViewPosition(viewId: number, x: number, y: number): boolean;

    /**
     * Registers a handler for an event emitted by an owned same-origin page.
     * @param viewId Owned view identifier.
     * @param eventName Page-to-script event name.
     * @param handler Resource-owned callback invoked by the view's callEvent bridge.
     */
    on(viewId: number, eventName: string, handler: WebEventHandler): void;

    /**
     * Removes page-event handlers from an owned view.
     * @param viewId Owned view identifier.
     * @param eventName Page-to-script event name.
     * @param handler Optional exact callback; omitting it removes every matching handler owned by the resource.
     * @returns True when at least one handler was removed.
     */
    off(viewId: number, eventName: string, handler?: WebEventHandler): boolean;

    /**
     * Dispatches a CustomEvent into an owned view.
     * @param viewId Owned view identifier.
     * @param eventName CustomEvent name dispatched in the page.
     * @param payload Optional JSON-serializable event detail.
     * @returns True when the dispatch script was queued.
     */
    emit(viewId: number, eventName: string, payload?: unknown): boolean;

    /**
     * Returns the current client viewport size.
     * @returns Width and height in physical pixels.
     */
    getScreenSize(): { width: number; height: number };
  };

  /**
   * A web view's browser finished being created.
   */
  interface BrowserCreatedEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * URL the view was created with.
     */
    url: string;
  }

  /**
   * A frame inside a web view started loading.
   */
  interface BrowserLoadingStartEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * URL being loaded.
     */
    url: string;

    /**
     * False for sub-frame loads.
     */
    isMainFrame: boolean;
  }

  /**
   * A web view's main frame finished loading.
   */
  interface BrowserDocumentReadyEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * URL that finished loading.
     */
    url: string;
  }

  /**
   * A load inside a web view was aborted.
   */
  interface BrowserLoadingFailedEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * URL that failed to load.
     */
    url: string;

    /**
     * CEF error text, such as ERR_CONNECTION_REFUSED.
     */
    description: string;

    /**
     * CEF error code.
     */
    errorCode: number;

    /**
     * False for sub-frame failures.
     */
    isMainFrame: boolean;
  }

  /**
   * A web view was asked to navigate.
   */
  interface BrowserNavigateEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * Requested URL.
     */
    url: string;

    /**
     * False for sub-frame navigation.
     */
    isMainFrame: boolean;

    /**
     * Whether the request was refused.
     */
    blocked: boolean;
  }

  /**
   * A page tried to open a new window or tab.
   */
  interface BrowserPopupEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * Target URL of the blocked popup.
     */
    url: string;

    /**
     * URL of the frame that requested it.
     */
    openerUrl: string;
  }

  /**
   * The cursor shape a page is asking for.
   */
  interface BrowserCursorChangeEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * CSS-style cursor name, or "custom" for shapes without one.
     */
    cursor: string;

    /**
     * Raw CEF cursor type.
     */
    cursorType: number;
  }

  /**
   * A page wants to display a tooltip.
   */
  interface BrowserTooltipEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * Tooltip text; empty when the tooltip is dismissed.
     */
    text: string;
  }

  /**
   * A form control inside a page gained or lost focus.
   */
  interface BrowserInputFocusChangeEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * True while the page holds keyboard input.
     */
    focused: boolean;
  }

  /**
   * A web view refused a request.
   */
  interface BrowserResourceBlockedEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * URL that was refused.
     */
    url: string;

    /**
     * Host component of that URL, empty when unparsable.
     */
    domain: string;

    /**
     * Why the request was refused.
     */
    reason: "cross-origin" | "invalid-url" | "host-filter" | "foreign-event";
  }

  /**
   * A console call made by a page.
   */
  interface BrowserConsoleMessageEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * Console message body.
     */
    message: string;

    /**
     * Script URL that logged it.
     */
    source: string;

    /**
     * Line number within that script.
     */
    line: number;

    /**
     * Console severity.
     */
    severity: "debug" | "info" | "warning" | "error" | "fatal";
  }

  /**
   * A web view's allowed origin changed.
   */
  interface BrowserOriginChangeEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * New locked origin, or "null" when the URL has none.
     */
    origin: string;

    /**
     * URL the lock was derived from.
     */
    url: string;
  }

  /**
   * Client-only, resource-owned physical key bindings exposed as the global Key.
   */
  const Key: {
    /**
     * Binds a resource-owned handler that fires while the game has foreground input and no UI is capturing it.
     * @param key Case-insensitive supported keyboard or mouse key name.
     * @param stateOrHandler Trigger state, or the handler itself to use the default down state.
     * @param handler Handler required when an explicit trigger state is provided.
     * @returns True after the binding is installed; invalid keys or states throw.
     */
    bind(key: string, stateOrHandler: "down" | "up" | "both" | KeyHandler, handler?: KeyHandler): boolean;

    /**
     * Removes matching bindings owned by the calling resource; omitting filters removes every binding for the key.
     * @param key Case-insensitive supported key name.
     * @param state Optional trigger-state filter.
     * @param handler Optional exact handler filter.
     * @returns True when at least one binding was removed.
     */
    unbind(key: string, state?: "down" | "up" | "both", handler?: KeyHandler): boolean;

    /**
     * Queries live physical key state using the same foreground and UI-input gate as binding dispatch.
     * @param key Case-insensitive supported key name.
     * @returns False when the key is up, the game is backgrounded, UI owns input, or the key name is invalid.
     */
    isDown(key: string): boolean;
  };

  /**
   * Client chat transport and native chat-box controls.
   */
  const Chat: {
    /**
     * Sends a chat line to the server, bypassing the chatSend event; incoming lines arrive through the reserved chatMessage event.
     * @param text Player-authored chat text sent to the server.
     */
    send(text: string): void;

    /**
     * Changes visibility of the native chat overlay without opening its input field.
     * @param visible Whether the chat overlay is rendered.
     */
    setUIVisible(visible: boolean): void;

    /**
     * Checks whether the native chat overlay is visible.
     * @returns True when the overlay is currently rendered.
     */
    isUIVisible(): boolean;

    /**
     * Opens and focuses the native chat input field.
     */
    open(): void;

    /**
     * Closes the native chat input field without submitting its contents.
     */
    close(): void;

    /**
     * Checks whether the native chat input field is active.
     * @returns True while chat is capturing keyboard input.
     */
    isOpen(): boolean;
  };

  /**
   * Client-only Discord rich-presence composer exposed as the global Discord; setters stage values until update or setPresence publishes them.
   */
  const Discord: {
    /**
     * Stages the Discord activity verb or numeric enum value.
     * @param type the Discord activity verb or numeric enum value.
     */
    setType(type: "playing" | "streaming" | "listening" | "watching" | number): void;

    /**
     * Stages the activity name.
     * @param name the activity name.
     */
    setName(name: string): void;

    /**
     * Stages the top presence line.
     * @param details the top presence line.
     */
    setDetails(details: string): void;

    /**
     * Stages the bottom presence line.
     * @param state the bottom presence line.
     */
    setState(state: string): void;

    /**
     * Stages the elapsed-time Unix timestamp in seconds; zero clears it.
     * @param seconds the elapsed-time Unix timestamp in seconds; zero clears it.
     */
    setStartTimestamp(seconds: number): void;

    /**
     * Stages the countdown Unix timestamp in seconds; zero clears it.
     * @param seconds the countdown Unix timestamp in seconds; zero clears it.
     */
    setEndTimestamp(seconds: number): void;

    /**
     * Stages the supported-platform bitmask: Desktop 1, Android 2, and iOS 4.
     * @param flags the supported-platform bitmask: Desktop 1, Android 2, and iOS 4.
     */
    setSupportedPlatforms(flags: number): void;

    /**
     * Stages the large image asset key.
     * @param assetKey the large image asset key.
     */
    setLargeImage(assetKey: string): void;

    /**
     * Stages the large image tooltip.
     * @param text the large image tooltip.
     */
    setLargeText(text: string): void;

    /**
     * Stages the small image asset key.
     * @param assetKey the small image asset key.
     */
    setSmallImage(assetKey: string): void;

    /**
     * Stages the small image tooltip.
     * @param text the small image tooltip.
     */
    setSmallText(text: string): void;

    /**
     * Stages the party grouping identifier.
     * @param id the party grouping identifier.
     */
    setPartyId(id: string): void;

    /**
     * Stages the party join-privacy name or numeric enum value.
     * @param privacy the party join-privacy name or numeric enum value.
     */
    setPartyPrivacy(privacy: "private" | "public" | number): void;

    /**
     * Stages the opaque match secret.
     * @param secret the opaque match secret.
     */
    setMatchSecret(secret: string): void;

    /**
     * Stages the opaque join secret that enables invitations.
     * @param secret the opaque join secret that enables invitations.
     */
    setJoinSecret(secret: string): void;

    /**
     * Stages the opaque spectate secret.
     * @param secret the opaque spectate secret.
     */
    setSpectateSecret(secret: string): void;

    /**
     * Stages whether the activity represents an instanced session.
     * @param instance whether the activity represents an instanced session.
     */
    setInstance(instance: boolean): void;

    /**
     * Stages multiple Discord image fields at once.
     * @param assets Asset keys and tooltips to merge into the staged activity.
     */
    setAssets(assets: { largeImage?: string; largeText?: string; smallImage?: string; smallText?: string }): void;

    /**
     * Stages the party's current and maximum size.
     * @param current Current party member count.
     * @param max Maximum party capacity.
     */
    setPartySize(current: number, max: number): void;

    /**
     * Stages multiple Discord party fields at once.
     * @param party Party fields to merge into the staged activity.
     */
    setParty(party: { id?: string; size?: [number, number]; privacy?: "private" | "public" | number }): void;

    /**
     * Stages multiple Discord activity secrets at once.
     * @param secrets Opaque secrets to merge into the staged activity.
     */
    setSecrets(secrets: { match?: string; join?: string; spectate?: string }): void;

    /**
     * Merges a complete option batch into the staged activity and publishes it immediately.
     * @param options Batch of supported activity, timestamp, asset, party, secret, instance, and platform fields.
     * @returns True when the update was dispatched; false when Discord is unavailable.
     */
    setPresence(options: Record<string, unknown>): boolean;

    /**
     * Publishes the currently staged activity as one rate-limited Discord update.
     * @returns True when dispatched; false when Discord is unavailable.
     */
    update(): boolean;

    /**
     * Clears the published Discord activity and resets staged state.
     * @returns True when dispatched; false when Discord is unavailable.
     */
    clear(): boolean;

    /**
     * Resets staged activity fields without publishing a Discord update.
     */
    reset(): void;

    /**
     * Returns the signed-in Discord user's snowflake.
     * @returns User ID string, or an empty string until available.
     */
    getUserId(): string;

    /**
     * Checks whether Discord is connected and can publish presence.
     * @returns True when rich presence is initialized.
     */
    isAvailable(): boolean;
  };

  /**
   * Local player's proximity voice chat settings: on/off, playback volume, hearing range and the push-to-talk binding.
   */
  const Voice: {
    /**
     * Turns voice chat on or off. Off closes the microphone and playback devices and tells the server to stop relaying voice to this client.
     * @param enabled Whether voice chat runs at all for this player.
     */
    setEnabled(enabled: boolean): void;

    /**
     * Checks whether voice chat is enabled for this player.
     * @returns True unless the player turned voice chat off.
     */
    isEnabled(): boolean;

    /**
     * Sets the playback volume of incoming voice. A game that plays voice through its own audio engine applies the same gain there.
     * @param volume Playback gain, where 1 is unattenuated. Clamped to 0..4.
     */
    setVolume(volume: number): void;

    /**
     * Reads the voice playback volume.
     * @returns Current gain, where 1 is unattenuated.
     */
    getVolume(): number;

    /**
     * Narrows how far this player hears others. Can only reduce the server's range, since a talker beyond it is never relayed.
     * @param range Audibility radius in world units; 0 removes the local limit.
     */
    setHearingRange(range: number): void;

    /**
     * Reads the local hearing-range limit.
     * @returns Radius in world units, or 0 when the server's range applies unreduced.
     */
    getHearingRange(): number;

    /**
     * Reads the server's proximity range for talkers with no override of their own.
     * @returns Radius in world units.
     */
    getRange(): number;

    /**
     * Rebinds push-to-talk. Unknown key names throw.
     * @param key Case-insensitive key name, using the same names as Key.bind.
     */
    setPushToTalkKey(key: string): void;

    /**
     * Reads the push-to-talk binding.
     * @returns Canonical key name, e.g. "v".
     */
    getPushToTalkKey(): string;

    /**
     * Sets how long transmission continues after push-to-talk is released, so letting go slightly early does not clip the end of a word. Muting, turning voice off and losing input focus still stop it at once.
     * @param milliseconds How long to keep transmitting after the key goes up. Clamped to 0..2000.
     */
    setPushToTalkReleaseDelay(milliseconds: number): void;

    /**
     * Reads the push-to-talk release delay.
     * @returns Delay in milliseconds; 0 when transmission stops the moment the key is released.
     */
    getPushToTalkReleaseDelay(): number;

    /**
     * Checks whether the local player is speaking right now. The same state raises the voiceStart and voiceStop events.
     * @returns True while push-to-talk is open -- held, or still inside the release delay -- and the microphone is producing audio.
     */
    isTalking(): boolean;

    /**
     * Checks whether a capture device opened for this session.
     * @returns False when the player has no working microphone, i.e. they are listen-only.
     */
    hasMicrophone(): boolean;

    /**
     * Chooses what opens the microphone. Switching closes whatever the previous mode had open. Unknown modes throw.
     * @param mode pushToTalk sends while the key is held; voiceActivity sends whenever the microphone is louder than the activation threshold.
     */
    setTransmitMode(mode: 'pushToTalk' | 'voiceActivity'): void;

    /**
     * Reads what opens the microphone.
     * @returns The current transmit mode.
     */
    getTransmitMode(): 'pushToTalk' | 'voiceActivity';

    /**
     * Sets the voice-activation sensitivity: lower sends quieter speech, higher ignores more background noise. Clamped to 0..1.
     * @param level Microphone level, 0 to 1 on the scale getInputLevel reports, above which voice activation sends.
     */
    setActivationThreshold(level: number): void;

    /**
     * Reads the voice-activation threshold.
     * @returns Level from 0 to 1.
     */
    getActivationThreshold(): number;

    /**
     * Reads how loud the microphone is right now, whether or not anything is being sent -- the value to draw a level meter from and to tune the activation threshold against.
     * @returns Smoothed level from 0 to 1; 0 while no microphone is open.
     */
    getInputLevel(): number;

    /**
     * Turns noise suppression on or off. It runs before the level is measured, so it also keeps steady noise from tripping voice activation.
     * @param enabled Whether to filter background noise out of the microphone.
     */
    setNoiseSuppression(enabled: boolean): void;

    /**
     * Checks whether noise suppression is on.
     * @returns True while background noise is being filtered.
     */
    isNoiseSuppressionEnabled(): boolean;

    /**
     * Lists the microphones voice can record from.
     * @returns Device names as the player would recognise them; empty when the game chooses the device itself.
     */
    getInputDevices(): string[];

    /**
     * Chooses the microphone. A running microphone is reopened on the new device; a device that is no longer connected falls back to the default.
     * @param name A name from getInputDevices, or an empty string for the system default.
     */
    setInputDevice(name: string): void;

    /**
     * Reads the chosen microphone.
     * @returns Its name, or an empty string for the system default.
     */
    getInputDevice(): string;
  };

  /**
   * The local player's view of the nametags above other players: whether they draw at all, and whether they carry a health bar.
   */
  const Nametags: {
    /**
     * Shows or hides all nametags for this player only. A player hidden with Player.setNametagVisible stays hidden either way.
     * @param visible True to draw nametags, false to hide every one of them.
     */
    setVisible(visible: boolean): void;

    /**
     * Checks whether this player draws nametags.
     * @returns True unless they were hidden locally.
     */
    isVisible(): boolean;

    /**
     * Shows or hides the health bar on all nametags for this player only, leaving the names alone.
     * @param visible True to draw the health bar under each name, false to hide it.
     */
    setHealthVisible(visible: boolean): void;

    /**
     * Checks whether this player draws health bars on nametags.
     * @returns True unless they were hidden locally.
     */
    isHealthVisible(): boolean;

    /**
     * Hangs a transient line on that entity's nametag, above its name -- speech, an emote, a status. It follows the body, fades with distance and hides behind cover exactly as the name does. Local to this player: the line is not replicated, and a nametag hidden with Nametags.setVisible draws neither. Player.setNametagText is the server-side counterpart for a lasting name.
     * @param entityId Network id of the entity to label (server-side `player.id`).
     * @param text Label text; '\n' splits lines. Empty clears the label.
     * @param durationMs How long to hold it (default 6000). <= 0 holds until cleared.
     * @param color Packed 0xAARRGGBB; 0 (default) uses the nametag's own colour.
     */
    setLabel(entityId: number, text: string, durationMs?: number, color?: number): void;

    /**
     * Removes an entity's label before its duration elapses.
     * @param entityId Network id of the entity whose label to remove.
     */
    clearLabel(entityId: number): void;

    /**
     * Removes every label this player is drawing.
     */
    clearLabels(): void;
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
  }

  interface BasePlayer extends Entity {}

  /**
   * The player this client controls, or null before the server created it.
   */
  const LocalPlayer: Player | null;

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
