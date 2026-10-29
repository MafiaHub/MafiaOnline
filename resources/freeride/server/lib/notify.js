// Presentation requests for client/hud.js. The server decides what happened;
// clients only render toasts, the feed, activity cards, markers and compass.
const {emit, broadcast} = require("./util.js");

const COLORS = Object.freeze({
    gold: "#e2b866", red: "#d0574a", green: "#8fc27a", blue: "#8fb4d8", muted: "#b9ae98", white: "#f3e9d6",
});

// tone: "info" | "good" | "bad" | "gold"
function toast(player, text, {title = "", tone = "info", seconds = 5} = {}) {
    emit(player, "fr:toast", {title, text, tone, seconds});
}

function toastAll(text, options = {}) {
    broadcast("fr:toast", {title: options.title ?? "", text, tone: options.tone ?? "info", seconds: options.seconds ?? 5});
}

// A feed line is a list of segments, each with its own colour.
function name(player, color = COLORS.white) {
    return {t: player?.nickname ?? "Someone", c: color};
}

function text(value, color = COLORS.muted) {
    return {t: String(value), c: color};
}

function feed(kind, parts) {
    broadcast("fr:feed", {kind, parts});
}

// Activity cards on the right of the HUD; null removes the card. Many are
// refreshed on a timer, so one that has not changed is not sent again.
const lastCards = new Map();
// Jobs refresh the compass and their markers on a timer too; the same goes
// for them (a marker list resent also reloads its models). The client
// forgets them with its HUD page or the mission, and so does this.
const lastCompass = new Map();
const lastMarkers = new Map();

function card(player, id, content) {
    const key = `${player.id}:${id}`;
    const payload = JSON.stringify(content ? {id, ...content} : {id, remove: true});
    if (lastCards.get(key) === payload) return;
    lastCards.set(key, payload);
    player.emit("fr:card", payload);
}

function forget(sent, player) {
    for (const key of [...sent.keys()]) if (key.startsWith(`${player.id}:`)) sent.delete(key);
}

function forgetCards(player) {
    forget(lastCards, player);
}

function forgetAll(player) {
    forget(lastCards, player);
    forget(lastCompass, player);
    forget(lastMarkers, player);
}

// A freshly loaded HUD page shows no cards, whatever was sent before; a
// mission load drops the markers, a spawn the compass.
Events.onClient("fr:hud:ready", (player) => { if (player) forgetAll(player); });
Events.on("playerMissionReady", (player) => { forget(lastMarkers, player); forget(lastCompass, player); });
Events.on("playerSpawn", (player) => forget(lastCompass, player));
Events.on("playerDisconnect", forgetAll);
Events.on("missionChange", () => {
    lastCards.clear();
    lastCompass.clear();
    lastMarkers.clear();
});

function sendOnce(sent, player, key, name, data) {
    const payload = JSON.stringify(data);
    if (sent.get(key) === payload) return;
    sent.set(key, payload);
    player.emit(name, payload);
}

function compass(player, source, priority, target) {
    sendOnce(lastCompass, player, `${player.id}:${source}`, "fr:compass", {source, priority, target: target ?? null});
}

function markers(player, group, items) {
    sendOnce(lastMarkers, player, `${player.id}:${group}`, "fr:markers", {group, items: items ?? []});
}

function markersAll(group, items) {
    for (const key of [...lastMarkers.keys()]) if (key.endsWith(`:${group}`)) lastMarkers.delete(key);
    broadcast("fr:markers", {group, items: items ?? []});
}

// Markers whose city-map blips only a crew sees: everyone gets the world
// markers, the crew also gets the blips. `sent` (playerId -> payload) keeps
// each player from being sent the same list twice; call it every tick, so
// duty changes show within one.
function crewMarkers(group, sent, items, isCrew) {
    const crewItems = items;
    const publicItems = items.map((item) => ({...item, map: false}));
    const crewPayload = JSON.stringify(crewItems);
    const publicPayload = JSON.stringify(publicItems);
    for (const player of World.getPlayers()) {
        const crew = isCrew(player);
        const payload = crew ? crewPayload : publicPayload;
        if (sent.get(player.id) === payload) continue;
        sent.set(player.id, payload);
        markers(player, group, crew ? crewItems : publicItems);
    }
}

function announce(player, text, seconds = 2, sound = null) {
    emit(player, "fr:announce", {text, seconds, sound});
}

module.exports = {COLORS, toast, toastAll, name, text, feed, card, compass, markers, markersAll, crewMarkers, announce};
