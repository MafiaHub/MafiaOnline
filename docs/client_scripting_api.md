# Client scripting API

This guide covers the Mafia1Online globals and the framework globals available
to every client resource. For individual signatures and parameter descriptions,
see the [generated client reference](../scripting-api/generated/client-api.d.ts). The
[TypeScript declaration](../scripting-api/client-api.d.ts) is suitable for
editor completion.

Client scripts run in each player's sandboxed V8 (no Node.js APIs). A
resource ships them by listing them in its `package.json`:

```json
{
  "mafiahub": {
    "serverScripts": ["server/main.js"],
    "clientScripts": ["client/main.js"]
  }
}
```

`clientScripts` and `sharedScripts` are packaged into an encrypted
`<resource>.fwpak`, streamed to the client on connect and mounted at
`/resources/<name>`; `serverScripts` never leave the server. Extra
non-script files (UI pages, images) go in `files` (globs allowed). A folder
name has no meaning of its own. `resources/sample-gamemode` is the example.

The server owns the world. Client scripts read replicated state and drive
presentation only this player sees: the HUD, the fade, the camera swing and
local sounds. Handles hold a network id and resolve the replica on every call,
so a handle to a player who left is inert. The reference comes from the
binding metadata registered by the game and framework at runtime.

## API map

| Global | Purpose |
| --- | --- |
| `World`, `LocalPlayer`, `Entity`, `Player`, `Vehicle`, `Pickup` | Read the loaded mission and streamed replicas. |
| `Hud`, `Fade`, `Camera`, `Sound` | Native presentation and local audio for this player. |
| `Scene`, `Draw` | Client-only 3D models, projection, and per-frame 2D drawing. |
| `Events`, `Messages`, `Exports`, `Imports` | Resource events, client/server events, and local resource communication. |
| `Web`, `Chat`, `Key`, `Voice`, `Nametags`, `Discord` | Client UI, input, voice, labels, and presence. |
| `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Color` | Mutable value types. |
| `ExecutionEnvironment`, `console` | Runtime flags and resource-aware logging. |

## First client script

```js
Events.on("missionReady", ({ mission }) => {
    Hud.showMessage(`Welcome to ${mission}`, 0xe8c070);
});

Events.on("playerStreamIn", (player) => {
    if (!player.isLocal) Hud.showMessage(`${player.nickname} is nearby`);
});

Events.on("example:marker", ({ position }) => {
    Hud.setCompassTarget(position);
    Sound.play("00_kompas.wav");
});
```

`missionReady` fires after native loading and the server's world overrides
have been applied. A resource started after that event should check
`World.isReady()` and query `World.getPlayers()` for the current state.

## Talking to the server

- Server to client: a server script calls
  `player.emit("name", JSON.stringify(data))` (one player) or
  `Events.emitAllClients("name", data)`. The client receives it with
  `Events.on("name", (data) => ...)`, where `data` is the parsed JSON.
- Client to server: `Events.emitServer("name", data)`. The server receives it
  with `Events.onClient("name", (player, data) => ...)`; `player` is the
  server `Player` of the sender. A client can never reach a server
  `Events.on` handler this way, and malformed JSON is dropped.

Bridge methods serialize non-string payloads as JSON. A string payload is
sent verbatim, so pass valid JSON text when sending a string, or pass an
object for automatic serialization.

## Player

`new Player(id)`, `new Vehicle(id)`, and `new Pickup(id)` require a streamed entity of the
matching type and throw otherwise. Prefer `World.getPlayers()`,
`World.getVehicles()`, `World.getPickups()`, and `LocalPlayer` when discovering entities.
An existing handle may outlive streaming: getters then return their documented
empty or default values.

- `nickname`: `string`. Server nickname.
- `model`: `string`. Server-selected native human model file, such as `"Tommy.i3d"`; empty after stream out.
- `health`: `number`. Server health from 0 to 100.
- `money`: `number`. Last replicated Free Ride balance. Read only; the server changes it.
- `alive`: `boolean`. Spawned and alive.
- `spawned`: `boolean`. Has a life in the current mission.
- `isLocal`: `boolean`. The player this client controls.
- `missionGeneration`, `spawnGeneration`: `number`. Identify the current life.
- `id`, `virtualWorld`, `position`, `rotation`, `state`: from `Entity`.
- `isNametagVisible()`, `isNametagHealthVisible()`,
  `getNametagText()`, `getNametagColor()`: replicated nametag settings.
- `getVehicle(): Vehicle | null` — The vehicle the player sits in.
- `getSeat(): number` — Seat index (0 is the driver), or -1 on foot.
- `LocalPlayer: Player | null` — Global; null until the server created the
  local player.

## Vehicle

- `model`: `string`, `health`: `number`, `engineOn`, `sirenOn`, `hornOn`:
  `boolean`, `fuel`: `number`, `gear`: `number`, `seatCount`: `number`.
- `opacity`: `number`, from 0 transparent to 1 opaque; the server sets it.
- `getOccupant(seat): Player | null`, `getOccupants(): Player[]`.

## Pickup

`Pickup` is a client view of a replicated weapon lying in the loaded mission.
The server creates these with `Pickup.create()` or when a player calls
`Player.dropWeapon()`; a dropped weapon keeps its loaded and reserve rounds.
Client scripts can inspect a pickup but cannot take, create, or destroy one.

- `weaponId`: stock weapon ID; `heading`: facing in radians.
- `loaded`: rounds or throwable count in the weapon; `reserve`: spare rounds.
- `missionGeneration`: server generation of the containing mission.
- `position`: replicated position, read only for pickups. `rotation`: read-only
  quaternion derived from `heading`.
- `id`, `virtualWorld`, `state`: inherited from `Entity`.
- `World.getPickups(): Pickup[]`: script-created and dropped pickups streamed
  to this client in the loaded mission; empty before a mission loads.

## Door

`Door` is the read-only client view of a synchronized stock mission door.
`World.getDoors()` lists doors managed in the loaded mission. Each handle
exposes `frameName`, `open`, `locked`, `enabled`, `reverse`, `openFraction`, and
`missionGeneration`, plus the replicated read-only hinge `position`. The
server owns all changes. `doorStreamIn(door)` runs
when a door first reaches this client, `doorChange(door)` runs when its
replicated state changes, and `doorStreamOut(doorId, lastKnown)` supplies a
plain snapshot after the handle stops resolving. Native use of a nearby door
registers it automatically.

## Client-only models and drawing

`Scene.createModelFrame("9money.i3d")` loads a stock visual as a live `Frame`
object. `Scene.createDummyFrame()` creates an invisible anchor.
`Scene.canUseHumanModel(filename)` checks the complete native skeleton before
`Scene.createHumanFrame(model, position, direction?, solid?)` creates a
stationary local `C_entity`. Collision defaults to off; passing `true` as the
fourth argument enables native human collision on this client. The numeric
`Scene.createHuman` API accepts the same arguments. Each client creates its
own human, so scripts should use the same position for a solid shopkeeper.
Native hit and death paths are suppressed for active solid local humans, so
combat cannot remove a shopkeeper on only one client. Their collision and
vehicle contact otherwise remain native. These frames do not appear on the
server or other clients.
`frame.getWorldPosition()` and
`frame.getWorldDirection()` read the current native world matrix;
`frame.getRotation()` and `frame.getScale()` read the local quaternion and
scale. `frame.setWorldPosition(position)`, `frame.setRotation({ w, x, y, z })`,
`frame.setDirection(direction)`, `frame.setScale(scale)`,
`frame.setVisible(visible)`, and `frame.destroy()` change an owned local frame.
Scale accepts one number or `{ x, y, z }`; human scale is fixed. All setters
return `false` after the frame expires. A mission unload or resource stop
releases owned frames; each client is limited to 128.

`World.findWorldFrame(name)` returns the same `Frame` interface for a named
stock scene frame. It is borrowed and read-only (`frame.readOnly === true`):
its getters resolve current values, while setters and `destroy()` return
`false`. Client scripts must not mutate server-owned mission geometry.
`frame.isValid()` reports whether the frame still resolves in this mission and
resource. Getters return `null` after expiration. Frames never retain a native
pointer across mission unload. `Scene.getFrame(id)` wraps a legacy numeric
handle; `frame.id` exposes the numeric ID of an owned frame for
`Scene.playHumanAnimation(id, filename, loop?)` and `Scene.setHumanIdle(id)`.
The existing `Scene.createModel/createDummy` numeric APIs and
`World.getMissionFrame(name)` snapshot remain available for older scripts.

`Scene.projectWorld(position, checkOcclusion?)` returns screen pixel
coordinates and explicit `onScreen`, `occluded`, and `visible` flags, or
`null` before a camera exists. With `checkOcclusion: true`, the visibility
test checks static world geometry. It does not check moving cars or people.
Off-screen and behind-camera points have `x: null` and `y: null`.

`Events.on("render", handler)` runs once per client update during a mission.
Draw calls made in that handler are shown in the native overlay for that
frame. `Draw.rect`, `Draw.line`, `Draw.circle`, `Draw.text`, and
`Draw.worldText` use screen pixels and packed `0xAARRGGBB` colors.
`Draw.worldText` projects the point and checks static-world occlusion by
default. Native text uses the game's stock font slots 0–3; slot 3 is the
default. For packaged custom fonts and CSS text styling, use a resource-owned
[Web view](#web) with `@font-face` and its `Web.emit` page bridge.

```js
let model = null;
Events.on("missionReady", () => { model = Scene.createModelFrame("9money.i3d"); });
Events.on("missionUnload", () => { model = null; });
Events.on("render", () => {
    if (model === null) return;
    const point = { x: 0, y: 1, z: 0 };
    model.setWorldPosition(point);
    model.setRotation({ w: Math.cos(Date.now() / 2000), x: 0, y: Math.sin(Date.now() / 2000), z: 0 });
    Draw.worldText("SUPPLIES", { x: point.x, y: point.y + 1, z: point.z }, 16, 0xffffd070);
});
```

The [sample client pickup script](../resources/sample-gamemode/client/visual_pickups.js)
uses this API to bob and rotate a model and label. Its matching
[server script](../resources/sample-gamemode/server/visual_pickups.js) owns
the pickup definition, validates the player's mission, life, and distance,
then grants the item. This is separate from the engine's replicated weapon
`Pickup` class.

For example:

```js
for (const pickup of World.getPickups()) {
    console.log(`Weapon ${pickup.weaponId} at ${pickup.position.x}, ${pickup.position.z}`);
}
```

Handles resolve the replica on each read. After a pickup streams out, the
weapon and ammunition getters return zero and `toString()` reports `gone`.

## Entity and state

`Entity` is the base class of `Player`, `Vehicle`, and `Pickup`. Its `id` and
`virtualWorld` are read-only. `position` reads as a `Vector3`; `rotation`
reads as a `Quaternion`. For `Player` and `Vehicle`, the inherited setters
also accept a `Vector3` position and either a `Quaternion` or Euler-angle
`Vector3` rotation.
On a client these assignments change only the local replica temporarily;
the server's next update remains authoritative. Change persistent transforms
from a server script.

For `Pickup`, position and rotation are read-only snapshots of the server's
placement; use a server script to move or replace one.

`entity.state` exposes replicated custom values without client write
methods. Use `get(key)`, `has(key)`, `keys()`, and `toObject()` to read
it. `onChange(key, handler)` returns an unsubscribe function; pass `null`
for the key to watch all keys. The handler receives
`(key, value, previous)`; a removed or previously absent value is
`undefined`. The `entityStateChange` global event carries
`(entity, key, value, previous)`.

## World

Read-only; change the world from a server script.

- `World.getPlayers(): Player[]`, `World.getVehicles(): Vehicle[]` — What is
  streamed to this client.
- `World.getPickups(): Pickup[]` — Replicated weapon pickups in the loaded
  mission, including script-created and dropped ones.
- `World.getDoors(): Door[]` — Server-managed stock mission doors.
- `World.getMission(): string | null`, `World.getMissionGeneration(): number`,
  `World.isReady(): boolean` — The loaded native mission.
- `World.getWeather(): { weather, intensity, cityMusic }` — The replicated
  weather preset (`"default" | "clear" | "rain" | "snow"`), its intensity
  (0 to 100, or null for the preset's own) and whether the city music plays.
- `World.getNightMode(): boolean | null` — The effective native night flag,
  including the server override; null before a mission is ready. This flag
  affects native night behavior such as vehicle lights, not the sky or clock.
- `World.findWorldFrame(name): Frame | null` — Live, read-only handle to a
  named frame in the loaded stock scene. Use its world position and direction
  for native spawn anchors and camera staging. `World.getMissionFrame(name)`
  remains as a snapshot API for older scripts.

## Hud

Mission-dependent calls return `false` or `null` while no mission is
loaded; the hide and clear calls return `void`. The game clears the watch,
score, and compass on every mission change, so restore them from
`missionReady`.

- `Hud.showMessage(text, color?)` — A line in the native HUD console (top
  left, five lines, five seconds each). `color` is `0xRRGGBB`, default white.
- `Hud.announce(text, durationSeconds = 3)` — Large white centred text (the
  race flash text), fading over its last second.
- `Hud.showWatch(hours, minutes, seconds?)` — The mission watch, running in
  real time from that clock time. Use whole hours 0–23 and minutes/seconds
  0–59.
- `Hud.startCountdown(seconds)` — The watch's countdown wedge. The
  `countdownEnd` event fires when it runs out. Use whole seconds up to
  356400. `Hud.getCountdown()` returns
  the seconds left or null; `Hud.hideWatch()` hides both without the event.
- `Hud.setScore(score)` sets the native Free Ride score value and shows its
  counter. `Hud.addScore(points)` adds to the current native value and shows it.
  `Hud.getScore()` reads the game's actual value, including while hidden.
  `Hud.setScoreVisible(visible)` shows or hides the counter without changing
  its value; `Hud.isScoreVisible()` reads that state. `Hud.hideScore()` is a
  shortcut for hiding it. The getters return `null` before a mission loads.
- The sample gamemode uses the counter for money. Call
  `Hud.setScore(LocalPlayer.money)` after `missionReady`, then refresh it as the
  replicated balance changes. Native Free Ride scripts can write this counter
  locally; `LocalPlayer.money` remains the server-owned balance.
- `Hud.setCompassTarget(target)` — Points the compass arrow at a position,
  or at a `Player` or `Vehicle` it follows. `Hud.clearCompassTarget()` hides
  it.
- Texts are converted from UTF-8 to the game's code page and cut to 127
  characters.

## Fade and Camera

- `Fade.out(durationSeconds, color?)` — Fades the screen to a colour
  (default black) and holds it. `Fade.in(durationSeconds, color?)` fades
  back to the scene. 0 is instant.
- `Camera.setSwing(intensity)` — The retail boat swing: the camera and sky
  roll slowly from side to side; `intensity` 0 to 100, 0 stops it. Mafia 1
  has no camera shake; neither the retail camera nor explosions shake it.
- `Camera.getFov()` and `Camera.setFov(degrees)` — Read or set the active
  camera's field of view in degrees; setters accept 1–179.
- `Camera.setRange(nearClip, farClip)` — Set clipping planes in world units.
  Near is 0.01–10; far is above near by more than 0.01 and at most 5000.
- `Camera.lock(position, direction)` — Freeze the camera at a world position
  looking along a normalized direction. `Camera.unlock()` returns control to
  the player camera and refreshes the light cache. A respawn or mission close
  returns to the normal player camera.
  These calls return false before an active camera exists; `getFov()` returns null.

## Sound (local)

Only this client hears these. `wave` is a file under the game's `Sounds`
directory or its archives, such as `"00_dog.wav"`.

- `Sound.play(wave, { volume?, loop? }): number | null` — Non-positional.
- `Sound.playAt(wave, position, { radius?, volume?, loop? }): number | null`
  — Positional; radius 1 to 200 (default 25), the sound is full volume
  within half of it.
- `Sound.stop(id): boolean`.
- The game releases a finished sound itself; a loop plays until `Sound.stop`
  or the mission closes (at most 32 loops). A missing file returns null and is
  logged once.

The [sample car radio](../resources/sample-gamemode/client/radio.js) uses a
resource-owned `Web` audio view for a live internet stream. The stream starts
when this client enters a car and stops on exit. The HTML page handles a lost
connection by retrying the same station. The directory page is not an audio
source; use the station's direct stream URL. Live streams do not support seek.

## Events

`Events.on(name, handler)` receives these native events:

| Event | Arguments | When |
| --- | --- | --- |
| `resourceStart` / `resourceStop` | `resourceName` | A resource on this client starts or stops. |
| `chatMessage` | `{ author, text, color }` | A chat line arrived. |
| `chatSend` | `text` | A locally typed chat line is about to be sent; return `false` to cancel it. `Chat.send` bypasses this event. |
| `entityStateChange` | `entity, key, value, previous` | A replicated entity's state key changed. |
| `voiceStart` / `voiceStop` | none | The local player started or stopped talking. |
| `missionReady` | `{ mission, missionGeneration }` | The native mission loaded and the server's weather and frame overrides were applied. |
| `missionUnload` | `{ mission, missionGeneration }` | The mission is closing; local sounds and HUD state are cleared right after. |
| `playerStreamIn` | `player` | A player's replica reached this client (the local player too). |
| `playerStreamOut` | `playerId` | A player's replica left; handles to it no longer resolve. |
| `playerSpawn` | `player` | A streamed player started a new life. |
| `playerDeath` | `player` | A streamed player's server health reached zero. |
| `pickupStreamIn` | `pickup` | A weapon pickup for the loaded mission reached this client. |
| `pickupStreamOut` | `pickupId, lastKnown` | A pickup left this client or the loaded mission. `lastKnown` is a plain snapshot with `id`, `weaponId`, `position`, `heading`, `loaded`, `reserve`, and `missionGeneration` from the last replicated update; the handle no longer resolves. |
| `doorStreamIn` | `door` | A managed mission door reached this client. |
| `doorChange` | `door` | The server changed the door's target, lock or native use state. |
| `doorStreamOut` | `doorId, lastKnown` | The door left this client or its mission unloaded; the handle no longer resolves. |
| `countdownEnd` | none | A `Hud.startCountdown` countdown ran out. |

The `browser*` events report lifecycle, navigation, loading, focus,
console, and security events for web views owned by this resource. They all
carry an object with `viewId`. The names are `browserCreated`,
`browserLoadingStart`, `browserDocumentReady`,
`browserLoadingFailed`, `browserNavigate`, `browserPopup`,
`browserCursorChange`, `browserTooltip`,
`browserInputFocusChange`, `browserResourceBlocked`,
`browserConsoleMessage`, and `browserOriginChange`. See the
[generated event reference](../scripting-api/generated/client-api.d.ts) for their fields.

Players already streamed when a resource starts produce no `playerStreamIn`;
use `World.getPlayers()` from `resourceStart`.

### Event and resource methods

| API | Meaning |
| --- | --- |
| `Events.on(name, handler)` | Subscribe to a native, server-sent, or script event. Returns an unsubscribe callback. |
| `Events.once(name, handler)`, `Events.off(name, handler)` | Subscribe once or remove a matching listener. |
| `Events.emit(name, ...args)`, `Events.emitTo(resource, name, ...args)` | Emit among client resources; return a promise for handler completion. Reserved native names cannot be emitted by scripts. |
| `Events.onLocal(name, handler)`, `Events.emitLocal(name, ...args)` | Private event namespace of the current resource. |
| `Events.emitServer(name, payload?)` | Send a JSON-serializable payload to `Events.onClient` handlers on the server. |
| `Events.listenerCount(name)` | Count listeners registered for a name. |
| `Messages.handle(type, handler)` | Register a local resource message handler; handler receives `(payload, reply)`. |
| `Messages.request(resource, type, payload?)` | Request a reply from another client resource; returns a promise. |
| `Messages.send(resource, type, payload?)` | Send a notification without waiting for a reply. |
| `Exports.register(name, value)`, `Exports.get(resource, name)` | Publish a value and read another resource's published value. |
| `Imports.get(resource)` | Read all currently published exports of another resource. |

Events sent between client and server use JSON data. `Messages`,
`Exports`, and `Imports` stay within the client runtime. Resource
dependencies should be declared in the resource manifest before importing
another resource's exports.

## Framework client globals

### Web

`Web.createView(url, options?)` creates a resource-owned CEF view and returns
its numeric ID. The optional geometry includes `width`, `height`, `x`,
`y`, and `zIndex`; `visible` and `focus` set its initial state. A
resource-relative URL can load a packaged page. The view is locked to its
allowed origin, and views and handlers are removed when the resource stops.

| API | Meaning |
| --- | --- |
| `Web.destroyView(id)`, `Web.showView(id)`, `Web.hideView(id)` | Control an owned view. |
| `Web.focusView(id, focused?)`, `Web.isViewVisible(id)` | Set input focus or read visibility. |
| `Web.setViewOffscreen(id, enabled?)`, `Web.isViewOffscreen(id)` | Keep a hidden view painting or read that state. |
| `Web.loadURL(id, url)`, `Web.resizeView(id, width, height)`, `Web.setViewPosition(id, x, y)` | Navigate or adjust view geometry. |
| `Web.on(id, name, handler)`, `Web.off(id, name, handler?)` | Subscribe to a page's `callEvent` messages. |
| `Web.emit(id, name, payload?)` | Dispatch a `CustomEvent` into the page. |
| `Web.getScreenSize()` | Return `{ width, height }` in physical pixels. |

See [UI integration](ui.md) for packaging, URLs, and the page bridge.

### Chat, input, voice, and nametags

| Global | API |
| --- | --- |
| `Chat` | `send(text)`, `setUIVisible(visible)`, `isUIVisible()`, `open()`, `close()`, `isOpen()`. |
| `Key` | `bind(key, handler)` or `bind(key, "down" | "up" | "both", handler)`, `unbind(key, state?, handler?)`, `isDown(key)`. Key handlers receive `(key, "down" | "up")`; bindings are removed when the resource stops. |
| `Voice` | `setEnabled` / `isEnabled`, `setVolume` / `getVolume`, `setHearingRange` / `getHearingRange`, `getRange`, `setPushToTalkKey` / `getPushToTalkKey`, `setPushToTalkReleaseDelay` / `getPushToTalkReleaseDelay`, `isTalking`, `hasMicrophone`. |
| `Nametags` | `setVisible` / `isVisible`, `setHealthVisible` / `isHealthVisible`, `setLabel(entityId, text, durationMs?, color?)`, `clearLabel(entityId)`, `clearLabels()`. Labels affect only this client's display. |

`Key` accepts case-insensitive letter, digit, function, arrow, modifier,
editing, numpad, and mouse button names. Its handlers pause while chat,
a menu, a focused web view, or another UI captures input. Voice volume is
clamped to 0–4, and push-to-talk release delay to 0–2000 ms. A hearing
range of 0 uses the server range. Nametag label color is packed
`0xAARRGGBB`; 0 uses the normal nametag color.

### Discord, values, and runtime

`Discord` stages rich-presence fields through `setType`, `setName`,
`setDetails`, `setState`, the timestamp and image setters, the party
setters, the secret setters, `setInstance`, and
`setSupportedPlatforms`. Call `update()` to publish the staged value,
or `setPresence(options)` to merge and publish a batch. `clear()`
removes the activity; `reset()` discards staged fields.
`isAvailable()` and `getUserId()` report connection state.

`Vector2`, `Vector3`, `Vector4`, `Quaternion`, and `Color` are
global mutable value classes with arithmetic, conversion, and cloning
methods. For example, `new Vector3(1, 2, 3)` constructs a position;
`Color.fromRGB(255, 120, 0)` constructs a color. See their generated
symbol pages for all methods.

`ExecutionEnvironment.isClient` is true and `isServer` is false.
`console.log`, `console.info`, `console.warn`,
`console.error`, and `console.debug` write resource-aware log lines.
The sandbox also provides `setTimeout`, `clearTimeout`,
`setInterval`, and `clearInterval`.

## What the client does with the server's World and Sound

These run whether or not a resource has client scripts.

- **Weather** (`World.setWeather`). Applied after every mission load and on
  every change. `rain` and `snow` reset the LS3DF weather system to its rain
  or alternate (snow) preset, drop the mission's weather volumes so it falls
  everywhere, and set the particle count to `intensity / 100 x 3000`
  (1000 without an intensity). Snow is drawn white. The game scales the count
  by the actor detail setting (low 1/20, medium 1/8), as retail does. `clear`
  turns the system off; `default` restores the values the mission scene
  loaded with.
- **City music** (`World.setCityMusic`). Pauses or resumes the city's ambient
  music the way the `CITYMUSIC_OFF/ON` script commands do. Every mission load
  resumes it, so it is applied again.
- **Night mode** (`World.setNightMode`). Applies the native
  `GAME_NIGHTMISSION` flag for each loaded mission and late joiner. Passing
  null on the server restores the mission's original flag. This changes
  native night behavior such as vehicle lights; it does not move the clock
  or replace the sky.
- **Frames** (`World.setFrameVisible`). The named scene frame is found as
  `FINDFRAME` does (primary and backdrop sectors) and switched on or off. The
  scene's own visibility is remembered and restored by `World.resetFrames`.
  Overrides belong to one mission generation; a frame missing from this
  client's scene is skipped.
- **Frame opacity** (`World.setFrameOpacity`). For a named static visual frame,
  the client applies the original game's `FRM_SETALPHA` transparency call with
  opacity 0–1. Only the seven visual types accepted by that command are
  changed. `World.resetFrameOpacity` restores the frame's original opacity;
  mission changes clear all overrides. Missing frames are skipped.
- **Explosions and fires** (`World.createExplosion`, `World.createFire`).
  Replayed natively for the matching mission generation only. Damage 0 is
  visual: the explosion skips every actor, glass and breakable object and the
  fire damages nothing. Otherwise human hits stay suppressed (the server
  applied player damage) and cars are damaged by their simulation controller.
- **Sounds** (`Sound.create`, `Sound.play`). Each replicated sound is one
  native looping 3D sound, created, moved (an attached sound follows the
  entity's native frame), and released when it is destroyed, disabled, when
  its mission generation no longer matches, or before the mission closes.
  `Sound.play` is a one-shot the game releases itself. A wave the client does
  not have is logged once and skipped.
