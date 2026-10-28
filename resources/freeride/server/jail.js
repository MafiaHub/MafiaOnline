// Jail. An arrest sends the player to a cell for a sentence that grows with
// the wanted level they were caught at; the police keep their weapons. On Lost
// Heaven Extended the cell is the motel lock-up (lib/maps.js); elsewhere the
// player is held where they would respawn. Each prisoner sits in a private
// virtual world, is walked back into the cell if they stray, and can pay bail
// to leave early. The sentence survives a reconnect.
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, card, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, formatMoney, formatMinutes, onStart, onStop} = require("./lib/util.js");

const BASE_SENTENCE_MS = 45_000;
const PER_LEVEL_MS = 30_000;
const BAIL_PER_SECOND = 5;
const CELL_REACH = 4;
const FIRST_JAIL_WORLD = 0x50000000;
const TICK_MS = 1000;

const prisoners = new Map();
let timer = null;

function cellFor(player) {
    const jail = maps.current()?.jail;
    if (jail) return {cell: jail.cell, release: jail.release, name: jail.name};
    const spot = life.spotFor(player);
    return spot ? {cell: spot, release: spot, name: "a holding cell"} : null;
}

function lockUp(player, until) {
    const place = cellFor(player);
    if (!place) return false;
    if (player.getVehicle()) player.removeFromVehicle();
    life.end(player);
    life.begin(player, "jail", "In jail");
    player.setVirtualWorld(FIRST_JAIL_WORLD + player.id);
    if (!life.teleport(player, place.cell.position, place.cell.heading ?? 0, {weapons: [], selected: 0, health: player.health})) {
        player.setVirtualWorld(MAIN_WORLD);
        life.end(player, "jail");
        return false;
    }
    prisoners.set(player.id, {until, place});
    profiles.setField(player, "jailUntil", until);
    return true;
}

function release(player, message) {
    const prisoner = prisoners.get(player.id);
    if (!prisoner) return;
    prisoners.delete(player.id);
    profiles.setField(player, "jailUntil", 0);
    card(player, "jail", null);
    life.end(player, "jail");
    player.setVirtualWorld(MAIN_WORLD);
    const {position, heading} = prisoner.place.release;
    life.teleport(player, position, heading ?? 0, {...life.STARTER_KIT, health: 100});
    if (message) toast(player, message, {title: "Released", tone: "good"});
}

// Called by police.js for an arrest (not a fine).
function sentence(player, level) {
    const ms = BASE_SENTENCE_MS + PER_LEVEL_MS * Math.max(0, level - 1);
    if (!lockUp(player, Date.now() + ms)) return false;
    const where = prisoners.get(player.id).place.name;
    toast(player, `${formatMinutes(ms)} in ${where}. The police kept your guns. /bail pays your way out.`, {title: "Arrested", tone: "bad", seconds: 8});
    feed("police", [name(player), text(` was locked up in ${where}`)]);
    return true;
}

function bail(player) {
    return Math.ceil((prisoners.get(player.id).until - Date.now()) / 1000) * BAIL_PER_SECOND;
}

function tick() {
    const now = Date.now();
    for (const [id, prisoner] of prisoners) {
        const player = World.getPlayers().find((other) => other.id === id);
        if (!player) {
            prisoners.delete(id);
            continue;
        }
        if (now >= prisoner.until) {
            release(player, "You served your time. Keep your nose clean.");
            continue;
        }
        if (!player.alive) continue;
        if (distance(player.position, prisoner.place.cell.position) > CELL_REACH) {
            life.teleport(player, prisoner.place.cell.position, prisoner.place.cell.heading ?? 0);
            toast(player, "The guard walks you back to your cell.", {tone: "bad", seconds: 3});
        }
        card(player, "jail", {title: "In jail", subtitle: prisoner.place.name, rows: [["Released in", formatMinutes(prisoner.until - now)], ["Bail", formatMoney(bail(player))]],
            tone: "bad"});
    }
}

commands.register({name: "bail", usage: "/bail", help: "Pay your way out of jail.", category: "Combat"}, (player) => {
    if (!prisoners.has(player.id)) {
        toast(player, "You are not in jail.");
        return;
    }
    const cost = bail(player);
    if (!player.trySpendMoney(cost)) {
        toast(player, `Bail is ${formatMoney(cost)}; you have ${formatMoney(player.money)}.`, {tone: "bad"});
        return;
    }
    release(player, `Bail paid: ${formatMoney(cost)}.`);
});

menu.provide("jail", (player) => prisoners.has(player.id) ? {secondsLeft: Math.ceil((prisoners.get(player.id).until - Date.now()) / 1000), bail: bail(player)} : null);

// A player who left mid-sentence finishes it when they come back.
Events.on("playerSpawn", (player) => {
    if (prisoners.has(player.id) || life.isTransit(player)) return;
    const until = profiles.get(player)?.jailUntil ?? 0;
    if (until > Date.now()) setTimeout(() => {
        if (player.alive && !prisoners.has(player.id)) lockUp(player, until);
    }, 0);
});
// A prisoner who dies comes back to the cell.
Events.on("playerRespawn", (player) => {
    const prisoner = prisoners.get(player.id);
    if (prisoner) setTimeout(() => {
        if (prisoners.has(player.id)) lockUp(player, prisoner.until);
    }, 0);
});
Events.on("playerDisconnect", (player) => prisoners.delete(player.id));
Events.on("missionChange", () => prisoners.clear());
onStart(() => { timer = setInterval(tick, TICK_MS); });
onStop(() => {
    clearInterval(timer);
    for (const player of World.getPlayers()) if (prisoners.has(player.id)) player.setVirtualWorld(MAIN_WORLD);
});

module.exports = {sentence, jailed: (player) => prisoners.has(player.id)};
