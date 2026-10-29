// Thirty cash stashes are hidden at the roadside across each map. Each one
// shows only when a player is close, pays once per player and the find is
// remembered between sessions; the last one on a map pays a large bonus.
// Finds are stored as "<map>:<number>".
const roads = require("./lib/roads.js");
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, markers, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, flatDistance, playable, formatMoney, onStart, onStop} = require("./lib/util.js");

const COUNT = 30;
const MIN_SPACING = 140;
const REACH = 2.2;
const PRIZE = 100;
const FULL_SET_BONUS = 2500;
const XP = 20;
const SHOW_WITHIN = 45;
const TICK_MS = 500;

// A fixed seed keeps the same spots on every server and every restart.
function seeded(seed) {
    let value = seed >>> 0;
    return () => {
        value = Math.imul(value ^ (value >>> 15), 0x2c1b3c6d) + 0x6d2b79f5 >>> 0;
        return value / 0x100000000;
    };
}

function chooseSpots(map) {
    const graph = roads.forMap(map);
    const random = seeded(0x1931);
    const spots = [];
    for (let attempt = 0; spots.length < COUNT && attempt < 5000; attempt++) {
        const {position, direction} = graph.roadPoint(random);
        // Toward the kerb, still on the road surface so it can always be reached.
        const spot = {x: position.x + direction.z * 3.5, y: position.y + 0.2, z: position.z - direction.x * 3.5};
        if (spots.every((other) => flatDistance(other.position, spot) >= MIN_SPACING)) spots.push({id: `${map.id}:${spots.length + 1}`, position: spot});
    }
    return spots;
}

const spotsByMap = new Map();
let timer = null;

function currentSpots() {
    const map = maps.current();
    if (!map) return [];
    if (!spotsByMap.has(map.id)) spotsByMap.set(map.id, chooseSpots(map));
    return spotsByMap.get(map.id);
}

// Every find the player has, on any map.
function allFound(player) {
    return (profiles.get(player)?.stashes ?? []).filter((id) => typeof id === "string");
}

function found(player) {
    const ids = new Set(currentSpots().map((spot) => spot.id));
    return new Set(allFound(player).filter((id) => ids.has(id)));
}

function nearby(player) {
    const done = found(player);
    markers(player, "stash", currentSpots().filter((spot) => !done.has(spot.id))
        .map((spot) => ({id: `s${spot.id}`, position: spot.position, label: "CASH", color: COLORS.green, model: "9money.i3d", range: SHOW_WITHIN})));
}

function tick() {
    for (const player of World.getPlayers()) {
        if (!playable(player) || player.virtualWorld !== MAIN_WORLD) continue;
        const done = found(player);
        const spot = currentSpots().find((candidate) => !done.has(candidate.id) && distance(player.position, candidate.position) <= REACH);
        if (!spot) continue;
        done.add(spot.id);
        profiles.setField(player, "stashes", [...new Set([...allFound(player), spot.id])]);
        player.giveMoney(PRIZE);
        profiles.addXp(player, XP, "Cash stash");
        nearby(player);
        if (done.size === currentSpots().length) {
            player.giveMoney(FULL_SET_BONUS);
            feed("stash", [name(player, COLORS.gold), text(` found all ${currentSpots().length} cash stashes · `), text(formatMoney(FULL_SET_BONUS), COLORS.gold)]);
            toast(player, `Every stash in the city. ${formatMoney(FULL_SET_BONUS)} bonus.`, {title: "Cash stash", tone: "gold"});
        } else {
            toast(player, `Cash stash ${done.size}/${currentSpots().length}: ${formatMoney(PRIZE)}.`, {title: "Cash stash", tone: "good", seconds: 3});
        }
    }
}

commands.register({name: "stashes", usage: "/stashes", help: "How many hidden cash stashes you have found.", category: "City"}, (player) => {
    toast(player, `${found(player).size} of ${currentSpots().length} cash stashes found. They show only up close, at the roadside.`);
});

menu.provide("stashes", (player) => ({found: found(player).size, total: currentSpots().length}));

onStart(() => { timer = setInterval(tick, TICK_MS); });
onStop(() => clearInterval(timer));
Events.on("playerSpawn", nearby);
