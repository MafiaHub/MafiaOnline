// The export list. Lucas Bertone wants three particular models, and only
// cars taken from the city's traffic count; a car from the motor pool is
// worthless to him. The list comes from the models actually driving around
// this server and changes once all three are in, or after an hour.
const roads = require("./lib/roads.js");
const profiles = require("./lib/profiles.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, toastAll, card, compass, markers, feed, name, text, COLORS} = require("./lib/notify.js");
const {flatDistance, playable, speedOf, formatMoney, formatMinutes, onStart, onStop} = require("./lib/util.js");

// A quiet crossing in the south-west, far from every landmark and spawn.
const YARD = roads.crossings[28];
const YARD_RADIUS = 12;
const LIST_SIZE = 3;
const LIST_LIFETIME_MS = 60 * 60_000;
const PAY = 400;
const COMPLETE_BONUS = 600;
const XP = 80;
const TICK_MS = 1000;
// Used until enough traffic has been seen to choose from it.
const FALLBACK = ["fordtto00.i3d", "forvtud00.i3d", "chev00.i3d", "hudfor00.i3d", "pontfor00.i3d", "buikfor00.i3d"];

const seen = new Set();
let list = null;
const hinted = new Map();
let timer = null;

function newList() {
    const pool = [...(seen.size >= LIST_SIZE * 2 ? seen : new Set([...seen, ...FALLBACK]))];
    const models = [];
    while (models.length < LIST_SIZE && pool.length) models.push(pool.splice(Math.floor(Math.random() * pool.length), 1)[0]);
    list = {models: models.map((model) => ({model, deliveredBy: null})), expiresAt: Date.now() + LIST_LIFETIME_MS};
    hinted.clear();
}

function wanted(vehicle) {
    return vehicle.traffic && vehicle.trafficKind === "civil" &&
        list.models.find((entry) => entry.model === vehicle.model.toLowerCase() && !entry.deliveredBy) || null;
}

function clearHint(player) {
    if (!hinted.delete(player.id)) return;
    compass(player, "export", 50, null);
    markers(player, "export", []);
    card(player, "export", null);
}

function deliver(player, vehicle, entry) {
    entry.deliveredBy = player.nickname;
    clearHint(player);
    player.giveMoney(PAY);
    profiles.bump(player, "exports");
    profiles.addXp(player, XP, "Car for Lucas");
    feed("export", [name(player, COLORS.gold), text(" delivered a "), text(vehicle.model.replace(/00\.i3d$/i, ""), COLORS.white), text(" to the export yard")]);
    toast(player, `Lucas looks it over and nods. ${formatMoney(PAY)}.`, {title: "Export", tone: "good"});
    player.removeFromVehicle();
    setTimeout(() => vehicle.destroy(), 1500);
    if (list.models.every((model) => model.deliveredBy)) {
        player.giveMoney(COMPLETE_BONUS);
        toastAll(`${player.nickname} closed Lucas's list. A new one is up.`, {title: "Export", tone: "gold"});
        newList();
    }
}

function tick() {
    for (const vehicle of World.getVehicles()) if (vehicle.traffic && vehicle.trafficKind === "civil") seen.add(vehicle.model.toLowerCase());
    if (!list || Date.now() > list.expiresAt) newList();
    for (const player of World.getPlayers()) {
        const vehicle = playable(player) && player.getSeat() === 0 ? player.getVehicle() : null;
        const entry = vehicle && wanted(vehicle);
        if (!entry) {
            clearHint(player);
            continue;
        }
        if (flatDistance(vehicle.position, YARD) <= YARD_RADIUS && speedOf(vehicle) < 3) {
            deliver(player, vehicle, entry);
            continue;
        }
        if (hinted.get(player.id) !== vehicle.id) {
            hinted.set(player.id, vehicle.id);
            compass(player, "export", 50, {position: YARD});
            markers(player, "export", [{id: "yard", position: YARD, label: "EXPORT YARD", color: COLORS.gold, beacon: true, primary: true}]);
            toast(player, "Lucas wants this one. Bring it to the export yard in one piece.", {title: "Export list", tone: "gold"});
        }
        card(player, "export", {title: "Export", subtitle: "Deliver to the export yard",
            rows: [["Distance", `${Math.round(flatDistance(vehicle.position, YARD))} m`], ["Pay", formatMoney(PAY)]], tone: "gold"});
    }
}

commands.register({name: "export", aliases: ["lucas"], usage: "/export", help: "Which traffic cars Lucas wants this hour.", category: "City"}, (player) => {
    if (!list) newList();
    const open = list.models.filter((entry) => !entry.deliveredBy).map((entry) => entry.model.replace(/00\.i3d$/i, ""));
    toast(player, `Lucas wants: ${open.join(", ")}. Take them from traffic, not the motor pool. New list in ${formatMinutes(list.expiresAt - Date.now())}.`,
        {title: "Export list", seconds: 9});
});

menu.provide("export", () => list && {models: list.models, minutesLeft: Math.ceil((list.expiresAt - Date.now()) / 60_000), pay: PAY});

onStart(() => {
    newList();
    timer = setInterval(tick, TICK_MS);
});
onStop(() => clearInterval(timer));
Events.on("playerDisconnect", (player) => hinted.delete(player.id));
Events.on("missionChange", () => hinted.clear());
