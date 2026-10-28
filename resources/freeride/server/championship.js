// The Autodrom championship, on maps with the official circuit (Lost Heaven
// Extended). A season is three rounds of growing length; anyone can enter any
// round, every finish scores points, and the standings after the last round
// pay out and crown a champion. Champions are remembered between sessions.
const fs = require("node:fs");
const path = require("node:path");
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const races = require("./races.js");
const {toast, toastAll, feed, name, text, COLORS} = require("./lib/notify.js");
const {formatMoney, formatDuration, onStop} = require("./lib/util.js");

const FILE = path.join(profiles.DATA_DIR, "championship.json");
const ROUNDS = [
    {laps: 1, name: "Round 1 · Sprint"},
    {laps: 2, name: "Round 2 · Feature"},
    {laps: 3, name: "Round 3 · Grand Prix"},
];
const POINTS = [25, 18, 15, 12, 10, 8, 6, 4, 2, 1];
const PRIZES = [3000, 1500, 750];
const CHAMPION_XP = 400;
const ROUND_LOBBY_MS = 60_000;
const BREAK_MS = 45_000;
const RETRY_MS = 15_000;

let season = null;
let champions = [];
let timer = null;

function load() {
    try {
        champions = JSON.parse(fs.readFileSync(FILE, "utf8")).champions ?? [];
    } catch (error) {
        if (error.code !== "ENOENT") console.error(`[freeride] Could not read ${FILE}: ${error.message}`);
    }
}

function save() {
    try {
        fs.mkdirSync(profiles.DATA_DIR, {recursive: true});
        fs.writeFileSync(`${FILE}.tmp`, JSON.stringify({version: 1, champions}, null, 1));
        fs.renameSync(`${FILE}.tmp`, FILE);
    } catch (error) {
        console.error(`[freeride] Could not save the championship: ${error.message}`);
    }
}

// Standings by points, then by wins; names carry over a reconnect.
function standings() {
    return [...(season?.points ?? new Map()).entries()].map(([player, entry]) => ({name: player, ...entry}))
        .sort((a, b) => b.points - a.points || b.wins - a.wins);
}

function scheduleRound(delay) {
    clearTimeout(timer);
    timer = setTimeout(openRound, delay);
}

function openRound() {
    timer = null;
    const map = maps.current();
    if (!season || !map?.circuit || map.id !== season.map) {
        if (season) toastAll("The championship was called off: the circuit is not on this map.", {title: "Autodrom"});
        season = null;
        return;
    }
    const round = ROUNDS[season.round];
    const route = races.circuitRoute(map, round.laps, `Autodrom Championship · ${round.name}`,
        `${round.laps} lap${round.laps === 1 ? "" : "s"} of the official circuit.`, `b:${map.id}:championship-${round.laps}`);
    const race = races.openRace(null, route, {title: "The Autodrom", lobbyMs: ROUND_LOBBY_MS, onConclude: scoreRound});
    if (!race) {
        scheduleRound(RETRY_MS);
        return;
    }
    season.raceId = race.id;
    toastAll(`${round.name}: entries close in ${ROUND_LOBBY_MS / 1000} seconds. Type /race join; a race car waits on the grid.`,
        {title: "Autodrom Championship", tone: "gold", seconds: 10});
}

function scoreRound(finishers) {
    if (!season) return;
    const round = ROUNDS[season.round];
    finishers.forEach(({player, ms}, index) => {
        const entry = season.points.get(player.nickname) ?? {points: 0, wins: 0, best: null};
        entry.points += POINTS[index] ?? 0;
        if (index === 0) entry.wins++;
        entry.best = entry.best === null ? ms : Math.min(entry.best, ms);
        season.points.set(player.nickname, entry);
    });
    if (finishers.length) {
        feed("race", [text(round.name, COLORS.gold), text(" won by "), name(finishers[0].player, COLORS.white), text(` · ${formatDuration(finishers[0].ms)}`)]);
    }
    season.round++;
    const table = standings();
    const lines = table.slice(0, 3).map((entry, index) => `${index + 1}. ${entry.name} ${entry.points}`).join(" · ");
    if (season.round < ROUNDS.length) {
        toastAll(`Standings: ${lines || "nobody has scored yet"}. ${ROUNDS[season.round].name} opens in ${BREAK_MS / 1000} seconds.`, {title: "Autodrom Championship"});
        scheduleRound(BREAK_MS);
        return;
    }
    finishSeason(table);
}

function finishSeason(table) {
    const ended = season;
    season = null;
    if (!table.length) {
        toastAll("The championship ends with nobody across the line.", {title: "Autodrom Championship"});
        return;
    }
    table.slice(0, PRIZES.length).forEach((entry, index) => {
        const player = World.getPlayers().find((other) => other.nickname === entry.name);
        if (!player) return;
        player.giveMoney(PRIZES[index]);
        if (index === 0) {
            profiles.bump(player, "championships");
            profiles.addXp(player, CHAMPION_XP, "Autodrom champion");
        }
        toast(player, `${["Champion", "Runner-up", "Third"][index]} of the Autodrom: ${formatMoney(PRIZES[index])}.`, {title: "Autodrom Championship", tone: "gold"});
    });
    champions.unshift({name: table[0].name, points: table[0].points, at: Date.now(), map: ended.map});
    champions = champions.slice(0, 20);
    save();
    toastAll(`${table[0].name} is the Autodrom champion with ${table[0].points} points.`, {title: "Autodrom Championship", tone: "gold", seconds: 10});
    feed("record", [text(table[0].name, COLORS.gold), text(" won the Autodrom Championship")]);
}

commands.register({name: "championship", aliases: ["champ"], usage: "/championship [start|status]",
    help: "Run a three-round championship on the official circuit.", category: "Races"}, (player, args) => {
    const action = (args[0] ?? "status").toLowerCase();
    if (action === "start") {
        if (season) {
            toast(player, "A championship is already under way. /championship shows the standings.", {tone: "bad"});
            return;
        }
        if (!maps.current()?.circuit) {
            toast(player, "There is no official circuit on this map. Vote for Lost Heaven Extended with /map.", {tone: "bad"});
            return;
        }
        season = {map: maps.current().id, round: 0, points: new Map(), raceId: null, startedBy: player.nickname};
        feed("race", [name(player, COLORS.gold), text(" started an Autodrom Championship")]);
        openRound();
        return;
    }
    const table = standings();
    player.sendMessage(season
        ? `Championship, ${ROUNDS[Math.min(season.round, ROUNDS.length - 1)].name}: ${table.map((entry, index) => `${index + 1}. ${entry.name} ${entry.points}`).join(" · ") || "no points yet"}`
        : `No championship running. ${champions[0] ? `Last champion: ${champions[0].name}.` : ""} /championship start begins one.`, 0xd9cdb4);
});

menu.provide("championship", () => ({
    available: Boolean(maps.current()?.circuit),
    running: season ? {round: Math.min(season.round + 1, ROUNDS.length), rounds: ROUNDS.length, name: ROUNDS[Math.min(season.round, ROUNDS.length - 1)].name,
        standings: standings().slice(0, 8)} : null,
    champions: champions.slice(0, 5),
}));

load();
onStop(() => clearTimeout(timer));
Events.on("missionChange", () => {
    if (!season) return;
    season = null;
    clearTimeout(timer);
});
