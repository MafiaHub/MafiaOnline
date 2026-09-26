# LHRP — Lost Heaven Roleplay

A small TypeScript gamemode for Mafia1Online: a city flyover, account registration
and login, a random character on first entry, and a saved place on return.

## Build and run

From this directory, with Node.js 25 or newer:

```sh
npm ci
npm run build
```

The build writes a standalone server working directory to `../../build/real-life`.
Run the native `Mafia1OnlineServer` executable from that directory:

```sh
Mafia1OnlineServer --config server.example.json
```

The example selects `freeride_extended` from the local
`mods/freeride-extended.zip` at the project root. The build requires this ZIP and
copies it into `build/real-life/mods/`; see [server mods](../../docs/server_mods.md)
for packaging instructions. The existing mod downloader transfers and mounts its
assets before the client loads the map.

Connect with the current Mafia1Online client, including mod integration, the shared
CEF cursor and scripted camera support. The resource owns spawning, so run it
without the sample gamemode.

Accounts live in `build/real-life/data/accounts.sqlite`, outside downloadable
resources. Builds preserve this directory. When deploying elsewhere, copy the
generated server directory with its `mods/`, `resources/`, config and `data/`.

## Development

```sh
npm run dev
npm run format
npm run format:check
npm test
npm run test:ui
```

The browser preview shows the interface and its validation; authentication and the
city camera run inside the game. Playwright uses its installed Chromium, or the
executable supplied through `LHRP_CHROMIUM_PATH`.

Prettier formats TypeScript, JSON, CSS and documentation. ESLint requires braces
and blank lines around control flow and between logical sections.

## Structure

| Directory    | Responsibility                                                          |
| ------------ | ----------------------------------------------------------------------- |
| `src/client` | CEF lifecycle, game events, Catmull–Rom flight and transitions          |
| `src/server` | Authentication, SQLite storage, spawning and position saving            |
| `src/shared` | Message contracts, validation and English/Czech translation JSON        |
| `src/ui`     | Preact components, StyleX design tokens and modal styles                |
| `tests`      | Storage, authentication lifecycle, spline and browser interaction tests |

All interface text belongs in both `src/shared/locales/en.json` and `cs.json`.
Translation keys are checked by tests. Fonts are bundled locally, including Czech
characters. The shared UI components and StyleX tokens provide the starting point
for future dialogs.

## Current behavior

- Registration assigns a random stock model and spawns immediately. Later logins
  keep that model and restore the saved position and heading in the same mission.
- Passwords use salted scrypt hashes. SQLite statements are parameterized.
  Authentication attempts and concurrent password hashing are limited.
- Select “Sign me in automatically on this device” when signing in or registering
  to remember the account for 30 days. Reconnecting signs in automatically using
  a random token stored in the CEF profile under this server's identity. Passwords
  are never saved in browser storage; SQLite stores only the token's SHA-256 hash.
  `/logout` saves your position, revokes this device's token and returns to login.
  Expired or revoked tokens fall back to password sign-in.
- Unauthenticated players stay unspawned in separate virtual worlds. An account
  can be online once. Async authentication checks that its session is still live.
- Position saves on disconnect, every 30 seconds, and when the resource stops.
  Abrupt termination can lose the last checkpoint interval. Dead players return
  at the mission spawn after five seconds.
- The camera replays recorded positions, viewing directions and roll on open,
  constant-speed Catmull–Rom paths, fading between shots. The default login sequence
  uses the two recorded 20-second shots in `src/shared/login-camera.json`. Missions
  without matching recordings hold a stationary view. Pause and reduced-motion
  stop camera motion.
- Native scripted camera locks load the wider city cache. This cinematic uses a
  550-metre far plane; unlocking restores the previous cache and projection.
- The multiplayer client owns one CEF cursor layer above all UI. It uses the same
  DirectInput coordinates as browser hit testing. Gamemodes need no cursor code.

## Ranks and admin commands

Accounts have a persisted `user` or `admin` rank. Existing accounts migrate to
`user`; registration never grants admin. Assign or remove admin access from the
**server console**, using an existing account name:

```text
setrole YourUsername admin
setrole YourUsername user
```

Changes apply immediately, including to connected players. Permissions are checked
on the server against SQLite. Add future ranks and their capabilities in
`src/shared/permissions.ts`.

| Command                            | Action                                                                           |
| ---------------------------------- | -------------------------------------------------------------------------------- |
| `/car [model.i3d]`                 | Spawn a radar-marked car and enter the driver's seat; defaults to `taxi00.i3d`   |
| `/kick <player> [reason]`          | Save and disconnect a player                                                     |
| `/ban <player\|username> [reason]` | Permanently ban an account, including offline accounts                           |
| `/unban <username>`                | Remove an account ban; also available as `unban` in the server console           |
| `/follow <player>`                 | Spectate the player's camera, including while they are driving                   |
| `/follow off`                      | Return to your own camera                                                        |
| `/tp <player>`                     | Teleport beside a player, leaving your vehicle and preserving health and weapons |
| `/weapon <id> [ammo] [player]`     | Give and equip a weapon; defaults to yourself                                    |
| `/savepos <comment>`               | Append your live position and full rotation to `data/saved-positions.jsonl`      |
| `/weapons`                         | List supported weapon IDs and names                                              |

Player targets accept an exact username (case insensitive) or `#ID` from `/players`.
`/goto` aliases `/tp`; `/giveweapon` aliases `/weapon`. Everyone can use `/help`,
`/players` and `/logout`. `/help` lists property commands and, for admins, the
property/interior editor commands too. Feedback and successful player-join chat
messages are translated separately for each recipient into English or Czech.
Join messages are sent once per connection after successful authentication and spawn.

Weapon ammo means total rounds, split between the magazine and reserve; the default
is four magazines for guns, one item for melee weapons and throwables. Each admin
keeps one current spawned car. Replaced cars and cars left after logout or disconnect
are removed once empty; occupied cars remain until everyone exits.

`/savepos my comment` captures the client's live native world position and full
quaternion. On foot it records the player; in any seat it records the vehicle,
including pitch and roll. The server rechecks admin permission, mission, world,
and vehicle before appending the result. Each line is valid JSON with `comment`,
`position`, `rotation`, `kind`, `model`, `mission`, `virtualWorld`, `account`, and
`savedAt`. Builds preserve this file. The exported server helper
`appendSavedPosition(file, record)` is available for other recording features.
This command requires client/server 1.2.0 or later.

Bans persist across restarts, block password and automatic sign-in, and revoke all
remembered tokens for that account. Unbanning requires signing in again. These are
account bans; they do not ban an IP address or prevent creating another account.

## Record the login camera

Start your local server with `LHRP_CAMERA_EDITOR=1`, then connect to it through
`127.0.0.1`. The recorder is available on the login screen before spawning. It is
disabled by default, and the server accepts saves only from a loopback connection.

```sh
LHRP_CAMERA_EDITOR=1 Mafia1OnlineServer --config server.example.json
```

| Control                    | Action                                      |
| -------------------------- | ------------------------------------------- |
| F4                         | Open the recorder / return to login         |
| WASD                       | Fly forward, backward and sideways          |
| Q / E                      | Down / up                                   |
| Arrow keys or right-drag   | Look around                                 |
| Space + mouse left / right | Roll the camera                             |
| Shift / Alt                | Fly faster / slower                         |
| F2                         | Record position, viewing direction and roll |
| F3                         | Keep the current spline and start a new one |
| F5                         | Replay / stop the current spline            |
| F6                         | Save all complete splines                   |
| Backspace                  | Remove the last point in the current spline |

Record at least two points per shot. Set its duration with the Seconds field.
The camera can pass through walls; preview each path to check framing and culling.
Each path stops at its final recorded point, then fades to the next shot.
Roll is stored in radians and interpolated with the viewing direction. Recordings
without roll remain compatible and play level.

F6 writes `build/real-life/data/camera-paths.json`. It survives rebuilds and server
restarts and overrides the bundled login sequence for the same mission. A fresh
server, or a saved recording for a different mission, uses
`src/shared/login-camera.json`, included in both client and server builds; the
client starts at its first recorded point as soon as the login screen opens.
New connections automatically receive saved paths for the matching mission.
Copy the saved JSON alongside the account database when deploying overrides. Unsaved
points remain only in the current client session. Limits are 32 splines, 128 points
per spline and 3–180 seconds per shot.

The account flow currently uses the multiplayer event transport. Password hashing
protects stored credentials; this resource does not add transport encryption.

## Houses, garages and public interiors

Use a client and server with the model-animation and vehicle snapshot APIs
(version 1.1.0 or later). House ownership, locks, balances and garage saves live in
`data/accounts.sqlite`. An account can own several houses. Houses start locked;
`/house_unlock` admits visitors, and `/house_lock` closes entry to visitors.
Players already inside can always leave. Stand within two metres of an entrance
and press **Enter**. Buying is explicit: `/house_buy <id>` at the entrance.
Unowned houses show a small rotating `9dumch4.i3d` model with a complete roof and their price; other doors
use the stock animated green `sipka.i3d`. Only nearby markers are loaded.

The editor commands below require the persisted admin rank. Each admin has their
own last recorded/selected house; another admin's work cannot change that selection.
Positions are server-observed. Templates must be locations in the **currently
loaded mission**, with existing interior geometry; these commands do not load a
second mission or create rooms.

| Command                                 | Action                                                                                                                                |
| --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| `/house_entry`                          | Record a new house at your current on-foot position and heading                                                                       |
| `/house_entry <house>`                  | Add another entrance to that house                                                                                                    |
| `/house_select <house>`                 | Select an existing house and show its configuration                                                                                   |
| `/house_garage [house]`                 | While stopped in the driver's seat, attach a parking slot to the selected house using the car's position and full quaternion rotation |
| `/interior_exit <name>`                 | Record a reusable interior's arrival point and exit arrow at your position; names use letters, digits, `_` and `-`                    |
| `/house_set <house> <price> <interior>` | Set a positive dollar price and an existing interior template                                                                         |
| `/interior_entry <name>`                | Record a public entrance to a template, for shops and other public buildings                                                          |
| `/interior_remove <entrance>`           | Remove a public entrance by the ID printed when recording it                                                                          |
| `/interior_go <name>`                   | Preview a recorded template; `/leave` returns to your previous exterior position                                                      |
| `/interiors`                            | List templates and show editor help                                                                                                   |

For example, record `/house_entry` at the front door, park a car and run
`/house_garage`, walk into the chosen existing interior and run
`/interior_exit apartment`, then `/house_set 1 4778 apartment`. To make that room a
public shop as well, use `/interior_entry apartment` at a public shop door.
There are no invented default city coordinates: record actual entrances and
interiors in the chosen map before making a house available to buy.

Definitions are plain text, one JSON object per line:

- `data/houses.jsonl`: house IDs, mission, entrances, price, template and parking slots.
- `data/interiors.jsonl`: reusable interior locations and headings.
- `data/entrances.jsonl`: public entrance IDs and their destinations.

Updates write a temporary file and atomically rename it. Stop the resource before
manual editing; malformed files fail loading instead of silently losing records.
Do not renumber house IDs or parking slots after purchases/saves. Back up these
files together with the database. They survive builds and server restarts.

Each house uses virtual world `0x10000000 + house ID`; each public template uses
`0x20000000 + interior ID`. The physical room may be reused, while occupants of
different houses remain separated. Multiple public entrances to the same template
share one public interior. Exit returns each visitor to their own entrance.
Logout/reconnect restores the exterior location, so an interior position is never
restored in the public city. Death respawns in the public city. `/leave` provides
an escape if an admin removes an entrance while someone is inside.

Each parking slot holds one car. As the owner, stop there without passengers and
use `/garage_save [house] [slot]` (slot numbering starts at 1), or press Enter at its
marker. Saving replaces that slot's previous car; an occupied previous car cannot
be replaced. The vehicle stays usable after saving. Its last **explicit save** is
restored after destruction, submersion, a fatal fall/out-of-world position, removal,
or a server restart. Recovery waits ten seconds after loss, waits for occupants to
leave, and waits for players/vehicles to clear the parking slot. It also needs a
mission-ready player in the city to run native vehicle simulation.

The native snapshot includes model, fuel/capacity, engine/gearbox/body/tank damage,
light/window/zone damage, tyre/wheel condition, missing-part flags, mesh deformation,
burn state, lights/siren/horn, opacity, radar color, seats, gear, steering and door
positions. Position comes from the parking slot; velocity, held pedals and occupants
are not restored. Loose debris actors are not recreated. Saving waits for complete
native condition/mesh reports; it preserves the server's latest accepted condition.
A burning saved car retains its burn countdown. Admin-spawned cars saved in a garage
become persistent and are no longer deleted by admin command cleanup.

House purchases debit the persisted balance and assign ownership in one SQLite
transaction. The default house price is $4,778; see [1930 economy references](economy-1930.md).
The initial $25 balance is a provisional gameplay starting point, not a historical
claim about personal savings. Use the server console `setmoney <username> <dollars>`
for setup/testing; it also updates connected players. This module uses whole-dollar
balances and does not introduce wages, jobs, mortgages, rent, or a full economy.
