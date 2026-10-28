# Free Ride gamemode

`resources/freeride` is the default Mafia1Online gamemode. It is built to show
what the multiplayer does: players race each other through the city's real
streets, put prices on each other's heads, settle scores in private duels,
run crews and fight over their street corners, work jobs on both sides of
the law, fight over city events with the police on their tail, and keep a
career between sessions. The server owns every outcome; clients present it.

Run the server with this project directory as its working directory. It
discovers the resource and writes its records to `data/freeride/`
(`profiles.json`, `crews.json`, `races.json`, `turf.json`, `speedtraps.json`,
`interiors.json`), which is not tracked.

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

Each life starts with a baseball bat and a Colt. Dead players come back where
they entered the city after five seconds (`server/lib/life.js`).

## Career

A player record belongs to a nickname on one machine (nickname plus the
framework hardware ID). It keeps cash, experience, kills, deaths, best kill
streak, race wins and records, deliveries, bounties, event wins, duel wins and
play time. New players start with $500. Experience comes from kills, races,
deliveries, events, duels, bounties and time on the streets; levels follow
100 · n(n−1)/2 XP and carry the ranks Greenhorn, Street Runner, Associate,
Soldier, Capo, Underboss and Don. The HUD shows the level strip and each gain.

Public values (level, rank, kills, deaths, crew, current activity, bounty)
travel in each player's replicated state bag under `fr.*` keys. The scoreboard,
the player list and the world labels on every client read them from there.

## Street races

`/race` (or F2 → Races) opens a lobby on a route. Everyone in town is invited;
racers have 45 seconds to reach the start line as a driver (`/car` works in
the lobby). The server then puts every car on a starting grid with
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
pump has a use. Police fines cost $100 and an arrest costs $250 bail and a
fresh start.

## Jobs

The Jobs tab of the city menu shows every one of these with its live state.

- **Export list.** Lucas Bertone wants three particular models this hour,
  chosen from the cars actually driving in this server's traffic. Steal one
  from traffic (a motor pool car does not count) and stop it at the export
  yard: $400 each, $600 more for closing the list. `/export` shows it.
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

## Buildings

Arrows by building doors lead inside: walk into one, the screen fades and you
arrive inside; the arrow by the arrival point leads back out with your weapons
and health. Public interiors are shared. A safehouse is private to your crew
(or to you, without one), dresses your wounds and tops up your magazines.
Doors stay shut while the police are after you, and during races and duels.

No interiors ship with the gamemode, because none can be verified outside the
game. Record them in development mode: stand on the street at the door facing
it and type `/entrance add <name> [public|safehouse]`, walk to where players
should arrive, face the way they should look and type `/entrance inside`.
`/entrance list` and `/entrance delete <name>` manage them; they are saved to
`data/freeride/interiors.json` and work at once.

## Getting together

`/goto <player>` asks someone for a ride; when they `/accept`, you arrive next
to them, or in a free seat of their car, with your weapons and health. It is
refused while wanted, in a fight or busy with an activity. `/pm` sends a
private message, `/spectate <player|off>` watches through someone's camera
and `/players` lists who is in town. Everyone's HUD lists who is talking on
proximity voice.

## Cars

`/car` opens the motor pool with native model previews; one personal car at a
time, marked on the radar in your crew colour. `/repair` fixes it every 45
seconds, `/flip` puts it back on its wheels and `/engine` starts or stops it.

## The living city

Free Ride always plays the stock day map, `freeride`; a server configured with
`freeridenoc` moves to day once the resource starts. A two-hour weather front, chosen from the date, brings rain,
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
cardamage|money` for combat, car and HUD testing, and `/entrance` for
recording interiors. They are off by default.

## How it is put together

| File | Role |
| --- | --- |
| `server/main.js` | Player lifecycle, profiles on join, personal cars, `/help`, menu requests, console commands. |
| `server/lib/` | Shared rules: commands, profiles and ranks, respawn and activities, invitations, notifications, nametags, the road graph. |
| `server/selector.js`, `ambience.js`, `shops.js`, `police.js`, `props.js`, `doors.js` | Entering the city, clock and weather, shops and courier, police outcomes, prop repair, the door lab. |
| `server/races.js`, `combat.js`, `duels.js`, `crews.js`, `world_events.js`, `social.js` | Races, bounties, duels, crews, city events, rides, messages and dice. |
| `server/export.js`, `police_duty.js`, `taxi.js`, `robbery.js` | The jobs. |
| `server/turf.js`, `stashes.js`, `speed_traps.js`, `interiors.js` | Turf, cash stashes, speed traps and building entrances. |
| `client/hud.js`, `client/hud.html` | One transparent web view for toasts, feed, activity cards, race panel, level strip and the city menu; it also arbitrates the single native compass by priority. |
| `client/markers.js` | Beacons, checkpoint and crate markers, door arrows, turf rings, bounty and crewmate labels. |
| `client/selector.js`, `shops.js`, `car_picker.js`, `radio.js`, `police.js`, `main.js` | The selector, shop staff, motor pool, radio, police HUD, fades and race countdown. |

Gameplay requests use `Events.emitServer` and are checked again on the
server; the server answers with `player.emit` or `Events.emitAllClients` and
state bags. For the complete contracts, see the
[server API](server_scripting_api.md) and [client API](client_scripting_api.md).
The shop positions were read from the stock `a1.dta` scene data with
[`tools/inspect_stock_dta.py`](../tools/inspect_stock_dta.py); neither tool
modifies the installation.
