// Which map Free Ride plays. At start the server moves from stock Free Ride
// (or its night map) to the default map, Lost Heaven Extended unless
// FREERIDE_MAP names another; a map that is not installed is skipped.
// Players can vote for another map; a majority of those in town switches it
// after a short warning.
const maps = require("./lib/maps.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, toastAll, feed, name, text, COLORS} = require("./lib/notify.js");
const {onStart, onStop} = require("./lib/util.js");

const SWITCH_WARNING_MS = 15_000;
const SWITCH_COOLDOWN_MS = 5 * 60_000;
const CHECK_MS = 60_000;

const votes = new Map();
const unavailable = new Set();
let pending = null;
let lastSwitch = 0;
let timer = null;

// Only a voted change starts the cooldown; the startup move to the default does not.
function change(id, voted = false) {
    if (World.changeMission(id)) {
        if (voted) lastSwitch = Date.now();
        votes.clear();
        return true;
    }
    unavailable.add(id);
    console.warn(`[freeride] Map ${id} is not installed on this server`);
    return false;
}

// At start stock Free Ride gives way to the default map; the night map does
// so whenever it loads, since Free Ride plays by day.
function settle(starting) {
    const mission = World.getMission();
    if (mission !== maps.NIGHT_MAP && !(starting && mission === "freeride")) return;
    if (maps.DEFAULT_MAP !== mission && !unavailable.has(maps.DEFAULT_MAP) && maps.MAPS[maps.DEFAULT_MAP] && change(maps.DEFAULT_MAP)) return;
    if (mission === maps.NIGHT_MAP) change("freeride");
}

function find(query) {
    const text = query.trim().toLowerCase();
    return Object.values(maps.MAPS).find((map) => map.id === text || map.title.toLowerCase() === text) ??
        Object.values(maps.MAPS).find((map) => map.title.toLowerCase().includes(text) || map.id.includes(text)) ?? null;
}

function needed() {
    return Math.floor(World.getPlayers().filter((player) => player.spawned).length / 2) + 1;
}

function tally() {
    const counts = new Map();
    for (const id of votes.values()) counts.set(id, (counts.get(id) ?? 0) + 1);
    return counts;
}

function consider() {
    if (pending) return;
    const current = maps.current()?.id;
    for (const [id, count] of tally()) {
        if (id === current || count < needed()) continue;
        const wait = lastSwitch + SWITCH_COOLDOWN_MS - Date.now();
        if (wait > 0) {
            toastAll(`${maps.MAPS[id].title} has the votes; the city can change again in ${Math.ceil(wait / 60_000)} min.`, {title: "Map vote"});
            return;
        }
        pending = setTimeout(() => {
            pending = null;
            if (!change(id, true)) toastAll(`${maps.MAPS[id].title} is not installed on this server.`, {title: "Map vote", tone: "bad"});
        }, SWITCH_WARNING_MS);
        toastAll(`The vote is in: ${maps.MAPS[id].title} in ${SWITCH_WARNING_MS / 1000} seconds.`, {title: "Map vote", tone: "gold", seconds: 10});
        feed("map", [text("Next map: ", COLORS.muted), text(maps.MAPS[id].title, COLORS.gold)]);
        return;
    }
}

commands.register({name: "map", aliases: ["votemap", "maps"], usage: "/map [name]", help: "Vote for the map the city plays.", category: "City"}, (player, args) => {
    if (!args.length) {
        const counts = tally();
        const list = Object.values(maps.MAPS).map((map) => `${map.title}${map.id === maps.current()?.id ? " (now)" : ""}${unavailable.has(map.id) ? " (not installed)" : ""}: ${counts.get(map.id) ?? 0}`);
        player.sendMessage(`Maps · ${list.join(" · ")} · ${needed()} votes switch. /map <name> to vote.`, 0xd9cdb4);
        return;
    }
    const map = find(args.join(" "));
    if (!map || unavailable.has(map.id)) {
        toast(player, map ? `${map.title} is not installed on this server.` : "No such map. /map lists them.", {tone: "bad"});
        return;
    }
    votes.set(player.id, map.id);
    feed("map", [name(player, COLORS.gold), text(" voted for "), text(map.title, COLORS.white)]);
    consider();
});

menu.provide("maps", (player) => {
    const counts = tally();
    return {current: maps.current()?.id ?? null, needed: needed(), maps: Object.values(maps.MAPS).map((map) => ({id: map.id, title: map.title,
        votes: counts.get(map.id) ?? 0, yours: votes.get(player.id) === map.id, available: !unavailable.has(map.id)}))};
});

onStart(() => {
    settle(true);
    timer = setInterval(() => settle(false), CHECK_MS);
});
onStop(() => {
    clearInterval(timer);
    clearTimeout(pending);
    pending = null;
});
Events.on("playerDisconnect", (player) => {
    votes.delete(player.id);
});
