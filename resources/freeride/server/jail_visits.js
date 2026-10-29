// Visiting hours at the county jail. An arrow outside the gate of the city's
// prison building (jail.visit in lib/maps.js) leads into the jail's visiting
// area: the corridor side of the bars, in the jail's virtual world, where
// visitors can see and talk to the prisoners and the warden. The guard holds
// a visitor's weapons at the door and hands them back on the way out, keeps
// visitors out of the cells, and turns away anyone the police want or who is
// busy with something else (police duty is welcome). Visitors free nobody:
// only a served sentence or bail opens a cell (jail.js).
const life = require("./lib/life.js");
const maps = require("./lib/maps.js");
const prison = require("./lib/prison.js");
const warden = require("./lib/warden.js");
const jail = require("./jail.js");
const {toast, markers, markersAll, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, playable, emit, onStart, onStop} = require("./lib/util.js");

const TICK_MS = 250;
const TRIGGER_RADIUS = 1.4;
const REARM_DISTANCE = 3;
const FADE_MS = 400;
const MARKER_RANGE = 60;

// player id -> {loadout, duty}
const visitors = new Map();
const armed = new Map();
const moving = new Set();
let timer = null;

function entrance() {
    return maps.current()?.jail?.visit ?? null;
}

function street() {
    return maps.current()?.jail?.release ?? null;
}

function publish(player = null) {
    const spot = entrance();
    const items = spot ? [{id: "jail", position: spot.position, label: "COUNTY JAIL · VISITS", color: COLORS.blue, model: "sipka.i3d", range: MARKER_RANGE}] : [];
    if (player) markers(player, "jail-entrance", items);
    else markersAll("jail-entrance", items);
}

function refused(player) {
    if (!prison.isBuilt()) return "Visiting hours are over.";
    if ((Police.getRecord(player)?.level ?? 0) > 0) return "The guard will not let a wanted man in.";
    const current = life.activity(player);
    if (current && current.kind !== "duty") return life.busyMessage(player);
    return null;
}

// Fade out, move, fade in; skipped if the life ended meanwhile. A move
// refused (a client still loading the mission) leaves the player where and
// in the world they were.
function transfer(player, pose, world, loadout, then) {
    const {spawnGeneration} = player;
    moving.add(player.id);
    emit(player, "fr:fade", {out: true, seconds: FADE_MS / 1000});
    setTimeout(() => {
        moving.delete(player.id);
        if (!World.getPlayers().some((other) => other.id === player.id) || !player.alive || player.spawnGeneration !== spawnGeneration) return;
        const previous = player.virtualWorld;
        player.setVirtualWorld(world);
        if (!life.teleport(player, pose.position, pose.heading ?? 0, loadout)) {
            player.setVirtualWorld(previous);
            emit(player, "fr:fade", {out: false, seconds: 0.3});
            return;
        }
        emit(player, "fr:fade", {out: false, seconds: 0.6});
        then();
    }, FADE_MS);
}

function enter(player) {
    const problem = refused(player);
    if (problem) {
        toast(player, problem, {tone: "bad", seconds: 3});
        return;
    }
    const loadout = life.loadoutOf(player);
    const duty = life.activity(player)?.kind === "duty";
    transfer(player, prison.visit(), prison.WORLD, {weapons: [], selected: 0, health: player.health}, () => {
        visitors.set(player.id, {loadout, duty});
        if (!duty) life.begin(player, "visit", "Visiting the jail");
        armed.set(player.id, false);
        markers(player, "jail-visit", [{id: "exit", position: prison.exit(), label: "EXIT", color: COLORS.green, model: "sipka.i3d", range: 40}]);
        toast(player, "Visiting hours. The guard holds your guns until you leave.", {title: "County jail", seconds: 6});
    });
}

function leave(player) {
    const visit = visits(player);
    const spot = street();
    if (!visit || !spot) return;
    transfer(player, spot, MAIN_WORLD, {...visit.loadout, health: player.health}, () => {
        forget(player);
        armed.set(player.id, false);
        toast(player, "The guard hands your guns back.", {title: "County jail"});
    });
}

function visits(player) {
    return visitors.get(player.id) ?? null;
}

function forget(player) {
    if (!visitors.has(player.id)) return;
    visitors.delete(player.id);
    life.end(player, "visit");
    markers(player, "jail-visit", []);
}

function tick() {
    warden.need("visitors", visitors.size > 0);
    const spot = entrance();
    for (const player of World.getPlayers()) {
        if (moving.has(player.id) || !player.alive) continue;
        const visit = visits(player);
        if (visit) {
            // Arrested while inside: jail.js has them now.
            if (jail.jailed(player)) {
                forget(player);
                continue;
            }
            if (prison.inAnyCell(player.position)) {
                const {position, heading} = prison.visit();
                life.teleport(player, position, heading, {weapons: [], selected: 0, health: player.health});
                toast(player, "The guard keeps visitors out of the cells.", {tone: "bad", seconds: 3});
                continue;
            }
            const gap = distance(player.position, prison.exit());
            if (gap > REARM_DISTANCE) armed.set(player.id, true);
            else if (gap <= TRIGGER_RADIUS && armed.get(player.id)) {
                armed.set(player.id, false);
                leave(player);
            }
            continue;
        }
        if (!spot || !playable(player) || player.virtualWorld !== MAIN_WORLD || player.getVehicle() || jail.jailed(player)) continue;
        const gap = distance(player.position, spot.position);
        if (gap > REARM_DISTANCE) armed.set(player.id, true);
        else if (gap <= TRIGGER_RADIUS && armed.get(player.id)) {
            armed.set(player.id, false);
            enter(player);
        }
    }
}

Events.on("playerSpawn", (player) => {
    if (!life.isTransit(player)) publish(player);
});
// A visitor who dies wakes up in the street world without the guns they left.
Events.on("playerDeath", (player) => {
    if (!visitors.has(player.id)) return;
    forget(player);
    if (!jail.jailed(player)) player.setVirtualWorld(MAIN_WORLD);
});
Events.on("playerDisconnect", (player) => {
    visitors.delete(player.id);
    armed.delete(player.id);
    moving.delete(player.id);
});
// The mission change took the jail down: its visitors leave its world and
// the visit.
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) {
        if (!visitors.has(player.id)) continue;
        forget(player);
        if (player.virtualWorld === prison.WORLD) player.setVirtualWorld(MAIN_WORLD);
    }
    visitors.clear();
    moving.clear();
    publish();
});
onStart(() => {
    publish();
    timer = setInterval(tick, TICK_MS);
});
onStop(() => {
    clearInterval(timer);
    for (const player of World.getPlayers()) {
        const visit = visits(player);
        if (!visit) continue;
        player.setVirtualWorld(MAIN_WORLD);
        const spot = street();
        if (player.alive && spot) life.teleport(player, spot.position, spot.heading ?? 0, {...visit.loadout, health: player.health});
    }
    visitors.clear();
});

module.exports = {visiting: (player) => visitors.has(player.id)};
