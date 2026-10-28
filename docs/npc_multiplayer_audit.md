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
| Traffic cars | Done (see Car traffic) | Driver AI (`C_driver`, 0x29c bytes) holds pointers into `C_roads`; crossing locks (`LockCrossing` 0x4b3ea0) are per process, so one host runs all of it. Spawn/despawn is camera relative (`ActivateElems` 0x4ab910, `GetNewPosInfo` 0x4af1c0) and is driven per player. |
| Police | Hardest | Per-person crime records are generic (`C_shvestky::m_Persons`), but the dispatcher (`g_PoliceNastenka` 0x646d10), town freeze and arrest flow are single instances; arrest (`PlukovnikuMamZmrdaPlayera` 0x53cbe0, called at 0x517260) ends the local game for any target. |

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

## NPC humans (gangsters and police on foot)

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
  source walker at the gangster's last pose. A living entity of behaviour 4
  (`DoesntReactOnWeapon`, C_human +0x5fc, every recruited officer) is retired
  by its case instead (0x49eb8f-0x49eba7): never while on one, and at once,
  wherever the camera is, when the police reset bit (0x08 of state priority
  +0xf48) is set; only a seated one keeps its car near the camera. The bit
  comes with the police state's idle substate, which only breathes (after an
  arrest or fine, a call-off, a case gone). The host leaves those officers in
  the pass, so one near a player turns back into its walker as in retail
  instead of standing frozen.
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

## Scripted NPCs

`Npc.spawn` makes a `C_entity` on the simulation host (`ScriptedNpcHost`,
`features/npc/scripted_npc_host.cpp`), tracked and reported like every other
NPC (kind `scripted`, server keys with bit 31 set). The server creates the
entity at once and sends `Npc::Create`; tasks (`Npc::Task`) and orders
(`Npc::Order`) follow on the same ordered channel, and the host answers each
task with `Npc::TaskDone`.

- Creation is `CreateMafianos`' (0x459b90) sequence: `C_mission::CreateActor`
  (27), `Init`, then behaviour mask (C_human +0x5fc: 2 guard, 8 reacts on hit,
  16 moves away), initial health (+0x604), morale (+0x63c) and aggressivity
  (+0x620) before `AddTemporaryActor`, whose `C_entity::GameInit` maps the mask
  to the behaviour class (+0xf4c) and copies the initial properties. It is in
  no retirement list and never network controlled.
- Each task makes the calls of the retail entity script command
  (`C_entity::Process` 0x51fda0, IDA): `Entity_ActionFollow` (0x520c04-0x520cdc:
  `NewFollow(leader, distance, 19, flags)` job, `NewTurnTo` idle,
  `ai_follow_manager::AddFollower`, `ForceAI(0, 1, 0, 0)`), `EntityMoveToFrame`
  (0x521f3d: `NewMove(position, 0, 19, 2 | run, radius)`; `Entity_Move` 0x51ff00
  passes run as bit 0), `Entity_MoveToCar` (0x52063f: `NewMoveToCar(car, 200,
  0.3, seat, run, 19)`) followed by `Enemy_UseCar`'s `Use_Actor(car, 1, seat, 0)`
  (reM C_entity.cpp 3431), `Entity_CarMoveTo` (0x520a36: `C_sledovacka::
  AddCarMoveTo` 0x4a1220, then `ActionStatus` 0x4a8670 until 1 arrived, 2 failed,
  3 blocked; `RemoveCar` 0x4a1c30) and `Entity_ActionFire` (0x5213a3-0x5214ae:
  `WeaponChoose`, then `NewJob(NewFollow(target, best range, 19, 8))`,
  `NewIdle(NewTurnTo)`, `NewModifier(NewFire(target, -1, ((flags & 0x8000) |
  0x10000) >> 14))`, `AddModifier(NewCarJump(1))`; seated, `NewJob(NewFire)`).
  `ForceAI` 0x523d30 does nothing while the process interval (+0xafc) is 0.75
  or more; an entity without a program has it at 0.
- The host watches the job's uid in the action manager (+0x1164, jobs vector
  first/last at +0x4/+0x8). A fight (`StateSwitcher`) replaces the jobs; once the
  entity is idle again (+0xf40 0) the host issues the task again (a walk at
  most three times). `C_game::InvalidateActor` 0x5afe40 already clears every
  entity's actions and followships aimed at a removed actor and a removed
  car's `C_sledovacka` record (`VypicujMiToyotu`).
- A car a scripted NPC boards becomes the host's: the server sets `hostDriven`
  and makes the host its controller (no traffic key, no player in it); host
  NPCs may only take seats in cars the host drives (`SeatService::
  BlocksNpcEntry`). The host's `CarService` now creates the native car of a
  host-driven car without a traffic key instead of waiting to adopt it. The
  server gives the car back (`hostDriven` false, `UpdateCarControllers`) once
  no scripted NPC boards or sits in it.
- Not done: following a player into a player-driven car (the follow manager
  boards the leader's car, which `BlocksNpcEntry` refuses), script animations
  (replicas would not play them) and changing the behaviour class at runtime.

## NPC melee

Retail NPCs swing through `C_human::Do_Shoot` 0x583590 like the player:
`C_entity::Do_Shoot_Timed` 0x528680 calls it at 0x52872d (press) and 0x52873d
(release). The host's `DoShootHook` reads its tracked NPCs' swings as it reads
the local player's (armed on press, release changed the combo, animation, work
state or overlay, else a cancel) and sends `Npc::Melee`; the server relays it as
a `Combat::Event` (`MeleeStart`, `Melee` with combo index or heavy finisher,
`MeleeCancel`), which every player replays on the NPC's replica with the
player-replica path. Before, only the hit arrived (`PlayerHit`) and the replica
stood idle: `ClassifyLocomotion` turns the swing's animation states into idle.
A hostile thrown-out traffic driver gets item 6 one time in five, otherwise
item 4 (baseball bat) half the time, otherwise fights with its fists (reM
C_human.cpp 8202-8224), so most of those fights are melee. The host now also
reports a weapon change before a shot or swing (`SendInventory`), since the
server takes a shot or swing only with the weapon it records in hand and the
first one after drawing used to be dropped.

## Police on foot

- Mission script alone turns the police manager on (`C_shvestky::SetActive`
  0x49b860, `ret 0Ch`, only caller `C_program::Process` 0x472409). Mod missions
  run no script, so the host calls it once per mission. It also fills the police
  car, barrier and roadblock officer pools. The police cars and their officers
  run natively (see Car traffic), and so do road blocks (`GenerateRoadBlock`
  0x49d980, see Road blocks).
- Crime records are per person (`AddPerson` 0x49c7e0 / `DelPerson` 0x49ca50,
  `ret 4`, active flag at +0x0, persons at +0x3c). The host records every
  spawned player's replica and drops the record before the replica goes, since
  `Tick` reads each person (0x49a5bb, 0x49aaca): on its removal, in the actor
  destructor, and for all of them at once when the mission closes, the
  connection closes or the client shuts down (`NpcHost::Reset` runs before the
  player replicas go). `C_entity::InvalidateActor` 0x528890 already clears
  the police board (`ZinvalidujAktora`) and officers' actions for a removed actor.
- `FizlosNamakatDoAkce` 0x44fde0 recruits active walkers of type 1 or 3
  (+0x15a) into `C_entity` officers at the walker's position from a duplicate
  of its model, and appends each to the retirement list; the host tracks them as
  `NpcKind::Police`.
- The police action is type 33 (`C_entity::actionTick` 0x50a2c0, `ret 10h`;
  payload at action +0x20, state +0x34, mode +0x44, target +0x58). IDA, not reM:
  mode 0 plays the arrest animations and calls `PlukovnikuMamZmrdaPlayera`
  0x53cbe0 (clamps the board's shift timer +0x24 to 12000, after which
  `C_shvestky::Tick` opens `GM_Menu_gamefailed`); mode 1 is the fine and at
  state 12 calls `C_player::LockControls(false)` 0x595f00 on the target without
  a type check, which writes +0xada, inside a `C_entity`'s own fields. Mode 0
  calls `PlukovnikuMamZmrdaPlayera` at 0x517260 as the animations start (state
  11); both modes finish in state 12 once the animations end, with
  `MakamSefe(false)` 0x53d490 (0x517298) just before the fine's `LockControls`.
  The host skips the shift timer clamp and reports both as `Npc::Caught` when
  they finish (server event `playerArrest`): the arrest at that `MakamSefe`, the
  fine at its `LockControls`. An officer killed or called off during the
  animations caught nobody. Retail's grab (0x515cfa-0x515d68) settles its own
  player's record, locks it and blocks new dispatches for it; for a replica the
  host settles it at the grab and keeps its crimes and pending level clear
  until the catch ends, since the replica keeps its weapon and a car case's
  members (the grabber included) would otherwise get GO_ARREST and a new action
  mid-animation. At state 11 the host reports `Npc::Seized`; the server relays
  it to everyone but the host, the arrested client plays `PolicajtZatykany` /
  `PolicajtPokutovany` on its ped with controls locked, the others on its
  replica, and every client (the host natively) holds the pose stream off the
  replica until the track ends. It settles the record (`DobreMiPokuril_MuzeJitDom`
  0x49f850, field resets only), and skips `LockControls` on non-players and
  `G_Camera::CumNaNe` 0x5f3a70.
- `AI_ACTION_KICK_DRIVER` (type 35, police mode 0 and gangster fights) calls
  `Do_ThrowCocotFromCar` 0x587d70 beside the seat; the thrower never takes the
  seat. `intern_ThrowMeFromCar` 0x588b40 frees it, plays the throw-out and drops
  the victim's items. The host reports it (`Npc::Kick`); the server frees the
  seat and every client replays the throw with the NPC replica, as for a
  player's steal. The thrower plays its pull-out as state 306/307, which
  `C_human::Update` 0x572f8a returns to idle at track end; NPC and player
  replicas hold the pose stream off it until then (5 s bound). A replay the
  native refuses is logged, and reconciliation then frees the seat without it.
  Host NPCs never enter a network car (`Use_Actor` enter vetoed).
- Open risk: `ai_fight_manager::Tick` 0x5350b0 dereferences `C_game::m_pPlayer`
  without a check at 0x535445 for a fight party that owns a car. Such a party
  needs a script-defined AI group (`ai_groups` lookup at 0x535e70), which mod
  missions do not create.

## Every player is a target

- `C_entity::Hit` 0x507ed0 (vtable 0x62510c, `ret 20h`) runs `C_human::Hit` and
  then `C_entity::Event(attacker, 0)`, which makes the attacker an enemy in the
  NPC's logs. The host applies a player's validated hit through it (one event
  per hit) and drops the replayed shot there.
- `ai_logs::Event` 0x4f6ea0 rates a type 2 player as behaviour class 0 but a
  type 27 entity by its own class (+0xf4c, 0x4f6f0c). The host sets every
  player replica's class to 0, so NPCs rate players exactly as retail rates its
  own. Class 0 is `ENTITY_BEHAVIOUR_GUARD`, but a replica never runs entity AI:
  it is network controlled, and `C_actor::Tick` 0x4064f0 then calls the empty
  `NetDirect` instead of `AI` (where `StateSwitcher` and the actions run).
- A cop (class 2) never changes offender by itself: `C_entity::Hit` escalates
  only when its action (`MamCisloDejAkci` 0x53bbf0 on +0xf54, offender +0x60)
  already targets the attacker. When a player who is not its offender hits it,
  the host moves it with `ZinvalidujAktora` 0x53c6f0 and a one-officer
  `MamProVasAkciHosi` 0x53bc50 (mode 0, as the ARREST_PLAYER script command
  does), at most once every 5 seconds, and only while the police are on.
- Retail has one player, so an officer only ever chases its case's offender.
  Board actions (`ai_police_action`, 0x6c bytes, mode +0x2c, offender +0x60)
  have modes kill 0/1, pursue 2/3, arrest 4/5 (car/foot), warn 6 and wind
  down 7; `MamProVasAkciHosi` takes the car mode and opens the foot one when
  its first officer sits in no car, and several actions may share an offender
  (their officers join one `ai_group`). Once a second the host moves an
  officer on foot (modes 1, 3, 5; not mid-arrest, police action states 11/12)
  to a wanted player within 40 and closer than half its offender's distance,
  in the mode that player's record would send (`VasekSelChlastatNaOslavu`
  0x49f980: lethal kill, arrest arrest, minor pursue), and moves it between
  the two records' officer lists (`AddNasrany` 0x49f5d0; Tick drops one by
  moving the last entry into its place, 0x49acee). A player who hurt it in
  the last 30 s keeps it. Car hunts, road block officers and winding-down
  cases are left to retail.
- Gangsters and other NPCs (`PickTarget`): retail's angriest log entry stands
  unless it is a player, or a player hurt the NPC in the last 30 s; then the
  player that hurt it most, else the nearest player it is at odds with, with a
  hold against flapping. `StateSwitcher` 0x5291d0 weighs the returned entry's
  anger (+0x4c) against the fear of `GetMaxFearCharacter` right after the call,
  so the player's entry briefly carries retail's pick's anger; it gets its own
  back before the NPC's next pick and on the host's next update.
- The host applies a hit on its NPC natively unless the attacker is a player's
  replica or a network car a player's client simulates (that client reports the
  hit, the server validates it). A traffic car the host drives hits its NPCs
  natively (`C_human::Collision` 0x5784a0: `Hit(8, ..., (|v| - 2.5) * 4.2, car)`
  above 3), and the blast of every exploded car, grenade or script explosion
  replays natively on the host, where `ApplyExplosionDamageToActor` 0x5ab680
  passes no attacker; the server's area damage covers players only.

## Police records for scripts

- `S_shvestky` (0x80 bytes): person +0x00, crime bits +0x08 (u16), level +0x3c
  and pending level +0x3e (u16), pending message +0x64, officers vector at +0x70
  (first +0x74, last +0x78). `m_Persons` first/last at manager +0x40/+0x44.
- The host wraps `C_shvestky::Tick` 0x49a300 (`ret 4`): script orders before it
  (a higher pending level dispatches the retail way at 0x49b29f; lowering or
  clearing settles the record and ends the actions on the player), record
  changes after it. An order for a player whose replica is not recorded yet
  (just spawned) waits up to 3 s. With police off it clears crime bits and
  pending levels before and after each tick (the evaluation's own detectors set
  them again every period), the server refuses raising a level
  (`Police.setWanted` returns false) and a hit officer opens no new case.
- `Tick` evaluates every person only when its timer (+0x04) passes the period
  (+0x08, 0x49aa7f-0x49aa9d), after its early returns; the host runs retail's
  player block for every player after such a tick only.
- A new simulation host starts every record clean and reports only changes, so
  on a host change the server drops its records and emits `playerWantedChange`
  to 0 for every player it had above 0.
- The host settles records unpunished: a punished settle counts an offense for
  everyone but `C_game::m_pPlayer`, which would escalate repeat catches.
- Players' clients show the retail feedback themselves:
  `G_IndicatorsClass::ConsoleAddTextByID` 0x5f9de0 and `SetWantedLevel` 0x47a300
  (badge drawn with indicator flag 0x4000000), both `ret 8`.
- Retail detects lockpicked car theft (`C_player::Can_UseCar`), speeding and red
  lights only for the local player or with car physics the host does not run,
  so those crimes rarely or never arrive for players yet.

## Car traffic

The host runs the one retail `C_traffic_car` (actor type 12, 0x26c bytes) for
every player (`features/traffic/traffic_host.cpp`). Its pool cars are `C_car`s
driven by `C_driver`; drivers and passengers are I3D models linked to the car
frame, never `C_human`s.

- `C_traffic_car::AI` 0x4afc40 (`ret 4`) copies the active camera into +0x23c /
  +0x248 and, while +0x70 is set, spawns every 350 ms (+0x260) with
  `ActivateElems` 0x4ab910 (`ret 8`). It despawns a driving car beyond the
  camera visibility radius of +0x224 (180) and turns on collision response
  (`C_car` +0x20f8 |= 0x10, `VEHICLE_RUNTIME_COLLISION_ACTIVE`) within 8 of
  `m_pPlayer` (0x4b2deb-0x4b2dfa). The host runs AI with +0x70 clear and +0x224
  huge, then despawns a car once no player is within 180 (a player's camera can
  look anywhere), sets 0x10 near any player, and spawns for the player with the
  fewest cars nearby by writing that player's position into the camera fields
  before `ActivateElems`. A new car closer than 95% of the spawn radius to any
  player is removed again at once. With no player spawned (a sole player
  respawning) nothing despawns.
- A police car going back to traffic waits in `gSpawnedCars` (0x63d86c/0x63d870);
  `ActivateElems` seats the first inactive one in a free slot before any new
  car, at its own pose (0x4ab980). Retail's AI frees a slot for it when the pool
  is full, inside the spawn block the host's clear +0x70 skips; the host does
  the same (the pool car farthest from every player) and exempts it from the
  spawn clearance.
- The server takes at most `Shared::Traffic::kMaxHostCars` (192) cars the host
  drives (a wreck the host already retired does not count); the host reports
  no more: past the cap a pool car leaves the road again and any other car
  waits unreported until there is room.
- The mission loader drops every mission-file actor but doors and pedestrian
  generators; the host also keeps the traffic actor (type 12, freeride's
  `cars`), players never do.
- Each player gets retail's density: the next car goes to the player with the
  fewest within 180, while that count is below the mission's +0x21c.
- `GameInit` 0x4b54a0 sizes the slots (+0x220) as +0x21c times
  `g_fTrafficCarDensity` (0x63d7fc, the profile's freeride density); the host
  runs it at density 1 and scales +0x21c by `simulation.trafficPoolScale`
  (default 8) for the call, at most 100 (the
  spawn schedule has 100 entries and is walked with the slot count as window).
- Every despawn, death and replacement ends in `DeactivateElem` 0x4ab140
  (`ret 8`); the host releases its network car before it runs, since
  `DeactivateCar` would otherwise meet the network car deactivation guard.
- Retail swaps a pool car for a temporary `C_car` when it leaves traffic:
  `SebraliMiKaaaruSvine` 0x4ba300 (a steal; also NPC groups), `Krles` 0x4b95a0
  (a rammed mafia car, crew in 0x63d818) and `HulaaaaaMuf` 0x4b8940 (`ret 10h`,
  traffic police cars become police cars at the same pose). The network car moves
  to the new native car, so players keep theirs.
- The steal (`C_player::Do_AB_OwnerNULL` case 2009) is
  `Do_ThrowCocotFromCar` 0x587d70 (`ret 8`): `Corovacka` takes the crew model,
  a new `C_entity` from its duplicate is seated, thrown out
  (`intern_ThrowMeFromCar`), hostile to the thief one time in three, and added to
  the retirement list. It reads `g_pMission` only for the police manager and car
  hunts, so the host runs it on the stealing player's replica.
- Retail offers 2009 only for a seat whose `m_fMass` a seated human set, and
  a player's crew is only a model, so the game offers an entry (2003). The thief
  instead calls `Do_ThrowCocotFromCar` itself. With no native owner, `Corovacka`
  finds no pool car, so only the thief's pull-out plays. Then it sends the
  `StealIntent` and vetoes the entry; the next use gets in, as in retail, where
  the throw does not enter. Retail offers a car's seats only while it is nearly
  still (`C_car::Update` 0x41fac0: `m_fSpeed` below 0.3); the client and the
  server take a steal up to 1.0 (`kStealMaxSpeed`, the host's reported
  velocity lags). The server answers every intent of a known player (a
  `StealRefused` echoes its mission generation) and refuses one sent within
  500 ms of that player's previous one without the `stealRefused` script event.
  The host's `Takeover` (throw, `ReplaceStolen`) runs whether the replicated
  `hostDriven = false` came first or not. The server broadcasts `SeatAction::StealTraffic` so
  other players replay the pull-out. The host reports the thrown driver with its
  car (`Npc::Spawned::carId` -> `NpcEntity::thrownFromCarId`). A player who
  played that steal in the last 3 s seats the new replica (`Intern_UseCar`) and
  calls `intern_ThrowMeFromCar` 0x588b40 on it, as `Corovacka` does. A thief
  already getting in holds seat 0 (`Use_Actor` sets the owner as its door
  animation starts, 0x582568), so the replay skips it.
- A seated human in a car the host drives is the host's NPC. Retail's
  occupied-seat use (`C_player.cpp` 2009, offered only while the car is nearly
  still) throws it out straight away (`Do_ThrowCocotFromCar`). A player does
  the same to the host's NPC in any seat but a police car's: the thief's
  client plays it and sends `StealBegin`, the server frees the seat
  (`SeatGate::NpcThrow`) and broadcasts it, and every client replays it, the
  host on its own NPC, natively. Should the host's native throw be refused,
  the host reports the NPC's seat again and players seat its replica back.
  The thief then gets in with the next use, which hands the car over.
- Crew model names are checked by the server before every client opens them:
  at most 63 printable characters, no path separator, drive, wildcard or `..`.
- A bullet on a crew model reaches `DostalJsemRanuKachlou` 0x4badd0 from
  `C_car::Hit` 0x423728 after a hierarchy ray test; players report it as a car
  hit and the host's re-ray finds its own crew model.
- `CoDelasTyCuraku1` 0x4b94d0 (`ret 4`) counts only `m_pPlayer` or a car with
  +0x215c set as the rammer; the host replaces it with "a player's replica or a
  car a player drives". `C_car::Hit` calls it (and the police one, 0x4b9460) with
  the shooter and force 1000, so the host passes the shooter's replica to
  authoritative car hits.
- Police cars: `VasekSelChlastatNaOslavu` 0x49f980 sends cars only while
  `GetFreeEnemy` 0x49e530 is above 1, seats pool officers with `Intern_UseCar`,
  hunts with `C_sledovacka` and sets the siren (+0x4a1). The host no longer
  blocks it, nor road blocks (`GenerateRoadBlock`).
- Road blocks: the host watches the manager's barrier pool (+0x60/+0x64, 8-byte
  slots, free +4) and reports each placed `C_irenka` (`RoadBarrierEntity`).
  Players build their own as the pool does (`barrier.i3d`, `Init`,
  `SetRemoveFrame`) and place it as the road block spawn 0x49d750 does
  (`Deactivate` 0x445bb0, pose, `Activate` 0x445be0), so cars collide with it.
  `C_irenka` is the spike strip: `C_car::Collision_Filter_Body` flattens a tyre
  whose contact sweep crosses it (`ZapichameSi` 0x445da0, wheel flag
  0x80000000 `VEHICLE_WHEEL_SPARE`, `TyreBreakSnd`). Road blocks are its only
  source (`SpawnRoadBlockIris`, only caller `GenerateRoadBlock`; `GetTempIris`
  0x49e560 has no other caller). Only a car's simulation controller rolls over
  it (the host for its traffic, a player's client for their car); every other
  copy ignores it, and the flat reaches them in the car's damage state, which
  carries the persistent wheel flags. A pool barrier retired and taken by the
  next road block in the same `C_shvestky::Tick` never shows free, so the host
  also reports a barrier that stands at a new pose as removed and placed again.
- Reinforcements: `C_shvestky::Tick` runs the hidden flag (+0x51) and
  `SendReinforcements` 0x49eee0 only for `m_pPlayer`
  (0x49b4c8-0x49b68a); the host runs that pass for every player's record.
- Drive-bys: the `C_entity` fire action opens the seat window, aims with
  `SetShootTarget` and fires with `Do_Shoot_Timed`; `C_human::Shooting` fires a
  seated human only in vehicle-anim state 2, which `Movement` (work state 9)
  enters while aiming. The host's capture wraps `Shooting`, so the shot, aim and
  window replay on the seated replica as a player's drive-by does. A target on
  the far side makes it climb across (`Do_ClimbInCarLR`); the server records
  that as `SeatResult::Moved` and players replay the climb.
- Explosions: a traffic car burns out and explodes on the host as its
  controller, through the network car explosion path (intent, `Exploded`,
  `CarExplosion` 0x421d60 replayed on every client). A car not yet bound to its
  network car holds the explosion (up to 3 s; a burning car asks each tick), so
  players never miss one. `CarExplosion` makes a new temporary wreck `C_car`
  and marks it and the original dead (+0x5e); the crew it burns
  (`EineMeineKleineAutoInLuft` 0x58a5a0) sits in the wreck, so the host skips
  dead cars when it looks for new ones. Retail's blast (`NewExplosion`
  radius 10, damage 200) reaches players on foot from the server, for any
  exploded car, and the host's NPCs natively. An exploded pool car stays in its slot until no player is near,
  then its network car goes after `TrafficService::kWreckLifetime`.
- `C_sledovacka` (C_game +0x2b18, hunts at +0x14/+0x18, 0x44 bytes, car +0x00,
  target car +0x08, target +0x0c) is never cleaned for a removed human; nor is a
  car police action, which `ZinvalidujAktora` winds down keeping its target
  (+0x60). Before removing a player's replica the host ends both
  (`RemoveCar` 0x4a1c30, `AkceDokoncenaPlukovniku` 0x53c550) and drops its crime
  record.
- NPC targeting: `ai_logs::GetMaxAngryCharacter` 0x4f7850 (logs at C_entity
  +0xe88) feeds `StateSwitcher`'s fight target (+0x1280) and
  `Can_Hit_FilterHumanType`; see Every player is a target.
- A player's predicted car-impact reaction (the server's answer is a round trip
  away) is keyed by car and target; the server's `DamageEvent` names the car
  (`sourceCarId`), so another car's impact on the same target still plays. Only
  the reaction is predicted, never health.

Players see a traffic car within 260 (`TrafficService::kStreamRange`). Its crew
models are created on their clients at the seat in-point (`C_car` +0x21cc, 0xa0
each), the driver posed by `AddDriverMan` 0x5b07e0, the passenger by anim 0xf8
(`GetAnimIndex` 0x408760, anim_info 0x62e488). A player takes a traffic car only
by a steal of its driver's seat or by entering a seat nobody holds; police cars
stay closed to players, as `SetPlayerUseDisabled` makes them in retail.

The crew carries its model names, so a walker that takes a parked car
(`ChodDoRici`) shows its own model. A driver that parks plays the door animation
out (`GetAnimIDForUseCar` 0x57dfc0, exit) and a walker taking a parked car plays
it in from `GetPlacementFromTCK` 0x41c550, as `C_traffic_car` does; only the
driver's door is animated there. Walkers with no pool are reported too
(generator 0xff).

## Trams and metro trains

Freeride's scene2.bin has 38 type-8 records, each one complete `C_railway`
vehicle (31 `salina` trams, 7 `metro` trains); tracks and stations are web
nodes, and freeride has no turnouts (24) or rail generators (38). Every client
loads them (`LoadMissionActor`) and keeps their frames (`KeepMissionActorGeometry`),
in the same order, so a railway is named by its load index plus an FNV-1a hash
of its frame name (`Shared::Railway`).

- The simulation host runs them natively. `AI` 0x488140 returns while the
  police freeze 0x63d7ac is set (`SetTrafficVisible(false)`), so the world hook
  lifts it around each call. Far from the camera (squared distance against
  250000, 0x624050) AI skips stations, braking and `Collide`, and `Update` hides
  the model, so the host patches the `jnz` at 0x4881ee (75 1E) to `jmp` (EB 1E):
  every railway runs as if near, everywhere on the map.
- The passenger-traffic mode (+0x2a8) is never written by the constructor
  (0x4865b0) or `GameInit` 0x48a390; every client sets it to active (0).
- The host reports all of them at 10 Hz (`Railway::Snapshot`: body position and
  direction at C_actor +0x24/+0x30, each wagon from +0xbc/+0xc0, speed +0xd4,
  state +0xdc). The server forwards to each spawned player the ones within
  600 m in every snapshot and all the others at 2 Hz: a player's copy runs no
  AI, so a railway it was not sent stood where it was last placed (seen from
  afar, the metro's raised track most of all). Players draw each railway two
  of its report intervals behind (200 ms near, 1 s far), easing between the
  two as it comes into or leaves the range.
- Players make theirs network controlled (+0x20): `C_actor::Tick` 0x4064f0
  then calls the empty `NetDirect` 0x4ab130 instead of AI, and `Update` 0x4894d0
  still places the frames and dynamic collision from those fields. Players
  interpolate two mean report intervals behind the host's clock (slewed
  toward the least delayed packet), write those fields in a hook just before
  `Update`, so the frames, passengers and riders share one placement each
  frame, and continue at most 300 ms past the newest report.
- Railway AI is not deterministic (`IsOnSchedule` 0x4979b0 reads
  `timeGetTime`, braking depends on local cars and traffic lights), so players
  never run it.
- Only the host's `C_railway` sweep hits humans (reM C_railway.cpp:2474,
  `actor->Collision`; `C_human::Collision` railway branch: `HIT_TYPE_MELEE_STRIKE`
  for (|v| - 2.5) * 4.2). On a player's replica the Hit hook sends
  `Railway::PlayerHit`; the server applies it to a player on foot within 25 m of
  that railway's body or wagons, once per 500 ms, as a knockdown. Host NPCs take
  it natively. A car only meets the railway's dynamic collision on its owner's
  client.
- A bullet on the driver (0x490b40) stops a railway in state 11 (retail's
  rail human stop) until `Update`'s out-of-range reset (0x4899de), which never
  comes on the host; the host applies that reset after 30 s once no step
  holds it, putting a dead driver back in the cab (`ridic` dummy, driver
  state 19, ranims 14) as retail does. Players keep their own driver, so they
  never see the host's die.
- Riding. AI registers the doorway use objects (`AddOuterEntries` 0x495100,
  `AddInnerEntries` 0x495160) only for its own local player, so players add
  theirs where AI would: `isInStop` 0x496db0 (states 1-4, or a tram, not a
  metro (+0x1d8), standing at exactly 0 in states 7, 10, 11 or 12) plus the
  schedule wait (5), which keeps the station's doorways until it departs.
  Retail `Use_Actor` 0x582180 locks the railway (`Lock` 0x497980, count
  +0x288) for a step aboard and a step off alike, so it cannot depart
  mid-step: a player's step first asks the host (`Railway::BoardRequest` ->
  `BoardAnswer`), which locks its railway until `BoardDone` or 5 s, and only
  then runs the native step, if the player is still alive, on foot (or aboard
  that railway, to step off) and within reach. The host holds only stops
  whose departure waits for the lock count (not collision braking, 7, which
  departs after its wait regardless), at most 20 s per stop in all, and the
  server takes one request a second per player. A rider reports its
  pose in the basis of the nearest part (body or wagon direction with world
  up), and every client places that rider on its own copy. Leaving a mission,
  disconnecting or a railway's `GameDone` first steps a rider back into the
  world (`Intern_UseRailway(false)` 0x571b90): its frame is the wagon's child
  and its collisions are in the wagon's list.
- Passengers (`S_rail_human`, the driver at +0x2b0 left out) are moved,
  animated and let off only by `trafficAI` 0x4845f0, which AI calls, so a
  player's copy kept `GameInit`'s random seated ones frozen while the host's
  walked out of the doors and became walkers there (`RemovePerson` ->
  `AddFreePerson`), which players saw appear from nowhere. Players now hide
  their own and show the host's: 5 Hz `Railway::Passengers` (every passenger of
  the listed railways, with model, ranims index and part-relative pose; the
  server forwards each player the railways within 600 m) drives cosmetic
  models eased across a report and placed in the scene sector they stand in.
  A passenger's model name rides only with its first three reports and one in
  ten after; a new id waits for one that names it. The boarding and alighting animations
  (`salina nastup`/`vystup`, which move along their track) play as `walk1`,
  since the reports move the model.
- The server takes a rider's claim only within reach of the host's report
  of that railway; a rider is then bounded by it, not by the on-foot speed,
  and its descent from the metro's raised track is not a fall.
- A new simulation host is told apart by `HostSession` in each snapshot, so
  players drop the old host's samples and clock at once.
- Not synchronized: the railway sounds AI plays, a player's bullets on a
  railway's driver or passengers (their copy of the hit stays local), and
  the host's driver dying.

## Drawbridges

The river's drawbridges are type-20 `C_bridge` actors (vtable 0x625588), a left
and a right one (frame name starting with `L` or not), each turning its own frame
(C_actor +0x68) and a counterpart (+0x90) about their local z axis. Every client
loads them (`LoadMissionActor`) in the same order; a bridge is named by its load
index plus an FNV-1a hash of its frame name (`Shared::Bridge`).

- `AI` 0x567fb0 is a phase machine on +0x144 with its timer at +0x150: closed
  until a steamer asks (`g_sBridgeRuntime` 0x646f80, requests +0x1c; AI counts
  the 120 s closed timer down but never tests it, 0x5683cd), signaling (4 s,
  road signal +0x138 red) until `C_traffic_car::GetBridgeCarCount` 0x4ba930 is
  0, preparing until its sound ends (at once on the host: `I3D_sound::IsPlaying`,
  LS3DF 0x10057c40, is false without a voice), opening (30 s), open (10 s, and
  on while a steamer asks), closing (20 s). The leaf angle (+0x148) follows the
  timer up to the open angle (+0x178, 50 degrees); AI applies it to both frames
  (`SetDir`, vtable +0x0c, opposite signs, +0x174 swaps them). Opening inserts the
  dynamic collision over `watercity.nadparnik1/2` (+0xb4, `DynInsert` 0x5c36c0),
  closed erases it (`DynErase` 0x5c38b0); both are idempotent. `Collision`
  0x56a7e0 (ret 18h) zeroes a `C_player`'s health and hits it with a fall,
  which the protected local player survives: `Death` runs only inside `Hit`.
  The client hooks it: the local player reports a fatal fall (`FallVolume`),
  which the server applies; a replica is left to the server's health.
- `Update` 0x568540 moves the leaves' collision primitives with the frames,
  blinks the signal lights on +0x154 and, on the right bridge, moves the two
  `lodriver.i3d` steamers along four routes (entries at 0x646fa8, 0x24 each:
  route, point, progress +0x0c, speed +0x14), stopping them at a closed bridge
  (open flags +0x14, written by AI) and picking the next route with `rnd`.
  `GameInit` 0x569510, on the right bridge only (`m_eSide`), randomizes the
  steamers and warms up with 60 AI and Update calls of a second; `GameDone` 0x56a600 releases the sounds and steamers.
- The host runs them natively (the former `ShutDown(true)` at GameInit is gone).
  It reports every bridge's phase, timer and angle and both steamers at 5 Hz,
  reliably at once on a phase change (`Bridge::Snapshot`); the server checks the
  sender is the host of the mission generation, validates the phase, angle,
  timer and steamer route, point and progress, and forwards to every spawned
  player.
- Players make theirs network controlled (+0x20), so `C_actor::Tick` 0x4064f0
  runs the empty `NetDirect` instead of AI. A hook before `Update` interpolates
  400 ms behind the host's clock and applies what AI would have: on a phase
  change the motion and request sounds (`ConstructSound` 0x55f820; one-shot
  sounds only for the next phase in order), the road signal, the light timer and
  the dynamic collision; then the angle to both frames, and the steamers'
  positions with speed 0, so `Update` places them without moving them on.
  Until the host reports, a player's bridge stands closed, and it closes again
  when the host has sent nothing for 3 s, so a host that left never keeps the
  river road raised. A new host is told apart by `HostSession` in each
  snapshot; the server keeps only reports newer than the last on that host's
  clock.
- The host has no sound driver; `I3D_sound::IsPlaying` without a voice is false,
  so its preparing phase should end at once and players rarely hear the
  preparing sound. Traffic waits at the bridge only on the host,
  where it runs.
