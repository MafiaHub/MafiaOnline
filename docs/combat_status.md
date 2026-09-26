# Combat synchronization status

The current stage establishes server-owned combat state and a reliable,
versioned protocol. It is **not a complete playable combat implementation**.

## Implemented

- The server owns inventory flags, loaded and reserve ammunition, selected
  item, health, and alive state for each player life. A player life is keyed by
  network ID, mission generation, and spawn generation. Mission changes,
  despawns, and disconnects invalidate the old state.
- Full snapshots go to all clients after mutations and to each new client at
  join. A client ignores older generations and revisions. Accepted actions
  are relayed reliably with their authoritative revision.
- The server rejects actions from another controller, a stale life, a replayed
  sequence, a missing item, an empty clip, an invalid action or aim direction,
  or a fire rate faster than its multiplayer rule. It applies only target hits
  in the same mission and life within the configured range and aim corridor.
- Script APIs can give and remove stock items 2–15 and set health. The server
  handles equip, drop, reload, firearm/melee fire, and throwable release as
  state transitions. This currently does not create a native dropped item or
  projectile.
- For a streamed native human, the client applies the server's selected stock
  item and ammunition to the retail inventory view and asks Mafia to rebuild
  its hand weapon model. A late join gets this from the full combat snapshot.
- Local aiming transitions and significant target-direction changes are sent
  to the server. Complete snapshots carry the lasting aim flag and direction;
  remote humans restore the retail aim flag and on-foot neck/back pose even
  after a later shot or a late join. The native aim calls and layout are
  audited in `native_contract.md`.
- A native shooting hook reports only shots Mafia actually fired. It captures
  the native `NewShoot` origin, full direction, scatter draws and each queued
  pellet's trajectory and static impact. The server bounds this Fire payload
  and relays it only after accepting cadence and ammunition. A remote client
  replays the original `NewShoot` once inside `C_human::Shooting` with the
  accepted draws, producing Mafia's own queued bullets for `TickShoot`.
  Divergent native collision results discard the new bullet records and the
  event is consumed once, avoiding repeated muzzle effects. Damage passed to
  this visual replay is zero; server health remains authoritative. The sample
  gamemode gives each spawned player a Colt Detective and ammunition so
  in-car shooting can be exercised.
- Retail human Hit is intercepted for server-owned humans, including the
  native player and streamed remote actors. Bullet collisions from the local
  shooter are matched to a recent native pellet trajectory and become
  generation-bound HitReport messages with Fire sequence and pellet index.
  The server keeps a bounded window of outstanding shots, validates the
  controller, target life, weapon, time, direction and geometry, and consumes
  each pellet at most once, then
  publishes health and a reliable DamageEvent. Clients wait for the matching
  state revision before a bounded nonfatal native hit reaction.
- Firearm magazine capacities, native ranges and base damage now match the
  stock `tables\\predmety.def` item records. For a reported native bullet
  impact, the server applies retail `NewShoot` pellet division, `TickShoot`
  distance falloff, and `C_human::Hit` arm, leg and head multipliers. Seated
  victims use retail `HitInCar` unscaled damage. Both stock shotguns have a
  separate fatal hit at three units against a victim on foot.
- Server inventory snapshots reconcile the native carried slots. When the
  selected weapon stays the same but its ammunition changes, the client also
  updates Mafia's separate HUD ammo cache through the retail indicator method;
  rebuilding the weapon model for an ammo-only change would reset aim and
  weapon animation.
- A fatal server health state carries a death animation ID chosen on the
  server. Each client calls the retail human Hit once under a scoped death
  animation override and retains the dead native actor for late joins. Native
  Hit, Death, forced death, car airborne death and dead actor-state transitions
  are guarded so the local game cannot finalize a protected death first.
- Native fatal falls send a candidate from the audited Movement path. The
  server accepts it only for that player's current life after recent accepted
  movement shows a substantial vertical drop; an early report waits briefly
  for movement packets before the server decides.
- A car impact report can inspect an accepted Fire pellet before vehicle pose
  validation, then consume that pellet after validation. Car and human hits
  share the same per-shot consumed mask, so one pellet cannot damage both.
  The preliminary evidence includes the server acceptance time and the
  accepted native origin and trajectory; vehicle geometry validation is
  owned by the car service.

## Retail evidence and native boundary

The exact native shooting, inventory, and weapon model contracts are recorded
in `native_contract.md`. reM remains read-only evidence. For a living human,
the client writes server health to the native field; a dead state is reserved
for the separate authoritative death replay. The retail Steam image
reads health at human `+0x644` in its damage path at `0x5766EE` and writes it
at `0x5766FE`; that alone does not establish a complete death contract.

## Melee, grenades and Molotovs

- Melee attacks with fists, knuckleduster, knife and bat are captured from
  retail `Do_Shoot`, validated on the server (combo, cadence, one hit per
  attack, reach, facing) and replayed on remote humans. Damage uses the retail
  per-item constants, doubled for a heavy blow, with the retail backstab.
- Grenade and Molotov throws are captured from `NewGrenade`, validated and
  shown on remote clients as disarmed copies. The thrower reports the
  detonation; the server applies explosion and fire damage.

## Release blockers

- Capture stock selection, reload, throw, pickup, and drop outcomes from
  native callbacks. The current selected-item mirror covers one equipped
  weapon; the full carried inventory and native item world objects still need
  reconciliation.
- The server still relies on the firing client's native hit reports for
  target selection and lacks mission collision geometry. A forged target
  behind an obstacle may pass the current range and direction checks. Fire
  cadence and melee damage remain multiplayer rules, and the fatal fall
  threshold needs live validation.
- Native nonfatal reaction branches still use retail randomness, so blood,
  screams and some reactions can differ between clients. Death animation ID is
  explicit, but all stock fatal paths and final pose behavior still need a
  two-client matrix. Script-controlled respawn must create a fresh life.
- Complete native inventory and all stock item IDs,
  including pickups, grenade and Molotov projectile/impact behavior.
- Native projectile parity, multi-pellet damage, HitReport, DamageEvent and
  direct-death guard behavior still need a two-client damage/death matrix.
  The corrected 16-byte retail shoot-vector layout passed an automated
  two-client Colt Fire smoke: four Fire intents reached the server, and its
  magazine state fell from six to two. Remote impact and transient animation
  still need frame-by-frame verification. Dynamic collision timing may differ
  between clients.
- Verify two clients under 100 ms RTT with jitter/loss, all weapons and
  throwables, late join, mission changes during combat, and final state
  convergence. Compilation alone does not satisfy this matrix.
