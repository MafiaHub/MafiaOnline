# Web UI (CEF)

Mafia1Online draws its main menu, chat, pause screen, player list and toasts
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
* **Native hand-over**: `Menu::SetNativeControlsSuppressed` hides every retail
  main menu component and the menu cursor (`G_IndicatorsClass::DrawCursor`
  hook), and the main menu ignores every click. The profile picker is skipped
  (see `docs/native_contract.md`). The
  3D menu scene keeps running behind the page. `ChatService` keeps its hooks
  and forwards keys through `SetKeyRouter`. `SetNativeChatEnabled(false)` turns
  off only its drawing. The game-key queue (`TakeGameKey`) still works.

### Screens and compositing

`WebUiService` picks the screen live: `menu` while the native main menu is
active, `game` while a server mission is running, and `hidden` otherwise
(profile picker, loading). It sends the screen to the page. The page answers
with `ui:screen` once it has painted that screen. Only then is the view
composited, so a stale menu is never drawn over the game or a loading screen.

### Layout and type

The root font size is `clamp(11px, min(1.9vh, 1.35vw), 40px)` times the
interface scale, and every size is in `rem`, so the screens scale from
640x480 to 4K. Below 620 px of height the menu compacts (smaller title, no
tagline, tighter panels); below 560 px of width the server book hides.
Connected-state buttons stack, so nothing can overflow its panel. Type is
Limelight (title), Playfair Display (headings, toasts) and EB Garamond (menu
body and inputs), all bundled. In-game chat uses a larger Arial-based sans
serif stack with upright notices for clearer reading over the scene.

### Input

* Mouse (menu only): the game owns the mouse through exclusive DirectInput.
  The service forwards the native menu cursor (`g_iMenuMouseX/Y`,
  `0x6bd8a0/0x6bd8a4`), `IGraph::GetMouseButtons` and `Mouse_rz` (wheel) to
  CEF. The page draws its own cursor.
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
  player list (`Tab` stays the retail city map). A toggle fires only on a
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

## Bridge API

C++ to page, as `window.__m1o({ type, payload })`:

| type | payload |
| --- | --- |
| `state` | `{ screen, version, limits: {nickname, message}, connection: {phase, status, active, playAvailable, host, port, downloading, progress}, defaults: {nickname, host, port}, settings: {nickname, recent[], favorites[], preferences} }` |
| `chat:message` / `chat:history` | `{ author, text, color (0xRRGGBBAA, 0 = default), time (ms) }` / an array of those |
| `chat:open` | `{ prefill }` |
| `chat:closed` | `{}`. The client took the keyboard back. |
| `pause:open` | `{}` |
| `scoreboard` | `{ visible }` |
| `players` | `{ mission, players: [{ id, name, health, alive, spawned, local }] }` |
| `toast` | `{ kind: info \| success \| error, text }` |
| `session:reset` | `{}`. The connection closed. |

Page to C++, as `callEvent(name, JSON)`. It is accepted only from the page's
own origin:

| name | payload | effect |
| --- | --- | --- |
| `ui:ready` | none | activates the web UI and sends state and chat history |
| `ui:alive` | none | heartbeat, once a second |
| `ui:screen` | `"menu"` \| `"game"` \| `"hidden"` | acknowledges the painted screen |
| `menu:connect` | `{ nickname, host, port, password }` | validated, then queued for the next client tick |
| `menu:disconnect` | none | disconnect (also works from the pause screen) |
| `menu:play` | none | enter the server's mission |
| `app:quit` | none | posts `WM_CLOSE`; the existing exit path closes in order |
| `chat:send` | `{ text }` | sanitized with `Shared::Chat::SanitizeLine` (128 code points), then sent |
| `chat:close` / `pause:close` | none | give the keyboard back to the game (Esc is handled by the client) |
| `servers:favorite` | `{ host, port, name, favorite }` | edits the favorites |
| `servers:forget` | `{ host, port }` | removes a recent entry |
| `settings:save` | `{ chatFadeSeconds, chatScale, uiScale, chatTimestamps, filmGrain, reduceMotion }` | filtered, clamped and stored |

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
and open `/#menu`, `/#menu-connected`, `/#game`, `/#game-chat`, `/#game-pause` or `/#game-score`.
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
* In game the mouse still turns the camera, because the camera reads
  `IGraph::Mouse_rx` directly. The in-game screens are keyboard-driven.
