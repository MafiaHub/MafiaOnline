// Who decides where a player comes back, what they carry, and what they are
// busy with. Death never respawns anyone by itself; this module does it after
// a short delay, asking the active override (a duel) or the default provider
// (the district the player chose) for the place.
const profiles = require("./profiles.js");

const RESPAWN_DELAY_MS = 5000;
// A bat; guns are bought at Yellow Pete's.
const STARTER_KIT = Object.freeze({weapons: [{weaponId: 4}], selected: 4});

const activities = new Map();
const overrides = new Map();
const transit = new Map();
const pending = new Set();
let provider = () => null;
let hospital = () => null;

// One exclusive activity per player: a race, a duel, a delivery run.
function begin(player, kind, label) {
    const current = activities.get(player.id);
    if (current && current.kind !== kind) return false;
    activities.set(player.id, {kind, label});
    profiles.setPublic(player, "fr.activity", label);
    return true;
}

function end(player, kind) {
    const current = activities.get(player.id);
    if (!current || (kind && current.kind !== kind)) return;
    activities.delete(player.id);
    profiles.setPublic(player, "fr.activity", null);
}

function activity(player) {
    return activities.get(player.id) ?? null;
}

function busyMessage(player) {
    const current = activities.get(player.id);
    return current ? `Finish your ${current.label.toLowerCase()} first.` : null;
}

function setRespawnProvider(handler) {
    provider = handler;
}

function setHospital(handler) {
    hospital = handler;
}

// override: {spawn?(player) -> {position, heading} | null, kit?, delayMs?}
function setOverride(player, override) {
    overrides.set(player.id, override);
}

function clearOverride(player) {
    overrides.delete(player.id);
}

// setInventory takes a selected weapon only when one is given; an explicit
// undefined is rejected.
function equip(player, weapons, selected) {
    return selected ? player.setInventory(weapons, selected) : player.setInventory(weapons);
}

function giveKit(player, kit = STARTER_KIT) {
    if (!equip(player, kit.weapons, kit.selected)) {
        console.error(`[freeride] Could not equip ${player.nickname}`);
    }
}

function loadoutOf(player) {
    const inventory = player.getInventory();
    return {
        weapons: (inventory?.weapons ?? []).map(({weaponId, loaded, reserve}) => ({weaponId, loaded, reserve})),
        selected: inventory?.selected ?? 0,
        health: player.health,
    };
}

// A move built on spawn that keeps a loadout: by default the current life's
// weapons and health. The new life is marked so no starting kit replaces it.
// Spawn events run inside player.spawn(), so the life is marked pending
// before the call and the spawn handlers already see it as a transit.
function teleport(player, position, heading = 0, loadout = loadoutOf(player)) {
    pending.add(player.id);
    const spawned = player.spawn(position, heading);
    pending.delete(player.id);
    if (!spawned) return false;
    transit.set(player.id, player.spawnGeneration);
    equip(player, loadout.weapons, loadout.selected);
    if (loadout.health < 100) player.setHealth(Math.max(1, loadout.health));
    return true;
}

// Where a player comes back: the active override, the map's hospital, the
// district they chose, or the native spawn hint.
function spotFor(player) {
    const suggested = player.getSuggestedSpawn();
    return overrides.get(player.id)?.spawn?.(player) ?? hospital() ?? provider(player) ??
        (suggested ? {position: suggested, heading: 0} : null);
}

// A fresh life at the player's usual place, as after an arrest.
function restart(player) {
    const spot = spotFor(player);
    return Boolean(spot) && player.spawn(spot.position, spot.heading ?? 0);
}

function isTransit(player) {
    return pending.has(player.id) || transit.get(player.id) === player.spawnGeneration;
}

function spawnKit(player) {
    if (isTransit(player)) return;
    giveKit(player, overrides.get(player.id)?.kit ?? STARTER_KIT);
}

Events.on("playerSpawn", spawnKit);
Events.on("playerRespawn", spawnKit);

Events.on("playerDeath", (player, _killer, {missionGeneration, spawnGeneration}) => {
    const delay = overrides.get(player.id)?.delayMs ?? RESPAWN_DELAY_MS;
    // Respawn fails while any client is still loading the mission; keep
    // trying for as long as this life is the one that died.
    const tryRespawn = () => {
        if (player.alive || player.spawnGeneration !== spawnGeneration ||
            World.getMissionGeneration() !== missionGeneration ||
            !World.getPlayers().some((other) => other.id === player.id)) return;
        const spot = World.isReady() ? spotFor(player) : null;
        if (spot && player.respawn(spot.position, spot.heading ?? 0)) return;
        setTimeout(tryRespawn, 1000);
    };
    setTimeout(tryRespawn, delay);
});

Events.on("playerDisconnect", (player) => {
    activities.delete(player.id);
    overrides.delete(player.id);
    transit.delete(player.id);
    pending.delete(player.id);
});

Events.on("missionChange", () => {
    overrides.clear();
    transit.clear();
});

module.exports = {begin, end, activity, busyMessage, setRespawnProvider, setHospital, setOverride, clearOverride,
    equip, giveKit, loadoutOf, teleport, restart, spotFor, isTransit, STARTER_KIT, RESPAWN_DELAY_MS};
