// Small helpers every Free Ride server module shares.
const maps = require("./maps.js");

const RESOURCE = "freeride";
const MAIN_WORLD = 0;

// Whether the loaded mission is one of the maps in lib/maps.js.
function inFreeRide() {
    return maps.current() !== null;
}

function distance(a, b) {
    return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

function flatDistance(a, b) {
    return Math.hypot(a.x - b.x, a.z - b.z);
}

function speedOf(vehicle) {
    const {x, y, z} = vehicle.velocity;
    return Math.hypot(x, y, z);
}

function plain(point) {
    return {x: point.x, y: point.y, z: point.z};
}

function formatMoney(amount) {
    return `$${Math.round(amount).toLocaleString("en-US")}`;
}

function formatDuration(ms) {
    const total = Math.max(0, Math.round(ms / 10));
    const minutes = Math.floor(total / 6000);
    const seconds = Math.floor(total / 100) % 60;
    const hundredths = total % 100;
    return `${minutes}:${String(seconds).padStart(2, "0")}.${String(hundredths).padStart(2, "0")}`;
}

function formatMinutes(ms) {
    const seconds = Math.max(0, Math.ceil(ms / 1000));
    return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
}

// The heading the server spawn APIs take, from a replicated quaternion.
function headingOf(rotation) {
    const {w, x, y, z} = rotation;
    const forwardX = 2 * (x * z + w * y);
    const forwardZ = 1 - 2 * (x * x + y * y);
    return Math.atan2(forwardX, forwardZ);
}

function headingFrom(from, to) {
    return Math.atan2(to.x - from.x, to.z - from.z);
}

// A living player in the shared city world of the current mission.
function playable(player) {
    return Boolean(player && player.spawned && player.alive && World.isReady() && inFreeRide() &&
        player.missionGeneration === World.getMissionGeneration());
}

function playerById(id) {
    return World.getPlayers().find((player) => player.id === id) ?? null;
}

function vehicleById(id) {
    return id === undefined || id === null ? null : World.getVehicles().find((vehicle) => vehicle.id === id) ?? null;
}

// "#4" picks a player ID; otherwise an exact nickname wins over a unique prefix.
function findPlayer(query) {
    const text = String(query ?? "").trim().toLowerCase();
    if (!text) return null;
    const players = World.getPlayers();
    if (/^#\d+$/.test(text)) return players.find((player) => player.id === Number(text.slice(1))) ?? null;
    const exact = players.find((player) => player.nickname.toLowerCase() === text);
    if (exact) return exact;
    const matches = players.filter((player) => player.nickname.toLowerCase().startsWith(text));
    return matches.length === 1 ? matches[0] : null;
}

function emit(player, name, data) {
    player.emit(name, JSON.stringify(data ?? {}));
}

function broadcast(name, data) {
    Events.emitAllClients(name, data ?? {});
}

// Client requests from a connection that is not a player (the simulation
// host runs the client scripts too) carry no sender and are dropped.
function onClient(name, handler) {
    Events.onClient(name, (player, data) => {
        if (player) handler(player, data);
    });
}

function onStart(handler) {
    Events.on("resourceStart", (name) => { if (name === RESOURCE) handler(); });
}

function onStop(handler) {
    Events.on("resourceStop", (name) => { if (name === RESOURCE) handler(); });
}

function pick(list) {
    return list[Math.floor(Math.random() * list.length)];
}

module.exports = {
    RESOURCE, MAIN_WORLD,
    inFreeRide, distance, flatDistance, speedOf, plain, formatMoney, formatDuration, formatMinutes,
    headingOf, headingFrom, playable, playerById, vehicleById, findPlayer, emit, broadcast,
    onClient, onStart, onStop, pick,
};
