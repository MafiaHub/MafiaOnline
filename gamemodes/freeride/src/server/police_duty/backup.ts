// Calling for backup. An officer on duty rings the precinct from one of the
// city's telephone booths (booths.ts) or over the radio of their squad car.
// A wanted player near the caller gets the retail police's own
// reinforcements: a police car with two officers from the police pool, sent
// the way a rising wanted level sends them (Police.sendBackup). Anything else
// (a bounty target, hostile gangsters, a quiet street) or a refused call gets
// a scripted squad: a squad car spawned out of sight on a road, whose two
// officers board it, drive to the scene and fight the suspect or cover the
// caller. Every unit stands down when the suspect is caught or dead, the
// caller clocks off, or its time runs out; a squad drives off before it is
// removed. The duty (index.ts) configures this with its state.
import * as roads from '../lib/roads';
import * as commands from '../lib/commands';
import {
    toast,
    compass,
    markers,
    feed,
    name,
    text,
    COLORS,
    type CompassTarget,
} from '../lib/notify';
import {
    MAIN_WORLD,
    ahead,
    distance,
    onClient,
    playable,
    playerById,
    vehicleById,
    type Placement,
    type Point,
} from '../lib/util';
import { events, every, onMissionChange, onStop } from '../lib/lifecycle';
import { PlayerMap } from '../lib/players';
import { spawnVehicle } from '../lib/vehicles';
import { CALL_COLOR, boothAt, hideBooths, nearestBooth, sendPolice, showBooths } from './booths';

const COOLDOWN_MS = 90_000;
// Scripted squads on the map at once, server wide; each is a car and two
// scripted NPCs.
const MAX_SQUADS = 2;
const SUSPECT_RANGE = 300;
const GANGSTER_RANGE = 70;
// How long the host may take to answer Police.sendBackup.
const NATIVE_REPLY_MS = 5000;
// The officers of a native car join the NPCs within this long of the answer,
// this close to the suspect.
const NATIVE_UNITS_MS = 10_000;
const NATIVE_UNITS_RANGE = 260;
// Police.setWanted reaches the record through the simulation host.
const WANTED_GRACE_MS = 5000;
const SQUAD_MODEL = 'poli00.i3d';
const SQUAD_RADAR = 0x6fa8ff;
const OFFICER_MODELS = ['pol01.i3d', 'pol02.i3d'];
const OFFICER_KITS = [
    { weapon: 11, ammo: 8, reserve: 24 },
    { weapon: 6, ammo: 8, reserve: 32 },
];
const OFFICER_HEALTH = 160;
const SPAWN_MIN = 110;
const SPAWN_MAX = 220;
const ARRIVE_DISTANCE = 30;
const REROUTE_DISTANCE = 60;
const BOARD_TIMEOUT_MS = 20_000;
const DRIVE_TIMEOUT_MS = 75_000;
// A call with no suspect covers the caller this long.
const ESCORT_MS = 90_000;
const CALL_LIFETIME_MS = 180_000;
const LEAVE_TIMEOUT_MS = 40_000;
const LEAVE_DISTANCE = 350;
const ALERT_RANGE = 800;
const ALERT_MS = 60_000;
const TICK_MS = 1000;
const COMPASS_PRIORITY = 50;

/** Someone the police want: a wanted level or a bounty. */
export interface Suspect {
    player: Player;
    level: number;
    bounty: number;
}

/** What backup asks of the duty. */
export interface DutyRoster {
    onDuty(player: Player): boolean;
    carOf(player: Player): Vehicle | null;
    suspects(): Suspect[];
}

type Stage =
    | 'calling'
    | 'dispatching'
    | 'native'
    | 'spawning'
    | 'boarding'
    | 'driving'
    | 'exiting'
    | 'engaging'
    | 'leaving'
    | 'done';

interface CallSuspect {
    type: 'player' | 'npc';
    id: number;
    level: number;
    name: string;
    /** Until then a player counts as wanted before the host's record shows it. */
    graceUntil?: number;
}

interface Call {
    id: number;
    callerId: number;
    via: 'radio' | 'booth';
    suspect: CallSuspect | null;
    stage: Stage;
    /** The NPC officers: a squad's two, or a native car's once they turn up. */
    units: number[];
    /** A scripted squad's car; null for a native car. */
    carId: number | null;
    startedAt: number;
    stageAt: number;
    /** Officers on duty shown the scene, until alertUntil. */
    alerted: Set<number>;
    alertUntil: number;
    arrived: boolean;
    /** The Police.sendBackup request, replied to by replyBy. */
    request: number;
    replyBy: number;
    unitsUntil: number;
    /** NPC id -> the id of the task it was last given. */
    tasks: Map<number, number>;
    goal: Point | null;
    escortUntil: number;
    compassKey: string | null;
    ready: Set<number>;
    seated: Set<number>;
    exited: Set<number>;
}

const STATUS: Record<Stage, string | null> = {
    calling: 'Calling',
    dispatching: 'Calling',
    native: 'Police car en route',
    spawning: 'Squad car en route',
    boarding: 'Squad car en route',
    driving: 'Squad car en route',
    exiting: 'On scene',
    engaging: 'On scene',
    leaving: 'Back to patrol',
    done: null,
};

let duty: DutyRoster = { onDuty: () => false, carOf: () => null, suspects: () => [] };
const calls = new Map<number, Call>();
const cooldowns = new PlayerMap<number>();
let nextCall = 1;

function sound(player: Player, wave: string, volume = 0.8): void {
    sendPolice(player, { action: 'sound', wave, volume });
}

function onDutyPlayers(): Player[] {
    return World.getPlayers().filter((player) => duty.onDuty(player));
}

function npcById(id: number): Npc | null {
    return Npc.getAll().find((npc) => npc.id === id) ?? null;
}

// The suspect a call goes after: the nearest wanted player or bounty
// target, else the nearest hostile gangster.
function pickSuspect(caller: Player): CallSuspect | null {
    const nearest = duty
        .suspects()
        .map((entry) => ({ entry, gap: distance(caller.position, entry.player.position) }))
        .filter(({ gap }) => gap <= SUSPECT_RANGE)
        .sort((a, b) => a.gap - b.gap)[0];

    if (nearest) {
        const { player, level } = nearest.entry;

        return { type: 'player', id: player.id, level, name: player.nickname };
    }

    const gangster = Npc.getInRange(caller.position, GANGSTER_RANGE)
        .filter((npc) => npc.kind === 'gangster' && npc.alive)
        .sort(
            (a, b) => distance(caller.position, a.position) - distance(caller.position, b.position),
        )[0];

    return gangster ? { type: 'npc', id: gangster.id, level: 0, name: 'armed gangsters' } : null;
}

function suspectEntity(call: Call): Player | Npc | null {
    const suspect = call.suspect;

    if (!suspect) {
        return null;
    }

    if (suspect.type === 'npc') {
        return Npc.getAll().find((npc) => npc.id === suspect.id && npc.alive) ?? null;
    }

    const player = playerById(suspect.id);

    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) {
        return null;
    }

    // An attacker the squad just made wanted counts before the host's record
    // shows the level.
    if (Date.now() < (suspect.graceUntil ?? 0)) {
        return player;
    }

    return duty.suspects().some((entry) => entry.player.id === player.id) ? player : null;
}

function unitsOf(call: Call): Npc[] {
    return call.units
        .map((id) => npcById(id))
        .filter((npc): npc is Npc => npc !== null && npc.alive);
}

// The caller and every officer on duty near the scene hear of it.
function alert(call: Call, caller: Player, where: Point): void {
    for (const officer of onDutyPlayers()) {
        if (officer.id === caller.id || distance(officer.position, where) > ALERT_RANGE) {
            continue;
        }

        toast(
            officer,
            `${caller.nickname} called for backup${call.suspect ? ` against ${call.suspect.name}` : ''}. The scene is on your map.`,
            { title: 'Police radio', tone: 'info', seconds: 7 },
        );

        markers(officer, `backup:${call.id}`, [
            {
                id: 'scene',
                position: where,
                label: 'OFFICER NEEDS ASSISTANCE',
                color: CALL_COLOR,
                beacon: true,
                primary: true,
            },
        ]);

        call.alerted.add(officer.id);
        sound(officer, '12_radio.wav', 0.6);
    }

    call.alertUntil = Date.now() + ALERT_MS;
}

function call(caller: Player): boolean {
    if (!duty.onDuty(caller)) {
        toast(caller, 'Only officers on duty can call the precinct. /duty to sign on.', {
            tone: 'bad',
        });

        return false;
    }

    if (!playable(caller) || caller.virtualWorld !== MAIN_WORLD) {
        return false;
    }

    const car = duty.carOf(caller);
    const seated = car !== null && caller.getVehicle()?.id === car.id;
    const booth = seated ? null : boothAt(caller.position);

    if (!seated && !booth) {
        const near = nearestBooth(caller.position);

        toast(
            caller,
            near
                ? `No telephone here. The nearest call box is ${Math.round(near.gap)} m away; sit in your squad car to use its radio.`
                : 'No telephone on this map. Sit in your squad car to use its radio.',
            { tone: 'bad' },
        );

        return false;
    }

    if (
        [...calls.values()].some(
            (entry) => entry.callerId === caller.id && entry.stage !== 'leaving',
        )
    ) {
        toast(caller, 'Your backup is already on its way.', { tone: 'bad' });

        return false;
    }

    const wait = (cooldowns.get(caller.id) ?? 0) - Date.now();

    if (wait > 0) {
        toast(
            caller,
            `The precinct has nobody free yet. Call again in ${Math.ceil(wait / 1000)} s.`,
            {
                tone: 'bad',
            },
        );

        return false;
    }

    const suspect = pickSuspect(caller);
    const now = Date.now();
    const entry: Call = {
        id: nextCall++,
        callerId: caller.id,
        via: seated ? 'radio' : 'booth',
        suspect,
        stage: 'calling',
        units: [],
        carId: null,
        startedAt: now,
        stageAt: now,
        alerted: new Set(),
        alertUntil: 0,
        arrived: false,
        request: 0,
        replyBy: 0,
        unitsUntil: 0,
        tasks: new Map(),
        goal: null,
        escortUntil: suspect ? 0 : Infinity,
        compassKey: null,
        ready: new Set(),
        seated: new Set(),
        exited: new Set(),
    };

    calls.set(entry.id, entry);
    cooldowns.set(caller.id, Date.now() + COOLDOWN_MS);

    if (entry.via === 'booth') {
        sound(caller, '06a_telzvednuti.wav');
        setTimeout(() => sound(caller, '15b_telefonvytaceni.wav', 0.7), 600);
    } else {
        sound(caller, '12_radio.wav');
    }

    const target = suspectEntity(entry);

    if (
        suspect?.type === 'player' &&
        suspect.level > 0 &&
        Police.isEnabled() &&
        target instanceof Player
    ) {
        entry.request = Police.sendBackup(target);
        entry.replyBy = Date.now() + NATIVE_REPLY_MS;
    }

    if (entry.request) {
        entry.stage = 'dispatching';
    } else {
        dispatchSquad(entry, caller);
    }

    if (entry.stage === 'done') {
        return false;
    }

    alert(entry, caller, target?.position ?? caller.position);
    feed('police', [
        text('Officer ', COLORS.blue),
        name(caller, COLORS.blue),
        text(' called for backup'),
    ]);

    return true;
}

// The squad's way in: a road crossing out of sight of the scene.
function squadStart(where: Point): Placement | null {
    const graph = roads.current();
    const index =
        graph.crossingAround(where, SPAWN_MIN, SPAWN_MAX) ?? graph.crossingAround(where, 60, 500);

    if (index === null) {
        return null;
    }

    const { position, direction } = graph.roadside(index);

    return {
        position: { x: position.x, y: position.y + 0.4, z: position.z },
        heading: Math.atan2(direction.x, direction.z),
    };
}

function dispatchSquad(entry: Call, caller: Player): void {
    const target = suspectEntity(entry);
    const scene = target?.position ?? caller.position;
    const squads = [...calls.values()].filter((other) => other.carId !== null).length;
    const start = squads < MAX_SQUADS ? squadStart(scene) : null;
    const car = start
        ? spawnVehicle(SQUAD_MODEL, start.position, start.heading, {
              radar: SQUAD_RADAR,
              state: { 'fr.backup': true },
          })
        : null;

    if (!start || !car) {
        toast(
            caller,
            squads >= MAX_SQUADS
                ? 'Every squad car is out on a call. Hold on.'
                : 'Dispatch could not raise a car.',
            { title: 'Precinct', tone: 'bad' },
        );

        cooldowns.set(caller.id, Date.now() + COOLDOWN_MS / 3);
        end(entry, null);

        return;
    }

    entry.carId = car.id;
    entry.stage = 'spawning';
    entry.stageAt = Date.now();
    OFFICER_MODELS.forEach((model, index) => {
        const side = index === 0 ? -2.2 : 2.2;
        const officer = Npc.spawn(
            model,
            ahead(start.position, start.heading + Math.PI / 2, side),
            start.heading,
            { behaviour: 'neutral', health: OFFICER_HEALTH, ...OFFICER_KITS[index] },
        );

        if (officer) {
            entry.units.push(officer.id);
        }
    });

    if (!entry.units.length) {
        toast(caller, 'Dispatch could not raise a car.', { title: 'Precinct', tone: 'bad' });
        end(entry, null);

        return;
    }

    toast(
        caller,
        entry.suspect
            ? `A squad car is on its way to ${entry.suspect.name}. Follow it on your compass.`
            : 'A squad car is on its way to you. Follow it on your compass.',
        { title: 'Precinct', tone: 'gold', seconds: 7 },
    );
}

// The caller's compass follows the arriving unit until it is on scene.
function pointCompass(entry: Call, caller: Player, target: CompassTarget): void {
    const key = JSON.stringify(target);

    if (entry.compassKey === key) {
        return;
    }

    entry.compassKey = key;
    compass(caller, 'backup', COMPASS_PRIORITY, target);
}

function giveTask(entry: Call, npc: Npc, start: (unit: Npc) => number): number {
    const id = start(npc);

    if (id) {
        entry.tasks.set(npc.id, id);
    }

    return id;
}

function board(entry: Call): void {
    const car = vehicleById(entry.carId);

    entry.stage = 'boarding';
    entry.stageAt = Date.now();
    entry.seated = new Set();
    unitsOf(entry).forEach((npc, index) => {
        if (!car || !giveTask(entry, npc, (unit) => unit.enterVehicle(car, index, { run: true }))) {
            entry.seated.add(npc.id);
        }
    });

    if (entry.seated.size >= unitsOf(entry).length) {
        drive(entry);
    }
}

function drive(entry: Call): void {
    const [driver] = unitsOf(entry);
    const target = suspectEntity(entry) ?? playerById(entry.callerId);

    entry.stage = 'driving';
    entry.stageAt = Date.now();
    entry.goal = target ? { ...target.position } : null;

    if (
        !driver ||
        !driver.getVehicle() ||
        !target ||
        !giveTask(entry, driver, (unit) => unit.driveTo(target.position))
    ) {
        arrive(entry);
    }
}

// On scene: out of the car, then the fight or the cover.
function arrive(entry: Call): void {
    entry.stage = 'exiting';
    entry.stageAt = Date.now();
    entry.exited = new Set();

    for (const npc of unitsOf(entry)) {
        if (!npc.getVehicle() || !giveTask(entry, npc, (unit) => unit.exitVehicle())) {
            entry.exited.add(npc.id);
        }
    }

    if (entry.exited.size >= unitsOf(entry).length) {
        engage(entry);
    }
}

// The fight with the suspect, or the caller's cover when there is none.
function fightOrCover(entry: Call, npc: Npc): void {
    const target = suspectEntity(entry);
    const caller = playerById(entry.callerId);

    if (target) {
        giveTask(entry, npc, (unit) => unit.attack(target));
    } else if (caller) {
        giveTask(entry, npc, (unit) => unit.follow(caller, 3));
    }
}

function engage(entry: Call): void {
    entry.stage = 'engaging';
    entry.stageAt = Date.now();

    for (const npc of unitsOf(entry)) {
        fightOrCover(entry, npc);
    }

    if (!entry.arrived) {
        onScene(entry);
    }
}

function onScene(entry: Call): void {
    entry.arrived = true;

    if (!entry.suspect) {
        entry.escortUntil = Date.now() + ESCORT_MS;
    }

    const caller = playerById(entry.callerId);

    if (!caller) {
        return;
    }

    sound(caller, 'poli_fife1.wav');
    toast(caller, 'Backup on scene.', { title: 'Precinct', tone: 'good' });
    compass(caller, 'backup', COMPASS_PRIORITY, null);
}

// Back in the car and away, then gone.
function standDown(entry: Call, reason: string | null): void {
    if (entry.stage === 'leaving' || entry.stage === 'done') {
        return;
    }

    const caller = playerById(entry.callerId);

    if (caller && reason) {
        toast(caller, reason, { title: 'Precinct', tone: 'info' });
    }

    if (caller) {
        compass(caller, 'backup', COMPASS_PRIORITY, null);
        sound(caller, entry.via === 'booth' ? '06a_telpolozeni.wav' : '12_radio.wav', 0.6);
    }

    clearAlerts(entry);

    if (entry.carId === null) {
        end(entry, null);

        return;
    }

    entry.stage = 'leaving';
    entry.stageAt = Date.now();
    const car = vehicleById(entry.carId);

    if (!car) {
        return;
    }

    unitsOf(entry).forEach((npc, index) => {
        if (npc.getVehicle()?.id === car.id) {
            if (index === 0) {
                leaveIn(entry, npc);
            }
        } else {
            giveTask(entry, npc, (unit) => unit.enterVehicle(car, index, { run: false }));
        }
    });
}

function leaveIn(entry: Call, driver: Npc): void {
    const graph = roads.current();
    const index = graph.crossingAround(driver.position, LEAVE_DISTANCE, LEAVE_DISTANCE + 400);

    if (index !== null) {
        giveTask(entry, driver, (unit) => unit.driveTo(graph.crossings[index]));
    }
}

function clearAlerts(entry: Call): void {
    for (const id of entry.alerted) {
        const officer = playerById(id);

        if (officer) {
            markers(officer, `backup:${entry.id}`, []);
        }
    }

    entry.alerted.clear();
}

function end(entry: Call, reason: string | null): void {
    if (entry.stage === 'done') {
        return;
    }

    entry.stage = 'done';
    calls.delete(entry.id);
    clearAlerts(entry);
    const caller = playerById(entry.callerId);

    if (caller) {
        compass(caller, 'backup', COMPASS_PRIORITY, null);

        if (reason) {
            toast(caller, reason, { title: 'Precinct', tone: 'info' });
        }
    }

    if (entry.carId === null) {
        return;
    }

    for (const id of entry.units) {
        npcById(id)?.remove();
    }

    const car = vehicleById(entry.carId);

    // Not with a player in it: only the squad's own NPCs leave with the car.
    if (car && car.getOccupants().every((occupant) => !(occupant instanceof Player))) {
        car.destroy();
    }
}

function nearAnyPlayer(position: Point, range: number): boolean {
    return World.getPlayers().some(
        (player) => player.spawned && distance(player.position, position) < range,
    );
}

function tickCall(entry: Call, now: number): void {
    const caller = playerById(entry.callerId);
    const target = suspectEntity(entry);
    const units = unitsOf(entry);

    if (entry.alertUntil && now > entry.alertUntil) {
        clearAlerts(entry);
        entry.alertUntil = 0;
    }

    if (entry.stage === 'leaving') {
        const car = vehicleById(entry.carId);
        const where = units[0]?.position ?? car?.position;

        if (!where || now - entry.stageAt > LEAVE_TIMEOUT_MS || !nearAnyPlayer(where, 120)) {
            end(entry, null);
        }

        return;
    }

    if (now - entry.startedAt > CALL_LIFETIME_MS) {
        standDown(entry, 'Your backup was called back to the precinct.');

        return;
    }

    if (!caller || !duty.onDuty(caller)) {
        standDown(entry, null);

        return;
    }

    if (entry.suspect && !target) {
        standDown(entry, `${entry.suspect.name} is no longer a threat. Units back to patrol.`);

        return;
    }

    if (!entry.suspect && now > entry.escortUntil) {
        standDown(entry, 'Quiet streets. Your backup goes back to patrol.');

        return;
    }

    if (entry.stage === 'dispatching') {
        if (now > entry.replyBy) {
            entry.request = 0;
            dispatchSquad(entry, caller);
        }

        return;
    }

    if (entry.stage === 'native') {
        if (now > entry.unitsUntil && !units.length) {
            standDown(entry, 'The police car is off the case.');

            return;
        }

        const lead = units.find((npc) => npc.getVehicle()) ?? units[0];

        if (
            lead &&
            !entry.arrived &&
            target &&
            distance(lead.position, target.position) < ARRIVE_DISTANCE * 1.5
        ) {
            onScene(entry);
        } else if (lead && !entry.arrived) {
            const vehicle = lead.getVehicle();

            pointCompass(
                entry,
                caller,
                vehicle ? { vehicleId: vehicle.id } : { position: lead.position },
            );
        }

        return;
    }

    if (entry.carId !== null && !units.length && entry.stage !== 'spawning') {
        feed('police', [
            text('Officers down answering '),
            name(caller, COLORS.blue),
            text("'s call", COLORS.muted),
        ]);

        end(entry, 'Your backup is down.');

        return;
    }

    const car = vehicleById(entry.carId);

    if (entry.stage === 'spawning' && now - entry.stageAt > BOARD_TIMEOUT_MS) {
        end(entry, 'Dispatch could not raise a car.');

        return;
    }

    if (entry.stage === 'boarding' && now - entry.stageAt > BOARD_TIMEOUT_MS) {
        drive(entry);
    } else if (entry.stage === 'driving') {
        const goal = target ?? caller;

        if (
            now - entry.stageAt > DRIVE_TIMEOUT_MS ||
            (car && distance(car.position, goal.position) < ARRIVE_DISTANCE)
        ) {
            arrive(entry);
        } else if (
            entry.goal &&
            distance(entry.goal, goal.position) > REROUTE_DISTANCE &&
            units[0]?.getVehicle()
        ) {
            // The suspect moved on: the driver heads for where they are now.
            entry.goal = { ...goal.position };
            giveTask(entry, units[0], (unit) => unit.driveTo(goal.position));
        }
    } else if (entry.stage === 'exiting' && now - entry.stageAt > BOARD_TIMEOUT_MS) {
        engage(entry);
    }

    if (!entry.arrived && car) {
        pointCompass(entry, caller, { vehicleId: car.id });
    }
}

function tick(): void {
    const now = Date.now();

    for (const entry of [...calls.values()]) {
        tickCall(entry, now);
    }
}

function callOf(npc: Npc): Call | undefined {
    return [...calls.values()].find(
        (candidate) => candidate.carId !== null && candidate.units.includes(npc.id),
    );
}

events.on('npcSpawn', (npc) => {
    for (const entry of calls.values()) {
        if (entry.stage === 'spawning' && entry.units.includes(npc.id)) {
            entry.ready.add(npc.id);

            if (entry.ready.size >= entry.units.length) {
                board(entry);
            }

            return;
        }

        // A native car's officers are the police NPCs that turn up near
        // the suspect right after the host sent it.
        if (
            entry.stage === 'native' &&
            npc.kind === 'police' &&
            Date.now() <= entry.unitsUntil &&
            entry.units.length < 2
        ) {
            const target = suspectEntity(entry);

            if (!target || distance(npc.position, target.position) > NATIVE_UNITS_RANGE) {
                continue;
            }

            entry.units.push(npc.id);

            return;
        }
    }
});

events.on('npcTaskComplete', (npc, info) => {
    for (const entry of calls.values()) {
        const task = entry.tasks.get(npc.id);

        if (task === undefined || task !== info.id) {
            continue;
        }

        entry.tasks.delete(npc.id);

        if (entry.stage === 'boarding' && info.task === 'enterVehicle') {
            entry.seated.add(npc.id);

            if (entry.seated.size >= unitsOf(entry).length) {
                drive(entry);
            }
        } else if (entry.stage === 'driving' && info.task === 'driveTo') {
            arrive(entry);
        } else if (entry.stage === 'exiting' && info.task === 'exitVehicle') {
            entry.exited.add(npc.id);

            if (entry.exited.size >= unitsOf(entry).length) {
                engage(entry);
            }
        } else if (entry.stage === 'engaging' && info.task === 'attack') {
            // The target is dead, or out of reach: the next tick decides.
            fightOrCover(entry, npc);
        } else if (
            entry.stage === 'leaving' &&
            info.task === 'enterVehicle' &&
            info.success &&
            unitsOf(entry)[0]?.id === npc.id
        ) {
            leaveIn(entry, npc);
        } else if (entry.stage === 'leaving' && info.task === 'driveTo') {
            end(entry, null);
        }

        return;
    }
});

events.on('policeBackup', (suspect, info) => {
    const entry = [...calls.values()].find(
        (candidate) => candidate.stage === 'dispatching' && candidate.request === info.id,
    );

    if (!entry) {
        return;
    }

    const caller = playerById(entry.callerId);

    if (!caller) {
        end(entry, null);

        return;
    }

    if (!info.sent) {
        dispatchSquad(entry, caller);

        return;
    }

    entry.stage = 'native';
    entry.unitsUntil = Date.now() + NATIVE_UNITS_MS;
    toast(
        caller,
        `The precinct sent a police car after ${suspect.nickname}. Follow it on your compass.`,
        { title: 'Precinct', tone: 'gold', seconds: 7 },
    );
});

// Hurting one of the squad's officers is an assault on the police: the
// attacker is wanted and the squad turns on them. Officers on duty are the
// squad's colleagues; their stray shots are forgiven.
events.on('npcDamage', (npc, info) => {
    const attacker = info.attacker;

    if (!(attacker instanceof Player) || duty.onDuty(attacker)) {
        return;
    }

    const entry = callOf(npc);

    if (!entry || entry.stage === 'leaving') {
        return;
    }

    const level = Police.getRecord(attacker)?.level ?? 0;

    // With the police off the attacker is not wanted, and the squad keeps its call.
    if (level < 2 && !Police.setWanted(attacker, 2)) {
        return;
    }

    if (!entry.suspect || entry.suspect.type !== 'player' || entry.suspect.id !== attacker.id) {
        entry.suspect = {
            type: 'player',
            id: attacker.id,
            level: 2,
            name: attacker.nickname,
            graceUntil: Date.now() + WANTED_GRACE_MS,
        };

        entry.escortUntil = 0;

        if (entry.stage === 'engaging') {
            for (const unit of unitsOf(entry)) {
                giveTask(entry, unit, (officer) => officer.attack(attacker));
            }
        }
    }
});

events.on('npcDeath', (npc, killer) => {
    if (callOf(npc) && killer instanceof Player && !duty.onDuty(killer)) {
        Police.setWanted(killer, 3);
    }
});

// A suspect the squad takes down is settled like one an officer takes down.
events.on('playerDeath', (victim, killer) => {
    if (!killer || killer instanceof Player) {
        return;
    }

    for (const entry of calls.values()) {
        if (
            entry.suspect?.type === 'player' &&
            entry.suspect.id === victim.id &&
            entry.units.includes(killer.id)
        ) {
            Police.clear(victim);
            const caller = playerById(entry.callerId);

            if (caller) {
                toast(caller, `Your backup took ${victim.nickname} down.`, {
                    title: 'Precinct',
                    tone: 'good',
                });
            }

            feed('police', [text('Police backup took down '), name(victim)]);

            return;
        }
    }
});

events.on('npcRetire', (npc) => {
    for (const entry of calls.values()) {
        if (!entry.units.includes(npc.id)) {
            continue;
        }

        entry.units = entry.units.filter((id) => id !== npc.id);

        if (entry.stage === 'spawning' && !entry.units.length) {
            end(entry, 'Dispatch could not raise a car.');
        } else if (entry.stage === 'spawning' && entry.units.every((id) => entry.ready.has(id))) {
            // The other officer is already in: the squad goes without this one.
            board(entry);
        }

        return;
    }
});

commands.register(
    {
        name: 'backup',
        aliases: ['callbackup', '911'],
        usage: '/backup',
        help: "On police duty: call for backup from a telephone booth or your squad car's radio.",
        category: 'City',
    },
    (player) => call(player),
);

onClient('fr:backup:call', (player) => call(player));

every(TICK_MS, tick);
onStop(() => {
    for (const entry of [...calls.values()]) {
        end(entry, null);
    }

    cooldowns.clear();
});

onMissionChange(() => {
    // The mission took every car and NPC with it.
    for (const entry of [...calls.values()]) {
        entry.carId = null;
        end(entry, null);
    }
});

/** One line for the duty card while a call runs. */
export function status(player: Player): string | null {
    const entry = [...calls.values()].find((candidate) => candidate.callerId === player.id);

    if (entry) {
        return STATUS[entry.stage];
    }

    const wait = (cooldowns.get(player.id) ?? 0) - Date.now();

    return wait > 0
        ? `Free again in ${Math.ceil(wait / 1000)} s`
        : 'Call box or car radio: /backup';
}

export function configure(roster: DutyRoster): void {
    duty = roster;
}

/** The officer clocked off: no booths, and their backup goes back to patrol. */
export function stop(player: Player): void {
    hideBooths(player);
    compass(player, 'backup', COMPASS_PRIORITY, null);

    for (const entry of [...calls.values()]) {
        if (entry.callerId === player.id) {
            standDown(entry, null);
        }
    }
}

export { showBooths as start, call };
