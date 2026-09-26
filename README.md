# Mafia1Online

This repository is checked out inside the [MafiaHub Framework](https://github.com/MafiaHub/Framework)
at `code/projects/mafia1online`. Use the Framework commit recorded in
[`FRAMEWORK_PIN`](FRAMEWORK_PIN) to reproduce a build. The Framework CMake
project discovers this folder automatically.

This project targets the 32 bit Mafia `Game.exe` identified by SHA-256
`303eb95ee2de3433511ce0cb518921dcb96b62b0f64ebfcc436723ff5083f298`.
The launcher uses Steam app `40990` for automatic discovery, or accepts a
manually selected copy in either a flat or nested game folder. Steam itself
is not required when that exact executable is supplied. The launcher checks
the hash before mapping it and requires the image at `0x400000`. Other GOG or
disc executables need a separate native address and hook audit before they
can be supported.
The Mafia 1 SDK is a separate, Framework independent target under `code/sdk`;
no reM object or library is linked. The exact image contract and the audited
fixed addresses live there. Shell hooks live in `client/src/core/boot`, while
future player and car hooks belong to their own feature folders.

## Current implementation

The Windows client and launcher have a lifecycle shell. Audited hooks attach
Framework updates to the native game tick, clear generation checked native
handles before mission destruction, and shut the client down before
`CloseSystem` destroys native systems. The three startup logos and intro video
are skipped. A project-owned Mafia menu supplies nickname, server address,
port, password, connection status, Play and Exit through the native menu
renderer. Connect and Disconnect are mutually exclusive, and Play appears
only after the server selects a mission. The password edit is masked while
drawn.

The server requires a stock `mod.mission`. It replicates the selected mission
and generation to every client. Play closes the native menu, opens the selected
mission, loads its collision tree, initializes the game and enters the retail
game loop. Each client reports Ready only after those calls succeed; errors
leave that client unready. `World.changeMission(name)` resets the generation,
despawns server cars and players, and requests the new load. Server scripts may
call `World.getMission()`, `World.getMissionGeneration()` and `World.isReady()`.

Connected players have server-owned replicas with nickname, health, life and
mission generations, and timestamped transforms. The client currently binds
the stock local player after the native mission loads and sends its pose to
the server. A separate native pose report lets the sample resource choose a
matching initial spawn. Server scripts control spawn, respawn, despawn and
health through `Players`, and car replicas through `Cars`. A sample resource
under `resources/sample-gamemode` spawns ready players. `/car` opens a catalog
with native model previews and seats the player in the chosen car. `/car
<stock-car.i3d>` can spawn a model directly, and `/model [stock-human.i3d]`
changes the player model. In a loaded mission, press T to open the
native chat input, Enter to send and Escape to cancel. Incoming chat uses the
game's own HUD console; the stock lives display receives the local player's
authoritative health. Run the server with this project directory as its
working directory so it discovers that resource.
The server resource API, including vehicle seat, engine, damage, death and
mission callbacks, is documented in
[`docs/server_scripting_api.md`](docs/server_scripting_api.md).
The [Mafia Online Docs](https://github.com/MafiaHub/MafiaOnlineDocs) repository
builds the generated API reference alongside the authored guides.
The [sample Free Ride guide](docs/sample_freeride.md) covers the day/night
missions, character selector, shops, weather, car radio, and synchronized doors.

This is still a development build. Remote player replicas now create visible
native humans with movement animations; car replicas create native vehicles
that can be entered and tick their physics outside the camera view. The local
two-client smoke test confirmed those paths, native chat and the HUD health
display. The server controls car engine state and replicates seat outcomes,
while native door animation, late-join occupancy and in-car shooting are under
integration testing. Full weapon, damage, death, vehicle terminal behavior,
bounded correction, a graphical fill health bar and multiplayer loss testing
remain release gates.

The main menu, chat, pause screen and player list are a CEF web UI
(Preact + StyleX, `ui/`). It is composited into the game's Direct3D 8 frame
through the Framework's D3D8 backend. Build it with `tools/build_ui.sh`; the
client build runs that script and copies the result to `bin/ui`. When CEF or
the page is unavailable, or with `MAFIA1ONLINE_NATIVE_UI=1`, the retail main
menu keeps only Exit, quick join connects, and the native HUD chat is used. See
[`docs/ui.md`](docs/ui.md).
The launcher does not write `steam_appid.txt` or a persistent launcher config;
the client places its asset cache next to the mod rather than in AppData.
On an unhandled launcher or game exception, the launcher writes a `.dmp` file
under its `logs` folder and records the path in `Mafia1Online.log`. Keep the
matching `Mafia1OnlineLauncher.pdb` and `Mafia1OnlineClient.pdb` from the same
build when analyzing the dump. Dumps include indirectly referenced memory
and the process memory map.
The client starts the native renderer in a 1024 by 768 window and keeps
running when its window loses focus. Both settings are applied in-process
during graphics initialization; the original game setup fields are restored
before the native registry save, so the mod does not persist these overrides.
For an isolated game copy, set `MAFIA1ONLINE_GAME_ROOT` to the directory that
contains either `Game.exe` directly or a nested `Mafia/Game.exe`. The launcher
still checks the exact `Game.exe` hash before loading it.

## Optional quick join

The client build copies `config/client.example.json` to
`builds/build-32/bin/config/client.example.json`. Copy that file to
`builds/build-32/bin/config/client.json` and set `quickJoin.enabled` to
`true`. Set `nickname`, `host`, `port`, `password` and the zero-based
`profileIndex` of an existing Mafia profile. This file stays inside the
repository and is ignored by Git. Quick join selects that profile during
native initialization, connects once, then enters the server mission when it
arrives. If the configured index is missing, the retail profile picker opens
so an existing profile can be selected. `MAFIA1ONLINE_CLIENT_CONFIG` may point
to a different client JSON file for a separate local instance.

Mafia1Online reads profiles but blocks profile creation, deletion, saving and
save-game writes during its sessions. The profile picker is skipped: it uses
`quickJoin.profileIndex` (default 0, else the first profile). If no profile
exists, startup displays an error and exits; create a profile in the original
game before launching the mod. Omitting `client.json` or setting `enabled` to
`false` leaves connecting to the web UI main menu.

## Build

From the Framework root, use the canonical wrapper:

```sh
bash builds/build.bat Mafia1OnlineLauncher 32
bash builds/build.bat Mafia1OnlineClient 32
bash builds/build.bat Mafia1OnlineServer 64
bash builds/build.bat Mafia1OnlineServer linux64
```

The client and launcher build only for Windows x86; the server builds for
Windows x64 or Linux x64. For server configuration, copy
`config/server.example.json` to a repository-local `server.json`, or pass it
with `--config`. `mod.mission` is required and must be one of the 77 lowercase
gameplay mission directory names in
[`code/shared/features/world/mission_catalog.h`](code/shared/features/world/mission_catalog.h).
The catalog was checked against the installed Steam `a1.dta`: every listed
mission has a collision tree. The three menu-only scenes without one are
excluded. This validates startup configuration; live testing of all names is
still required.
Set the optional top-level `password` key to require a password when clients
connect. The server publishes only whether a password is required; it does not
send the password to clients.

## Client ZIP

After building the 32-bit launcher and client, run
`python3 code/projects/mafia1online/tools/package_client.py` from the Framework
root. It creates `builds/releases/Mafia1OnlineClient-<VERSION>-windows-x86.zip`.
The archive contains the launcher, client, runtime DLLs, CEF resources, UI,
license notices and a sample configuration. It excludes logs, caches, tests,
debug symbols, local settings and the original game. Extract it to a writable
folder and run the launcher; the launcher resolves its files from its own
folder even when a shortcut has a different working directory. A matching
Mafia executable and an existing Mafia profile are required. The
example `127.0.0.1` server address only reaches a server on the same machine.

## Two-client local test

On this Linux/Hyprland test machine, `tools/run_two_clients.sh` launches two
isolated Wine clients and places their 1024 by 768 windows side by side on
`DP-1` workspace `8`. It uses only repository-local writable paths under
`builds/runtime`. The script expects the isolated game copy at
`builds/runtime/game/Mafia/Mafia/Game.exe`, prepared Wine prefixes at
`builds/runtime/prefix` and `builds/runtime/prefix-second`, and quick join in
`builds/build-32/bin/config/client.json`. It gives the second client the
nickname `Smoke2` through a separate repository-local configuration, so the
players are distinguishable during replication tests. The two windows are
fully opaque, and only the focused game's native and web audio streams are
audible; switching to another application mutes both test clients. Start the server first,
then run:

```sh
code/projects/mafia1online/tools/run_two_clients.sh
```

For a late-join check, close the second client while the first remains in the
mission, then run the script with `--late-join`. It reconnects the second
repository-local prefix and restores the two-window layout.

For a repeatable impaired-network check, run the local UDP proxy in another
terminal before starting the clients:

```sh
python3 code/projects/mafia1online/tools/udp_test_proxy.py --listen-port 27017 --server-port 27015 --one-way-ms 50 --jitter-ms 10 --loss-percent 2
MAFIA1ONLINE_TEST_PORT=27017 code/projects/mafia1online/tools/run_two_clients.sh
```

The proxy gives each client a distinct upstream socket and affects both
directions. It changes no system network settings. The vehicle case matrix is
in [`docs/vehicle_verification.md`](docs/vehicle_verification.md).

`tools/two_client_smoke.mjs` automates the repeatable part of this check. It
requires Node.js, `hyprctl`, `xdotool`, `grim`, a running server with its local
Node inspector on port 9229, and no existing test windows for a new run. It
starts two clients, checks server-side spawn, movement, Colt ammo consumption, `/car`,
`/engine`, and late join, then saves screenshots and a pass/fail JSON under
`builds/research`.
Use `--impair` to start the 100 ms RTT, 10 ms jitter, 2% loss proxy as part of
the run. Use `--attach --no-late-join` to test already-running clients without
closing either. Clients launched by the script are closed at the end; use
`--keep-clients` to leave them open for inspection. The screenshots are evidence
for visual review; this script does not assert native glass, wheel, or mesh
parity from pixels. The report hashes the supported game executable, client
DLL, running server image, and server file; a stale running server stops the
test unless `--allow-server-mismatch` is supplied for debugger sessions.

```sh
node code/projects/mafia1online/tools/two_client_smoke.mjs
node code/projects/mafia1online/tools/two_client_smoke.mjs --impair
```

The Wine x87 regression test builds with
`bash builds/build.bat Mafia1OnlineWineFpuTests 32`. Run
`builds/build-32/bin/Mafia1OnlineWineFpuTests.exe` through Wine in a test prefix.
It checks the `nearbyint` replacement across rounding modes, precision settings,
exception flags, signed zero, subnormals, infinities, NaNs and x87/SSE state.
The optional `--reproduce-wine` argument calls the original Wine CRT instead;
on affected Wine versions it deliberately aborts with the `_setfp` assertion.
V8's optimizer triggered this assertion during gameplay when another x87
exception flag was already set. The client replaces `ucrtbase!nearbyint` only
under Wine, preserving the caller's rounding mode and inexact flag.

Framework transports the nickname entered in the native menu in its connection
identity. Mafia1Online copies that name into the server owned player replica;
native nametag rendering is still needed to show it above a player in game.

Before adding any native call, hook, or patch, record its retail bytes,
convention, lifetime, callers, side effects and unload behavior in
[`docs/native_contract.md`](docs/native_contract.md).
