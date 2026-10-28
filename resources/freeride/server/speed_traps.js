// Speed traps on the city's six longest roads. Driving through one records
// the car's speed against the player's own best and the server record;
// beating the record is announced and pays.
const fs = require("node:fs");
const path = require("node:path");
const graph = require("./data/roads.js");
const roads = require("./lib/roads.js");
const profiles = require("./lib/profiles.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, markersAll, markers, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, flatDistance, playable, speedOf, formatMoney, onStart, onStop} = require("./lib/util.js");

const FILE = path.join(profiles.DATA_DIR, "speedtraps.json");
const TRAP_COUNT = 6;
const RADIUS = 14;
const REPEAT_MS = 10_000;
const MIN_KMH = 40;
const RECORD_PRIZE = 150;
const TICK_MS = 150;

// The middle of each of the longest stock roads.
const traps = graph.edges.slice().sort((a, b) => b[2] - a[2]).slice(0, TRAP_COUNT).map(([from, to, , points], index) => {
    const line = [roads.crossings[from], ...points.map(([x, y, z]) => ({x, y, z})), roads.crossings[to]];
    return {id: index + 1, position: roads.pointAlong(line, roads.lineLength(line) / 2).position};
});
let records = {};
const lastHit = new Map();
let timer = null;

function load() {
    try {
        records = JSON.parse(fs.readFileSync(FILE, "utf8")).records ?? {};
    } catch (error) {
        if (error.code !== "ENOENT") console.error(`[freeride] Could not read ${FILE}: ${error.message}`);
    }
}

function save() {
    try {
        fs.mkdirSync(profiles.DATA_DIR, {recursive: true});
        fs.writeFileSync(`${FILE}.tmp`, JSON.stringify({version: 1, records}, null, 1));
        fs.renameSync(`${FILE}.tmp`, FILE);
    } catch (error) {
        console.error(`[freeride] Could not save speed traps: ${error.message}`);
    }
}

function items() {
    return traps.map((trap) => ({id: `t${trap.id}`, position: trap.position, range: 250, color: COLORS.blue,
        label: records[trap.id] ? `SPEED TRAP ${trap.id} · ${records[trap.id].kmh} KM/H` : `SPEED TRAP ${trap.id}`}));
}

function tick() {
    const now = Date.now();
    for (const player of World.getPlayers()) {
        const car = playable(player) && player.virtualWorld === MAIN_WORLD && player.getSeat() === 0 ? player.getVehicle() : null;
        if (!car) continue;
        for (const trap of traps) {
            const key = `${player.id}:${trap.id}`;
            if (flatDistance(car.position, trap.position) > RADIUS || now - (lastHit.get(key) ?? 0) < REPEAT_MS) continue;
            const kmh = Math.round(speedOf(car) * 3.6);
            if (kmh < MIN_KMH) continue;
            lastHit.set(key, now);
            const best = profiles.get(player)?.speedBest ?? {};
            const personal = kmh > (best[trap.id] ?? 0);
            if (personal) profiles.setField(player, "speedBest", {...best, [trap.id]: kmh});
            const record = records[trap.id];
            if (!record || kmh > record.kmh) {
                records[trap.id] = {kmh, name: player.nickname};
                save();
                markersAll("speedtraps", items());
                player.giveMoney(RECORD_PRIZE);
                feed("record", [name(player, COLORS.gold), text(` set the speed record at trap ${trap.id} · `), text(`${kmh} km/h`, COLORS.gold)]);
                toast(player, `${kmh} km/h, a new record. ${formatMoney(RECORD_PRIZE)}.`, {title: `Speed trap ${trap.id}`, tone: "gold", seconds: 4});
            } else {
                toast(player, `${kmh} km/h${personal ? " · your best" : ` · best ${best[trap.id]}`} · record ${record.kmh} by ${record.name}`,
                    {title: `Speed trap ${trap.id}`, seconds: 3});
            }
        }
    }
}

commands.register({name: "speedtraps", aliases: ["traps"], usage: "/speedtraps", help: "Speed trap records and your bests.", category: "Races"}, (player) => {
    const best = profiles.get(player)?.speedBest ?? {};
    player.sendMessage(traps.map((trap) => `#${trap.id}: ${records[trap.id] ? `${records[trap.id].kmh} (${records[trap.id].name})` : "-"}, you ${best[trap.id] ?? "-"}`).join("  "), 0xd9cdb4);
    markers(player, "speedtraps", items());
});

menu.provide("speedtraps", (player) => traps.map((trap) => ({id: trap.id, record: records[trap.id] ?? null,
    best: profiles.get(player)?.speedBest?.[trap.id] ?? null})));

onStart(() => {
    load();
    markersAll("speedtraps", items());
    timer = setInterval(tick, TICK_MS);
});
onStop(() => clearInterval(timer));
Events.on("playerSpawn", (player) => markers(player, "speedtraps", items()));
Events.on("playerDisconnect", (player) => {
    for (const key of [...lastHit.keys()]) if (key.startsWith(`${player.id}:`)) lastHit.delete(key);
});
