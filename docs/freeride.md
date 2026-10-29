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
| Tab (hold) | The retail city map, with blips for job stations, fires and ambulance calls, your objectives (an arrow at the border when off the shown part) and your crewmates. |
| T | Chat. Every command below can also be typed. |
| N / B | At a shop or bar counter: next offer / buy or accept it. |
| F9 | Car radio on or off. |
| K | Siren, in a police car. |
| U | At a telephone booth, on police duty: call for backup. |
| G | Under a station's arrow: sign on or clock off. On fire duty: hold to spray the hose (or the truck's roof cannon from its wheel), press next to a trapped civilian to lead them out. On ambulance duty: hold next to a patient to treat them. Down and waiting for a medic: press to give up and wake at the hospital. On the tobacco farm: press on an empty plot to plant, hold to water, pick off hornworms or pick ripe leaves. At the rum still: press to fire it up, tap to vent the pressure, hold to bottle, patch or steal; press beside a character to talk, and in a car stopped at the village store to buy molasses. |

Every button in the city menu sends the same command a player could type, so
chat and menu share one set of server checks (`server/lib/commands.js`).

## Entering Lost Heaven

A joining player sees the city from the air: a camera flies a loop over the
map behind a Lost Heaven ticket with two tabs, Log in and Register. Tab moves
between the fields, Enter sends, and a wrong name, a short password or a
taken name is marked on its own field. The flight is built by the server
(`server/lib/flyover.js`, `server/lib/spline.js`) and played back by the
client (`client/selector.js`); see "The login flight" below.

- **Log in** with a name and password puts a returning player straight on
  the street, with no character choice: where they left off, or else at their
  usual district (the map's first district if they have none on this map).
- **Register** creates the account and then, once only, shows the look
  picker: the camera stops at the starting district and aims at a temporary
  animated human; Left/Right (or the arrow buttons) change the look (sixty-one
  stock street, crew and cast models, each checked against the native
  skeleton first; no nude or shirtless models, uniforms or cutscene
  variants), C turns the
  camera and Enter begins. What a player becomes after that comes from play;
  the look they spawn with is the record's `model` field, which gameplay can
  change with `profiles.setField(player, "model", file)`.
- **Remember me** (ticked by default) keeps the player logged in on that
  computer. The next start shows "Welcome back, *name*" over the flight and
  enters after five seconds or on Enter; "Not you? Switch account" (or Esc)
  logs out on that computer and shows the form.

Until they log in, players are called "Guest *id*" (the nickname they joined
with only fills in the form, and account names may not start with "Guest"),
have no crew and run no commands on a Free Ride map; logging in renames them
to the account's name, and an account can be in town only once. After a map
change a logged-in player goes straight back in. A restart of the resource
keeps the logins of the players in town for ten minutes; anyone it does not
cover is taken off the street and back to the login card. A client that
cannot show any look starts as the first one.

### Accounts

`server/lib/accounts.js` keeps the accounts in the database document
`accounts`. Names are 3 to 24 letters, digits, spaces, dots, dashes or
underscores and are unique regardless of case; passwords are 6 to 128
characters and may not be the name. A password is stored only as an scrypt
hash (N 2^15, r 8, p 1, 64 bytes) with a random 16-byte salt per account,
checked with a timing-safe comparison; an unknown name costs the same
derivation as a wrong password and gets the same answer. Attempts are
limited: five failed logins for one name from one address and twenty per
address in fifteen minutes, while a name locks for everyone only after fifty
failures in five minutes (so nobody can keep another player out); ten failed
remember tokens per address in fifteen minutes, three new accounts per
address an hour, and one request a second per player. The game connection is
not encrypted yet, so the form asks players to use a password they use
nowhere else.

A remember token is 32 random bytes the server issues at login. The server
keeps only its SHA-256, the hardware ID it was issued to and its expiry (30
days); each use replaces it with a new one, a token from another machine is
refused and thrown away, an account holds at most five, and "Switch account"
revokes it. A replaced token is remembered until its expiry: if it is ever
shown again, a copy is in other hands, and every remembered login of that
account is signed out. "Log out every other computer" on the login form, or
`/logout` in town, signs out all of them. A rate limit, a server error or the
account being in town already (which leaves the token unspent) keeps it on
the "Welcome back" card for another try; only an expired, revoked or copied
token is forgotten. The client keeps the token, never the password, in the browser
storage of its web views, per server: that storage is the CEF profile under
the client's own `cache/profiles` folder, which the Framework keeps across
launches (`gui/cef/runtime.cpp`). Every server's pages share that one
`fw://resources` origin, which is why a token is bound to the hardware ID and
works only on the server that issued it.

Player records from before accounts were keyed by nickname on one machine.
The first registration of that name *from the same machine* takes that record
over, so its cash, career, cars and admin rights carry on; a claimed record
that already has a look skips the look picker. A record from another machine
is never claimed by its name alone; the server console moves one over.

Console: `account list`, `account revoke <name>` (all remembered logins),
`account password <name> <new password>` (also revokes them) and
`account claim <name> <record key>` (record keys as `fr admin` prints them).

### Where they come in

Districts use the stock Free Ride scene frames `emeth_1`, `emeth_3` and
`emeth_4` for Little Italy, Downtown and Hoboken. The client reads the actual
frames, and the server offers a district only when the reported position
matches that mission's stock frame, so no client can pick its own spawn
point. Each waiting player is placed in a private virtual world, so another
player's ped never walks into the preview.

Where they left off is the spot on this map's streets where they last stood,
with the health, weapons and ammunition and the wanted level they had. The
server saves it every five seconds while they are on the street and not busy
with a race, a duel, a job or a sentence, so those never become the place to
come back to; a player with time left to serve goes back to jail instead, and
an arrest takes the guns and the wanted level out of the saved spot. After a
death they come back at the map's hospital where it has one (Lost Heaven
Extended), else at their usual district, never at the saved spot.

### The login flight

Each map has its own loop of ground-track points in `server/lib/flyover.js`:
Lost Heaven circles the city from Little Italy over the north of the
island, Downtown and Hoboken to the villas on the eastern hill, then back
along the south shore (7.8 km, under eight minutes a lap); Lost Heaven
Extended swings north over the fields between the city and the mountain
instead (9.4 km); the countryside rings its road network well clear of the
mountain (8.3 km). The heights come from the map itself, never from a
guess: `tools/export_skyline.py` reads the mission's `scene.4ds`,
`scene2.bin` and `cache.bin` with every model they place (stock `A1.dta`
and `A2.dta`, and the mod's mission folder and `mods/freeride-extended.zip`
for Extended) and writes `server/data/skyline_<map>.js`, the highest
geometry in each 32 m cell (the camera-relative backdrop left out; trees
count as the sphere their billboards sweep). Every road crossing and road
point lies at or below its cell, and the prison and hospital placements
quoted elsewhere in this guide come out where `cache.bin` puts them.

At start the server lifts each point 28 m above the highest cell within
48 m of it and of the stretches to its neighbours, smooths the heights
upwards only, resamples the centripetal Catmull-Rom curve (alpha 0.5,
`server/lib/spline.js`) every 8 m of arc length, and raises points until
every sample clears the cells within 48 m of it by 28 m; a track that cannot
clear its skyline is refused and logged, and the client then holds a still
view. The camera looks at a second loop 150 m ahead and 14° down, flies
17 m/s and banks into the turns by the lean of a craft at that speed,
smoothed over 60 m and held within 15°. The client plays the table back with
`Camera.lock(position, direction, roll)` on every render, interpolating
between the evenly spaced samples, and unlocks the camera on spawn, resource
stop and mission unload (which a disconnect also brings). The flights run
85 to 175 m high, 29 m or more above anything below them.

Regenerate a skyline with, for example,
`python tools/export_skyline.py freeride resources/freeride/server/data/skyline_freeride.js --game <Mafia folder> --roads resources/freeride/server/data/roads_freeride.js --report`;
for Extended add `--mod-dir <mod mission folder> --mod-zip mods/freeride-extended.zip`.

Each life starts with a baseball bat; guns are bought at Yellow Pete's. Dead players come back after five
seconds (`server/lib/life.js`): at the map's hospital where it has one, else
at their usual district.

## Career

A player record belongs to an account (see "Accounts" above). It keeps cash, experience, kills, deaths, best kill
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
car markers, and see each other on the city map (hold Tab).

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

The event is announced to everyone and marked on the city map; its card shows to players within 250 m. `/event` follows it (compass and card from anywhere) or stops following. The server console accepts
`fr event [supply|hotcar]` to start one, `fr money <player> <amount>`,
`fr say <text>`, `fr dev on|off` and `fr save`.

## Places and jobs

Pete, the dispensary clerk and Salieri's barman are solid, client-only
`C_entity` humans; the pump is minded by the stock gas station attendants,
whom every player sees the same (`Pumper`). Their positions come from stock
mission scene frames; the server checks distance, life, mission generation and
money before any purchase.

| Place | Stock anchor | Purpose |
| --- | --- | --- |
| Yellow Pete's doorway | `MISE19-MESTO`: `1dvere u peteho` | Buy a Colt, shotgun, Thompson or grenades. |
| The Night Pump | `FREERIDE`: `_pumpa2` | Stop at a pump with the engine off and press the exit key: the attendant fills the tank at $3 a litre, or as far as your money goes. |
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

  On duty the city's police take you for one of their own
  (`Police.setOfficer`): the simulation host keeps no crime record for you,
  as retail keeps none for its officers, so speeding with the siren on,
  shooting at suspects or a weapon in hand raise no wanted level, and no
  officer chases, fines or arrests you. The host's NPCs rate you as a
  policeman. Off duty you are a citizen again with a clean record.

  **Calling for backup.** Every telephone booth in the city (the stock
  `PHbox01` booths, 127 of them, listed in `server/data/phone_booths.js`) is
  a call box on an officer's map. Stand at one and press U, or type
  `/backup` in your squad car to use its radio. The call goes after the
  nearest wanted player or bounty target within 300 m, else armed gangsters
  within 70 m, else it sends cover for you:
  - A wanted player gets the retail police's own reinforcements: a police
    car with two officers from the police pool sets out from a road about
    140 m away and hunts them as their wanted level says.
  - Anything else, or when the pool has no car free, gets a squad car
    spawned on a road out of sight. Its two officers board it, drive to the
    scene (following a suspect who moves on), get out and fight the suspect,
    or cover you for 90 seconds. They are friends to every officer on duty;
    anyone else who shoots them is wanted.

  The compass points at the arriving car, a whistle sounds when it is on
  scene, and officers on duty within 800 m hear the call on the radio with
  the scene marked on their map. Units stand down when the suspect is
  caught, dead or gone, when you clock off, or after three minutes; a
  squad drives off and is removed out of sight. One call at a time per
  officer, 90 seconds between calls, two squad cars in the city at once.
- **Taxi.** Drive a cab (`taxi00`, `fordapicktaxi00`, `phantomtaxi00`) and
  people wave you down, as in single player: a walker up the street turns
  to the cab and waves (everyone sees it), and a HAILING marker shows them.
  Stop next to them and they get into the back seat; that starts a shift if
  you are not busy with something else. They name a corner across town: the
  compass and a DROP-OFF marker lead there, and the card shows the meter
  ($8 plus 12 cents a metre, up to one and a half times the quote), the
  distance and the tip window (retail's ride time). Stop there to be paid
  the meter, a speed tip (up to 40% of the quote for the time left) and a
  smooth-ride tip (20% with no crash); then they get out and walk off. A
  crash gets a complaint and loses the smooth tip; the third one, or leaving
  the driver's seat, and they get out without paying. `/taxi` starts or ends
  a shift and brings a cab. With no walkers about (no simulation host, the
  countryside) or no hail for 45 seconds, dispatch sends you to a fare
  waiting at a street corner instead. Any number of cabs work at once.
- **Fire brigade** (the city maps). At the fire station in the Works Quarter
  (the stock `9hasic` building the city scenes place at (-1158.6, -5.9,
  306.5), its garage doors on the street at z 296.5) the retail mission arrow
  spins over a spot on the apron, and the station is on the city map. Stand
  under the arrow and press G to sign on: you put on the stock fireman's clothes
  (`Hasic01`), the only fireman model the game has. Two stock fire trucks
  (`fire00`) stand in front of the doors; only firefighters on duty drive
  them, anyone may ride along. A truck that burns out, or is left empty away
  from the station for three minutes, is replaced at its spot. Press G under
  the arrow again to clock off; `/fire` shows the way there, or clocks off
  from anywhere. Fires break out on the
  road graph every four minutes while anyone is on duty (every eleven
  otherwise, then burning themselves out after nine): a house front (flames
  up the facade), a burning car that explodes after four minutes, or a crash
  with one or two people lying in the road; a resident may crouch at a
  burning house's door. Everyone sees the fires and the feed; only firefighters on duty get them on the city map (medics likewise for their calls).
  Hold G within 35 m of a fire truck to spray the hose onto the nearest fire
  in front of you (24 m reach), or at a truck's wheel to use its roof cannon
  (32 m): the water arcs onto the flames, the fire shrinks as it goes down and
  grows back slowly if left alone, and every hose on it counts, so a crew is
  quicker. A house takes one firefighter about 45 seconds. G is the
  brigade's only near a fire, a trapped person or at a truck's wheel;
  anywhere else it does what it would off duty. The fire pays
  $420 (house), $320 (crash) or $260 (car), split by the water each
  firefighter put on it (at least $25 each). G next to a trapped person gets
  them up to follow you; 12 m clear of the fire they are safe ($120); with a
  medic on duty they become an ambulance call. `/fires` lists the fires.

  With a crew on duty the bigger calls come in too (the toast says what to
  do, the card shows the next step):
  - *A filling station alight.* A car burns at the pumps of one of the
    three stock filling stations (the `_pumpa_pumpaN` pumps of both city
    scenes, the Night Pump among them) and the pump itself is on fire. The
    card counts down to the pumps going up: at least two and a half minutes,
    more for a crew far away, with a warning at thirty seconds. Get every
    flame there down to a third and the pumps are safe: $380 for the car,
    $40 for the pump and a $160 bonus for the crew ($580 for one
    firefighter). Too late, the car explodes and a bigger fire takes the
    forecourt ($40 more), with no bonus.
  - *A bar on fire.* Morello's Bar on Lost Heaven Extended, Salieri's back
    room on stock Lost Heaven. Flames come out of the front and burn in
    two places in the room, and the barman and two drinkers (the bar's own
    people, at their own places) cower inside, crying for help; the bar
    serves nobody until it is over. Hose the front ($260), walk in through
    the door arrow and hose the room ($40 a fire): the hose reaches in from a
    truck within 35 m of the door. G next to someone gets them to follow
    you; at the exit arrow they come out with you onto the pavement ($50
    each), where a medic on duty takes them (an ambulance call with $40
    more). Leave without them and they stay behind. Everyone out alive
    pays the crew a $160 bonus, half by water, half evenly ($650 in all for
    one firefighter).
  - *Arson.* A house fire may have its arsonist running off down the street,
    marked on the map. Police officers on duty within 1.5 km hear it on the
    radio and their compass points at him. A firefighter or an officer on
    duty grabs him with G next to him, or hits him or knocks him over with a
    car: $150. Anyone else who hits him stops him too, for no reward. He
    gets away after 100 seconds.
  - *A burning wreck at a pile-up* (see the ambulance below).
- **Ambulance** (the city maps). Sign on with G under the arrow at the
  foot of Lost Heaven Hospital's steps (the stock `9medic` building at
  (-759.2, 11.6, 779.0); the hospital is on the city map) and take one of
  the two stock ambulances (`ambulance00`) at the kerb in front. They work
  like the fire trucks: medics on duty drive them, a lost one is replaced,
  G under the arrow clocks off and `/medic` shows the way or clocks off. The game
  has no medic clothes, so medics keep their own. A badly hurt patient rides
  in the ambulance you last drove. Calls come in every three minutes while a medic is on duty: someone dazed
  on the pavement or someone badly hurt lying there, plus the fire brigade's
  smoke victims. Hold G beside the patient for five seconds to treat them.
  The dazed get up and walk off ($140, 40% more if you got there within four
  minutes); the badly hurt get into the ambulance's front seat (or onto the
  stretcher, out of sight, when they cannot) and must reach the hospital's
  street front: $220 plus 10 cents a metre, and a 25% tip inside the time on
  the card (about $300 to $460). Without an ambulance the badly hurt stay
  where they lie: treating them pays nothing until one is brought up. While a medic is on duty a player who goes down outside a duel or
  race waits up to a minute for one instead of respawning: a medic who holds
  G beside them for four seconds brings them back where they fell with 60
  health and the starting kit, no hospital bill ($200 to the medic); G gives
  up and respawns as usual.

  Some calls are more than that. Patients talk in their own voices (the
  retail voice lines: moaning while they wait, crying out at a bump).
  - *A woman in labour.* Treat her (G, five seconds), then drive her in: a
    shorter tip window, $330 plus the distance, the tip and a 20% tip for a
    smooth ride (about $510 to $700). Every crash or knock on the ambulance
    costs that tip.
  - *A heart attack.* He lies unconscious with four minutes to live (more
    when you are far away), on the card. Tap G steadily beside him, about
    twice a second: eighteen good compressions start his heart; too fast
    takes one back, a pause keeps what you did; one medic does the CPR at a
    time. Then he rides in ($400 plus the distance and the tip, about $520 to
    $690). Too late and he is lost.
  - *A pile-up.* Two wrecks across a street and three people hurt around
    them, labelled CRITICAL, SERIOUS and MINOR up close (one blip on the
    map). The critical one bleeds out in three and a half minutes unless
    treated, then rides in ($160 plus the distance and the tip); the other
    two are patched up where they sit ($100 and $70, no quick tip). Treating
    the critical one first pays $40 for good triage; everyone seen to with
    nobody lost pays $100, split between the medics who treated one (about
    $530 to $700 for one medic). With the fire brigade on duty one
    wreck burns beside them, a fire call of its own ($260): it does not
    explode, and the wrecks are towed once it is out.
- **Tobacco farm** (Lost Heaven Extended and The Countryside, `server/farm.js`).
  The farm is the stock barn (`4stodola`) both countryside scenes place in
  the lowlands at (-2295.3, -3.6, 711.6), beside the road north-west of
  crossing (-2306.2, -5.9, 668.8); it is on the city map. Sign on with G
  under the arrow at its lean-to side. Two stock flatbed trucks
  (`trucka00`) stand on the apron towards the road; only farmhands on duty
  drive an empty one. The field is the open grass east of the barn: seven
  rows of eight plots with a round hay bale (`baliksena`) at each corner.
  Press G on an empty plot to plant a seedling ($10, up to eight plants each);
  the plants are stock bushes (`s_ker`) every player sees, growing through
  seedling, young, leafy and ripe (about 70, 90 and 90 seconds). A plant
  grows only once watered, and needs water again at every stage: hold G
  within 9 m of dry plants, with a farm truck within 30 m, and its hose
  waters them one after another (2.5 seconds each). Rain waters the whole
  field. Left dry for a minute a plant droops and loses quality (15% a
  minute); hornworms appear on about one plant in three and eat 20% a minute
  until picked off (hold G at it). Only drooping or infested plants carry a
  label. A ripe plant is picked with G (two seconds) and keeps for 15
  minutes, then rots. Anyone on the farm may water anyone's plants (a $5
  tip each) or pick off their hornworms; only the planter and their crew
  pick the leaves. Carry the leaves to the barn's arrow: they cure in two
  and a half minutes. Then stop a farm truck at the loading bay in front of
  the barn for six seconds and drive the load to a buyer: the village co-op
  on the countryside main street pays $110 a bundle at full quality (35% of
  that at none), and on Lost Heaven Extended the container docks pay 70%
  more. Within 900 m of the docks the whole city is told and sees the truck
  on the map, and a loaded farm truck can be driven by anyone: whoever stops
  it at a buyer is paid. A wrecked truck loses its load; a loaded truck left
  standing is towed back and unloaded at the barn. `/farm` shows the way to
  the barn, or clocks off from anywhere.

  The farm keeps its own clock: plants grow and cure only while a map with
  the farm is loaded, so a crop waits through the city map or a restart,
  but not through its owner leaving town. Plants, picked leaves, curing
  batches and cured tobacco are saved in the database by player record; a
  load on a truck goes back to the barn when the map changes or the resource
  stops. Both maps share the same field.
- **Courier.** Salieri's back window, as before.
- **Bootleg run** (`/bootleg`, Lost Heaven Extended). Drive to the still in
  the hills, wait eight seconds while the crates are loaded (the police are
  tipped off), then reach Salieri's bar within ten minutes in the same car:
  $700. A one-off delivery for Salieri; the rum distillery below is a
  business of your own.
- **Rum distillery** (`/rum`, Lost Heaven Extended, `server/distillery.js`
  and `client/distillery.js`). A hidden still that no map shows until a
  player learns of it, run by old Hank (a scripted NPC, voice 27) at the
  woodcutters' clearing in the lowland woods: the sheds (`@bouda06`) and log
  piles (`klady`) both countryside scenes place around (-1736, 15.2, 1260),
  130 m west of the lowland road beyond a fenced pasture (cars get there
  round its north side). Its gear is stock models placed as map objects in
  the open ground between the log piles: the pot (`xv sud`), a pipe
  (`xv trubka4`), the worm tub (`xsud 01`), mash vats (`tutsud`), molasses
  sacks (`xpytel2`), casks (`sud01`) and crates (`bedna01`).

  The way in is a five-step quest. *A sailor's tale*: the F2 Jobs tab and
  `/rum` point to Salieri's, where Ernesto, bars.js's sailor in the billiard
  room, gives directions (G next to him). *The woodcutters' clearing*: the
  compass leads to the camp; arriving on foot or in a stopped car plays a
  short locked camera shot of the still and Hank asks for copper. *Copper*: a hardware dealer's truck
  (`truckb00`) parks on the road north of the village store with its driver
  smoking beside it; hold G at it for four seconds to unbolt the copper
  worm (the driver shouts and the police are called, wanted level 1), then
  bring it to the still on foot or by car ($100, once per player). The
  still now belongs to the player's operation: their crew's if they are in
  one (crewmates share stock, heat and progress), otherwise their own. A
  player who brought the copper runs the still in any operation they are in
  later, without the quest again; joining a crew brings their own stock,
  loads and heat into the crew's (what the crew's shed has no room for
  stays theirs, for when they leave). *First fire* and
  *Luigi's thirst*: the first batch, bottling and sale, after which Ernesto
  introduces Paco at the docks.

  The loop: press G in a car stopped at the village store to buy molasses
  ($40 for three sacks; Hank keeps six), and stop the car in the clearing
  east of the sheds to unload. At the still, G fires a batch (one sack). Its
  pressure climbs on its own with sudden surges; the prompt bar is the gauge
  and G vents it (22%) anywhere within 4 m of the pot, before anything else
  G would do there. Between 45% and 80% the batch runs at full speed and
  counts as good; below it crawls at 40%; above it the steam whistle blows;
  at 100% the still explodes (a fire, up to 60 damage within 7 m, a white
  flash, +15 heat) and stays wrecked for two minutes unless someone holds G
  at it and pays $80. A batch takes about 45 seconds of tending; walking
  away banks the fire (any member can take over within three minutes). The
  cask ages for two and a half minutes, then holding G at the casks bottles
  four cases at the batch's quality (the share of time in the green). Stop
  any car in the clearing for five seconds to load up to twelve cases (the
  hotter the operation, the likelier someone on the road calls the police),
  and stop at a buyer: Luigi at Salieri's ($30 a case, +1 heat a case),
  the speakeasy at Morello's ($45, +2, a one-in-three chance a patrolman
  notices) or Paco at the container docks ($60, +6; within 900 m the city
  is told, the car shows on everyone's map and whoever stops it there is
  paid). Quality scales the price from 40% to 100%: a full car of the best
  pays $360, $540 or $720, and a docks run adds 72 heat. No buyer deals
  with a driver the police are after.

  Heat (0-100) rises with every batch and sale and cools a point every 20
  seconds; above 75 buyers pay 15% less. `/rum bribe` at Salieri's door
  pays Luigi's man at the precinct (3 x heat, at least $100) for 40 points,
  once in five minutes. At 55 or more Sergeant Doyle may walk up to the camp
  while a member is there: G next to him pays 2 x heat (at least $80) for 50
  points, unless the member is tending a batch (G vents); ignoring him for a
  minute brings the police and 10 heat, shooting him a wanted level 2. At
  100 the revenue men set out once a member is on the street (not in an
  interior, a duel, a race or the jail): the members are warned with the
  choice, defend the still or `/rum bribe` at Salieri's (which turns them
  back), and the card counts down four and a half minutes, time to drive
  there from the docks. The clock stops while no member is on the street
  and resumes with at least two minutes. A member within 250 m of the
  still when the first minute is up, or when the time runs out, meets three
  armed agents trying to reach the crates; killing them pays $100 each
  defender and leaves heat at 60. Nobody there when the time runs out, and
  every cask, case and sack stored at the camp is seized and heat drops to
  30. An operation with nobody in town is never raided. With six or more cases in the shed and a
  member at the camp, Morello's boys may come for them (three armed
  gangsters; $150 each defender for seeing them off, six cases lost if
  nobody does). Now and then the Kowalskis' village wedding orders four
  cases for $260 (`/rum wedding` takes the order, eight minutes to deliver
  to the village crossing), and Salieri's has a party night when Luigi pays
  half again. A player who knows the camp but is not in the operation may
  hold G at the crates for eight seconds to steal four cases while one of
  its members is in town (once in ten minutes per operation); the members
  are told at once (once in 30 seconds), and the cases ride in the thief's
  next car, marked on everyone's map, until sold or smashed by the thief's
  death. Stolen rum sold heats the thief's own still, if they run one, not
  its owners'.

  Labels stay small and close: the spinning arrow over the still or the
  casks when that is the next step, a label over the clearing's stop, and
  map blips only for the known camp and the current objective. `/rum hide`
  puts the quest aside (no card, compass or markers) until `/rum`. Quest
  progress, stock, ageing casks and heat are saved; a batch on the still,
  loads in cars and stolen cases in hand go back to their operation when the
  map changes or the resource stops.
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
the jail, and leaves 15 seconds after the last person does. The jail has no
pedestrian paths, so he walks in straight lines (`npc.walkTo` with `direct`).
When the time is up or bail is paid he walks to the latch of the cell door,
turns to it and works the lock with the clip Ralph unlocks a car with in the
story (`RalphOdemykani.i3d`, about 10 seconds, which MISE14-SALIERY plays
with `carunlock4.wav`); 4 seconds in the lock clunks and the barred door
slides aside, and once the clip has ended he walks back to the desk, or on to
the next door. Someone standing in his way stops the walk; he then works the
lock from where he is. Between unlocks he does the rounds the way the
watchman of MISE15-MESTO keeps his beat: 20 seconds after he arrives, and 45
seconds after each return to the desk, he walks to each cell in turn, faces
it and stands guard (`PolSt01` into the stance, `PolSt03` held for 6
seconds, `PolSt04` out of it), then walks back to the desk. An unlock calls
him off the round at once. A lost warden is sent again after 10 seconds.
Without a simulation host the door opens on its own after the same 4 second
pause. The
prisoner then walks down the corridor to the EXIT arrow; the screen fades and
they stand at the map's release point (`jail.release` in `lib/maps.js`) with
the starting kit. In the city that is the street outside the gate of the stock
prison building (`vezeni.4ds`, placed by `cache.bin` at (−1105.68, −6.12,
311.77); its gate spans x −1102.31 to −1094.44 at z 296.14, facing the road),
in the countryside the crossroads. A cell door closes again once its freed
prisoner has stepped out, so a cellmate stays locked in; a freed prisoner who
has not walked out after two minutes, or who dies, is shown out. A sentence
survives a reconnect, a map change and a restart of the resource. While the jail cannot be built (no map
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

Lost Heaven Extended ships two: Sarah's apartment in Little Italy, a
safehouse whose street door (`&& futro out`) and corridor (`&& chodba`) come
from the mod's `quarters_sr.4ds`, and Morello's bar (see Bars below). Stock
Lost Heaven's arrow at Salieri's door leads into its back-room bar. Other
interiors are not shipped, because
their street side cannot be told from the model data. Record them in development mode: stand on the street at the door facing
it and type `/entrance add <name> [public|safehouse]`, walk to where players
should arrive, face the way they should look and type `/entrance inside`.
`/entrance list` and `/entrance delete <name>` manage them; they are saved to
the database and work at once.

## Bars

`/bar` points the compass at the nearest bar. At the counter, N picks a
drink and B orders it; the barman turns to you and pours.

| Drink | Price | Drunkenness | Health |
| --- | --- | --- | --- |
| Beer | $4 | +10 | +2 |
| Red wine | $6 | +14 | +3 |
| Bathtub gin | $7 | +18 | +3 |
| Canadian whiskey | $10 | +22 | +5 |
| The house grappa | $15 | +32 | +6 |
| Black coffee | $3 | −15 | +4 |
| A round for the house | $4 a head | a beer for everyone inside | +2 each |

On Lost Heaven Extended the mod places Salieri's bar of the story missions
(`salierybar.4ds`) behind the stock front, with working doors: walk in
through the front door or the side door off the alley. Morello's bar
(`morellobar.4ds`, the bar of the mission Molotov Party) has a fixed door, so
an arrow on the pavement in front of its windows leads in. Stock Lost Heaven
has no rooms behind Salieri's door; its arrow leads to a back room built
from stock models (`server/lib/bar_room.js`) 150 m under the fields beside
the county jail, in its own virtual world: brick walls, board floor, a
counter with five bar stools, cupboards of bottles, a radio, tables and
chairs from Salieri's bar.

The people are scripted NPCs, there while anyone is inside and gone 20
seconds after the last guest. Their places, models and clips are the story
missions' own, played with `npc.playAnimation`: Luigi stands behind
Salieri's counter and wipes it or rinses glasses (`x barman stativ`,
`x barman stul`, `x barman umyvani`), three of Salieri's men sit in the
booths, a woman leans on the end of the counter with a glass and drinks, and
a sailor waits in the billiard room; in Morello's bar his barman cleans up
while Morello's men talk and smoke. When someone buys a round, the drinkers
raise their glasses.

**Drunkenness.** Each drink adds to your level (0 to 100), which wears off at
15 a minute; a coffee takes 15 off. The HUD card shows how drunk you are and
when you will be sober. From Tipsy (20) the world sways with the retail boat
swing, from 25 the view breathes in and out, and a warm haze and dark edges
close in. Wasted (70) on foot, your view lurches every few seconds. At 100
you black out: the screen goes dark, somebody helps themselves to 15% of
your cash ($20 to $400), and you come round on the pavement outside the bar
with a hangover (35). The level survives a reconnect, less what wore off.

From 30 a driver's car lurches off its line every few seconds; from 45 a
policeman, a squad car or an officer on duty within 35 m who sees you weave
may book you for drunk driving (wanted, minor). Officers on duty are not
booked.

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
map object four metres ahead, `off` removes it), `/entrance` for
recording interiors and `/drunk <0-100>` to set your drunkenness. They are off by default. The server console runs any of
them on a player with `fr as <player> <action> ...`, whether or not
development mode is on.

## How it is put together

| File | Role |
| --- | --- |
| `server/main.js` | Player lifecycle, profiles on join, the admin motor pool, `/repair`, `/help`, menu requests, console commands. |
| `server/dealership.js`, `economy.js` | The car lot, the garage and car locks; `/pay` and the street wage. |
| `server/lib/` | Shared rules: the database (`store.js`), commands, profiles and ranks, respawn and activities, invitations, notifications, nametags, the road graph, admins, car prices, and the county jail building and its warden (`prison.js`, `warden.js`). |
| `server/selector.js`, `lib/accounts.js`, `lib/flyover.js`, `lib/spline.js` | Logging in and registering, the one-time look and entering the city; accounts and remember tokens; the login flight. |
| `server/ambience.js`, `shops.js`, `police.js`, `props.js`, `doors.js` | Clock and weather, shops and courier, police outcomes, prop repair, the door lab. |
| `server/races.js`, `combat.js`, `duels.js`, `crews.js`, `world_events.js`, `social.js` | Races, bounties, duels, crews, city events, rides, messages and dice. |
| `server/export.js`, `police_duty.js`, `police_backup.js`, `taxi.js`, `robbery.js` | The jobs; `police_backup.js` is police duty's call boxes and backup. |
| `server/firefighters.js`, `ambulance.js`, `lib/stations.js`, `lib/jobkey.js`, `client/rescue.js` | The fire brigade and the ambulance; their stations (sign-on points and parked vehicles); the job key (G) and its prompt. |
| `server/farm.js` | The tobacco farm: its plots, plants (map objects), curing, loads and buyers. |
| `server/distillery.js`, `client/distillery.js` | The hidden rum distillery: its quest, still minigame, casks, loads and buyers, heat, raids and characters; the reveal shot and blast flash. |
| `server/turf.js`, `stashes.js`, `speed_traps.js`, `interiors.js` | Turf, cash stashes, speed traps and building entrances, each per map. |
| `server/bars.js`, `drunk.js`, `lib/bar_room.js` | Bars, their staff and drink counters; drunkenness, blackouts and drunk driving; the stock map's back-room bar. |
| `server/map_vote.js`, `bootleg.js`, `lib/maps.js` | The default map, map votes, the bootleg run and the map registry. |
| `server/championship.js`, `jail.js`, `jail_visits.js`, `hospital.js` | The Autodrom championship, jail sentences, jail visits and hospital respawns. |
| `client/hud.js`, `client/hud.html` | One transparent web view for toasts, feed, activity cards, race panel, level strip and the city menu; it also arbitrates the single native compass by priority. |
| `client/markers.js` | Beacons, checkpoint and crate markers, door arrows, turf rings, station arrows, bounty labels, and the city map blips (`CityMap`). World labels show only near and in sight. |
| `client/selector.js`, `client/selector.html` | The login flight, the login and register ticket and the one-time look. |
| `client/shops.js`, `car_picker.js`, `radio.js`, `police.js`, `jail.js`, `main.js` | Shop staff, the car lot, garage and motor pool, radio, police HUD, the jail countdown, fades and race countdown. |
| `client/bars.js`, `client/drunk.js` | The drink menu at a bar counter; the sway, view wobble, haze and blackout of a drunk player. |

Gameplay requests use `Events.emitServer` and are checked again on the
server; the server answers with `player.emit` or `Events.emitAllClients` and
state bags. For the complete contracts, see the
[server API](server_scripting_api.md) and [client API](client_scripting_api.md).
The shop positions were read from the stock `a1.dta` scene data with
[`tools/inspect_stock_dta.py`](../tools/inspect_stock_dta.py); neither tool
modifies the installation.
