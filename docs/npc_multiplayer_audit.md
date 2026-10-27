# NPC multiplayer audit

Audit date: 2026-10-27, against reM (`C:\Develop\reM`) and the retail Game.exe in
IDA (`reM\IDBs\Game.i64`). It decides who runs NPC AI and how hard each NPC
kind is. Detailed pedestrian requirements remain in
[pedestrian_multiplayer_research.md](pedestrian_multiplayer_research.md).

## Decision

All NPC AI runs in one **simulation host**: a stripped Mafia client, started
next to the dedicated server, that authenticates as a trusted peer and owns
every NPC entity. Players receive passive replicas. Nothing is handed between
players, so one RNG, one junction lock table, one police dispatcher and one
collision world decide every NPC outcome.

## Findings per NPC kind

| Kind | Effort | Main reason (evidence) |
|---|---|---|
| Pedestrians | Medium | No native restore: `ActivateFree` 0x456fa0 rerandomises health/walk/voice. ~8 inline camera/render-stamp sites in element `Tick` 0x452cd0, `Activate` 0x4577f0, `C_don_ocas::Collision` 0x44ca50 (vtable 0x623f70; IDA's function start 0x44ca4a is a mis-decode). Panic origin is one global at 0x63d740. |
| Gangsters (full AI) | Medium | Remote players are `C_entity` (type 27) and AI targets any type 2/27 actor (`ai_vis_logs.cpp:169`). A network-controlled entity skips AI: vtable+0x3c is an empty `retn 4` (0x4ab130). Shots funnel through `Do_Shoot_Timed` 0x528680 -> `NewShoot` 0x5a84a0. |
| Traffic cars | Hard | Driver AI (`C_driver`, 0x29c bytes) holds pointers into `C_roads`; crossing locks (`LockCrossing` 0x4b3ea0) are per process. Spawn/despawn is camera relative (`ActivateElems` 0x4ab910, `GetNewPosInfo` 0x4af1c0). |
| Police | Hardest | Per-person crime records are generic (`C_shvestky::m_Persons`), but the dispatcher (`g_PoliceNastenka` 0x646d10), town freeze and arrest flow are single instances; arrest (`PlukovnikuMamZmrdaPlayera` 0x517260) ends the local game for any target. |

The simulation host removes the cross-process problems (locks, dispatcher,
hand-off) and keeps the per-player ones: every native "local player" or
"camera" decision must become a decision over all connected players.

## Native facts the implementation relies on

- Every traffic generator's `GameInit` 0x44e500 creates models for its whole
  pool at mission start; slot order is shuffled with `rnd()`.
- Only one generator spawns at a time: `SetActiveTrafficP`, inlined into
  `C_game::Tick` 0x5a51c0 and `Init` 0x5a0810 (reM `C_game.cpp:925`).
- `g_pTrafficGen` 0x63d730 is a byte "traffic initialised" flag, not a pointer.
- `TickElements` 0x44ef70 writes camera XZ distance squared to element +0x54.
- `SetTrafficVisible` 0x5a8470 = generator `SetVisible` + `C_traffic_car::Scipni`
  + police freeze byte 0x63d7ac.
- The retail game loop is `GameLoopSingle1` 0x5be750: IGraph `ProcessWinMessages`
  (+0x2c), input, `GetTimer(16, 83)` (+0x100), `C_mission::Tick` 0x5407e0,
  ESC pause menu, indicators, scene `Render` (scene vtable +0x54),
  IGraph `Present` (+0x3c). It returns when `C_game` state (+0x48) is 1 or the
  game is no longer initialised (+0x40).
- Pedestrian navigation: `missions\<m>\check.bin`, loader `C_web_path::LoadNodes`
  0x54a8c0, signature 0x01ABCEDF, 30-byte nodes.
- Road navigation: `missions\<m>\road.bin`, loader `C_roads::LoadData` 0x559e60.

## NPC humans (gangsters implemented, police next)

Every armed NPC is a retail `C_entity`, so one feature (`features/npc`) runs
them all: `NpcHost` on the simulation host, `NpcService` on the server (entity,
`Combat::State` as the inventory record, script `Npc` with `kind`) and
`NpcReplicaService` on players. A kind adds only its spawn hook
(`gangster_spawn.cpp`; police will hook `FizlosNamakatDoAkce`).


- `C_traffic_element::Hit` 0x4588d0 hands a type 4 walker to `CreateMafianos`
  0x459b90 (thiscall, `retn 14h`, only caller 0x458914). It duplicates the
  walker model into a new `C_entity` (health 200, item 10 Thompson with 50 + 300
  rounds), deactivates the walker with generation disabled, appends the entity
  to the police manager (`C_game` +0x2da0) with `AddDoRiciActor` 0x49e820 and
  hits it as the attacker did, unless the hit was a car impact.
- `C_shvestky::TickDoRiciActors` 0x49ea80 (`retn 4`) reads the active camera
  once and, unforced, retires an entity at XZ distance 150 or more. Its list is
  a `std::vector` at +0x7c (first +0x80, last +0x84, end of storage +0x88).
  The host removes every actor near a player from the list, runs the pass
  from far away and re-adds them, so the native retire sequence runs only for
  actors far from every player. `C_human::GameDone` 0x5757c0 reactivates the
  source walker at the gangster's last pose.
- Players aim at a replica that trails the host's gangster, so their own
  client detects hits (HitReport) and the server validates them like hits on
  players; the host applies them as the native Hit by that player's replica and
  ignores the replayed shot. Car impacts arrive the same way as a VehicleImpact.
- `C_don_ocas::Collision` 0x44ca50 hits a walker with `Hit(5, direction, 5,
  car, |direction| * 4.2)` (0x44cba3-0x44cbb5, 4.2 at 0x623f90) and deactivates
  it instead when it is outside the active sector and over 20 from the camera.
  `Hit` charges a car impact only when the car's seat 0 owner is `m_pPlayer`.
  The driver's client reports the contact; the host applies it with
  `m_pPlayer` set to that driver's replica.
- The fatal branch of `C_human::Hit` 0x5762a0 calls `InventoryTruncate` (clamps
  the hand and coat weapon rounds to the item caps) and `Do_WeaponDrop`
  0x585d90, which drops the hand item through `DropOutItems`. In a mod mission
  the host suppresses the native drop and reports the items; the server takes
  them out of its inventory record (never more rounds than it holds) and
  creates the pickups, as it does for a dead player.
