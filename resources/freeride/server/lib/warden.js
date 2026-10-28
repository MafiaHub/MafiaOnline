// The warden: a policeman (a scripted NPC on the simulation host) stationed
// at the desk in the jail's hall while anyone is inside, prisoners or
// visitors. Only the jail's virtual world sees him.
//
// To unlock a cell he tries to walk to its door (npc.walkTo with a small
// radius). The walk follows the mission's pedestrian paths (check.bin) and
// the jail, built under the fields, has none, so the host may fail it at
// once; then he only turns to the door from where he stands. Either way he
// works the lock, the door slides open, and he goes back to the desk. A walk
// that leads him out of the hall puts him back at the desk. Without a
// simulation host Npc.spawn returns null and the door opens on its own after
// the same pause.
const prison = require("./prison.js");

const MODEL = "Pol01.i3d";
const LOCK_RADIUS = 0.6;
// A walk that has not ended by then is given up.
const WALK_TIMEOUT_MS = 10_000;
// The lock takes this long once he is at (or facing) the door.
const UNLOCK_MS = 1500;
// He stays this long after the last person left.
const STAND_DOWN_MS = 15_000;
const RESPAWN_MS = 10_000;
const TICK_MS = 1000;

const demand = new Map();
const queue = [];
let officer = null;
let ready = false;
let job = null;
let emptySince = 0;
let nextSpawnAt = 0;
let timer = null;

function canSpawn() {
    return typeof Npc !== "undefined" && typeof Npc.spawn === "function";
}

function post() {
    const {position, heading} = prison.post();
    return {position: {...position, y: position.y + 0.05}, heading};
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
}

function clearJob() {
    if (job?.timer) clearTimeout(job.timer);
    job = null;
}

function finish(opened) {
    for (const callback of opened) callback();
    if (officer && ready) {
        const back = post();
        if (!officer.walkTo(back.position, {radius: LOCK_RADIUS})) officer.face(back.heading);
    }
    clearJob();
    next();
}

// At the door, or as close as he got: turn to it and work the lock.
function atDoor() {
    if (!job || job.stage !== "walking") return;
    if (job.timer) clearTimeout(job.timer);
    job.stage = "unlocking";
    if (officer && ready) {
        // A walk that led him out of the hall ends at the desk instead.
        if (!prison.inHall(officer.position, 1)) {
            standDown();
            station();
        } else officer.face(prison.toWorld({...job.cell.door.closed, y: 0}));
    }
    const {opened} = job;
    job.timer = setTimeout(() => finish(opened), UNLOCK_MS);
}

function next() {
    if (job || !queue.length) return;
    const {cell, opened} = queue.shift();
    job = {cell, opened, stage: "walking", taskId: 0, timer: null};
    // Requests for the same cell ride along.
    for (let index = queue.length - 1; index >= 0; index--) {
        if (queue[index].cell === cell) job.opened.unshift(...queue.splice(index, 1)[0].opened);
    }
    if (officer && ready) job.taskId = officer.walkTo(prison.lockOf(cell), {radius: LOCK_RADIUS}) || 0;
    if (!job.taskId) {
        atDoor();
        return;
    }
    job.timer = setTimeout(atDoor, WALK_TIMEOUT_MS);
}

// Sends the warden to the cell; opened() runs once he has unlocked it.
function unlock(cell, opened) {
    const waiting = queue.find((entry) => entry.cell === cell);
    if (job?.cell === cell && job.stage === "walking") job.opened.push(opened);
    else if (waiting) waiting.opened.push(opened);
    else queue.push({cell, opened: [opened]});
    next();
}

// source: "prisoners" or "visitors"; he is on duty while any needs him.
function need(source, wanted) {
    demand.set(source, wanted);
}

function tick() {
    const wanted = [...demand.values()].some(Boolean) || job !== null || queue.length > 0;
    if (wanted) {
        emptySince = 0;
        station();
    } else if (officer) {
        if (!emptySince) emptySince = Date.now();
        else if (Date.now() - emptySince >= STAND_DOWN_MS) standDown();
    }
}

Events.on("npcSpawn", (npc) => {
    if (!officer || npc.id !== officer.id) return;
    ready = true;
    npc.face(post().heading);
});
Events.on("npcTaskComplete", (npc, info) => {
    if (!officer || npc.id !== officer.id) return;
    if (job?.stage === "walking" && info.id === job.taskId) atDoor();
    else if (!job && info.task === "walkTo") npc.face(post().heading);
});
const lost = (npc) => {
    if (!officer || npc.id !== officer.id) return;
    officer = null;
    ready = false;
    nextSpawnAt = Date.now() + RESPAWN_MS;
};
Events.on("npcRetire", lost);
Events.on("npcDeath", lost);

function reset(remove) {
    if (remove) standDown();
    officer = null;
    ready = false;
    clearJob();
    queue.length = 0;
    demand.clear();
    emptySince = 0;
    nextSpawnAt = 0;
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
    // A mission change has already removed every NPC.
    forget: () => reset(false),
};
