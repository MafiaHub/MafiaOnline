// Speed traps on each map's six longest roads. Driving through one records
// the car's speed against the player's own best and the server record;
// beating the record is announced and pays. Records and bests are keyed
// "<map>:<trap>"; plain numbers are stock Lost Heaven's from before maps.
const store = require("./lib/store.js");
const roads = require("./lib/roads.js");
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, markersAll, markers, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, flatDistance, playable, speedOf, formatMoney, onStart, onStop} = require("./lib/util.js");

const TRAP_COUNT = 6;
const RADIUS = 14;
const REPEAT_MS = 10_000;
const MIN_KMH = 40;
const RECORD_PRIZE = 150;
const TICK_MS = 150;

const trapsByMap = new Map();

// The middle of each of the map's longest roads.
function currentTraps() {
    const map = maps.current();
    if (!map) return [];
    if (!trapsByMap.has(map.id)) {
        const lines = roads.forMap(map).lines.slice().sort((a, b) => b.length - a.length).slice(0, TRAP_COUNT);
        trapsByMap.set(map.id, lines.map((road, index) => ({id: index + 1, key: `${map.id}:${index + 1}`,
            position: roads.pointAlong(road.line, roads.lineLength(road.line) / 2).position})));
    }
    return trapsByMap.get(map.id);
}

function migrate(table) {
    return Object.fromEntries(Object.entries(table ?? {}).map(([key, value]) => [/^\d+$/.test(key) ? `freeride:${key}` : key, value]));
}

let records = {};
const lastHit = new Map();
let timer = null;

function load() {
    const stored = store.load("speedtraps");
    if (stored) records = migrate(stored.records);
}

function save() {
    store.save("speedtraps", {version: 1, records});
}

function items() {
    return currentTraps().map((trap) => ({id: `t${trap.id}`, position: trap.position, range: 250, color: COLORS.blue,
        label: records[trap.key] ? `SPEED TRAP ${trap.id} · ${records[trap.key].kmh} KM/H` : `SPEED TRAP ${trap.id}`}));
}

function bests(player) {
    return migrate(profiles.get(player)?.speedBest);
}

function tick() {
    const now = Date.now();
    for (const player of World.getPlayers()) {
        const car = playable(player) && player.virtualWorld === MAIN_WORLD && player.getSeat() === 0 ? player.getVehicle() : null;
        if (!car) continue;
        for (const trap of currentTraps()) {
            const key = `${player.id}:${trap.key}`;
            if (flatDistance(car.position, trap.position) > RADIUS || now - (lastHit.get(key) ?? 0) < REPEAT_MS) continue;
            const kmh = Math.round(speedOf(car) * 3.6);
            if (kmh < MIN_KMH) continue;
            lastHit.set(key, now);
            const best = bests(player);
            const personal = kmh > (best[trap.key] ?? 0);
            if (personal) profiles.setField(player, "speedBest", {...best, [trap.key]: kmh});
            const record = records[trap.key];
            if (!record || kmh > record.kmh) {
                records[trap.key] = {kmh, name: player.nickname};
                save();
                markersAll("speedtraps", items());
                player.giveMoney(RECORD_PRIZE);
                feed("record", [name(player, COLORS.gold), text(` set the speed record at trap ${trap.id} · `), text(`${kmh} km/h`, COLORS.gold)]);
                toast(player, `${kmh} km/h, a new record. ${formatMoney(RECORD_PRIZE)}.`, {title: `Speed trap ${trap.id}`, tone: "gold", seconds: 4});
            } else {
                toast(player, `${kmh} km/h${personal ? " · your best" : ` · best ${best[trap.key]}`} · record ${record.kmh} by ${record.name}`,
                    {title: `Speed trap ${trap.id}`, seconds: 3});
            }
        }
    }
}

commands.register({name: "speedtraps", aliases: ["traps"], usage: "/speedtraps", help: "Speed trap records and your bests.", category: "Races"}, (player) => {
    const best = bests(player);
    player.sendMessage(currentTraps().map((trap) => `#${trap.id}: ${records[trap.key] ? `${records[trap.key].kmh} (${records[trap.key].name})` : "-"}, you ${best[trap.key] ?? "-"}`).join("  "), 0xd9cdb4);
    markers(player, "speedtraps", items());
});

menu.provide("speedtraps", (player) => {
    const best = bests(player);
    return currentTraps().map((trap) => ({id: trap.id, record: records[trap.key] ?? null, best: best[trap.key] ?? null}));
});

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
