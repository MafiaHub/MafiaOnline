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

function card(player, id, content) {
    const key = `${player.id}:${id}`;
    const payload = JSON.stringify(content ? {id, ...content} : {id, remove: true});
    if (lastCards.get(key) === payload) return;
    lastCards.set(key, payload);
    player.emit("fr:card", payload);
}

function forgetCards(player) {
    for (const key of [...lastCards.keys()]) if (key.startsWith(`${player.id}:`)) lastCards.delete(key);
}

// A freshly loaded HUD page shows no cards, whatever was sent before.
Events.onClient("fr:hud:ready", (player) => { if (player) forgetCards(player); });
Events.on("playerDisconnect", forgetCards);
Events.on("missionChange", () => lastCards.clear());

function compass(player, source, priority, target) {
    emit(player, "fr:compass", {source, priority, target: target ?? null});
}

function markers(player, group, items) {
    emit(player, "fr:markers", {group, items: items ?? []});
}

function markersAll(group, items) {
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
