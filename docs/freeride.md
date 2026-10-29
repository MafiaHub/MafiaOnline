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
| N / B | At a shop or bar counter: next offer / buy or accept it. |
| F9 | Car radio on or off. |
| K | Siren, in a police car. |
| U | At a telephone booth, on police duty: call for backup. |
| G | On fire duty: hold to spray the hose (or the truck's roof cannon from its wheel), press next to a trapped civilian to lead them out. On ambulance duty: hold next to a patient to treat them. Down and waiting for a medic: press to give up and wake at the hospital. |

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
with time left to serve goes back to jail instead, and an arrest takes the
guns and the wanted level out of the saved spot. After a death they come
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
- **Fire brigade** (`/fire`, the city maps). Report to the fire station in
  the Works Quarter (the stock `9hasic` building the city scenes place at
  (-1158.6, -5.9, 306.5), its garage doors on the street at z 296.5; a FIRE
  STATION marker shows it), put on the stock fireman's clothes (`Hasic01`)
  and take the stock truck (`fire00`) from the apron. Fires break out on the
  road graph every four minutes while anyone is on duty (every eleven
  otherwise, then burning themselves out after nine): a house front (flames
  up the facade), a burning car that explodes after four minutes, or a crash
  with one or two people lying in the road; a resident may crouch at a
  burning house's door. Everyone sees the fires on the map and in the feed.
  Hold G within 35 m of your truck to spray the hose onto the nearest fire in
  front of you (24 m reach), or at the truck's wheel to use its roof cannon
  (32 m): the water arcs onto the flames, the fire shrinks as it goes down and
  grows back slowly if left alone, and every hose on it counts, so a crew is
  quicker. A house takes one firefighter about 45 seconds. The fire pays
  $420 (house), $320 (crash) or $260 (car), split by the water each
  firefighter put on it (at least $25 each). G next to a trapped person gets
  them up to follow you; 12 m clear of the fire they are safe ($120); with a
  medic on duty they become an ambulance call. `/fires` lists the fires.
- **Ambulance** (`/medic`, the city maps). Report to Lost Heaven Hospital
  (the stock `9medic` building at (-759.2, 11.6, 779.0); an AMBULANCE marker
  at the steps) and take the stock ambulance (`ambulance00`) from the street.
  Calls come in every three minutes while a medic is on duty: someone dazed
  on the pavement or someone badly hurt lying there, plus the fire brigade's
  smoke victims. Hold G beside the patient for five seconds to treat them.
  The dazed get up and walk off ($140, 40% more if you got there within four
  minutes); the badly hurt get into the ambulance's front seat (or onto the
  stretcher, out of sight, when they cannot) and must reach the hospital's
  street front: $220 plus 25 cents a metre, and a 40% tip inside the time on
  the card. While a medic is on duty a player who goes down outside a duel or
  race waits up to a minute for one instead of respawning: a medic who holds
  G beside them for four seconds brings them back where they fell with 60
  health and the starting kit, no hospital bill ($200 to the medic); G gives
  up and respawns as usual.
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
| `server/selector.js`, `ambience.js`, `shops.js`, `police.js`, `props.js`, `doors.js` | Entering the city, clock and weather, shops and courier, police outcomes, prop repair, the door lab. |
| `server/races.js`, `combat.js`, `duels.js`, `crews.js`, `world_events.js`, `social.js` | Races, bounties, duels, crews, city events, rides, messages and dice. |
| `server/export.js`, `police_duty.js`, `police_backup.js`, `taxi.js`, `robbery.js` | The jobs; `police_backup.js` is police duty's call boxes and backup. |
| `server/firefighters.js`, `ambulance.js`, `lib/jobkey.js`, `client/rescue.js` | The fire brigade and the ambulance, and their job key (G) and prompt. |
| `server/turf.js`, `stashes.js`, `speed_traps.js`, `interiors.js` | Turf, cash stashes, speed traps and building entrances, each per map. |
| `server/bars.js`, `drunk.js`, `lib/bar_room.js` | Bars, their staff and drink counters; drunkenness, blackouts and drunk driving; the stock map's back-room bar. |
| `server/map_vote.js`, `bootleg.js`, `lib/maps.js` | The default map, map votes, the bootleg run and the map registry. |
| `server/championship.js`, `jail.js`, `jail_visits.js`, `hospital.js` | The Autodrom championship, jail sentences, jail visits and hospital respawns. |
| `client/hud.js`, `client/hud.html` | One transparent web view for toasts, feed, activity cards, race panel, level strip and the city menu; it also arbitrates the single native compass by priority. |
| `client/markers.js` | Beacons, checkpoint and crate markers, door arrows, turf rings, bounty and crewmate labels. |
| `client/selector.js`, `shops.js`, `car_picker.js`, `radio.js`, `police.js`, `jail.js`, `main.js` | The selector, shop staff, the car lot, garage and motor pool, radio, police HUD, the jail countdown, fades and race countdown. |
| `client/bars.js`, `client/drunk.js` | The drink menu at a bar counter; the sway, view wobble, haze and blackout of a drunk player. |

Gameplay requests use `Events.emitServer` and are checked again on the
server; the server answers with `player.emit` or `Events.emitAllClients` and
state bags. For the complete contracts, see the
[server API](server_scripting_api.md) and [client API](client_scripting_api.md).
The shop positions were read from the stock `a1.dta` scene data with
[`tools/inspect_stock_dta.py`](../tools/inspect_stock_dta.py); neither tool
modifies the installation.
