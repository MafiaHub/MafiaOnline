// Turf. Eight street corners spread across the city can be held by a crew.
// Members standing on an unheld or rival corner with no other crew around
// take it over in a minute; a held corner pays every online member of its
// crew. Ownership persists between sessions.
const fs = require("node:fs");
const path = require("node:path");
const roads = require("./lib/roads.js");
const profiles = require("./lib/profiles.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, card, markersAll, markers, feed, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, flatDistance, playable, formatMoney, onStart, onStop} = require("./lib/util.js");

const FILE = path.join(profiles.DATA_DIR, "turf.json");
const TURF_COUNT = 8;
const RADIUS = 45;
const CAPTURE_MS = 60_000;
const TICK_MS = 2000;
const INCOME_EVERY_MS = 3 * 60_000;
const INCOME = 40;
const CAPTURE_XP = 60;
const LETTERS = "ABCDEFGH";

// Corners chosen by farthest-point sampling over the road crossings, so they
// spread evenly and every one lies on a street.
function chooseCorners() {
    const chosen = [roads.nearestCrossing({x: 0, y: 0, z: 0})];
    while (chosen.length < TURF_COUNT) {
        let best = -1;
        let bestGap = -1;
        roads.crossings.forEach((crossing, index) => {
            const gap = Math.min(...chosen.map((other) => flatDistance(crossing, roads.crossings[other])));
            if (gap > bestGap) {
                best = index;
                bestGap = gap;
            }
        });
        chosen.push(best);
    }
    return chosen.map((index, order) => ({id: LETTERS[order], position: roads.crossings[index], owner: null, progress: 0, by: null, contested: false}));
}

const turfs = chooseCorners();
let timer = null;
let lastIncome = 0;

function load() {
    try {
        const stored = JSON.parse(fs.readFileSync(FILE, "utf8")).owners ?? {};
        for (const turf of turfs) turf.owner = stored[turf.id] ?? null;
    } catch (error) {
        if (error.code !== "ENOENT") console.error(`[freeride] Could not read ${FILE}: ${error.message}`);
    }
}

function save() {
    try {
        fs.mkdirSync(profiles.DATA_DIR, {recursive: true});
        fs.writeFileSync(`${FILE}.tmp`, JSON.stringify({version: 1, owners: Object.fromEntries(turfs.map((turf) => [turf.id, turf.owner]))}, null, 1));
        fs.renameSync(`${FILE}.tmp`, FILE);
    } catch (error) {
        console.error(`[freeride] Could not save turf: ${error.message}`);
    }
}

function items() {
    return turfs.map((turf) => ({id: turf.id, position: turf.position, ring: RADIUS, range: 700,
        label: turf.owner ? `TURF ${turf.id} · [${turf.owner.tag}]` : `TURF ${turf.id} · OPEN`,
        color: turf.contested ? COLORS.red : turf.owner?.color ?? COLORS.muted}));
}

function publish(player = null) {
    if (player) markers(player, "turf", items());
    else markersAll("turf", items());
}

function tick() {
    const now = Date.now();
    let changed = false;
    const street = World.getPlayers().filter((player) => playable(player) && player.virtualWorld === MAIN_WORLD);
    for (const turf of turfs) {
        const present = street.filter((player) => flatDistance(player.position, turf.position) <= RADIUS);
        const crews = new Map();
        for (const player of present) {
            const crew = player.state.get("fr.crew");
            if (crew) crews.set(crew.tag, crew);
        }
        const contested = crews.size > 1;
        if (contested !== turf.contested) {
            turf.contested = contested;
            changed = true;
        }
        const [taker] = crews.size === 1 ? [...crews.values()] : [];
        if (!taker || taker.tag === turf.owner?.tag) {
            if (!contested) turf.progress = 0;
        } else {
            if (turf.by !== taker.tag) {
                turf.by = taker.tag;
                turf.progress = 0;
            }
            turf.progress += TICK_MS;
            if (turf.progress >= CAPTURE_MS) {
                const previous = turf.owner;
                turf.owner = {tag: taker.tag, name: taker.name, color: taker.color};
                turf.progress = 0;
                changed = true;
                save();
                feed("turf", [text(`[${taker.tag}]`, taker.color), text(previous ? ` took turf ${turf.id} from ` : ` claimed turf ${turf.id}`),
                    ...(previous ? [text(`[${previous.tag}]`, previous.color)] : [])]);
                for (const player of present) {
                    profiles.bump(player, "turfCaptures");
                    profiles.addXp(player, CAPTURE_XP, `Took turf ${turf.id}`);
                }
            }
        }
        for (const player of present) {
            const crew = player.state.get("fr.crew");
            card(player, "turf", {title: `Turf ${turf.id}`, subtitle: turf.owner ? `Held by [${turf.owner.tag}] ${turf.owner.name}` : "Nobody holds this corner",
                rows: [["Status", contested ? "Contested" : !crew ? "Join a crew to take it" : crew.tag === turf.owner?.tag ? "Yours" :
                    `Taking over · ${Math.round(turf.progress / CAPTURE_MS * 100)}%`], ["Pays", `${formatMoney(INCOME)} each, every 3 min`]],
                progress: turf.progress / CAPTURE_MS, tone: contested ? "bad" : "gold", seconds: 3});
        }
    }
    if (changed) publish();
    if (now - lastIncome >= INCOME_EVERY_MS) {
        lastIncome = now;
        for (const player of World.getPlayers()) {
            const tag = player.state.get("fr.crew")?.tag;
            const held = tag ? turfs.filter((turf) => turf.owner?.tag === tag).length : 0;
            if (!held || !player.spawned) continue;
            player.giveMoney(INCOME * held);
            toast(player, `Your crew's ${held} turf${held === 1 ? "" : "s"} paid ${formatMoney(INCOME * held)}.`, {title: "Turf", tone: "good", seconds: 4});
        }
    }
}

commands.register({name: "turf", usage: "/turf", help: "Who holds the city's corners.", category: "Crew"}, (player) => {
    player.sendMessage(turfs.map((turf) => `${turf.id}: ${turf.owner ? `[${turf.owner.tag}]` : "open"}`).join("  "), 0xd9cdb4);
    markers(player, "turf", items());
});

menu.provide("turf", () => turfs.map((turf) => ({id: turf.id, owner: turf.owner, contested: turf.contested})));

onStart(() => {
    load();
    lastIncome = Date.now();
    publish();
    timer = setInterval(tick, TICK_MS);
});
onStop(() => clearInterval(timer));
Events.on("playerSpawn", (player) => publish(player));
