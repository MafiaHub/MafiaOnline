# Native shot replication contract

The selected multiplayer path replays Mafia's own `C_game::NewShoot` and
`TickShoot` on other clients. It does not load mission collision geometry on
the server. The client that fires captures the actual `NewShoot` origin,
full range vector, scatter draws, and resulting pellet trajectories. The
server accepts a bounded Fire action after checking life generation,
inventory, ammunition, weapon range, pellet count and cadence, then reliably
relays the native shot inputs. Each receiver invokes `NewShoot` once with
those inputs and compares the queued records before `TickShoot` advances them.
It discards a divergent record instead of creating a different bullet hole.

Retail evidence for `C_human::Shooting` (`0x584620`), `C_game::NewShoot`
(`0x5a84a0`), `C_game::TickShoot` (`0x5a8e70`), `rnd()` (`0x408470`), and the
`S_shoot` queue is in [native_contract.md](native_contract.md). The game
owns all scene collision, frames, effects and projectile records; the mod
retains no native pointers after stream out or mission shutdown.

Native visual projectile replay passes zero direct damage. The firing
client's protected `C_human::Hit` callback reports a target, body part and
impact point matched to one outstanding pellet. The server validates that
report against the accepted Fire, consumes the pellet once, applies stock
weapon damage and publishes health/death. The stock item values and
falloff/body-part formulas are documented in `native_contract.md`.

This remains a development stage. The server has no mission wall query and
cannot prove that a reported human or car impact was unobstructed. Native
dynamic collision can differ across clients because moving actors and cars
are sampled at different times. Persistent bullet holes and temporary
projectiles are not restored to late joiners. Grenade and Molotov throws
currently consume inventory but do not create a replicated native actor.
Fire acceptance, remote muzzle/bullet playback, shotgun pellets and damage
convergence still require a live two-client loss/jitter test before release.
