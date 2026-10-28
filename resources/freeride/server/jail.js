// Jail. An arrest sends the player to a cell in the county jail
// (lib/prison.js) for a sentence that grows with the wanted level they were
// caught at; the police keep their weapons. A guard walks a prisoner who gets
// out of the cell straight back. When the time is served or bail is paid,
// the warden (lib/warden.js) unlocks the cell door, it slides open, and the
// prisoner walks down the corridor to the door at the end, which leads back
// to the city. The sentence survives a reconnect.
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const prison = require("./lib/prison.js");
const warden = require("./lib/warden.js");
const {toast, card, feed, markers, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, emit, formatMoney, formatMinutes, onStart, onStop} = require("./lib/util.js");

const BASE_SENTENCE_MS = 45_000;
const PER_LEVEL_MS = 30_000;
const BAIL_PER_SECOND = 5;
const TICK_MS = 1000;
const GUARD_MS = 250;
const FADE_MS = 400;
// How far past the bars a locked-up prisoner may lean before the guard acts.
const CELL_MARGIN = 0.4;
// A freed prisoner this far into the corridor has left the cell, and its
// door closes behind them.
const OUT_OF_CELL = 0.9;
const EXIT_REACH = 1.4;
// The door stays open at least this long, and a freed prisoner who has not
// walked out after the longest wait is shown out.
const DOOR_MIN_OPEN_MS = 4000;
const WALK_OUT_MS = 120_000;
const SPAWN_RETRY_MS = 2000;

// player id -> {until, cell, stage: "locked" | "unlocking" | "free", freedAt}
const prisoners = new Map();
const moving = new Set();
let timer = null;
let guard = null;

function online(id) {
    return World.getPlayers().find((player) => player.id === id) ?? null;
}

function releasePoint(player) {
    return maps.current()?.jail?.release ?? life.spotFor(player);
}

// The first empty cell, or the one with the fewest prisoners.
function assignCell() {
    const count = (cell) => [...prisoners.values()].filter((prisoner) => prisoner.cell === cell).length;
    return prison.CELLS.reduce((best, cell) => count(cell) < count(best) ? cell : best, prison.CELLS[0]);
}

// Fade out, move, fade in, as an entrance does; the move is skipped if the
// life ended meanwhile.
function transfer(player, pose, world, loadout, then) {
    const {spawnGeneration} = player;
    moving.add(player.id);
    emit(player, "fr:fade", {out: true, seconds: FADE_MS / 1000});
    setTimeout(() => {
        moving.delete(player.id);
        if (!online(player.id) || !player.alive || player.spawnGeneration !== spawnGeneration) return;
        player.setVirtualWorld(world);
        if (!life.teleport(player, pose.position, pose.heading ?? 0, loadout)) player.setVirtualWorld(MAIN_WORLD);
        emit(player, "fr:fade", {out: false, seconds: 0.6});
        then?.();
    }, FADE_MS);
}

function lockUp(player, until) {
    if (!prison.isBuilt()) return false;
    if (player.getVehicle()) player.removeFromVehicle();
    life.end(player);
    life.begin(player, "jail", "In jail");
    const cell = prisoners.get(player.id)?.cell ?? assignCell();
    prisoners.set(player.id, {until, cell, stage: "locked", freedAt: 0});
    profiles.setField(player, "jailUntil", until);
    markers(player, "jail", []);
    transfer(player, prison.spawnOf(cell), prison.WORLD, {weapons: [], selected: 0, health: player.health}, () => {
        // The game's own countdown watch runs on the prisoner's client.
        emit(player, "fr:jail", {active: true, seconds: Math.ceil((until - Date.now()) / 1000)});
    });
    return true;
}

// The time is up or bail is paid: the warden comes to open the cell.
function unlock(player, message) {
    const prisoner = prisoners.get(player.id);
    if (!prisoner || prisoner.stage !== "locked") return;
    prisoner.stage = "unlocking";
    profiles.setField(player, "jailUntil", 0);
    toast(player, message, {title: "Released", tone: "good"});
    warden.unlock(prisoner.cell, () => {
        prison.openDoor(prisoner.cell);
        for (const [id, other] of prisoners) {
            if (other.cell !== prisoner.cell || other.stage !== "unlocking") continue;
            other.stage = "free";
            other.freedAt = Date.now();
            const freed = online(id);
            if (!freed) continue;
            emit(freed, "fr:jail", {active: false});
            markers(freed, "jail", [{id: "exit", position: prison.exit(), label: "EXIT", color: COLORS.green, model: "sipka.i3d", range: 40}]);
            toast(freed, "Your cell is open. The way out is the door at the end of the corridor.", {seconds: 6});
        }
    });
}

// Out the jail's door and back to the city.
function walkOut(player) {
    const prisoner = prisoners.get(player.id);
    if (!prisoner || moving.has(player.id)) return;
    transfer(player, releasePoint(player), MAIN_WORLD, {...life.STARTER_KIT, health: 100}, () => {
        prisoners.delete(player.id);
        card(player, "jail", null);
        markers(player, "jail", []);
        life.end(player, "jail");
        toast(player, "Keep your nose clean.", {title: "Released", tone: "good"});
    });
}

// Called by police.js for an arrest (not a fine).
function sentence(player, level) {
    const ms = BASE_SENTENCE_MS + PER_LEVEL_MS * Math.max(0, level - 1);
    if (!lockUp(player, Date.now() + ms)) return false;
    toast(player, `${formatMinutes(ms)} in the county jail. The police kept your guns. /bail pays your way out.`, {title: "Arrested", tone: "bad", seconds: 8});
    feed("police", [name(player), text(" was locked up in the county jail")]);
    return true;
}

function bail(player) {
    return Math.ceil((prisoners.get(player.id).until - Date.now()) / 1000) * BAIL_PER_SECOND;
}

function tick() {
    const now = Date.now();
    warden.need("prisoners", prisoners.size > 0);
    for (const [id, prisoner] of prisoners) {
        const player = online(id);
        if (!player) {
            prisoners.delete(id);
            continue;
        }
        if (prisoner.stage === "locked" && now >= prisoner.until) unlock(player, "You served your time. The warden is coming to open your cell.");
        if (prisoner.stage === "free" && now - prisoner.freedAt >= WALK_OUT_MS) {
            walkOut(player);
            continue;
        }
        const rows = prisoner.stage === "locked" ? [["Released in", formatMinutes(prisoner.until - now)], ["Bail", formatMoney(bail(player))]]
            : prisoner.stage === "unlocking" ? [["Cell", "The warden is coming"]] : [["Cell", "Open: walk out"]];
        card(player, "jail", {title: "In jail", subtitle: "the county jail", rows, tone: prisoner.stage === "free" ? "good" : "bad"});
    }
}

// The guard: a prisoner still locked up who is out of the cell is put
// straight back. A freed one walks out through the door at the end of the
// corridor; one who somehow leaves the hall is shown out.
function watch() {
    for (const [id, prisoner] of prisoners) {
        const player = online(id);
        if (!player?.alive || moving.has(id)) continue;
        const {position} = player;
        if (prisoner.stage === "free") {
            if (prison.atExit(position, EXIT_REACH) || !prison.inHall(position, 2)) walkOut(player);
            continue;
        }
        if (player.virtualWorld === prison.WORLD && prison.inCell(prisoner.cell, position, CELL_MARGIN)) continue;
        if (player.getVehicle()) player.removeFromVehicle();
        player.setVirtualWorld(prison.WORLD);
        const {position: cellSpot, heading} = prison.spawnOf(prisoner.cell);
        life.teleport(player, cellSpot, heading, {weapons: [], selected: 0, health: player.health});
        toast(player, "The guard walks you back to your cell.", {tone: "bad", seconds: 3});
    }
    // An open door closes once no freed prisoner is left in its cell.
    for (const cell of prison.CELLS) {
        if (!prison.doorOpen(cell) || prison.doorOpenFor(cell) < DOOR_MIN_OPEN_MS) continue;
        const lingering = [...prisoners].some(([id, prisoner]) => {
            const player = online(id);
            return prisoner.cell === cell && prisoner.stage === "free" && player && prison.inCell(cell, player.position, OUT_OF_CELL);
        });
        if (!lingering) prison.closeDoor(cell);
    }
}

commands.register({name: "bail", usage: "/bail", help: "Pay your way out of jail.", category: "Combat"}, (player) => {
    const prisoner = prisoners.get(player.id);
    if (!prisoner) {
        toast(player, "You are not in jail.");
        return;
    }
    if (prisoner.stage !== "locked") {
        toast(player, "You are free to go: walk out the door at the end of the corridor.");
        return;
    }
    const cost = bail(player);
    if (!player.trySpendMoney(cost)) {
        toast(player, `Bail is ${formatMoney(cost)}; you have ${formatMoney(player.money)}.`, {tone: "bad"});
        return;
    }
    unlock(player, `Bail paid: ${formatMoney(cost)}. The warden is coming to open your cell.`);
});

menu.provide("jail", (player) => {
    const prisoner = prisoners.get(player.id);
    if (!prisoner) return null;
    if (prisoner.stage !== "locked") return {secondsLeft: 0, bail: 0, stage: prisoner.stage};
    return {secondsLeft: Math.ceil((prisoner.until - Date.now()) / 1000), bail: bail(player), stage: prisoner.stage};
});

// A player who left mid-sentence finishes it when they come back; the jail
// may still be going up after a mission change.
Events.on("playerSpawn", (player) => {
    if (prisoners.has(player.id) || life.isTransit(player)) return;
    const until = profiles.get(player)?.jailUntil ?? 0;
    if (until <= Date.now()) return;
    const {spawnGeneration} = player;
    const tryLockUp = () => {
        if (!online(player.id) || !player.alive || player.spawnGeneration !== spawnGeneration || prisoners.has(player.id)) return;
        if (lockUp(player, until)) return;
        if (prison.available()) setTimeout(tryLockUp, SPAWN_RETRY_MS);
    };
    setTimeout(tryLockUp, 0);
});
// A prisoner who dies comes back to the cell, or, once freed, to the city.
Events.on("playerRespawn", (player) => {
    const prisoner = prisoners.get(player.id);
    if (!prisoner) return;
    setTimeout(() => {
        if (!prisoners.has(player.id)) return;
        if (prisoner.stage === "locked") lockUp(player, prisoner.until);
        else walkOut(player);
    }, 0);
});
Events.on("playerDisconnect", (player) => {
    prisoners.delete(player.id);
    moving.delete(player.id);
});
Events.on("missionChange", () => {
    prisoners.clear();
    moving.clear();
    warden.forget();
    prison.forget();
    prison.build();
});
onStart(() => {
    prison.build();
    warden.start();
    timer = setInterval(tick, TICK_MS);
    guard = setInterval(watch, GUARD_MS);
});
onStop(() => {
    clearInterval(timer);
    clearInterval(guard);
    // Out of the jail before it goes; the sentence resumes on the next start.
    for (const player of World.getPlayers()) {
        if (!prisoners.has(player.id)) continue;
        player.setVirtualWorld(MAIN_WORLD);
        const spot = releasePoint(player);
        if (player.alive && spot) life.teleport(player, spot.position, spot.heading ?? 0, {...life.STARTER_KIT, health: player.health});
    }
    warden.stop();
    prison.teardown();
});

module.exports = {sentence, jailed: (player) => prisoners.has(player.id)};
