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

// Activity cards on the right of the HUD; null removes the card.
function card(player, id, content) {
    emit(player, "fr:card", content ? {id, ...content} : {id, remove: true});
}

function compass(player, source, priority, target) {
    emit(player, "fr:compass", {source, priority, target: target ?? null});
}

function markers(player, group, items) {
    emit(player, "fr:markers", {group, items: items ?? []});
}

function markersAll(group, items) {
    broadcast("fr:markers", {group, items: items ?? []});
}

function announce(player, text, seconds = 2, sound = null) {
    emit(player, "fr:announce", {text, seconds, sound});
}

module.exports = {COLORS, toast, toastAll, name, text, feed, card, compass, markers, markersAll, announce};
