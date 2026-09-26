# Sample Free Ride gamemode

`resources/sample-gamemode` is an example of a server-owned game mode with
client-only presentation. It uses the stock `freeride` mission from 06:00 to
18:59 in the server's local time zone, then loads **`freeridenoc`** from 19:00
to 05:59. The change reloads the mission, so players pick a character again.
The default server configuration starts in `freeride`; `server/ambience.js`
corrects the map as soon as the resource starts.

The server selects a two-hour weather front from the local date and time.
Rain, and sometimes snow in winter, grows and fades over the front instead of
switching to a fixed intensity. Each client receives the same authoritative
weather. City music is disabled. On entering a car, `client/radio.js` tunes a
1930s/1940s internet station through a client-only HTML audio view. It stops
on exit and retries if the stream drops. Press **F9** to mute or restore it.
A live stream has no seekable timeline. The current station is
[Jazz Club Bandstand](https://www.internet-radio.com/station/bandstand/)
from the [30s directory](https://www.internet-radio.com/search/?radio=30s).

## Entering Lost Heaven

The character selector uses the stock `emeth_3` and `emeth_4` Free Ride scene
frames for Downtown and Hoboken. The camera aims at a temporary animated
human at the chosen point. The client reads the actual frames; the server
checks their report against its native spawn hint before accepting a choice.
Each waiting player is placed in a private virtual world, so another player's
ped cannot appear in the preview. After the server accepts a spawn, that
player returns to the shared city world. The native life display stays hidden
until the local player has spawned.

| Key | Action |
| --- | --- |
| Up / Down | Change district and spawn location. |
| Left / Right | Change character. |
| Enter | Spawn at the previewed location. |
| C | Rotate the preview camera. |

Each new life receives a baseball bat, Colt and a little spare ammunition.
`/weapons bat|shotgun|thompson` remains a development command for combat
testing. The balance and inventory are owned by the server.

## Places and jobs

The four staff members are solid, client-only `C_entity` humans. Their
positions come from stock mission scene frames; the server checks player
distance, life, mission generation, money and other requirements before any
purchase. Press **N** to cycle the nearby attendant's offers and **B** to buy
or accept one. The native Free Ride score counter displays your balance.

| Place | Stock anchor | Purpose |
| --- | --- | --- |
| Yellow Pete's doorway | `MISE19-MESTO`: `1dvere u peteho` | Buy a Colt, shotgun, Thompson or grenades. |
| The Night Pump | `FREERIDE`: `_pumpa2` | Buy up to 25 litres for a nearby car. |
| The Dispensary | `FREERIDE`: `lekarna` | Restore health with bandages. |
| Salieri's Back Window | `FREERIDE`: `Salieri_save` | Buy coffee and pie or take a courier envelope. |

The courier job directs you to Yellow Pete's with the native compass. Deliver
within eight minutes to earn $180. New players receive $300; money persists
through death and map changes until disconnect. Script-spawned cars start with
little fuel so the pump has a practical use. A floating supply cache near the
first spawn demonstrates a separate custom pickup: the server owns its state
and validates collection, while each client draws its rotating model and 3D
label.

The **city circuit** is a three-stop driving route through the same landmarks.
Enter a car as the driver and use `/tour` to start. The first checkpoint is
the next landmark after your current area. Follow the native compass and stop
within 30 metres of each destination before the 15-minute clock runs out.
Completing the route pays $240 and records your best time for this connection.
`/tour status` reports progress; `/tour cancel` ends the run. Death, leaving
the city, or a mission change also ends it. The server checks each checkpoint
against the current car, mission and speed before paying.

The client **field guide** opens after spawning and briefly when a circuit
starts. Press **F7** to show or hide its pages, and **F8** to move to the next
page. Its compact strip keeps the next stop and time visible during a run.
It reads the circuit state sent by the server; it cannot advance a checkpoint
or award money locally.

## Synchronized door demonstration

Use a stock door once to register it with the server. `/doors` lists nearby
registered doors. `/door status|open|openback|close|ajar|lock|unlock|enable|disable`
controls the nearest one. Door state, paired leaves and opening direction
replicate to other clients and late joiners. The
[server API](server_scripting_api.md) also exposes `Door.create(name, position)`
for explicit registration in a custom gamemode.

## How the example is put together

The [manifest](../resources/sample-gamemode/package.json) declares server and
client scripts plus their HTML pages sent to clients. Server files manage
missions, selection, doors, purchases, the city circuit and the supply cache.
Client files render the selector, guide, shop staff, labels and cache, and
play radio. Gameplay requests use `Events.emitServer` and are checked again
on the server; the client displays the result from replicated state or server
events. For the
complete contracts, see the [server API](server_scripting_api.md) and
[client API](client_scripting_api.md).

The authored positions were read from the stock `a1.dta` scene data with
[`tools/inspect_stock_dta.py`](../tools/inspect_stock_dta.py). That tool reads
the game archives without modifying the installation.
