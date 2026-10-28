// Entrances into the city's buildings. A glowing arrow marks each door; a
// player who walks into it fades out and arrives inside, and the arrow by the
// arrival point leads back out. Public interiors share the street world. A
// safehouse is private: each crew, or each crewless player, gets its own copy
// in a virtual world, and it patches wounds and tops up ammunition.
//
// No interior positions ship with the gamemode, because none can be verified
// outside the game. Record them in-game in development mode: stand on the
// street at the door, "/entrance add <name> [public|safehouse]", walk to the
// arrival point inside, "/entrance inside". They are saved in
// data/freeride/interiors.json, each with its map, and live at once.
const fs = require("node:fs");
const path = require("node:path");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const devmode = require("./lib/devmode.js");
const maps = require("./lib/maps.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, markers, markersAll, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, playable, plain, headingOf, emit, onStart, onStop} = require("./lib/util.js");

const FILE = path.join(profiles.DATA_DIR, "interiors.json");
const TICK_MS = 250;
const TRIGGER_RADIUS = 1.4;
const REARM_DISTANCE = 3;
const FADE_MS = 400;
const MARKER_RANGE = 60;
const FIRST_SAFEHOUSE_WORLD = 0x30000000;
const BLOCKING_ACTIVITIES = new Set(["race", "race-lobby", "duel"]);

let interiors = [];
const inside = new Map();
const armed = new Map();
const pending = new Map();
const recording = new Map();
let nextId = 1;
let timer = null;

function load() {
    try {
        interiors = JSON.parse(fs.readFileSync(FILE, "utf8")).interiors ?? [];
    } catch (error) {
        if (error.code !== "ENOENT") console.error(`[freeride] Could not read ${FILE}: ${error.message}`);
    }
    nextId = interiors.reduce((highest, interior) => Math.max(highest, interior.id), 0) + 1;
}

function save() {
    try {
        fs.mkdirSync(profiles.DATA_DIR, {recursive: true});
        fs.writeFileSync(`${FILE}.tmp`, JSON.stringify({version: 1, interiors}, null, 1));
        fs.renameSync(`${FILE}.tmp`, FILE);
    } catch (error) {
        console.error(`[freeride] Could not save interiors: ${error.message}`);
    }
}

function label(interior) {
    return interior.kind === "safehouse" ? `${interior.name.toUpperCase()} · SAFEHOUSE` : interior.name.toUpperCase();
}

// Interiors of the loaded map; ones recorded before maps existed are stock Lost Heaven's.
// Built into the map (lib/maps.js) first, then the recorded ones.
function here() {
    const map = maps.current();
    return [...(map?.interiors ?? []), ...interiors.filter((interior) => (interior.mission ?? "freeride") === map?.id)];
}

function publishEntrances(player = null) {
    const items = here().map((interior) => ({id: `in${interior.id}`, position: interior.outside.position, label: label(interior),
        color: interior.kind === "safehouse" ? COLORS.blue : COLORS.gold, model: "sipka.i3d", range: MARKER_RANGE}));
    if (player) markers(player, "entrances", items);
    else markersAll("entrances", items);
}

// One private copy per crew, or per player without one.
function safehouseWorld(player) {
    const tag = player.state.get("fr.crew")?.tag;
    if (!tag) return FIRST_SAFEHOUSE_WORLD + player.id;
    let hash = 0;
    for (const character of tag) hash = (hash * 31 + character.charCodeAt(0)) >>> 0;
    return FIRST_SAFEHOUSE_WORLD + 0x8000000 + hash % 0x7000000;
}

function blocked(player) {
    const current = life.activity(player);
    if (current && BLOCKING_ACTIVITIES.has(current.kind)) return life.busyMessage(player);
    // Moving through a door starts a new native life, which would shake off the police.
    if ((Police.getRecord(player)?.level ?? 0) > 0) return "The door stays shut while the police are after you.";
    return null;
}

// Fade out, move, fade in; the move is skipped if the life ended meanwhile.
function transfer(player, pose, world, then) {
    const {spawnGeneration} = player;
    pending.set(player.id, true);
    emit(player, "fr:fade", {out: true, seconds: FADE_MS / 1000});
    setTimeout(() => {
        pending.delete(player.id);
        if (!player.alive || player.spawnGeneration !== spawnGeneration) return;
        player.setVirtualWorld(world);
        if (!life.teleport(player, pose.position, pose.heading)) {
            player.setVirtualWorld(MAIN_WORLD);
            emit(player, "fr:fade", {out: false, seconds: 0.3});
            return;
        }
        emit(player, "fr:fade", {out: false, seconds: 0.6});
        then();
    }, FADE_MS);
}

function enter(player, interior) {
    const problem = blocked(player);
    if (problem) {
        toast(player, problem, {tone: "bad", seconds: 3});
        armed.set(`${player.id}:${interior.id}:outside`, false);
        return;
    }
    const world = interior.kind === "safehouse" ? safehouseWorld(player) : MAIN_WORLD;
    transfer(player, interior.inside, world, () => {
        inside.set(player.id, interior.id);
        armed.set(`${player.id}:${interior.id}:inside`, false);
        markers(player, "exit", [{id: "exit", position: interior.inside.position, label: "EXIT", color: COLORS.green, model: "sipka.i3d", range: 30}]);
        if (interior.kind === "safehouse") {
            player.setHealth(100);
            for (const weapon of player.getWeapons()) if (weapon.kind === "firearm") player.giveAmmo(weapon.weaponId, weapon.magazine * 2);
            toast(player, "Safe for now. Wounds dressed, magazines filled.", {title: interior.name, tone: "good"});
        }
    });
}

function leave(player, interior) {
    transfer(player, interior.outside, MAIN_WORLD, () => {
        inside.delete(player.id);
        armed.set(`${player.id}:${interior.id}:outside`, false);
        markers(player, "exit", []);
    });
}

function tick() {
    for (const player of World.getPlayers()) {
        if (!playable(player) || player.getVehicle() || pending.has(player.id)) continue;
        const current = here().find((interior) => interior.id === inside.get(player.id));
        const checks = current ? [[current, "inside"]] : player.virtualWorld === MAIN_WORLD ? here().map((interior) => [interior, "outside"]) : [];
        for (const [interior, side] of checks) {
            const key = `${player.id}:${interior.id}:${side}`;
            const gap = distance(player.position, interior[side].position);
            if (gap > REARM_DISTANCE) armed.set(key, true);
            else if (gap <= TRIGGER_RADIUS && armed.get(key)) {
                armed.set(key, false);
                if (side === "outside") enter(player, interior);
                else leave(player, interior);
                break;
            }
        }
    }
}

function forget(player) {
    inside.delete(player.id);
    pending.delete(player.id);
    recording.delete(player.id);
    for (const key of [...armed.keys()]) if (key.startsWith(`${player.id}:`)) armed.delete(key);
}

const subcommands = {
    add(player, args) {
        const kind = ["public", "safehouse"].includes(args[args.length - 1]) ? args.pop() : "public";
        const name = args.join(" ").trim();
        if (!/^[A-Za-z0-9 '.&-]{3,28}$/.test(name)) {
            toast(player, "Use /entrance add <name> [public|safehouse], standing on the street at the door.", {tone: "bad"});
            return;
        }
        recording.set(player.id, {name, kind, outside: {position: plain(player.position), heading: headingOf(player.rotation) + Math.PI}});
        toast(player, `Door recorded for ${name}. Walk to where players should arrive inside, face the way they should look, and type /entrance inside.`, {seconds: 10});
    },
    inside(player) {
        const record = recording.get(player.id);
        if (!record) {
            toast(player, "Record the door first with /entrance add <name>.", {tone: "bad"});
            return;
        }
        if (distance(player.position, record.outside.position) < REARM_DISTANCE * 2) {
            toast(player, "The arrival point must be away from the door marker.", {tone: "bad"});
            return;
        }
        recording.delete(player.id);
        const interior = {id: nextId++, ...record, inside: {position: plain(player.position), heading: headingOf(player.rotation)},
            mission: maps.current()?.id ?? "freeride", author: player.nickname, created: Date.now()};
        interiors.push(interior);
        save();
        publishEntrances();
        toast(player, `${interior.name} is open (${interior.kind}).`, {tone: "good"});
    },
    delete(player, args) {
        const name = args.join(" ").trim().toLowerCase();
        const before = interiors.length;
        const doomed = new Set(interiors.filter((interior) => interior.name.toLowerCase() === name && (interior.mission ?? "freeride") === maps.current()?.id));
        interiors = interiors.filter((interior) => !doomed.has(interior));
        if (interiors.length === before) {
            toast(player, "No interior by that name.", {tone: "bad"});
            return;
        }
        save();
        publishEntrances();
        toast(player, "Interior removed.");
    },
    list(player) {
        if (!here().length) player.sendMessage("No interiors on this map yet.", 0xd9cdb4);
        for (const interior of here()) {
            player.sendMessage(`${interior.name} (${interior.kind}) · ${Math.round(distance(player.position, interior.outside.position))} m away`, 0xd9cdb4);
        }
    },
};

commands.register({name: "entrance", usage: "/entrance add <name> [public|safehouse] | inside | delete <name> | list",
    help: "Record building entrances (development mode).", category: "Dev", hidden: true}, (player, [action, ...args]) => {
    if (!devmode.isOn()) {
        toast(player, "Entrances are recorded in development mode.", {tone: "bad"});
        return;
    }
    if (!playable(player) || player.getVehicle()) return;
    (subcommands[action] ?? subcommands.list)(player, args);
});

commands.register({name: "interiors", usage: "/interiors", help: "List the buildings you can walk into.", category: "City"}, (player) => subcommands.list(player));

menu.provide("interiors", (player) => here().map((interior) => ({name: interior.name, kind: interior.kind,
    distance: Math.round(distance(player.position, interior.outside.position))})));

onStart(() => {
    load();
    publishEntrances();
    timer = setInterval(tick, TICK_MS);
});
onStop(() => {
    clearInterval(timer);
    for (const player of World.getPlayers()) if (inside.has(player.id)) player.setVirtualWorld(MAIN_WORLD);
});
Events.on("playerSpawn", (player) => {
    if (!life.isTransit(player)) publishEntrances(player);
});
// A death inside sends the player back to the street world for the respawn.
Events.on("playerDeath", (player) => {
    if (!inside.has(player.id)) return;
    inside.delete(player.id);
    markers(player, "exit", []);
    if (player.virtualWorld !== MAIN_WORLD && !life.activity(player)) player.setVirtualWorld(MAIN_WORLD);
});
Events.on("missionChange", () => {
    inside.clear();
    pending.clear();
});
Events.on("playerDisconnect", forget);
