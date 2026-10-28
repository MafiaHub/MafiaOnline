# Free Ride gamemode

`resources/freeride` is the default Mafia1Online gamemode. It is built to show
what the multiplayer does: players race each other through the city's real
streets, put prices on each other's heads, settle scores in private duels,
run crews and fight over their street corners, work jobs on both sides of
the law, fight over city events with the police on their tail, and keep a
career between sessions. The server owns every outcome; clients present it.

## Maps

| Map | Mission | What it has |
| --- | --- | --- |
| Lost Heaven Extended (default) | `freeride_extended` | The Freeride Extended mod: the city and the countryside in one map, with the container docks and Lucas Bertone's garage. Seven districts, ten race routes, the bootleg run. |
| Lost Heaven | `freeride` | The stock city. Three districts, five race routes. |
| The Countryside | `freekrajina` | The stock countryside. Four districts, three race routes, no shops. |

At start the server moves from stock `freeride` (or its night map) to the
default map, `freeride_extended` unless `FREERIDE_MAP` names another. A map
that is not installed is skipped. `/map` (or F2 → City → Map) lets players
vote; a majority of those in town switches the map after 15 seconds, at most
every five minutes.

Lost Heaven Extended needs the mod packed into `mods/`, from an extracted
Freeride Extended 3.1 folder:

```bash
python3 tools/package_mod.py "<extracted mod folder>" mods/freeride-extended.zip --mission freeride:freeride_extended
```

Clients download it when they join. Each map's districts, landmarks and
routes are in `server/lib/maps.js`; its road graph is `server/data/roads_<map>.js`,
exported by [`tools/export_road_graph.py`](../tools/export_road_graph.py) from
the stock `a1.dta` (`--mission FREEKRAJINA`) or from the mod's loose
`missions/FREERIDE/road.bin`. The Extended map's city districts use its own
`emeth_*` frames; its countryside districts are the stock `FREEKRAJINA` frames,
whose ground the mod's countryside shares at the same heights. Bertone's
garage is the crossing beside the mod's `sector moto2` frames that carry his
name, and the container docks the crossing among its new container frames.

Run the server with this project directory as its working directory. It
discovers the resource and keeps everything it saves in one SQLite database,
`data/freeride/freeride.db` (the server's built-in `node:sqlite`; see
`server/lib/store.js`), which is not tracked. Player records are rows of the
`players` table, with name, money, experience, kills, deaths and last-seen
time as columns for queries; crews, races, turf, speed traps, interiors, the
championship and the admin list are documents in `documents`. Changes are
written within two seconds, each in its own transaction. The JSON files of
earlier versions (`profiles.json`, `crews.json` and so on) are imported on the
first start and left in place.

## Controls

| Key | Action |
| --- | --- |
| F2 | City menu: events, jobs, race lobbies, bounties, players, crews, leaderboards and help. Esc or F2 closes it; 1–7 switch tabs. |
| F1 (hold) | The mod's own list of who is in town. |
| Tab | The retail city map. |
| T | Chat. Every command below can also be typed. |
| N / B | At a shop counter: next offer / buy or accept it. |
| F9 | Car radio on or off. |
| K | Siren, in a police car. |

Every button in the city menu sends the same command a player could type, so
chat and menu share one set of server checks (`server/lib/commands.js`).

## Entering Lost Heaven

The character selector uses the stock Free Ride scene frames `emeth_1`,
`emeth_3` and `emeth_4` for Little Italy, Downtown and Hoboken. The camera aims
at a temporary animated human at the chosen point. The client reads the
actual frames, and the server offers a district only when the reported
position matches that mission's stock frame, so no client can pick its own
spawn point. Each waiting player is placed in a private virtual world, so
another player's ped never walks into the preview. Up/Down change the
district, Left/Right the character (thirteen stock models, each checked
against the native skeleton first), C turns the camera and Enter spawns. A
returning player starts on the district and character they chose last time.

A returning player is also offered "Where you left off", selected first: the
spot on this map's streets where they last stood, with the health, weapons
and ammunition and the wanted level they had. The server saves it every five
seconds while they are on the street and not busy with a race, a duel, a job
or a sentence, so those never become the place to come back to; a player
with time left to serve goes back to jail instead. After a death they come
back at their usual district.

Each life starts with a baseball bat; guns are bought at Yellow Pete's. Dead players come back where
they entered the city after five seconds (`server/lib/life.js`).

## Career

A player record belongs to a nickname on one machine (nickname plus the
framework hardware ID). It keeps cash, experience, kills, deaths, best kill
streak, race wins and records, deliveries, bounties, event wins, duel wins,
play time, owned cars, stashes found, speed-trap bests, a jail sentence still
to serve, and where the player last was. New players start with $800. Experience comes from kills, races,
deliveries, events, duels, bounties and time on the streets; levels follow
100 · n(n−1)/2 XP and carry the ranks Greenhorn, Street Runner, Associate,
Soldier, Capo, Underboss and Don. The HUD shows the level strip and each gain.

Public values (level, rank, kills, deaths, crew, current activity, bounty)
travel in each player's replicated state bag under `fr.*` keys. The scoreboard,
the player list and the world labels on every client read them from there.

## Street races

`/race` (or F2 → Races) opens a lobby on a route. Everyone in town is invited;
racers have 45 seconds to reach the start line as a driver (`/garage` works
in the lobby). The server then puts every car on a starting grid with
`Vehicle.setTransform`, repairs it, holds it there through the countdown and
releases all cars on the same tick. It checks each checkpoint against the
car's replicated position, ranks the field live and sends each racer their
place, time and standings. Checkpoints appear as beacons, labels and the
compass. Up to three races and twelve racers per race can run at once.

The pool is $100 + $120 per entrant, split 60/25/15 among the podium. A solo
run on a built-in route pays $120, at most once every five minutes. Every
record is announced to everyone; one set against rivals pays $200 more. Grid
places are drawn by lot. Leaving the driver's seat for 15 seconds, dying or
leaving town drops a racer; the race ends 90 seconds after the winner
finishes.

Built-in routes follow the stock road graph between the landmarks: Pump Loop,
Dispensary Dash, Night Pump Run, Little Italy Sprint and the Grand Tour. The
graph is `server/data/roads.js`, exported from `MISSIONS\FREERIDE\road.bin`
with [`tools/export_road_graph.py`](../tools/export_road_graph.py) (the layout
follows reM `C_roads::LoadData`); shortest paths between landmarks become
street lines, checkpoints roughly every 170 m and grid slots down the road.

Players can publish their own routes: `/race create <name>` from the driver's
seat records the start and heading, `/race cp` drops a checkpoint at the car,
`/race undo`, `/race save` or `/race discard`. A route needs at least 600 m
between its checkpoints; its grid lines up behind the start on the road the
author drove in on. Each author may keep five routes and record one every five
minutes; `/race delete <name>` removes one.

## The Autodrom and its championship

Lost Heaven Extended has the official race circuit: the mod places the stock
`MISE06-AUTODROM` track model at its `anchr` frame (−3736.52, 10.52, −1005.79),
rotated about 161.5°. Its 16-car grid and 3.2 km lap come from that mission's
`start0`–`start15` frames and the 26 trackside nodes of its `check.bin`
(type 8, linked into the racing line), transformed the way LS3D's
`S_quat::RotationMatrix` does; the mod's tunnel lights fall on the circuit's
south-west corner under that transform, and nowhere near it under the
opposite rotation.

The route book has an Autodrom Sprint (one lap) and an Autodrom Grand Prix
(three laps). Circuit races need no car and no drive out: when entries close,
every entrant on the street with a clean record is put on the grid in an
identical Speedster, and ten seconds after leaving the race is returned to
where they were.

`/championship start` (or F2 → Races) runs a season of three rounds of one,
two and three laps, with a minute to enter each round and a short break
between them. Finishes score 25-18-15-12-10-8-6-4-2-1. After the last round
the top three take $3,000, $1,500 and $750, the champion gains experience, and
the champion is remembered in the database.

## Bounties and the kill feed

Every death appears in the kill feed with the weapon or the cause. Three kills
in a row put $250 on the killer's head, $150 more for every kill after that.
A player with a bounty has a red nametag and a red label over their head for
everyone nearby; `/hunt` points the compass at the biggest one. Whoever drops
the target collects the money. A target who lives ten minutes keeps half of
it. The police add $300 on anyone at the lethal wanted level. Crewmates
earn nothing from each other.

## Duels

`/duel <player> [first-to 1-5] [stake] [colt|shotgun|thompson|bat]` challenges
someone within 80 m; they answer with `/accept`. Both move to a private
virtual world where nobody else can see or reach them, fight rounds from where
they stood with the same weapon, and come back afterwards with the weapons and
health they had before. The winner takes both stakes. Leaving town forfeits;
eight minutes without a winner is a draw with stakes returned.

## Crews

`/crew create <TAG> [name]` founds a crew with its own colour. `/crew invite`,
`/crew leave`, `/crew kick` (leader) and `/crew info` manage it; `/c <message>`
is the crew radio. Members wear `[TAG]` and the colour on their nametags and
car markers, and see each other labelled across the city.

## City events

Every nine minutes the city starts one event for everyone, placed on a street
near a random player:

- **Supply drop.** A crate beacon appears. The first player to reach it gets
  $250 and the server drops a Thompson, a shotgun, grenades and a Colt as
  native weapon pickups around it.
- **Hot car.** A rare car waits on the road, marked on every radar. Whoever
  takes the wheel is reported to the police (wanted for arrest, when the
  simulation host runs the police) and has to stop it at a landmark across
  town for $600; passengers get $150. Everyone else's compass follows the car.

`/event` shows the current one. The server console accepts
`fr event [supply|hotcar]` to start one, `fr money <player> <amount>`,
`fr say <text>`, `fr dev on|off` and `fr save`.

## Places and jobs

The four staff members are solid, client-only `C_entity` humans. Their
positions come from stock mission scene frames; the server checks distance,
life, mission generation and money before any purchase.

| Place | Stock anchor | Purpose |
| --- | --- | --- |
| Yellow Pete's doorway | `MISE19-MESTO`: `1dvere u peteho` | Buy a Colt, shotgun, Thompson or grenades. |
| The Night Pump | `FREERIDE`: `_pumpa2` | Buy up to 25 litres for a nearby car. |
| The Dispensary | `FREERIDE`: `lekarna` | Restore health with bandages. |
| Salieri's Back Window | `FREERIDE`: `Salieri_save` | Buy coffee and pie or take a courier envelope. |

The courier job pays $180 for delivering to Yellow Pete's within eight minutes
(`/courier` shows the time left). Personal cars start with little fuel so the
pump has a use. Police fines cost $100; an arrest is a jail sentence (see
below).

## Jobs

The Jobs tab of the city menu shows every one of these with its live state.

- **Export list.** Lucas Bertone wants three particular models this hour,
  chosen from the cars actually driving in this server's traffic. Steal one
  from traffic (your own car does not count) and stop it at the map's
  yard (Lucas Bertone's garage on Lost Heaven Extended): $400 each, $600 more for closing the list. `/export` shows it.
- **Police duty.** `/duty` puts on the uniform, issues a Colt, a shotgun and
  a squad car, and points the compass at the nearest wanted player or bounty
  target. Taking one down pays $150 per wanted level and clears their record;
  shooting a citizen costs the badge and a $250 fine. Officers on duty do not
  run up bounty streaks. `/duty` again clocks off and gives back the old
  clothes and weapons.
- **Taxi.** `/taxi` starts a shift in your cab, or brings one. Dispatch sends
  you to street corners; stop by the fare, then drive them across town.
  Arriving inside the tip window adds 30%.
- **Courier.** Salieri's back window, as before.
- **Bootleg run** (`/bootleg`, Lost Heaven Extended). Drive to the still in
  the hills, wait eight seconds while the crates are loaded (the police are
  tipped off), then reach Salieri's bar within ten minutes in the same car:
  $700.
- **Hold-ups.** Draw a firearm at a shop counter and `/rob`. The police are
  called at once (wanted for arrest); keep the counter for 15 seconds to take
  $250–$450 and leave wanted dead or alive. A till takes ten minutes to refill.

## Turf

Eight street corners spread across the city, chosen from the road graph and
drawn as rings, can be held by a crew. Crew members standing on an open or
rival corner with no other crew inside the ring take it in a minute; two
crews in one ring contest it. Each held corner pays every online member of
its crew $40 every three minutes. `/turf` shows who holds what; ownership
persists.

## Around town

- **Cash stashes.** Thirty stashes lie at the roadside across the city. Each
  shows only within 45 m, pays $100 once per player and is remembered; all
  thirty pay a $2,500 bonus. `/stashes` shows your count.
- **Speed traps** sit at the middle of the six longest roads. Driving through
  one records your speed against your best and the server record; a new
  record is announced and pays $150. `/speedtraps` lists them.
- **Dice.** `/dice <player> <bet>` bets someone within 15 m $10–$1,000; the
  server rolls two dice for each and the pot goes to the higher roll.

## Hospital and jail

On Lost Heaven Extended the dead wake up at the doors of Lost Heaven Hospital,
the mod's `9medic` building (placed by its `cache.bin` at (−759.22, 11.56,
778.99); its doors open beside the stock Dispensary counter), and the doctors
bill $50. The mod builds the hospital as an outer shell with doors and
windows, so it has no rooms to walk into.

An arrest is a jail sentence: 45 seconds, plus 30 for each wanted level above
minor that the case reached. The police keep the prisoner's weapons, the
screen fades, and the prisoner wakes in a locked cell of the county jail
(`server/lib/prison.js`). The jail is built from stock map objects 150 m under
the empty fields north of the city, at (−400, −150, 1500), more than 500 m from
the nearest road on every map, and only exists in its own virtual world
(`0x50000000`), so nobody on the street receives it. Its hall is 24 by 8.1 m
and 2.5 m high: white walls over a green dado (`xzed`), a floor of weathered
boards (`plotdrev`), a bare concrete ceiling (`plotcihly3`), bare brick and
stained plaster on the end walls (`plotcihly1`, `plotcihly4`), and bare bulbs
on cords (`2zarc`, from Norman's cell). Three 7.24 by 3 m cells line one side,
each with a front of steel bars (`plot08`), a plank bunk on chains, a toilet
and a stain on the floor (`xpostel 3`, `xv zachod1`, `xv spina2`, furniture
of the stock `MISE17-VEZENI` scene). No stock model is a tiled floor on its
own: the tiled floors in `models/` belong to whole rooms or buildings, and the
prison scene's floor is part of its mission `scene.4ds`. The warden's desk
(`stulprac`, `xzidle 01`) stands at one end; the other end is the visiting
area, with a bench, a boiler and the way out (`xlavicka`, `xbojler`,
`plotdrev7`, a steel door with an EXIT arrow). A fourth prisoner shares a
cell.

The client shows the countdown. A prisoner who gets out of the cell is walked
back within a quarter second, `/kill` is refused, and `/bail` pays $5 per
second left. The warden, a policeman the simulation host runs
(`server/lib/warden.js`, `Npc.spawn`), stands at his desk while anyone is in
the jail, and leaves 15 seconds after the last person does. When the time is
up or bail is paid he walks to the cell door (`npc.walkTo`); `walkTo` follows
the mission's pedestrian paths, which the jail lacks, so when the host fails
the walk he turns to the door from where he stands instead. He works the lock,
the barred door slides aside after 1.5 seconds, and he goes back to the desk.
Without a simulation host the door opens on its own after the same pause. The
prisoner then walks down the corridor to the EXIT arrow; the screen fades and
they stand at the map's release point (`jail.release` in `lib/maps.js`) with
the starting kit. In the city that is the street outside the gate of the stock
prison building (`vezeni.4ds`, placed by `cache.bin` at (−1105.68, −6.12,
311.77); its gate spans x −1102.31 to −1094.44 at z 296.14, facing the road),
in the countryside the crossroads. A cell door closes again once its freed
prisoner has stepped out, so a cellmate stays locked in; a freed prisoner who
has not walked out after two minutes, or who dies, is shown out. A sentence
survives a reconnect and a map change. While the jail cannot be built (no map
objects), an arrest costs $250 bail and a fresh start instead. A fine still
costs $100.

Visiting hours (`server/jail_visits.js`): in the city, an arrow outside that
gate (−1098.4, −6.0, 294.6) leads into the visiting area, on the corridor side
of the bars, where visitors see and talk to the prisoners. The guard holds a
visitor's weapons at the door and hands them back at the EXIT arrow, which
leads back to the street. Anyone the police want, or busy with a race, duel
or job, is turned away; police duty is welcome. A visitor who walks into an
open cell is put back in the corridor, and visitors open no cells.

## Buildings

Arrows by building doors lead inside (the stock `sipka` arrow, which points
down and spins about the vertical like the retail mission arrows): walk into
one, the screen fades and you arrive inside; the arrow by the arrival point leads back out with your weapons
and health. Public interiors are shared. A safehouse is private to your crew
(or to you, without one), dresses your wounds and tops up your magazines.
Doors stay shut while the police are after you, and during races and duels.

Lost Heaven Extended ships one: Sarah's apartment in Little Italy, a
safehouse whose street door (`&& futro out`) and corridor (`&& chodba`) come
from the mod's `quarters_sr.4ds`. Other interiors are not shipped, because
their street side cannot be told from the model data. Record them in development mode: stand on the street at the door facing
it and type `/entrance add <name> [public|safehouse]`, walk to where players
should arrive, face the way they should look and type `/entrance inside`.
`/entrance list` and `/entrance delete <name>` manage them; they are saved to
the database and work at once.

## Getting together

`/goto <player>` asks someone for a ride; when they `/accept`, you arrive next
to them, or in a free seat of their car, with your weapons and health. It is
refused while wanted, in a fight or busy with an activity. `/pm` sends a
private message, `/spectate <player|off>` watches through someone's camera
and `/players` lists who is in town. Everyone's HUD lists who is talking on
proximity voice.

## Cars

Cars are bought, not spawned. Each map has a car lot on the road beside a
landmark (the pump on Lost Heaven, Bertone's garage on Lost Heaven Extended,
the lowlands in The Countryside): `/dealer` points the compass there
and, on the lot, opens the catalog with native model previews and prices,
from $450 for a Model T to $9,000 for the racing cars. Police cars are not for
sale. A garage holds three cars (`server/lib/cars.js` has the prices).

One owned car is out at a time, marked on the radar in your crew colour and
locked: `/lock` toggles it, and anyone outside your crew is pulled out of the
driver's seat. `/garage` has a car brought to the nearest road ($75 away from
the lot, once a minute), `/park` puts it away with its damage and fuel
(`Vehicle.saveState`), and it survives a reconnect. A wrecked car stays in the
garage and is rebuilt for 30% of its price; `/sellcar <n>` sells one at the
lot for half. `/repair` costs $150, or $50 within 25 m of the pump or
Bertone's, `/flip` puts a car back on its wheels and `/engine` starts or
stops it. Traffic cars can still be taken from the street.

## Money

Jobs pay best (see above). `/pay <player> <amount>` hands cash to someone
within 6 m, and every player on the street gets $30 for odd jobs every ten
minutes.

## Admins

Admins are listed by player record in the database; the server
console manages them with `fr admin add|remove|list <player>`. `/car` gives
an admin the full motor pool, or spawns a model directly with
`/car <model.i3d>`.

## The living city

Free Ride plays by day; the night map gives way to the default map. A
two-hour weather front, chosen from the date, brings rain,
or sometimes snow in winter, that grows and fades over the front. City music
is off; the car radio plays the
[Jazz Club Bandstand](https://www.internet-radio.com/station/bandstand/) live
stream while you drive.

Broken booths, hydrants and bins stay down for everyone until the city crew
comes round five minutes after the first damage; `/props` shows the damage
and `/repairprops` calls the crew at once. `/doors` and `/door ...` control
the synchronized stock doors near you.

## Development commands

Start the server with `FREERIDE_DEV=1`, or type `fr dev on` in the server
console, to enable `/dev weapons|fists|drop|model|wanted|hud|opacity|carstate|
cardamage|jail|object|money` for combat, car, HUD, jail and map object testing
(`/dev jail [level]` locks you up at once; `/dev object <model.4ds>` places a
map object four metres ahead, `off` removes it), and `/entrance` for
recording interiors. They are off by default. The server console runs any of
them on a player with `fr as <player> <action> ...`, whether or not
development mode is on.

## How it is put together

| File | Role |
| --- | --- |
| `server/main.js` | Player lifecycle, profiles on join, the admin motor pool, `/repair`, `/help`, menu requests, console commands. |
| `server/dealership.js`, `economy.js` | The car lot, the garage and car locks; `/pay` and the street wage. |
| `server/lib/` | Shared rules: the database (`store.js`), commands, profiles and ranks, respawn and activities, invitations, notifications, nametags, the road graph, admins, car prices, and the county jail building and its warden (`prison.js`, `warden.js`). |
| `server/selector.js`, `ambience.js`, `shops.js`, `police.js`, `props.js`, `doors.js` | Entering the city, clock and weather, shops and courier, police outcomes, prop repair, the door lab. |
| `server/races.js`, `combat.js`, `duels.js`, `crews.js`, `world_events.js`, `social.js` | Races, bounties, duels, crews, city events, rides, messages and dice. |
| `server/export.js`, `police_duty.js`, `taxi.js`, `robbery.js` | The jobs. |
| `server/turf.js`, `stashes.js`, `speed_traps.js`, `interiors.js` | Turf, cash stashes, speed traps and building entrances, each per map. |
| `server/map_vote.js`, `bootleg.js`, `lib/maps.js` | The default map, map votes, the bootleg run and the map registry. |
| `server/championship.js`, `jail.js`, `jail_visits.js`, `hospital.js` | The Autodrom championship, jail sentences, jail visits and hospital respawns. |
| `client/hud.js`, `client/hud.html` | One transparent web view for toasts, feed, activity cards, race panel, level strip and the city menu; it also arbitrates the single native compass by priority. |
| `client/markers.js` | Beacons, checkpoint and crate markers, door arrows, turf rings, bounty and crewmate labels. |
| `client/selector.js`, `shops.js`, `car_picker.js`, `radio.js`, `police.js`, `jail.js`, `main.js` | The selector, shop staff, the car lot, garage and motor pool, radio, police HUD, the jail countdown, fades and race countdown. |

Gameplay requests use `Events.emitServer` and are checked again on the
server; the server answers with `player.emit` or `Events.emitAllClients` and
state bags. For the complete contracts, see the
[server API](server_scripting_api.md) and [client API](client_scripting_api.md).
The shop positions were read from the stock `a1.dta` scene data with
[`tools/inspect_stock_dta.py`](../tools/inspect_stock_dta.py); neither tool
modifies the installation.
