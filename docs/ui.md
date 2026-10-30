# Web UI (CEF)

Mafia1Online draws its main menu, mission loading screen, chat, pause screen, settings, inventory, action chooser, player list and toasts
with Chromium (CEF). The mod ships no native menu definition. The page is a
Preact + StyleX app in `ui/`. The client paints it off screen and composites
it into the game's Direct3D 8 frame. If CEF, the page or its heartbeat fails,
the retail main menu reappears with only Exit, quick join is the way to
connect, and the native HUD chat comes back.

## Architecture

```
ui/ (Preact + StyleX)  --vite build-->  resources/ui  --client build-->  bin/ui
                                                                          |
fw://mafia1online/index.html  <--  Framework::GUI::Manager (CEF, OSR, CPU paint)
        |  OnPaint BGRA + dirty rects                                     ^
        v                                                                 | callEvent
ViewD3D8 (managed A8R8G8B8 texture, pow2) --IGraph::Present hook--> frame |
        ^ window.__m1o(...) (ExecuteJavaScript)                           |
Features::WebUi::WebUiService  <------------------------------------------+
```

* **Framework** (`code/framework/src`):
  * `graphics/backend/d3d8_api.h`: a vtable-slot binding for the few
    `IDirect3DDevice8`/`Texture8`/`Surface8` calls needed. The Windows SDK no
    longer ships `d3d8.h`.
  * `graphics/backend/d3d8.*`: `D3D8Backend`, which borrows the game's device.
    `RendererBackend::BACKEND_D3D_8` and `RendererConfiguration::d3d8.device`
    select it.
  * `gui/backend/view_d3d8.*`: `ViewD3D8`. It uploads only the union of CEF's
    dirty rects (`RenderHandler::GetDirtyBounds`) into a `D3DPOOL_MANAGED`
    texture, which survives the game's `Reset` after a lost device. It draws
    one pre-transformed, premultiplied-alpha quad inside a captured
    `D3DSBT_ALL` state block, so the engine's cached render states are left
    alone. While `TestCooperativeLevel` fails, it skips drawing.
* **Client** (`code/client/src/features/web_ui`):
  * `web_ui_hooks.cpp`: hooks `IGraph::Present` (the finished frame in every
    loop) and subclasses the game window to pass keyboard messages to the
    focused view.
  * `web_ui_service.*`: starts CEF, owns the view and the bridge, and decides
    which screen shows and who owns the input.
  * `web_ui_settings.*`: the recent/favorite server book and preferences.
    They live in `bin/config/ui.json`, or next to `MAFIA1ONLINE_CLIENT_CONFIG`
    as `<name>.ui.json`. The password is never stored.
* **Client** (`code/client/src/features/settings`): the settings screen's
  model and its in-process application; see [Settings](#settings) below.
* **Native hand-over**: `Menu::SetNativeControlsSuppressed` hides every retail
  main menu component and the menu cursor (`G_IndicatorsClass::DrawCursor`
  hook), and the main menu ignores every click. The profile picker is skipped
  (see `docs/native_contract.md`). The
  3D menu scene keeps running behind the page. `ChatService` keeps its hooks
  and forwards keys through `SetKeyRouter`. `SetNativeChatEnabled(false)` turns
  off only its drawing. The game-key queue (`TakeGameKey`) still works.

### Inventory and interaction choices

The inventory binding (normally I) and ambiguous use actions open a web overlay.
Click a row, press 1–9, or use arrows and Enter; Escape/0 closes it. Inventory
also provides Drop buttons and X/Delete for the selected row. Opening a menu
captures gameplay controls while the mission, physics, damage, replication,
voice and scripts continue running. The ESC menu's buttons explicitly enable
pointer events inside the otherwise noninteractive overlay layout.

`GameplayMenuService` intercepts only `GM_Inventory` and `GM_ItemPickUp` in a
server mission. It copies labels and choice identities, destroys the uncreated
native menu, and returns cancellation immediately. A web selection queues one
normal input frame. The native player rebuilds its menu candidates, the service
matches the requested item/target against those fresh candidates, and the native
inventory or pickup operation executes using that call's live output vectors.
The caller still performs its normal weapon-model update, drop or `Use_Actor`.
Nested pickup menus retain a path of choice identities, never stack pointers.

Requests are scoped to a menu ID, player binding, spawn and mission generation,
and seating context. A target stream-out, death, mission change, disconnect,
focus loss or another resource taking input cancels the open menu. A selection
that no longer exists is discarded; a nearby replicated pickup cannot replace
an explicitly selected door/seat during revalidation. Single native actions
retain their immediate behavior. If CEF is unavailable, these modal menus are
cancelled and logged instead of entering the blocking retail loop; normal
weapon cycling/holstering and single interactions remain available.

### Screens and compositing

`WebUiService` picks the screen live: `loading` from Play (or a server
mission change) until the mission's game loop runs, `menu` while the native
main menu is active, `game` while a server mission is running, and `hidden`
otherwise (profile picker, the retail loads of the menu scene). It sends the
screen to the page. The page answers with `ui:screen` once it has painted that
screen. Only then is the view composited, so a stale menu is never drawn over
the game or a loading screen.

### Loading screen

A mission load blocks the main loop: `C_mission::Open` and `C_game::Init`
run to completion inside one call, so nothing ticks, CEF is not pumped and the
only frames are the retail loader's own. Each of them is one
`GM_LoadProgress::Present` (scene and mission data roughly every 2 %, then
the game's initialisation steps; see `docs/native_contract.md`). The client
hooks that call and its two progress setters:

* The setters report the bar's share of the whole load to the page (`loading`).
* `Present` pumps CEF once (`Manager::Update`), which delivers the page's
  newest paint and its `ui:screen` answer, then presents a cleared black frame
  instead of the retail menu. The `IGraph::Present` hook composites the page
  onto it once the page has acknowledged `loading`.
* The main menu waits up to 1.5 s for that acknowledgement before it closes
  for the mission, so the first loader frame already shows the page. A
  mission change starts from the game loop instead and shows black until the
  page answers, usually within the first few loader frames.
* If the page has not acknowledged `loading` within 4 s of the load, or the
  web UI is not active, the retail loading screen draws as before.

The page is opaque, mostly static, and repaints only for the bar and the line
of tips and period flavour that changes every 9 s. It shows the mission (free
ride scenes by name, other missions from their directory name), the server's
name from the favourites or recent list with its address, the nickname, the
loader's stage and its real progress. Before the first report the bar is a
static tint, never a sweep. Respawns stay in the running mission and show no
loading screen.

### Layout and type

The root font size is `clamp(11px, min(1.9vh, 1.35vw), 40px)` times the
interface scale, and every size is in `rem`, so the screens scale from
640x480 to 4K. Below 620 px of height the menu compacts (smaller title, no
tagline, tighter panels); below 560 px of width the server book hides.
Connected-state buttons stack, so nothing can overflow its panel. The look
is clean film noir: near-black panels with a light hairline border, paper
white text, greys, and one blood red for primary buttons and for the marker
every selected or focused item carries (a red bar on its left); green and
red mark status (`src/tokens.stylex.ts`). Corners are sharp, and titles sit
on a double hairline. Two bundled fonts: Playfair Display (700) for the title
and headings, Inter (400/500/600) for everything else, the in-game chat
included; small labels and buttons are uppercase and letter-spaced. Over the
menu scene the backdrop is static noir lighting: deep shadow on the left, a
diagonal edge and faint venetian-blind stripes on the right. Nothing
animates in a loop: CEF paints on the CPU and uploads each changed rect, so
motion is kept to short opacity/transform transitions on interaction.

### Input

* Mouse: the game owns the mouse through exclusive DirectInput.
  The service forwards the native menu cursor (`g_iMenuMouseX/Y`,
  `0x6bd8a0/0x6bd8a4`), `IGraph::GetMouseButtons` and `Mouse_rz` (wheel) to
  CEF. In a mission, it integrates the device's relative movement into one screen
  position. The focused resource receives input before the built-in UI; window
  mouse messages are swallowed so they cannot duplicate the device's clicks.
  Coordinates sent to a positioned browser subtract that view's origin.
* Cursor: one client-owned, unfocused CEF view (`index.html#cursor`) renders the
  shared pointer above the shell and all resources. Its coordinates and pressed
  state come from the same input sample delivered to the focused browser. CEF's
  cursor type supplies arrow, hand, text, move, wait and blocked shapes. It hides
  when the window loses activation or gameplay owns the mouse. Resource pages
  keep their normal CSS cursors and never implement a cursor element or
  `__m1oSetCursor`. DirectInput's cooperative mode and the OS cursor are unchanged.
* Keyboard: every page key and every in-game toggle comes from window
  messages (`WM_KEYDOWN`/`WM_CHAR`/...) through the window subclass. Under
  Wine an exclusive DirectInput keyboard swallows them, so while the page is
  active the service reacquires the keyboard non-exclusive once
  (`KeyboardInit(flags & ~1)`) and restores the flags on deactivation.
  Buffered mode is not used, so `ReadKey` keeps DirectInput edges for the
  game and for `TakeGameKey` (K siren, F10 car sync). While the UI owns the
  keyboard, the `ReadKey` router returns 0 to the game and `C_Input::Update`
  clears the action vectors, so typing never drives the player.
* Hotkeys in game: `T` opens the chat, `/` opens it prefilled with `/`, `Esc`
  toggles the pause screen and closes the chat, and holding `F1` shows the
  player list (`Tab` stays the retail city map). These are defaults; the
  player can move all but Escape in Settings. A toggle fires only on a
  fresh press (bit 30 clear and not already down); the key then stays latched
  until its key-up, so auto-repeat and its `WM_CHAR` never reach the page.
  Keys already held when the chat or pause screen opens (walking) are latched
  the same way. The
  DirectInput copy of T, `/` and Esc is consumed without acting. Losing focus
  releases every key held in the view, clears that state and closes the chat
  and pause screen. They only take effect once the page is on screen.
* Safety valve: if the page does not close its chat within 700 ms of Enter,
  the client takes the keyboard back. After 8 s without the page's 1 s
  heartbeat (and 240 ticks), the native UI comes back.

## Settings

The main menu's Settings button and the pause screen's Settings entry open
one screen with five pages. Every change applies at once and is saved a
second later (and when the screen closes) to `bin/config/settings.json`, or
next to `MAFIA1ONLINE_CLIENT_CONFIG` as `<name>.settings.json`. The file is the
mod's own: the Mafia profile and every game file stay untouched.

* **Controls**: every retail on-foot and car action with its primary and
  secondary key, the mod's keys (chat, chat with `/`, held player list,
  push-to-talk, police siren, car sync mode) and the keys resources list
  with `Settings.describeKey`, grouped per resource. Click a key, then press
  a key, a mouse button or turn the wheel; Escape cancels and Unbind clears.
  Retail actions take keyboard keys, mouse buttons 1-3 and the wheel; the
  mod's keys take keyboard keys (push-to-talk also mouse buttons); resource
  keys take any key that can be held. Escape is never bindable: it opens
  the pause screen. Keys that would fire together are flagged "Also: ...":
  on-foot and car actions are only compared with their own group, as retail
  does, while the mod's and resources' keys work everywhere.
* **Gameplay**: look sensitivity (both axes), turning speed limit, invert
  vertical look, keyboard steering linearity, double-tap roll, crosshair
  (off, dot, cross), speedometer unit, player names and health bars above
  heads, and the on/off switches resources define.
* **Audio**: master, music, cars and speech (the retail volumes), voice
  chat (0-200 %) and resource web audio such as the car radio.
* **Voice**: voice chat on/off, push-to-talk or voice activity, the
  push-to-talk key and release delay, the activation level against a live
  microphone meter, noise suppression and the microphone. The microphone
  opens with the server's voice session, so the meter moves only while
  connected.
* **Interface**: the chat and interface preferences kept in `ui.json`.

Each page has "Reset page to defaults". Retail bindings, the mouse and HUD
values and the four game volumes follow the selected Mafia profile until the
player changes one of them; from then on the file holds the whole group.
The client validates everything it reads: a missing file gives the
defaults, an unreadable one is kept as `settings.json.invalid` and the
defaults apply, and each bad entry falls back on its own. The page is not
trusted either: its changes go through the same parser.

While a key is being captured, the page owns the keyboard and mouse like any
other captured screen, so the press reaches neither the game nor resource
key binds. Moving a resource's key remaps it where `Key.bind` polls
(`RemappedInput`), so its handlers and `Key.isDown` follow the new key.

## Bridge API

C++ to page, as `window.__m1o({ type, payload })`:

| type | payload |
| --- | --- |
| `state` | `{ screen: menu/game/loading/hidden, version, limits: {nickname, message}, connection: {phase, status, active, playAvailable, host, port, downloading, progress}, defaults: {nickname, host, port}, settings: {nickname, recent[], favorites[], preferences} }` |
| `chat:message` / `chat:history` | `{ author, text, color (0xRRGGBBAA, 0 = default), time (ms) }` / an array of those |
| `chat:open` | `{ prefill }` |
| `chat:closed` | `{}`. The client took the keyboard back. |
| `pause:open` | `{ settings }`: true opens it on the settings page (`Settings.open()`) |
| `settings` | the settings page model: `groups`, `slots` (key, label, defaultLabel, mouse, wheel, clearable, conflicts), `mouse`, `audio`, `media`, `voice`, `hud`, `devices`, `voiceAvailable`, `toggles`, `profile`. Sent while the page is open, only after a change. |
| `settings:level` | `{ level, talking, microphone }`, about 15 times a second while the voice page asks for it, only when it changed |
| `gameplay:open` | `{ id, kind: inventory \| interaction, title, choices: [{ label, detail, canSelect, canDrop }] }` |
| `gameplay:closed` | `{}`. Discard the current inventory/action panel. |
| `scoreboard` | `{ visible }` |
| `players` | `{ mission, players: [{ id, name, health, alive, spawned, local }] }` |
| `loading` | `{ mission, progress, stage }` while `screen` is `loading`, when it changes: `progress` is the retail loader's share of the whole load rounded to 0.01, or -1 before its first report; `stage` is `preparing`, `scene` (scene and mission data) or `world` (`C_game::Init`) |
| `toast` | `{ kind: info \| success \| error, text }` |
| `session:reset` | `{}`. The connection closed. |

Page to C++, as `callEvent(name, JSON)`. It is accepted only from the page's
own origin:

| name | payload | effect |
| --- | --- | --- |
| `ui:ready` | none | activates the web UI and sends state and chat history |
| `ui:alive` | none | heartbeat, once a second |
| `ui:screen` | `"menu"` \| `"game"` \| `"loading"` \| `"hidden"` | acknowledges the painted screen |
| `menu:connect` | `{ nickname, host, port, password }` | validated, then queued for the next client tick |
| `menu:disconnect` | none | disconnect (also works from the pause screen) |
| `menu:play` | none | enter the server's mission |
| `app:quit` | none | posts `WM_CLOSE`; the existing exit path closes in order |
| `chat:send` | `{ text }` | sanitized with `Shared::Chat::SanitizeLine` (128 code points), then sent |
| `chat:close` / `pause:close` | none | give the keyboard back to the game (Esc is handled by the client) |
| `gameplay:select` | `{ id, index, drop }` | queue a selection for this menu; revalidate it against fresh native candidates on the next player tick |
| `gameplay:close` | `{ id }` | cancel this inventory/action menu and release input |
| `servers:favorite` | `{ host, port, name, favorite }` | edits the favorites |
| `servers:forget` | `{ host, port }` | removes a recent entry |
| `settings:save` | `{ chatFadeSeconds, chatScale, uiScale, chatTimestamps, filmGrain, reduceMotion }` | filtered, clamped and stored |
| `settings:open` / `settings:close` | none | the settings page shows / hides; close writes pending changes. While open, Escape goes to the page. |
| `settings:set` | part of `{ mouse, audio, media, voice, hud }` | validated like the file, applied and saved |
| `settings:bind` | `{ slot, key }` | `native:<slot>`, `mod:<key>` or `script:<resource>/<key>` to a catalog key name; `""` unbinds |
| `settings:reset` | `{ section }` | `controls`, `mouse`, `audio`, `voice` or `hud` |
| `settings:toggle` | `{ resource, id, value }` | raises the resource's `settingToggle` event |
| `settings:meter` | `{ on }` | starts or stops `settings:level` |
| `settings:devices` | none | lists the microphones again |

The built-in `/q` command quits the game through its normal window-close path.
Type it in the in-game chat/command input and press Enter. It is handled locally
before outgoing chat reaches the server, with both the web and native chat.

## Building the UI

```sh
code/projects/mafia1online/tools/build_ui.sh    # npm ci (first time) + tsc + vite build
```

This writes `resources/ui` with stable file names (no hashes) and bundles the
fonts locally through `@fontsource`. Nothing loads from a CDN at runtime.
`bash builds/build.bat Mafia1OnlineClient 32` runs the script inside its
container before the Windows build. If the script fails, the existing
`resources/ui` is kept. The `Mafia1OnlineUi` CMake target then copies
`resources/ui` to `bin/ui`. CEF's runtime files (`libcef.dll`,
`cef_subprocess.exe`, `.pak`, `locales/`) are copied into `bin` by the
framework's `CEFCopyDlls`.

To design without the game, run `npm --prefix code/projects/mafia1online/ui run dev`
and open `/#menu`, `/#menu-connected`, `/#game`, `/#game-chat`, `/#game-pause`, `/#game-score`,
`/#loading` (the bar runs through a load) or `/#loading-start` (before the first report).
A mock bridge stands in for the client.

## Switches

* `MAFIA1ONLINE_NATIVE_UI=1` skips CEF entirely: retail main menu with only
  Exit, quick join, native chat.
* Under Wine, CPU off-screen rendering disables GPU rasterization and
  SwiftShader. Chromium's remaining GPU service runs in the browser process.
* A missing `bin/ui/index.html`, a failed `CefInitialize` (logged under `Web`,
  with `bin/logs/cef.log`) or a failed view has the same result.

## Known limits

* CEF runs with `--disable-gpu --disable-gpu-compositing` and
  `windowless_rendering_enabled`, uses the CPU paint path and no sandbox. Wine
  adds `--disable-gpu-rasterization --disable-software-rasterizer` and passes
  the CPU rendering switches to child command lines too. There is no IME
  composition window.
* Input queries are suppressed while a modal owns focus, so clicking or typing
  in a resource cannot also move the player or fire a weapon.
