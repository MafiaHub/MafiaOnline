# Free Ride source

The TypeScript source of [Free Ride](../../docs/freeride.md), the default
Mafia1Online gamemode. `npm run build` type-checks it and bundles it into
`resources/freeride`: `server.js` (Node, CommonJS), `client.js` (the clients'
sandboxed V8), the HUD pages and the resource's `package.json`. That output is
committed, like `resources/ui`, so the server and `tools/release.py` ship it
without a Node toolchain. Never edit `resources/freeride` by hand.

```sh
npm install
npm run build          # typecheck, then bundle into resources/freeride
npm run build:only     # bundle without the typecheck
npm run lint           # eslint
npm run format         # eslint --fix and prettier
npm test               # unit tests (tests/*.test.ts)
node scripts/build.mjs --check   # fails if resources/freeride is stale
```

## Layout

- `src/server/index.ts`: the server entry. It imports every module in the
  order their event handlers register.
- `src/server/<feature>.ts`, or `src/server/<feature>/` for the large ones: one
  feature each (a job, an activity, a place). A feature folder has an
  `index.ts` that is its only public face; other modules import that.
- `src/server/lib/`: what features share. Start here before writing a helper:
  - `lifecycle.ts`: the typed `events` bus, `onStart`/`onStop`, `every(ms, fn)`
    (an interval that stops with the resource), `afterDisconnect`,
    `onMissionChange`.
  - `log.ts`: `log.info/warn/error` (tagged with the resource),
    `log.describe(caught)`, `log.Once` (a repeating failure logged once).
  - `util.ts`: `Point`/`Placement`, distances, headings (`headingFrom`,
    `ahead`), formatting (`formatMoney`, `plural`), `playable`/`onStreet`,
    `playerById`/`vehicleById`/`connected`, `emit`/`broadcast`/`onClient`.
  - `players.ts`: `PlayerMap`/`PlayerSet` (per-player state dropped after the
    player disconnects, optionally on a mission change), `SentCache` (skip
    resending an unchanged payload).
  - `payload.ts`: `fields`, `isFiniteNumber`, for checking client payloads.
  - `timers.ts`: `Delay`, one pending one-shot call that the resource's stop
    cancels (or, with `runOnStop`, runs: a save).
  - `cooldowns.ts`: `Cooldown`, a wait per player between uses of something.
  - `login.ts`: the `fr:login` local event (`onLogin`, `announceLogin`).
  - `console.ts`: server console commands (`command`) and their actions
    (`action`); a module adds its own action to a shared command like `fr`.
  - `commands.ts`: the one table of chat and F2-menu commands.
  - `menu.ts`: the F2 city menu's sections, each provided by its module.
  - `devmode.ts`: whether development mode (/dev commands) is on.
  - `admin.ts`: server admins, granted from the console.
  - `store.ts`: the SQLite database (player rows, one JSON document per
    module).
  - `profiles.ts`: the persistent player record (cash, experience, rank,
    career counters) and its public values.
  - `accounts.ts`: usernames, scrypt passwords and remember-me tokens.
  - `rewards.ts`: `reward`, any of cash, career counters, experience and a
    feed line, in that order (a level-up comes after the money).
  - `life.ts`: the one exclusive activity per player, kits, respawns.
  - `notify.ts`: toasts, the feed, HUD cards, compass, markers, `crewMarkers`,
    `arrow` (the stock arrow marker over a door).
  - `nametag.ts`: nametags (crew tag and colour, police, bounties).
  - `invites.ts`: pending requests between players (/accept, /decline).
  - `challenges.ts`: one player taking on another: `opponent`, `challenge`
    (an invite) and `collectStakes` (refunded when one cannot pay).
  - `maps.ts`: the maps Free Ride plays: landmarks, districts, road graphs.
  - `roads.ts`: each map's traffic roads and what is built on them (race
    lines, grids, road points).
  - `spots.ts`: `perMap` (what a map has, built once per map), `nearest`
    (the nearest item in reach), `WalkIn` (a spot that fires once when walked
    onto and re-arms when left).
  - `cars.ts`: the stock car catalogue and prices.
  - `vehicles.ts`: `occupied`, `emptyBut`, `spawnVehicle` (full tank, owner,
    radar blip, state, a saved condition), `destroyWhenEmpty`.
  - `jobkey.ts`: the job key (G): presses, holds and the layered prompt line.
  - `interactions.ts`: `Interactions`, what the job key does for a module
    where a player stands: hints sent when they change, presses and holds.
  - `stations.ts`: job stations (sign-on arrow, parked fleet).
  - `shift.ts`: `defineShift`, everything a duty job shares (activity, uniform
    and loadout, station sign-on, HUD cleanup, disconnect/mission/stop
    teardown, the per-player tick).
  - `errands.ts`: what a job that sends a player somewhere shares: `beacon`,
    `lead` and `clearHud` (destination on the compass and map), `stoppedAt`,
    `Countdown` (loading, holding, deadlines), `spawnBeside` (a job car with
    the player at its wheel).
  - `callouts.ts`: emergency calls (ambulance, fire brigade): road spots,
    wrecks, feed names, `CallClock` (when the next call is due).
  - `npcs.ts`: scripted NPCs: `npcsAvailable`, `Character` (one NPC through
    spawn, the host placing it, its death or retirement and the mission
    change), `walkOff` (sent away and removed later), `speak` (a line on
    screen and a voice in the world).
  - `civilians.ts`: the street civilians a job puts out (models, clips, voice
    lines, `spawnCivilian`, `say`), on top of `npcs.ts`.
  - `warden.ts`: the jail's warden NPC.
  - `transfer.ts`: `Transfers`, the faded move into another virtual world
    (homes, the jail, buildings) that is skipped when the life ends meanwhile.
  - `map_objects.ts`: `MapStructure`, the build/retry/teardown of the rooms
    built from stock models under the map, and `tryCreate`.
  - `placed_objects.ts`: `PlacedObjects`, single map objects kept standing
    per key in the loaded mission and swapped when their spec changes.
  - `bar_room.ts`: the back-room bar behind Salieri's door.
  - `prison.ts`: the county jail's cell block and its doors.
  - `house_rooms.ts`: the insides of the homes.
  - `house_decor.ts`: what a home owner can buy to furnish a home.
  - `flyover.ts`: the login camera's flight over each map.
  - `spline.ts`: Catmull-Rom curves and banking for the flyover.
- `src/server/data/`: generated by `tools/export_*.py`; CommonJS with a
  `.d.ts` beside each. Regenerate, never edit.
- `src/client/`: one module per HUD feature. `src/client/lib/` is what they
  share: `runtime.ts` (typed `events`, `onServer<T>` for `fr:*` payloads,
  `onStart`/`onStop`, `log`), `page.ts` (the web-view `Page`), `panels.ts`
  (counter cards and key prompts), `color.ts`, `geometry.ts`, `player.ts`
  (local player checks), `timers.ts` (timer groups), `payload.ts` (checking
  page messages), `picker.ts`, `format.ts`.
- `src/client/*.html`: the HUD pages, copied as they are.

## Conventions

- Strict TypeScript, no `any` (client payloads are `unknown` until checked),
  no `@ts-nocheck`, no non-null `!` where a check reads as well.
- `events.on(...)` (lib/lifecycle), never the untyped global `Events.on`.
  Native event payloads are typed from the scripting API's `EventMap`.
- Per-player state is a `PlayerMap`/`PlayerSet`, not a `Map` plus a
  `playerDisconnect` handler that deletes from it. Timers are `every()`, not
  `setInterval` plus an `onStop` that clears it.
- Duty jobs are `defineShift` shifts.
- Modules export functions and types by name; no default exports.
- Comments say why, not what. Keep the existing ones that explain game
  behaviour (reverse-engineered facts, retail quirks, measured positions):
  they are the reason the numbers are what they are.
- Formatting is Prettier's (`npm run format`); lint must pass.
