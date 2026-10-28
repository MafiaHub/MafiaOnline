// Owning a car. Cars are bought at the map's car lot and kept in the
// player's garage between sessions, damage, fuel and all (Vehicle.saveState).
// One owned car is on the street at a time: /garage has it delivered to the
// nearest road for a fee, /park puts it away, /lock keeps others out of the
// driver's seat, and a wrecked car can be recovered for part of its price.
// Free car spawning is for admins (/car in main.js).
const maps = require("./lib/maps.js");
const roads = require("./lib/roads.js");
const cars = require("./lib/cars.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, compass, markers, feed, name, text, COLORS} = require("./lib/notify.js");
const util = require("./lib/util.js");

const {MAIN_WORLD, distance, flatDistance, playable, formatMoney, emit, vehicleById, onStop} = util;
const LOT_REACH = 14;
const MAX_OWNED = 3;
const DELIVERY_FEE = 75;
const DELIVERY_COOLDOWN_MS = 60_000;
const PARK_REACH = 10;
const RECOVER_FRACTION = 0.3;
const SELL_FRACTION = 0.5;
const ROADSIDE = 3;
const BLOCKED = new Set(["race", "duel", "jail"]);

// playerId -> {vehicleId, slot, locked}
const out = new Map();
const lastDelivery = new Map();

// Each map's lot is on the road beside one of its landmarks.
const LOTS = {freeride: "pump", freeride_extended: "bertone", freekrajina: "lowlands"};

function lot() {
    const map = maps.current();
    const key = map && LOTS[map.id];
    if (!key) return null;
    const graph = roads.forMap(map);
    return {name: `the car lot at ${maps.landmark(key).name}`, position: graph.crossings[graph.nearestCrossing(maps.landmark(key).position)]};
}

function garage(player) {
    return profiles.get(player)?.cars ?? [];
}

function saveGarage(player, list) {
    profiles.setField(player, "cars", list);
}

function nearLot(player) {
    const place = lot();
    return Boolean(place && flatDistance(player.position, place.position) <= LOT_REACH);
}

function busy(player) {
    const current = life.activity(player);
    return current && BLOCKED.has(current.kind) ? life.busyMessage(player) : null;
}

// Puts the car that is out back into the garage with its condition.
function store(player) {
    const entry = out.get(player.id);
    if (!entry) return true;
    const vehicle = vehicleById(entry.vehicleId);
    if (vehicle && vehicle.getOccupants().some((occupant) => occupant !== null && occupant.id !== player.id)) return false;
    const list = garage(player).slice();
    if (vehicle && list[entry.slot]) {
        const state = vehicle.saveState();
        if (state) list[entry.slot] = {...list[entry.slot], state};
        saveGarage(player, list);
    }
    out.delete(player.id);
    if (vehicle) {
        if (player.getVehicle()?.id === vehicle.id) player.removeFromVehicle();
        vehicle.destroy();
    }
    return true;
}

function deliver(player, slot, {fee}) {
    const list = garage(player);
    const owned = list[slot];
    if (!owned || owned.totaled) return false;
    if (!store(player)) {
        toast(player, "Someone is still sitting in your other car.", {tone: "bad"});
        return false;
    }
    const graph = roads.current();
    const spot = graph.nearestRoadPoint(player.position);
    if (!spot || spot.gap > 120) {
        toast(player, "No road nearby to bring the car to.", {tone: "bad"});
        return false;
    }
    if (fee && !player.trySpendMoney(fee)) {
        toast(player, `Delivery costs ${formatMoney(fee)}.`, {tone: "bad"});
        return false;
    }
    const {x, y, z} = spot.position;
    const heading = Math.atan2(spot.direction.x, spot.direction.z);
    const position = {x: x + spot.direction.z * ROADSIDE, y: y + 0.6, z: z - spot.direction.x * ROADSIDE};
    const vehicle = Vehicle.spawn(owned.model, position, heading, player);
    if (!vehicle) {
        if (fee) player.giveMoney(fee);
        toast(player, "The car could not be brought out. Try again.", {tone: "bad"});
        return false;
    }
    // A saved condition is applied before the first native report; a new
    // car comes with a full tank.
    if (owned.state) vehicle.restoreState(owned.state);
    vehicle.state.set("fr.fullTank", true);
    vehicle.state.set("fr.owner", player.nickname);
    const crew = player.state.get("fr.crew");
    vehicle.setRadarMarker(true, crew?.color ? parseInt(crew.color.slice(1), 16) : 0xe8c57d);
    out.set(player.id, {vehicleId: vehicle.id, slot, locked: true});
    vehicle.state.set("fr.locked", true);
    lastDelivery.set(player.id, Date.now());
    compass(player, "garage", 20, {vehicleId: vehicle.id});
    setTimeout(() => compass(player, "garage", 20, null), 30_000);
    return true;
}

function openPicker(player, mode) {
    const list = garage(player);
    const offers = mode === "dealer"
        ? cars.forSale().map((entry) => ({id: entry.id, model: entry.model, price: entry.price,
            owned: list.some((owned) => owned.model.toLowerCase() === entry.model.toLowerCase())}))
        : list.map((owned, slot) => ({id: 1000 + slot, model: owned.model, totaled: Boolean(owned.totaled),
            recover: owned.totaled ? Math.round((cars.byModel(owned.model)?.price ?? 1000) * RECOVER_FRACTION) : 0}));
    emit(player, "fr:car:open", {generation: World.getMissionGeneration(), mode, offers, fee: DELIVERY_FEE, room: MAX_OWNED - list.length});
}

function buy(player, entry) {
    const list = garage(player);
    if (!nearLot(player)) return {ok: false, message: "Come to the car lot to buy."};
    if (list.length >= MAX_OWNED) return {ok: false, message: `Your garage holds ${MAX_OWNED} cars. Sell one with /sellcar first.`};
    if (entry.price === null) return {ok: false, message: "That car is not for sale."};
    if (!player.trySpendMoney(entry.price)) return {ok: false, message: `That car costs ${formatMoney(entry.price)}; you have ${formatMoney(player.money)}.`};
    saveGarage(player, [...list, {model: entry.model, state: null, totaled: false, bought: Date.now()}]);
    profiles.bump(player, "carsBought");
    feed("car", [name(player, COLORS.gold), text(" drove a new car off the lot")]);
    deliver(player, list.length, {fee: 0});
    return {ok: true, message: `Yours for ${formatMoney(entry.price)}. It is parked on the road by the lot; /lock and /park look after it.`};
}

function fromGarage(player, slot) {
    const owned = garage(player)[slot];
    if (!owned) return {ok: false, message: "That car is not in your garage."};
    const wait = (lastDelivery.get(player.id) ?? 0) + DELIVERY_COOLDOWN_MS - Date.now();
    if (wait > 0) return {ok: false, message: `The driver is on his way back; try again in ${Math.ceil(wait / 1000)} s.`};
    if (owned.totaled) {
        const cost = Math.round((cars.byModel(owned.model)?.price ?? 1000) * RECOVER_FRACTION);
        if (!player.trySpendMoney(cost)) return {ok: false, message: `Recovering the wreck costs ${formatMoney(cost)}.`};
        const list = garage(player).slice();
        list[slot] = {...owned, state: null, totaled: false};
        saveGarage(player, list);
        toast(player, `The wreck was hauled off and rebuilt for ${formatMoney(cost)}.`, {tone: "good"});
    }
    const fee = nearLot(player) ? 0 : DELIVERY_FEE;
    return deliver(player, slot, {fee}) ? {ok: true, message: fee ? `Delivered to the nearest road for ${formatMoney(fee)}.` : "Your car is out front."}
        : {ok: false, message: "The car could not be brought out."};
}

// A choice from the car lot or the garage; main.js routes fr:car:choose here.
function choose(player, request) {
    const reply = (result) => {
        emit(player, "fr:car:result", {id: request.id, ...result});
        toast(player, result.message, {tone: result.ok ? "good" : "bad"});
    };
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) return reply({ok: false, message: "Step out onto the street first."});
    const problem = busy(player);
    if (problem) return reply({ok: false, message: problem});
    if (request.mode === "dealer") {
        const entry = cars.byId(request.id);
        return reply(entry ? buy(player, entry) : {ok: false, message: "That car is not for sale."});
    }
    reply(fromGarage(player, request.id - 1000));
}

// A locked car keeps everyone but its owner and their crew out of the driver's seat.
Events.on("vehiclePlayerEntered", (vehicle, player, {seat}) => {
    if (seat !== 0) return;
    for (const [ownerId, entry] of out) {
        if (entry.vehicleId !== vehicle.id || ownerId === player.id || !entry.locked) continue;
        const owner = util.playerById(ownerId);
        const crew = owner?.state.get("fr.crew")?.tag;
        if (crew && crew === player.state.get("fr.crew")?.tag) return;
        player.removeFromVehicle();
        toast(player, "It's locked.", {tone: "bad", seconds: 2});
        if (owner) toast(owner, `${player.nickname} tried the door of your car.`, {seconds: 4});
        return;
    }
});

Events.on("vehicleTerminal", (vehicle, state) => {
    for (const [ownerId, entry] of out) {
        if (entry.vehicleId !== vehicle.id) continue;
        const player = util.playerById(ownerId);
        out.delete(ownerId);
        if (!player) return;
        const list = garage(player).slice();
        if (list[entry.slot]) list[entry.slot] = {...list[entry.slot], state: null, totaled: true};
        saveGarage(player, list);
        toast(player, state === 1 ? "Your car is a burnt-out wreck. /garage can have it rebuilt." : "Your car is lost. /garage can have it recovered.", {tone: "bad"});
        return;
    }
});

Events.on("vehicleDestroy", (vehicle) => {
    for (const [ownerId, entry] of out) if (entry.vehicleId === vehicle.id) out.delete(ownerId);
});

commands.register({name: "dealer", aliases: ["buycar", "carlot"], usage: "/dealer", help: "Buy a car at the car lot.", category: "Cars"}, (player) => {
    const place = lot();
    if (!place) {
        toast(player, "There is no car lot on this map.", {tone: "bad"});
        return;
    }
    if (!nearLot(player)) {
        compass(player, "garage", 20, {position: place.position});
        markers(player, "garage", [{id: "lot", position: place.position, label: "CAR LOT", color: COLORS.gold, beacon: true, primary: true}]);
        toast(player, `Cars are sold at ${place.name}. Your compass points there.`);
        return;
    }
    markers(player, "garage", []);
    compass(player, "garage", 20, null);
    openPicker(player, "dealer");
});

commands.register({name: "garage", aliases: ["mycar", "cars"], usage: "/garage", help: "Have one of your cars brought to you.", category: "Cars"}, (player) => {
    if (!garage(player).length) {
        toast(player, "Your garage is empty. Buy a car with /dealer, or take one from the street.");
        return;
    }
    openPicker(player, "garage");
});

commands.register({name: "park", usage: "/park", help: "Put your car away in your garage.", category: "Cars"}, (player) => {
    const entry = out.get(player.id);
    const vehicle = entry && vehicleById(entry.vehicleId);
    if (!vehicle) {
        toast(player, "None of your cars is out.");
        return;
    }
    if (distance(player.position, vehicle.position) > PARK_REACH) {
        toast(player, "Stand by your car to park it.", {tone: "bad"});
        return;
    }
    toast(player, store(player) ? "Parked. It will be in your garage next time." : "Someone is still sitting in it.", {tone: "good"});
});

commands.register({name: "lock", aliases: ["unlock"], usage: "/lock", help: "Lock or unlock your car.", category: "Cars"}, (player) => {
    const entry = out.get(player.id);
    const vehicle = entry && vehicleById(entry.vehicleId);
    if (!vehicle) {
        toast(player, "None of your cars is out.");
        return;
    }
    entry.locked = !entry.locked;
    vehicle.state.set("fr.locked", entry.locked);
    toast(player, entry.locked ? "Locked." : "Unlocked.", {seconds: 2});
});

commands.register({name: "sellcar", usage: "/sellcar <number>", help: "Sell a car from your garage at the lot for half its price.", category: "Cars"}, (player, args) => {
    const list = garage(player);
    const slot = Number(args[0]) - 1;
    if (!list[slot]) {
        player.sendMessage(`Your garage: ${list.map((owned, index) => `${index + 1}. ${owned.model.replace(/00\.i3d$/i, "")}${owned.totaled ? " (wrecked)" : ""}`).join(", ") || "empty"}. /sellcar <number>`, 0xd9cdb4);
        return;
    }
    if (!nearLot(player)) {
        toast(player, "Sell cars at the car lot.", {tone: "bad"});
        return;
    }
    if (out.get(player.id)?.slot === slot && !store(player)) {
        toast(player, "Someone is sitting in that car.", {tone: "bad"});
        return;
    }
    const entry = out.get(player.id);
    if (entry && entry.slot > slot) entry.slot--;
    const price = list[slot].totaled ? 0 : Math.round((cars.byModel(list[slot].model)?.price ?? 0) * SELL_FRACTION);
    saveGarage(player, list.filter((_, index) => index !== slot));
    if (price) player.giveMoney(price);
    toast(player, price ? `Sold for ${formatMoney(price)}.` : "The wreck went for scrap.", {tone: "good"});
});

menu.provide("garage", (player) => ({lot: lot()?.name ?? null, near: nearLot(player), fee: DELIVERY_FEE, max: MAX_OWNED,
    cars: garage(player).map((owned, slot) => ({slot: slot + 1, model: owned.model, totaled: Boolean(owned.totaled), out: out.get(player.id)?.slot === slot}))}));

Events.on("playerDisconnect", (player) => {
    store(player);
    out.delete(player.id);
    lastDelivery.delete(player.id);
});
Events.on("missionChange", () => out.clear());
onStop(() => {
    for (const player of World.getPlayers()) store(player);
});

module.exports = {choose};
