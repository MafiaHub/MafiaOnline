// Persistent player records: cash, experience, rank and career statistics.
// Free Ride has no accounts, so a record belongs to a nickname on one machine.
// Public values travel in each player's replicated state bag, which the
// scoreboard and the world labels on every client read.
const fs = require("node:fs");
const path = require("node:path");
const {toast, feed, name, text, COLORS} = require("./notify.js");
const {emit, onStop} = require("./util.js");

const DATA_DIR = path.join(process.cwd(), "data", "freeride");
const FILE = path.join(DATA_DIR, "profiles.json");
const SAVE_DELAY_MS = 10_000;
const STARTING_CASH = 800;
const PLAYTIME_XP_EVERY_MS = 5 * 60_000;
const PLAYTIME_XP = 25;

const RANKS = [
    [1, "Greenhorn"], [3, "Street Runner"], [5, "Associate"], [8, "Soldier"],
    [12, "Capo"], [16, "Underboss"], [20, "Don"],
];

const records = new Map();
const sessions = new Map();
let saveTimer = null;

function load() {
    try {
        const stored = JSON.parse(fs.readFileSync(FILE, "utf8"));
        for (const [key, record] of Object.entries(stored.players ?? {})) records.set(key, record);
        console.log(`[freeride] Loaded ${records.size} player records`);
    } catch (error) {
        if (error.code !== "ENOENT") console.error(`[freeride] Could not read ${FILE}: ${error.message}`);
    }
}

function writeNow() {
    if (saveTimer !== null) clearTimeout(saveTimer);
    saveTimer = null;
    try {
        fs.mkdirSync(DATA_DIR, {recursive: true});
        const temporary = `${FILE}.tmp`;
        fs.writeFileSync(temporary, JSON.stringify({version: 1, players: Object.fromEntries(records)}, null, 1));
        fs.renameSync(temporary, FILE);
    } catch (error) {
        console.error(`[freeride] Could not save player records: ${error.message}`);
    }
}

function saveSoon() {
    if (saveTimer === null) saveTimer = setTimeout(writeNow, SAVE_DELAY_MS);
}

function keyFor(player) {
    const nickname = player.nickname.toLowerCase();
    return player.hardwareId ? `${nickname}@${player.hardwareId}` : nickname;
}

function fresh(player) {
    const now = Date.now();
    return {name: player.nickname, money: STARTING_CASH, xp: 0, kills: 0, deaths: 0, bestStreak: 0,
        racesRun: 0, racesWon: 0, raceRecords: 0, deliveries: 0, bountiesClaimed: 0, eventsWon: 0,
        duelsWon: 0, cars: [], exports: 0, fares: 0, arrests: 0, robberies: 0, turfCaptures: 0, stashes: [], speedBest: {}, playMs: 0, model: null, district: null, firstSeen: now, lastSeen: now};
}

function levelFor(xp) {
    return Math.max(1, Math.floor((1 + Math.sqrt(1 + 8 * xp / 100)) / 2));
}

function xpForLevel(level) {
    return 100 * level * (level - 1) / 2;
}

function rankFor(level) {
    let rank = RANKS[0][1];
    for (const [minimum, title] of RANKS) if (level >= minimum) rank = title;
    return rank;
}

// The record key of a connected player, or null before attach.
function keyOf(player) {
    return sessions.get(player.id)?.key ?? null;
}

function get(player) {
    const session = sessions.get(player.id);
    return session ? records.get(session.key) : null;
}

function progress(record) {
    const level = levelFor(record.xp);
    const base = xpForLevel(level);
    return {level, rank: rankFor(level), xp: record.xp, levelXp: record.xp - base, levelSpan: xpForLevel(level + 1) - base};
}

// Private HUD details go to the owner; public ones ride on the state bag.
function publish(player) {
    const record = get(player);
    if (!record) return;
    const {level, rank, levelXp, levelSpan} = progress(record);
    player.state.set("fr.level", level);
    player.state.set("fr.rank", rank);
    player.state.set("fr.kills", record.kills);
    player.state.set("fr.deaths", record.deaths);
    emit(player, "fr:profile", {level, rank, levelXp, levelSpan, xp: record.xp, money: player.money});
}

function setPublic(player, key, value) {
    if (value === null || value === undefined) {
        if (player.state.has(key)) player.state.remove(key);
    } else if (player.state.get(key) !== value) {
        player.state.set(key, value);
    }
}

function attach(player) {
    const key = keyFor(player);
    let record = records.get(key);
    const returning = Boolean(record);
    if (!record) {
        record = fresh(player);
        records.set(key, record);
    }
    record.name = player.nickname;
    record.lastSeen = Date.now();
    sessions.set(player.id, {key, lastPlaytimeAt: Date.now()});
    player.setMoney(Math.max(0, Math.min(1_000_000_000, Math.round(record.money))));
    publish(player);
    saveSoon();
    return {record, returning};
}

function detach(player) {
    const session = sessions.get(player.id);
    const record = get(player);
    if (session && record) {
        record.playMs += Date.now() - session.lastPlaytimeAt;
        record.lastSeen = Date.now();
    }
    sessions.delete(player.id);
    writeNow();
}

function addXp(player, amount, reason) {
    const record = get(player);
    if (!record || amount <= 0) return;
    const before = levelFor(record.xp);
    record.xp += Math.round(amount);
    const after = levelFor(record.xp);
    emit(player, "fr:xp", {amount: Math.round(amount), reason: reason ?? ""});
    publish(player);
    saveSoon();
    if (after > before) {
        const rank = rankFor(after);
        const promoted = rank !== rankFor(before);
        toast(player, promoted ? `You are now a ${rank}.` : `Level ${after} reached.`,
            {title: `Level ${after}`, tone: "gold", seconds: 6});
        emit(player, "fr:levelup", {level: after, rank, promoted});
        feed("level", [name(player, COLORS.gold), text(promoted ? ` made ${rank} (level ${after})` : ` reached level ${after}`)]);
    }
}

// Counters added in later versions start at zero on older records.
function bump(player, field, amount = 1) {
    const record = get(player);
    if (!record || !["number", "undefined"].includes(typeof record[field])) return;
    record[field] = (record[field] ?? 0) + amount;
    saveSoon();
}

function setField(player, field, value) {
    const record = get(player);
    if (!record) return;
    record[field] = value;
    saveSoon();
}

function leaderboard(field, count = 8) {
    const rows = [...records.values()].map((record) => ({name: record.name, value: field === "level" ? record.xp : record[field] ?? 0,
        level: levelFor(record.xp)}));
    return rows.filter((row) => row.value > 0).sort((a, b) => b.value - a.value).slice(0, count);
}

function tickPlaytime() {
    const now = Date.now();
    for (const player of World.getPlayers()) {
        const session = sessions.get(player.id);
        const record = get(player);
        if (!session || !record) continue;
        if (now - session.lastPlaytimeAt >= PLAYTIME_XP_EVERY_MS) {
            record.playMs += now - session.lastPlaytimeAt;
            session.lastPlaytimeAt = now;
            if (player.spawned) addXp(player, PLAYTIME_XP, "Time on the streets");
        }
    }
}

Events.on("playerMoneyChange", (player, _old, money) => {
    const record = get(player);
    if (!record) return;
    record.money = money;
    emit(player, "fr:profile", {...progress(record), money});
    saveSoon();
});

const playtimeTimer = setInterval(tickPlaytime, 30_000);
onStop(() => {
    clearInterval(playtimeTimer);
    for (const player of World.getPlayers()) {
        const session = sessions.get(player.id);
        const record = get(player);
        if (session && record) record.playMs += Date.now() - session.lastPlaytimeAt;
    }
    writeNow();
});

// The HUD view exists only once the client has loaded the resource.
Events.on("playerSpawn", publish);

load();

module.exports = {get, keyOf, attach, save: writeNow, detach, publish, setPublic, addXp, bump, setField, progress, leaderboard,
    levelFor, rankFor, STARTING_CASH, DATA_DIR};
