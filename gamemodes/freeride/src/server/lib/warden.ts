// The warden: a policeman (a scripted NPC on the simulation host) stationed
// at the desk in the jail's hall while anyone is inside, prisoners or
// visitors. Only the jail's virtual world sees him.
//
// The jail has no pedestrian paths, so he walks in straight lines
// (npc.walkTo with direct). To unlock a cell he walks to the latch of its
// door, turns to it and works the lock with the clip Ralph unlocks a car
// with in the story (MISE14-SALIERY plays it with carunlock4.wav): the door
// slides open as the key turns, and once the clip has ended he walks back to
// the desk, or on to the next door. Between unlocks he does the rounds the
// way the watchman of MISE15-MESTO keeps his beat: to each cell in turn,
// where he faces it and stands guard (PolSt01 into the stance, PolSt03 held,
// PolSt04 out of it), then back to the desk. An unlock calls him off his
// rounds at once. A walk that fails (someone in his way) ends where he
// stands: at a door he works the lock from there, on his rounds he heads
// back to the desk. Without a simulation host Npc.spawn returns null and the
// door opens on its own after the same pause.
import * as prison from './prison';
import { events } from './lifecycle';
import { npcsAvailable } from './npcs';
import type { Placement, Point } from './util';

const MODEL = 'Pol01.i3d';
// How close each walk must come: the lock and the stops he stands on.
const LOCK_RADIUS = 0.3;
const ROUND_RADIUS = 0.4;
const HOME_RADIUS = 0.5;
// A walk that has not ended by then is given up (the hall is 24 m long).
const WALK_TIMEOUT_MS = 25_000;
// RalphOdemykani is 247 frames at 25 fps; the key turns, and the door
// slides open, UNLOCK_MS into it.
const UNLOCK_CLIP = 'RalphOdemykani.i3d';
const UNLOCK_CLIP_MS = 9900;
const UNLOCK_MS = 4000;
const UNLOCK_SOUND = 'carunlock4.wav';
const SOUND_RADIUS = 20;
// MISE15-MESTO's beat: [clip, loop, ms] at each stop.
const GUARD: readonly (readonly [string, boolean, number])[] = Object.freeze([
    ['PolSt01.i3d', false, 1900],
    ['PolSt03.i3d', true, 6000],
    ['PolSt04.i3d', false, 1800],
]);
// He waits this long at the desk before the first round and between rounds.
const FIRST_ROUND_MS = 20_000;
const ROUND_EVERY_MS = 45_000;
// He stays this long after the last person left.
const STAND_DOWN_MS = 15_000;
const RESPAWN_MS = 10_000;
const TICK_MS = 1000;

/** Who needs him there. */
export type Demand = 'prisoners' | 'visitors';

/** Where he is: at the desk, or away on an errand. */
export type DutyKind = 'desk' | Errand['kind'];

interface ErrandBase {
    /** The id of the walk under way, 0 for none. */
    taskId: number;
    /** The errand's one pending step. */
    timer: ReturnType<typeof setTimeout> | null;
}

interface UnlockErrand extends ErrandBase {
    kind: 'unlock';
    cell: prison.Cell;
    opened: (() => void)[];
    stage: 'walking' | 'unlocking' | 'done';
}

interface RoundErrand extends ErrandBase {
    kind: 'round';
    stop: number;
    stage: 'walking' | 'guarding';
}

interface HomeErrand extends ErrandBase {
    kind: 'home';
}

type Errand = UnlockErrand | RoundErrand | HomeErrand;

interface UnlockRequest {
    cell: prison.Cell;
    opened: (() => void)[];
}

const demand = new Map<Demand, boolean>();
const queue: UnlockRequest[] = [];
let warden: Npc | null = null;
let ready = false;
// What he is doing away from the desk.
let errand: Errand | null = null;
let nextRoundAt = 0;
let emptySince = 0;
let nextSpawnAt = 0;
let timer: ReturnType<typeof setInterval> | null = null;

function canSound(): boolean {
    return typeof Sound !== 'undefined' && typeof Sound.play === 'function';
}

function post(): Placement {
    const { position, heading } = prison.post();

    return { position: { ...position, y: position.y + 0.05 }, heading };
}

// The warden once he stands in the jail, else null.
function present(): Npc | null {
    return ready ? warden : null;
}

function endErrand(): void {
    if (errand?.timer) {
        clearTimeout(errand.timer);
    }

    errand = null;
}

// The errand's next step after ms; it goes with the errand.
function schedule(current: Errand, ms: number, action: () => void): void {
    if (current.timer) {
        clearTimeout(current.timer);
    }

    current.timer = setTimeout(() => {
        current.timer = null;

        if (errand === current) {
            action();
        }
    }, ms);
}

// A straight walk; 0 when he cannot go (no host, not spawned yet).
function walk(position: Point, radius: number): number {
    return present()?.walkTo(position, { direct: true, radius }) || 0;
}

function station(): void {
    if (warden || !npcsAvailable() || Date.now() < nextSpawnAt) {
        return;
    }

    const { position, heading } = post();

    warden = Npc.spawn(MODEL, position, heading, {
        behaviour: 'neutral',
        virtualWorld: prison.WORLD,
    });

    ready = false;

    if (!warden) {
        nextSpawnAt = Date.now() + RESPAWN_MS;
    }
}

function standDown(): void {
    warden?.remove();
    warden = null;
    ready = false;

    // An unlock goes on without him; the rest was his alone.
    if (errand && errand.kind !== 'unlock') {
        endErrand();
    }
}

// Pushed out of the hall: back at the desk the only way there is.
function backInHall(npc: Npc): boolean {
    if (prison.inHall(npc.position, 1)) {
        return true;
    }

    standDown();
    station();

    return false;
}

function goHome(): void {
    endErrand();
    const home: HomeErrand = {
        kind: 'home',
        taskId: walk(post().position, HOME_RADIUS),
        timer: null,
    };

    errand = home;

    if (!home.taskId) {
        endErrand();

        return;
    }

    schedule(home, WALK_TIMEOUT_MS, atDesk);
}

// Back at the desk, or as close as he got.
function atDesk(): void {
    endErrand();
    nextRoundAt = Date.now() + ROUND_EVERY_MS;
    const npc = present();

    if (npc && backInHall(npc)) {
        npc.face(post().heading);
    }
}

function next(): void {
    if (errand?.kind === 'unlock') {
        return;
    }

    const request = queue.shift();

    if (!request) {
        return;
    }

    // Off his rounds at once.
    endErrand();
    const { cell, opened } = request;

    // Requests for the same cell ride along.
    for (let index = queue.length - 1; index >= 0; index--) {
        if (queue[index].cell === cell) {
            opened.unshift(...queue.splice(index, 1)[0].opened);
        }
    }

    const unlock: UnlockErrand = {
        kind: 'unlock',
        cell,
        opened,
        stage: 'walking',
        taskId: walk(prison.lockOf(cell), LOCK_RADIUS),
        timer: null,
    };

    errand = unlock;

    if (!unlock.taskId) {
        atDoor(unlock);

        return;
    }

    schedule(unlock, WALK_TIMEOUT_MS, () => atDoor(unlock));
}

// At the door, or as close as he got: turn to it and work the lock.
function atDoor(unlock: UnlockErrand): void {
    if (unlock.stage !== 'walking') {
        return;
    }

    unlock.stage = 'unlocking';
    const npc = present();

    if (npc && backInHall(npc)) {
        npc.face(prison.latchOf(unlock.cell));
        npc.playAnimation(UNLOCK_CLIP, { loop: false });
    }

    schedule(unlock, UNLOCK_MS, () => unlocked(unlock));
}

// The key turns: the door opens, and he finishes the clip.
function unlocked(unlock: UnlockErrand): void {
    unlock.stage = 'done';

    if (present() && canSound()) {
        Sound.play(UNLOCK_SOUND, prison.latchOf(unlock.cell), { radius: SOUND_RADIUS });
    }

    for (const callback of unlock.opened) {
        callback();
    }

    schedule(unlock, present() ? UNLOCK_CLIP_MS - UNLOCK_MS : 0, () => {
        endErrand();

        if (queue.length) {
            next();
        } else if (present()) {
            goHome();
        }
    });
}

function startRound(): void {
    endErrand();
    const round: RoundErrand = { kind: 'round', stop: 0, stage: 'walking', taskId: 0, timer: null };

    errand = round;
    toStop(round);
}

function toStop(round: RoundErrand): void {
    if (round.stop >= prison.CELLS.length) {
        goHome();

        return;
    }

    round.stage = 'walking';
    round.taskId = walk(prison.roundOf(prison.CELLS[round.stop]).position, ROUND_RADIUS);

    if (!round.taskId) {
        endErrand();

        return;
    }

    schedule(round, WALK_TIMEOUT_MS, goHome);
}

// At a stop: he faces the cell and stands guard, then walks on.
function guard(round: RoundErrand, step: number): void {
    const npc = present();

    if (!npc) {
        endErrand();

        return;
    }

    if (step === 0) {
        round.stage = 'guarding';
        npc.face(prison.roundOf(prison.CELLS[round.stop]).look);
    }

    if (step >= GUARD.length) {
        round.stop++;
        toStop(round);

        return;
    }

    const [clip, loop, ms] = GUARD[step];

    npc.playAnimation(clip, { loop });
    schedule(round, ms, () => guard(round, step + 1));
}

/** Sends the warden to the cell; opened() runs once he has unlocked it. */
export function unlock(cell: prison.Cell, opened: () => void): void {
    const waiting = queue.find((entry) => entry.cell === cell);

    if (errand?.kind === 'unlock' && errand.cell === cell && errand.stage !== 'done') {
        errand.opened.push(opened);
    } else if (waiting) {
        waiting.opened.push(opened);
    } else {
        queue.push({ cell, opened: [opened] });
    }

    next();
}

/** He is on duty while any source needs him. */
export function need(source: Demand, wanted: boolean): void {
    demand.set(source, wanted);
}

function tick(): void {
    const wanted =
        [...demand.values()].some(Boolean) || errand?.kind === 'unlock' || queue.length > 0;

    if (wanted) {
        emptySince = 0;
        station();

        if (present() && !errand && Date.now() >= nextRoundAt) {
            startRound();
        }
    } else if (warden) {
        if (!emptySince) {
            emptySince = Date.now();
        } else if (Date.now() - emptySince >= STAND_DOWN_MS) {
            standDown();
        }
    }
}

function isWarden(npc: Npc): boolean {
    return warden !== null && npc.id === warden.id;
}

events.on('npcSpawn', (npc) => {
    if (!isWarden(npc)) {
        return;
    }

    ready = true;
    nextRoundAt = Date.now() + FIRST_ROUND_MS;
    npc.face(post().heading);
});

events.on('npcTaskComplete', (npc, info) => {
    if (!isWarden(npc) || !errand || !errand.taskId || info.id !== errand.taskId) {
        return;
    }

    errand.taskId = 0;

    if (errand.kind === 'unlock') {
        atDoor(errand);
    } else if (errand.kind === 'home') {
        atDesk();
    } else if (errand.stage === 'walking') {
        if (info.success) {
            guard(errand, 0);
        } else {
            goHome();
        }
    }
});

function lost(npc: Npc): void {
    if (!isWarden(npc)) {
        return;
    }

    warden = null;
    ready = false;
    nextSpawnAt = Date.now() + RESPAWN_MS;

    if (errand?.kind === 'unlock') {
        atDoor(errand);
    } else {
        endErrand();
    }
}

events.on('npcRetire', lost);
events.on('npcDeath', lost);

function reset(remove: boolean): void {
    if (remove) {
        standDown();
    }

    warden = null;
    ready = false;
    endErrand();
    queue.length = 0;
    demand.clear();
    emptySince = 0;
    nextSpawnAt = 0;
    nextRoundAt = 0;
}

export function start(): void {
    if (timer === null) {
        timer = setInterval(tick, TICK_MS);
    }
}

export function stop(): void {
    if (timer !== null) {
        clearInterval(timer);
    }

    timer = null;
    reset(true);
}

/** A mission change has already removed him. */
export function forget(): void {
    reset(false);
}

export function officer(): Npc | null {
    return warden;
}

export function duty(): DutyKind {
    return errand?.kind ?? 'desk';
}
