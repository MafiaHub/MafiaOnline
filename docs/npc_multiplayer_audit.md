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

- `npc.playAnimation` sends `Npc::Animation` to the host, which plays the clip
  as `PERSON_PLAYANIM` does (`C_program::Process` 0x470393:
  `C_human::Do_PlayAnim(name, simple, 1)`, 0x585e80) after ending the running
  task, and stops it with `Do_StopAnim` (0x57d2e0, `PERSON_STOPANIM`); any
  task stops it too. Without a displacement track (`.tck`) the clip plays on
  the animation stage while the human stays in work state 1, and
  `PersonAnim` leaves it alone while `m_iAnimState` equals `m_iAnimID`. The
  clip, its loop flag and a revision ride on the `NpcEntity` fields, so every
  player's copy replays it, also one constructed later (a looping clip only),
  and holds its locomotion stream off while it plays.

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
- Not done: following a player into a player-driven car by itself (the follow
  manager boards the leader's car, which `BlocksNpcEntry` refuses) and
  changing the behaviour class at runtime.

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
- A player on police duty (`Police.setOfficer`, `Npc::PoliceOfficers`) is
  treated as retail treats its own officers. Retail records crimes only for
  persons: `SetActive` adds C_game's player (0x49b860) and the mission script
  adds others with `POLICEMANAGER_ADD`; officers are never persons, and every
  detector (`Firing` 0x49cb20 and the rest) and dispatch looks its person up
  in `m_Persons` first. The host drops the officer's replica from the records
  (`DelPerson`), ends the board actions and car hunts aimed at it
  (`ZinvalidujAktora`, the C_sledovacka hunts) and gives it class 2, the class
  `C_entity::GameInit` 0x5074d0 gives police walkers (`DoesntReactOnWeapon`).
  `ai_logs::Event` rates an actor once, when it creates the log entry
  (0x4f6f22): class 2 is a friend to class 3 owners, neutral to 4, 5 and 6
  and unknown to 0 and 1, and a police owner (2) is neutral to everyone. So the
  host erases the replica's entry from every tracked NPC's logs
  (`ai_logs::Destroy` 0x4f7a90 on `C_entity` +0xe88, as `InvalidateActor`
  0x5288e3 does) whenever its class changes, and each rates it afresh.
  `VseckyUbozakyStopni` 0x53d530's freeze, which also tests class 2, runs only
  for a type 2 target and so never on the host.
- `Police.sendBackup` runs `C_shvestky::DalsiChalpiDoPartie` 0x49eee0 (`ret
  0Ch`), retail's reinforcements for its player's highest levels, on a wanted
  player's replica from the police manager tick: it needs two free pool
  officers (`GetFreeEnemy` 0x49e530, 0x49ef12) and the person's record, picks
  a road node at least 0.66 of the radius away in ten tries (0x49efc0-
  0x49f0c1), seats two shotgun officers in a pool car and opens a board action
  of the level's car mode (`MamProVasAkciHosi`). The host answers with
  `Npc::PoliceBackupSent`.
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

### Retiring police, mafia and road block cars

`Traffic.resetArea` and `Traffic.restartHost` reach the cars retail police
and mafia code made, not only the traffic pool's: a road block's police car
(`GenerateRoadBlock` -> `CreateCar`), a reinforcement's or a chase party's
police car (`DalsiChalpiDoPartie` 0x49eee0, `VasekSelChlastatNaOslavu`
0x49f980), a mafia pursuit car (`Krles`), and a police car back in traffic
(a dynamic traffic element, `NoTakMiTedaDajTuPleckuJaJuOdvezu` 0x4baec0). A
police one is a slot of the police car pool, never deleted: deleting it
natively, as the mod's `CarService` removal does for other temporary cars,
would leave the pool's pointer dangling. So the server only asks
(`Shared::Traffic::Reset`), and the host takes such a car off the road
through retail's own retirement or keeps it (`TrafficHost::
UpdateCrewCarRetirement`, the verdict in `traffic/crew_car_retire.h`).

Every place retail keeps a pointer to such a car or its crew, and how it is
released:

| Holder | Evidence | Released by |
| --- | --- | --- |
| `C_shvestky::m_TempCars` (the police car pool, `+0x4c`, 8-byte slots with the free flag at `+4`) | `FreeTempCar` 0x49e490 `53 56 57 8B F9 8B 47 50 85 C0 74 22 8B 77 54 2B`: finds the slot, sets it free, `C_game_RemoveActorFromAllVectors` 0x5a7d40, `C_car::DeactivateCar` 0x422ad0, frame off, `g_PoliceNastenka.ZinvalidujAktora` 0x53c6f0 (0x49e4c6-0x49e51c). | The retirement pass: `FreeMyTempActor` 0x49e7e0 (`8B 44 24 04 8B 50 10 83 FA 04 74 21 83 FA 1B 74 13 83 FA 1F`) sends a car to `FreeTempCar`, an entity to `FreeTempEnemy`, a barrier to `FreeTempIris`; anything not pooled is removed with `C_game::RemoveTemporaryActor` 0x5a79a0. |
| `C_shvestky::m_DoRiciActors` (`+0x7c`, `_First +0x80`) | `AddDoRiciActor` 0x49e820 `8B 91 80 00 00 00 83 EC 08 85 D2 56 8D 71 7C 57`; `TickDoRiciActors` 0x49ea80 `A1 8C 78 63 00 83 EC 20 56 8B F1 8B 48 10 57 8B`. | The pass itself. For each actor it retires (0x49ec99 `8B 96 80 00 00 00 8B CE 8B 04 9A 50 E8`): `GlobalDelNasrany` 0x49f540 (every crime record's officer list), and for a car `G_IndicatorsClass::RadarRemoveCar` 0x5fa720, `C_sledovacka::RemoveCar` 0x4a1c30 (car hunts), `C_traffic_car::ChlupatejVystrik` 0x4b9560 for a police car (`C_car +0x2104`); then `C_game::InvalidateActor` 0x5afe40 (every actor's used-actor, attacker and script registers, shots, fires) and `FreeMyTempActor`, else `RemoveTemporaryActor`. A far entity seated in a car is unseated first (`C_car::SetOwner(0, seat)` 0x49ebe2); a far car is kept while a seat has an owner (0x49ec39) or a human (types 2, 0x1b in `g_vActors` 0x63d2bc/0x63d2c0) uses it to enter or leave (0x49ec4a `A1 BC D2 63 00 8B 2D C0 D2 63 00 3B C5`, `+0x98`/`+0x9c`). |
| `S_shvestky::m_Nasrany` (each crime record's officers, `+0x70`, `_First +0x74`) | Road block, reinforcement and chase officers join it (`MarkActorNasranyInBlock` 0x49f580, `AddNasrany` 0x49f5d0). | `GlobalDelNasrany` in the pass. A record above level 0 that lists a crew member is an open case: the host keeps the car. |
| `S_shvestky::m_pDrivenCar` `+0x04`, `m_pRecordedVehicle` `+0x54`, `m_pCopHitCar` `+0x60` | `m_pCopHitCar` is stored by `StrcilJsemHoPoldumDoVyfuku` 0x4a00a0 at 0x4a011d (`8B 44 24 14 89 42 60`); the other two are in the SDK already. | Nothing clears them on retirement: the host keeps a car any record names. |
| `ai_police_nastenka` (`g_PoliceNastenka` 0x646d10) actions: `m_Actions` `_First +0x14`, `_Last +0x18`, 0x6c each; members `+0x1c/+0x20`, car `+0x5c`, offender `+0x60` | `CoVlastneRobimPlukovniku` 0x53bb60 `51 53 55 8B 6C 24 10 33 DB 56 57` walks the members (0x53bb9b, 0x53bbac, stride 108); `ZinvalidujAktora` 0x53c6f0 `8B 44 24 04 53 55 56 8B F1 33 ED 57 3B 46 30 75` compares the offender (`+0x60`, 0x53c749) and the car (`+0x5c`, 0x53c75a). | `ZinvalidujAktora` for a pool car (inside `FreeTempCar`). Nothing for an officer: the host keeps a car whose crew serves an action, or that an action uses as its car. |
| A recruited officer (behaviour `+0x5fc` = 4) | The pass ignores its distance and retires it only once the police reset bit (`C_entity +0xf48` & 8) is set (0x49eb9c `8B 87 48 0F 00 00 C1 E8 03`). | Its case winding down. The host does not ask before then; the pass would keep it and the car it sits in. |
| `C_game::m_RoadBlocks` (`_First +0x1b8`, `_Last +0x1bc`, 0x14 each; `m_bActive +0x02`, `m_Members +0x08/+0x0c`) | `GenerateRoadBlock` 0x49d980 indexes it at 0x49e045 (`8B 8D B8 01 00 00`) and appends members at 0x49e05a (`8B 44 0F 0C 8D 4C 0F 04`); `Tick` tests `m_bActive` at 0x49afaf (`8D 44 29 02 8A 4C 29 02 84 C9`), and as a block comes down clears it (0x49afcb `C6 00 00`), sends each officer event 19 and hands every member to the retirement list (`AddDoRiciActor` call at 0x49b010). | The block coming down. An up block's members are in no retirement list; the host keeps a car an up block lists, or whose crew it lists. |
| `g_TrafficCarPursuitEntries` 0x63d818 (car, driver, passenger) | `C_traffic_car::Update` 0x4b5270 reads it (0x4b52f8-0x4b53b4): when the pursuit ends it has the crew get out and lists crew and car for retirement (`AddDoRiciActor` calls at 0x4b53f0, 0x4b5412, 0x4b5430). | The pursuit ending. The host keeps a pursuit car while its entry lasts: its crew still hunts the player who rammed it. |
| Traffic elements: `g_TrafficCarElements` 0x63d848 and `gSpawnedCars` 0x63d86c | `ActivateElems` 0x4ab910 takes a waiting `gSpawnedCars` element into a slot (0x4ab980). `DeactivateElem` 0x4ab140 frees a dynamic element's police car with `FreeTempCar` (call at 0x4ab885), or lists it for retirement (`AddDoRiciActor` at 0x4ab8a6), and deletes the element. | `DeactivateElem` on the slot of an active dynamic element (the car is judged again after it). A car a waiting element holds is kept. |

What the host does (`TrafficHost::CrewCarFactsOf` reads every holder above,
`JudgeCrewCar` decides):

- A pursuit, an up road block, a case (board action, open record listing a
  crew officer, a record naming the car, an officer on duty) keeps the car.
  A player's replica or a scripted NPC in a seat keeps it too. Nothing is
  ended on purpose to free a car: a case on a player runs out its own way
  (see "Police on foot", "NPC: Wind a police case down around its
  offender").
- Otherwise every seat owner (the crew, each a tracked NPC) and the car go
  into `m_DoRiciActors` (`NpcHost::RequestRetirement`), marked so the hooked
  pass (`NpcHost::TickRetirement`) retires them whatever the players' distance:
  it runs the native pass from far away, so crew get unseated and retired
  first, and the car goes on a later loop or pass once nobody sits in or uses
  it. Each crew NPC goes as a server `Npc.Remove` makes one go
  (`removeRequested`): `FreeTempEnemy` or `RemoveTemporaryActor`, then
  `NpcHost::Retire` reports `Retired`, the server's `NpcService` retires it
  (`npcRetire`) and `TrafficService::OnNpcGone` frees its seat record.
- The car's record goes the way any temporary traffic car's does: freed to
  the pool it leaves `C_game`'s temporary actors (`TrafficHost::Watch`), or
  removed natively (`OnNativeCarDestroyed`); either reports `Retired`, the
  server despawns the network car, and the host's `CarService` no longer
  holds it, so nothing removes it twice.
- A request still open after 30 s (`kCrewCarWait`) is dropped and logged; the
  server's `resetKept` keeps the jam watch off that car, and the next script
  reset or restart asks again.

The jam watch (`Shared::Traffic::JamEligible`, `JamScope::Automatic`) still
takes only ordinary traffic: no police car, no NPC crew, no car the host kept.

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

## Gas station attendants

The pump attendants are type-25 `C_pumper` actors (vtable 0x624990, 0x2ac
bytes, reM `actors/C_pumper.cpp`). Every Free Ride scene has three, `pumpar`,
`pumpar1` and `pumpar2`, for pumps 0 to 2 (`m_uPumpID` +0x80), each walking
between web nodes 666+2n and 667+2n (+0x78, +0x7c) to his pump `_pumpa%i`
with the hose model `_pumpa_hadice%i` (+0x234) and the camera
`_pumpa_camera%i`. Every client now loads them (`LoadMissionActor`) in the same
order; one is named by its load index and an FNV-1a hash of its frame name
(`Shared::Pumper`).

- `AI` 0x47ea30 runs `E_pumper_state` (+0xd0): the idle, idle-pause and
  smoke-loop states count the action timer (+0x25c against +0x260), the
  animated ones wait for `GetActiveAnimTime` + 250 ms, the four dialogue states
  for `DabingIsSpeaking` 0x5f5c40 on +0x230, then `ChooseNextAction` 0x47f950
  picks the next one with `rnd` (idle stand, Nuda01-03, KurakZapaleni/Loop/
  Potahnuti/Zahozeni). Every state ends with `SetAnim` 0x47f8e0 (+0xd8, finite
  +0xdc, track +0xdd, started only when it differs from +0xd4), the animation
  machine's `Tick` 0x40f1b0 (+0xe0) and the frame's `Tick` (vtable +0x14),
  whose `notify` child fires `PumperAnimEventHandler` 0x4809c0: footsteps,
  `StartPumpFuel` 0x480da0 (event 42, the looping fuel sound on channel 3),
  `EndPumpFuel` 0x480dd0 (event 43: `C_Vehicle::SetFuel(capacity)` on m_pCar
  +0x24c and `ScoreSet(score - 500)`), the four cigar and cigarette props
  (+0x238) and their smoke particles (+0xa0..+0xa8). `FUELING` also ticks the
  hose and adds `frameTime * rate` to m_pCar's fuel while +0x258 is set.
- Retail service: `C_player::Do_AB_OwnerCar` 0x594fa0 offers "use the pump"
  when `CanPump` 0x4805d0 is AVAILABLE (fuel cap within 6 m of the pump, car
  within 4 m and slower than 0.1, facing within 45 degrees). `Start` 0x480190
  greets (0x1b996f7), puts out the cigarette, aligns the car's fuel cap with
  the pump (`Deactivate`, `SetDir`, `Reset` 0x422be0 twice), sets it active,
  raises its control override (`++C_car+0x21dc` at 0x480533, which reM's Start
  leaves out), switches the engine off, locks the driver's controls and points
  the camera at him. The player answers (0xe3f763, model `C_game::m_pPlayer`),
  he walks out, `PUMP_READY` rewinds the hose and computes the rate, he fuels
  (animation 0x153), turns (turn1), says his line (+0x84, 301004 in Free Ride),
  hears the player (0x987165) and walks back; `ChooseNextAction`'s WALKING
  branch for the start node releases the car (1 m forward, `Reset`,
  `G_Camera::SetCar`, engine on, `--override`, `LockControls(false)`,
  `PopMode` 0x5f4220, a no-op on an empty stack).
- The simulation host runs them. The server hands it a service (`Command`
  Serve) only after checking the player drives that live car, the attendant
  is free (not `IsPumping` 0x481810) and the policy and price; the host then
  does Start's attendant half itself (state, idle animation, greeting, smoking
  off, m_pCar = its copy of the network car). Its `ChooseNextAction` hook
  answers the two dialogue states with the served car's driver as the model
  (the host has no `m_pPlayer`; with no sound driver a line lasts as long as
  its `tables\dat` lip-sync data), turns him back when a service is given up,
  and runs the release branch without m_pCar, reporting the end (`Progress`
  Finished). `EndPumpFuel` reports `Progress` Fuelled and runs with m_pCar
  cleared: the server fills the network car (`CarService::SetFuel`) and charges
  `Pumper.setPrice`. A served car that disappears from the host makes him give
  up (`Progress` Abandoned); he walks back the way he came.
- It reports every attendant's state, animation and flags, position, direction
  and served car at 10 Hz, reliably at once on a change (`Pumper::Snapshot`);
  the server validates and forwards to every spawned player.
- Players hook `AI`: the host's state, animation, position (followed at 12/s,
  snapped beyond 3 m) and direction are put on the actor, then the original AI
  runs in the idle state with a timer that never runs out and +0x258 cleared,
  which leaves exactly its tail: `SetAnim`, the animation machine and the frame
  tick with its notify events, so footsteps, the fuel loop, the props and the
  smoke play locally as retail plays them. The hose is ticked in FUELING. On a
  replayed state change the client speaks what `Start`/`ChooseNextAction`
  would have (`DabingSpeak` 0x5f5570 through `DabingGetFreeChanel` 0x5f4ca0 and
  `DabingSetModel` 0x5f5210), the answers with the served car's driver.
- The customer: `Start` is hooked; the retail choice becomes a `Request`, and
  the server's Accepted runs the original `Start` (`CanPump` again) on the
  local copy, so the car is aligned, locked and filmed as retail does it. The
  server's Release (the host's Finished, or the service ending) runs the
  WALKING release branch on the local copy with its replayed state put back.
  `EndPumpFuel` never fills the local car or touches the score; the car
  service would undo a local fuel increase anyway.
- Panic is blocked everywhere: `Collision` 0x47e940 and `Hit` 0x47e9c0 return
  false and `PumperPanic` 0x480fd0 returns null, because a panic hands a copy of
  his model to the local pedestrian generator (`AddFreePerson` 0x450f00) and
  reactivates only 200 m from the active camera, neither of which has a shared
  meaning here.

## Taxi passengers

Retail (reM, verified in IDA against Game.exe):

- `C_traffic_generator::TickElements` (0x44ef70) ends with the hail countdown
  (0x44f052-0x44f07c): while the mission script enabled taxi passengers
  (0x63d72a, `EnableTaxiDriver` 0x44fcf0, `taxidriver_enable`) and C_game's
  player is in a taxi (0x63d729, `PlayerInTaxi` 0x44f740: C_car's name at
  +0x218c is one of `taxi`, `hotrodp4`, `hotrodp5`, `tbirdold`, `fordhot`,
  `phantomtaxi`, `hotrodp2`, `fordapicktaxi`, `blackdragon`), it counts
  0x63d72c down and calls `GeneratePassengerForTaxi` (0x44f7d0).
- `GeneratePassengerForTaxi` restarts the countdown at 500 + 524 * rnd(4) ms,
  then needs: the player's car (+0x98, type 4), the HUD timer off, the
  player in seat 0 (+0xac), driver speed (C_Vehicle +0x59c) at most 20, taxi
  points in C_game (+0x1f8/+0x1fc, `LoadTaxiPoints`: `taxi_point*` frames),
  no officer on the player (`PocetNasranych` 0x49d460 while the police
  manager, C_game +0x2da0, is on), no passenger (seats 1 and up of the vector
  at +0xd14), and at most one walker already waving (else +4000 ms). It takes
  the last walker of the active vector that is not type 1, 3 or 4, active,
  not panic-blocked (+0x00) or panicking (+0x19e), owned by a generator
  (+0x04), following no car (+0x1a8), not waving (+0x1fe), walking (current
  animation +0x160 == 6), fully faded in (+0xbc >= 1), allowed to hail
  (+0x1ff), 20 to 90 from the taxi and within 0.6283 rad of its heading. It
  sets +0x1fe, adds 4000 ms, and plays "Mavani01/02/03 HPT.i3d" (rnd(3)) as
  the animation machine's extended animation at 0.9 with note 0xdca9 at
  machine +0x10e (0x44fc4d-0x44fcd5).
- Element `Tick` (0x45334c) runs `MavaNaTaxi` (0x451ff0) on a waving walker
  while the police manager is on and `PocetNasranych(player)` is 0, else it
  ends every wave (`ZrusVsechnyMavajici` 0x44fd10). With the police manager
  off it returns with the walker frozen.
- `MavaNaTaxi` ends the wave when the player has no car, turns the walker to
  the taxi (anims 8/9, breath 1), and once the driver speed is below 0.5 within
  10 (squared 100) of the walker (0x4522ed-0x45232f) duplicates the walker's
  model into a `C_entity` running `system\taxi_pas.dat` (a9.dta), ends every
  wave, deactivates the walker and sets the countdown to 20000 (0x452c3f-
  0x452c4b). It retires a waving walker past its despawn radius.
- `EndMavaniNaTaxi` (0x452ca0) fades the wave over 4 s when the machine's note
  is 0xdca9 and clears +0x1fe; panics and hits call it.
- `taxi_pas.dat`: waits for the cab to stop within 15, locks it, boards seat 3
  of a car with more than three seats (else seat 1), sets the compass and a
  countdown of the ride time (60 * ((20 + length / 13.8) / 60 + 1) s, whole
  minutes, from the road path), shows the destination's camera child for
  2 s, then rides while the player owns the cab. Stopped within 15 of the
  destination it pays fare + fare * time left / ride time (fare 200 * (length
  / 1000 + 1)), the passenger gets out and returns to traffic. If the player
  leaves the cab the passenger gets out unpaid.

Multiplayer: the simulation host runs the hailing for every player a script
allows (`Shared::Taxi::Drivers`) with the retail gates per driver and its own
countdown (`TaxiHost`, features/pedestrian/taxi_host.*): each driver's replica
and its replicated car stand in for C_game's player and taxi; the speed is the
car's replicated velocity, since the host's copy of a player's car runs no
physics. A waving walker ticks with C_game's player swapped to its driver;
`MavaNaTaxi` runs with the taxi's speed field held at 1.0 so retail never
makes its own passenger, and the host reports the boarding moment as a
`Pickup` instead. Hails, their ends and pickups go to the server
(`TaxiService`), which sets the walker's `taxiWave` so every player's replica
shows the same wave. A script boards the walker with `Taxi.board`: a scripted
NPC created on the host where the walker stands (`Npc::Create::walkerKey`; the
walker retires, players swap it via `sourcePedestrianId`), which enters a
passenger seat of the player's car with `npc.enterVehicle`. The server accepts
NPC seats in passenger seats of a player-driven car (no host claim), so the
passenger sits in that car on every client while the player keeps driving.
Differences from retail: hails need a script's `Taxi.setHailable`; each
driver counts only its own waving walkers and its own police record; a
wanted driver ends only its own waves; the car is not locked while the
passenger boards (the host waits for it to be nearly still); the passenger is
removed out of sight instead of returning to traffic.
