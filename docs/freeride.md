# Free Ride gamemode

`resources/freeride` is the default Mafia1Online gamemode. It is built to show
what the multiplayer does: players race each other through the city's real
streets, put prices on each other's heads, settle scores in private duels,
run crews, fight over city events with the police on their tail, and keep a
career between sessions. The server owns every outcome; clients present it.

Run the server with this project directory as its working directory. It
discovers the resource and writes its records to `data/freeride/`
(`profiles.json`, `crews.json`, `races.json`), which is not tracked.

## Controls

| Key | Action |
| --- | --- |
| F1 | City menu: events, race lobbies, bounties, players, crews, leaderboards and help. Esc or F1 closes it; 1–6 switch tabs. |
| F2 (hold) | Scoreboard with crews, ranks, kills, deaths, what everyone is doing and ping. |
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

`/race` (or F1 → Races) opens a lobby on a route. Everyone in town is invited;
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
`fr say <text>` and `fr save`.

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

Free Ride follows the server's local clock: `freeride` from 06:00 to 18:59,
`freeridenoc` at night. The change reloads the mission, so players pick a
character again. A two-hour weather front, chosen from the date, brings rain,
or sometimes snow in winter, that grows and fades over the front. City music
is off; the car radio plays the
[Jazz Club Bandstand](https://www.internet-radio.com/station/bandstand/) live
stream while you drive.

Broken booths, hydrants and bins stay down for everyone until the city crew
comes round five minutes after the first damage; `/props` shows the damage
and `/repairprops` calls the crew at once. `/doors` and `/door ...` control
the synchronized stock doors near you.

## Development commands

Start the server with `FREERIDE_DEV=1` to enable `/dev weapons|fists|drop|
model|wanted|hud|opacity|carstate|cardamage|money` for combat, car and HUD
testing. They are off by default.

## How it is put together

| File | Role |
| --- | --- |
| `server/main.js` | Player lifecycle, profiles on join, personal cars, `/help`, menu and scoreboard requests, console commands. |
| `server/lib/` | Shared rules: commands, profiles and ranks, respawn and activities, invitations, notifications, nametags, the road graph. |
| `server/selector.js`, `ambience.js`, `shops.js`, `police.js`, `props.js`, `doors.js` | Entering the city, clock and weather, shops and courier, police outcomes, prop repair, the door lab. |
| `server/races.js`, `combat.js`, `duels.js`, `crews.js`, `world_events.js`, `social.js` | The multiplayer activities above. |
| `client/hud.js`, `client/hud.html` | One transparent web view for toasts, feed, activity cards, race panel, level strip, scoreboard and the city menu; it also arbitrates the single native compass by priority. |
| `client/markers.js` | Beacons, checkpoint and crate markers, bounty and crewmate labels. |
| `client/selector.js`, `shops.js`, `car_picker.js`, `radio.js`, `police.js`, `main.js` | The selector, shop staff, motor pool, radio, police HUD, fades and race countdown. |

Gameplay requests use `Events.emitServer` and are checked again on the
server; the server answers with `player.emit` or `Events.emitAllClients` and
state bags. For the complete contracts, see the
[server API](server_scripting_api.md) and [client API](client_scripting_api.md).
The shop positions were read from the stock `a1.dta` scene data with
[`tools/inspect_stock_dta.py`](../tools/inspect_stock_dta.py); neither tool
modifies the installation.
