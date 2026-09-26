# Vehicle multiplayer verification

The first release gate is final-state agreement between the server and two
clients after a transient network interruption. Inspect the rendered native
car on both clients as well as `Cars.getState(id)`; a matching metadata flag
alone does not pass a visual damage case.

Use one client as the current physics controller and the second as observer.
Repeat each action after swapping controller ownership, after a late join, and
after the controller disconnects during the action. Run a clean LAN pass and
a pass through `tools/udp_test_proxy.py` with 50 ms one-way delay, 10 ms jitter
and 2% packet loss in both directions. Start the two clients with
`MAFIA1ONLINE_TEST_PORT=27017` for the impaired pass.

| Area | Cases to observe on both clients and the server |
| --- | --- |
| Driving | Position, rotation, linear and angular velocity, steering input and wheel angle, gear and engine speed, fuel consumption, engine start/stop, lights and horn; acceleration, braking, reverse, turn, collision, idle and teleport. |
| Seats | Entry door animation, completed entry, steal, exit and blocked exit; each seat, while driving and while stationary; late join with occupants already seated and shooting from a seat. |
| Glass and lights | Crack and break each glass pane, break each light, and verify individual health and visible state through another impact, late join and controller transfer. |
| Wheels | Tire puncture, reduced wheel health, detached wheel and the resulting handling/visual change; repeat for each wheel and after a physics owner change. |
| Body | Collision and projectile deformation at each available model zone, zone health/crack state, displaced vertices, detached door/hood/trunk/bumper/mirror/wing/roof where available, and resulting debris lifetime. |
| Mechanical damage | Engine, gearbox and fuel tank health; smoke/fire, explosion, terminal engine stop and occupied-car outcome. |
| World terminal cases | Water/submersion and out-of-map conditions, including late join and mission change while terminal. |
| Recovery | Packet loss followed by a quiet period, stream out/in, reconnect, owner disconnect, car despawn and mission change during damage/entry. Confirm no stale native handles or repeated effects. |

For each case, record the stock car model, seat or part index, initiating
action, controller GUID, mission and car generations, final server state,
native result on each client, and whether a late join reconstructs it. A case
passes only when the native visible and physical result converges, not merely
when the event arrives.

No complete damage matrix has passed yet.

## Results so far

A local two-client pass on 2026-09-24 (thunderbird00, parked, no occupants)
confirmed:

- A controller shot at a wheel is accepted and replayed; wheel health falls
  on the server snapshot.
- An observer shot at the rear glass is accepted, replayed by the controller
  and breaks the pane on the observer.
- An observer shot at a wheel is accepted after a `Cars.setTransform`
  teleport; the teleport holds on the controller.
- Six observer shots destroy a wheel (`0x40000000`).

Open: after the wheel is destroyed, observer shots at the same car fail hit
geometry validation. The observer's reset native physics settles the damaged
car on its own, so its collision and frame drift from the replicated pose.
Observer cars need their native physics pose driven from replication. See `native_contract.md` for the
audited native calls and `server_scripting_api.md` for the current API surface.
