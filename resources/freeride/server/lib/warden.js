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
const prison = require("./prison.js");

const MODEL = "Pol01.i3d";
// How close each walk must come: the lock and the stops he stands on.
const LOCK_RADIUS = 0.3;
const ROUND_RADIUS = 0.4;
const HOME_RADIUS = 0.5;
// A walk that has not ended by then is given up (the hall is 24 m long).
const WALK_TIMEOUT_MS = 25_000;
// RalphOdemykani is 247 frames at 25 fps; the key turns, and the door
// slides open, UNLOCK_MS into it.
const UNLOCK_CLIP = "RalphOdemykani.i3d";
const UNLOCK_CLIP_MS = 9900;
const UNLOCK_MS = 4000;
const UNLOCK_SOUND = "carunlock4.wav";
const SOUND_RADIUS = 20;
// MISE15-MESTO's beat: [clip, loop, ms] at each stop.
const GUARD = Object.freeze([["PolSt01.i3d", false, 1900], ["PolSt03.i3d", true, 6000], ["PolSt04.i3d", false, 1800]]);
// He waits this long at the desk before the first round and between rounds.
const FIRST_ROUND_MS = 20_000;
const ROUND_EVERY_MS = 45_000;
// He stays this long after the last person left.
const STAND_DOWN_MS = 15_000;
const RESPAWN_MS = 10_000;
const TICK_MS = 1000;

const demand = new Map();
const queue = [];
let officer = null;
let ready = false;
// What he is doing away from the desk: null, or
//   {kind: "unlock", cell, opened, stage: "walking" | "unlocking" | "done"}
//   {kind: "round", stop, stage: "walking" | "guarding"}
//   {kind: "home"}
// each with the id of its walk (taskId) and its one pending timer.
let duty = null;
let nextRoundAt = 0;
let emptySince = 0;
let nextSpawnAt = 0;
let timer = null;

function canSpawn() {
    return typeof Npc !== "undefined" && typeof Npc.spawn === "function";
}

function canSound() {
    return typeof Sound !== "undefined" && typeof Sound.play === "function";
}

function post() {
    const {position, heading} = prison.post();
    return {position: {...position, y: position.y + 0.05}, heading};
}

function present() {
    return officer !== null && ready;
}

function endDuty() {
    if (duty?.timer) clearTimeout(duty.timer);
    duty = null;
}

// The duty's next step after ms; it goes with the duty.
function schedule(ms, action) {
    if (duty.timer) clearTimeout(duty.timer);
    const current = duty;
    current.timer = setTimeout(() => {
        current.timer = null;
        if (duty === current) action();
    }, ms);
}

// A straight walk; 0 when he cannot go (no host, not spawned yet).
function walk(position, radius) {
    return present() ? officer.walkTo(position, {direct: true, radius}) || 0 : 0;
}

function station() {
    if (officer || !canSpawn() || Date.now() < nextSpawnAt) return;
    const {position, heading} = post();
    officer = Npc.spawn(MODEL, position, heading, {behaviour: "neutral", virtualWorld: prison.WORLD}) ?? null;
    ready = false;
    if (!officer) nextSpawnAt = Date.now() + RESPAWN_MS;
}

function standDown() {
    if (officer) officer.remove();
    officer = null;
    ready = false;
    // An unlock goes on without him; the rest was his alone.
    if (duty && duty.kind !== "unlock") endDuty();
}

function goHome() {
    endDuty();
    duty = {kind: "home", taskId: walk(post().position, HOME_RADIUS), timer: null};
    if (!duty.taskId) {
        endDuty();
        return;
    }
    schedule(WALK_TIMEOUT_MS, atDesk);
}

// Back at the desk, or as close as he got.
function atDesk() {
    endDuty();
    nextRoundAt = Date.now() + ROUND_EVERY_MS;
    if (!present()) return;
    // Pushed out of the hall: back at the desk the only way there is.
    if (!prison.inHall(officer.position, 1)) {
        standDown();
        station();
    } else officer.face(post().heading);
}

function next() {
    if (duty?.kind === "unlock" || !queue.length) return;
    // Off his rounds at once.
    endDuty();
    const {cell, opened} = queue.shift();
    // Requests for the same cell ride along.
    for (let index = queue.length - 1; index >= 0; index--) {
        if (queue[index].cell === cell) opened.unshift(...queue.splice(index, 1)[0].opened);
    }
    duty = {kind: "unlock", cell, opened, stage: "walking", taskId: walk(prison.lockOf(cell), LOCK_RADIUS), timer: null};
    if (!duty.taskId) {
        atDoor();
        return;
    }
    schedule(WALK_TIMEOUT_MS, atDoor);
}

// At the door, or as close as he got: turn to it and work the lock.
function atDoor() {
    if (duty?.kind !== "unlock" || duty.stage !== "walking") return;
    duty.stage = "unlocking";
    if (present()) {
        if (!prison.inHall(officer.position, 1)) {
            standDown();
            station();
        } else {
            officer.face(prison.latchOf(duty.cell));
            officer.playAnimation(UNLOCK_CLIP, {loop: false});
        }
    }
    schedule(UNLOCK_MS, unlocked);
}

// The key turns: the door opens, and he finishes the clip.
function unlocked() {
    duty.stage = "done";
    if (present() && canSound()) Sound.play(UNLOCK_SOUND, prison.latchOf(duty.cell), {radius: SOUND_RADIUS});
    for (const callback of duty.opened) callback();
    schedule(present() ? UNLOCK_CLIP_MS - UNLOCK_MS : 0, () => {
        endDuty();
        if (queue.length) next();
        else if (present()) goHome();
    });
}

function startRound() {
    endDuty();
    duty = {kind: "round", stop: 0, stage: "walking", taskId: 0, timer: null};
    toStop();
}

function toStop() {
    const cell = prison.CELLS[duty.stop];
    if (!cell) {
        goHome();
        return;
    }
    duty.stage = "walking";
    duty.taskId = walk(prison.roundOf(cell).position, ROUND_RADIUS);
    if (!duty.taskId) {
        endDuty();
        return;
    }
    schedule(WALK_TIMEOUT_MS, goHome);
}

// At a stop: he faces the cell and stands guard, then walks on.
function guard(step) {
    if (!present()) {
        endDuty();
        return;
    }
    if (step === 0) {
        duty.stage = "guarding";
        officer.face(prison.roundOf(prison.CELLS[duty.stop]).look);
    }
    const entry = GUARD[step];
    if (!entry) {
        duty.stop++;
        toStop();
        return;
    }
    const [clip, loop, ms] = entry;
    officer.playAnimation(clip, {loop});
    schedule(ms, () => guard(step + 1));
}

// Sends the warden to the cell; opened() runs once he has unlocked it.
function unlock(cell, opened) {
    const waiting = queue.find((entry) => entry.cell === cell);
    if (duty?.kind === "unlock" && duty.cell === cell && duty.stage !== "done") duty.opened.push(opened);
    else if (waiting) waiting.opened.push(opened);
    else queue.push({cell, opened: [opened]});
    next();
}

// source: "prisoners" or "visitors"; he is on duty while any needs him.
function need(source, wanted) {
    demand.set(source, wanted);
}

function tick() {
    const wanted = [...demand.values()].some(Boolean) || duty?.kind === "unlock" || queue.length > 0;
    if (wanted) {
        emptySince = 0;
        station();
        if (present() && !duty && Date.now() >= nextRoundAt) startRound();
    } else if (officer) {
        if (!emptySince) emptySince = Date.now();
        else if (Date.now() - emptySince >= STAND_DOWN_MS) standDown();
    }
}

Events.on("npcSpawn", (npc) => {
    if (!officer || npc.id !== officer.id) return;
    ready = true;
    nextRoundAt = Date.now() + FIRST_ROUND_MS;
    npc.face(post().heading);
});
Events.on("npcTaskComplete", (npc, info) => {
    if (!officer || npc.id !== officer.id || !duty || !duty.taskId || info.id !== duty.taskId) return;
    duty.taskId = 0;
    if (duty.kind === "unlock") atDoor();
    else if (duty.kind === "home") atDesk();
    else if (duty.stage === "walking") {
        if (info.success) guard(0);
        else goHome();
    }
});
const lost = (npc) => {
    if (!officer || npc.id !== officer.id) return;
    officer = null;
    ready = false;
    nextSpawnAt = Date.now() + RESPAWN_MS;
    if (duty?.kind === "unlock") atDoor();
    else endDuty();
};
Events.on("npcRetire", lost);
Events.on("npcDeath", lost);

function reset(remove) {
    if (remove) standDown();
    officer = null;
    ready = false;
    endDuty();
    queue.length = 0;
    demand.clear();
    emptySince = 0;
    nextSpawnAt = 0;
    nextRoundAt = 0;
}

function start() {
    if (timer === null) timer = setInterval(tick, TICK_MS);
}

function stop() {
    if (timer !== null) clearInterval(timer);
    timer = null;
    reset(true);
}

module.exports = {
    start, stop, unlock, need,
    officer: () => officer,
    // What he is doing: "desk", "unlock", "round" or "home".
    duty: () => duty?.kind ?? "desk",
    // A mission change has already removed every NPC.
    forget: () => reset(false),
};
