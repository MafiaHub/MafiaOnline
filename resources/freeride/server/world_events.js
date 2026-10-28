// City-wide events every player sees at once: a supply drop of weapons that
// goes to whoever reaches it first, and a hot car that has to be delivered
// across town with the police on its tail. Places come from the stock road
// graph, so both always appear on a street.
const roads = require("./lib/roads.js");
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const commands = require("./lib/commands.js");
const menu = require("./lib/menu.js");
const notify = require("./lib/notify.js");
const util = require("./lib/util.js");

const {toast, toastAll, card, compass, markersAll, feed, name, text, COLORS} = notify;
const {distance, flatDistance, inFreeRide, playable, formatMoney, formatMinutes, speedOf, vehicleById, pick} = util;

const FIRST_EVENT_MS = 3 * 60_000;
const EVENT_EVERY_MS = 9 * 60_000;
const TICK_MS = 500;
const CARD_EVERY_MS = 2000;
const DROP_LIFETIME_MS = 6 * 60_000;
const DROP_REACH = 3.5;
const DROP_PRIZE = 250;
const DROP_XP = 100;
const HOT_CAR_LIFETIME_MS = 10 * 60_000;
const HOT_CAR_PRIZE = 600;
const HOT_CAR_PASSENGER_PRIZE = 150;
const HOT_CAR_XP = 150;
const DELIVERY_RADIUS = 14;

const HOT_CARS = [
    {model: "bugatti00.i3d", name: "Bugatti Type 57"}, {model: "duesenberg00.i3d", name: "Duesenberg"},
    {model: "alfa8c00.i3d", name: "Alfa Romeo 8C"}, {model: "speedster00.i3d", name: "Speedster"},
    {model: "merced500k00.i3d", name: "Mercedes 500K"},
];
const DROP_LOOT = [
    {weaponId: 10, loaded: 50, reserve: 100}, {weaponId: 11, loaded: 8, reserve: 24},
    {weaponId: 15, loaded: 3, reserve: 0}, {weaponId: 6, loaded: 6, reserve: 30},
];

let current = null;
let nextEventAt = 0;
let tickTimer = null;
let lastCardAt = 0;

function streetPlayers() {
    return World.getPlayers().filter((player) => playable(player) && player.virtualWorld === util.MAIN_WORLD);
}

function spotNear(minimum, maximum) {
    const anchor = pick(streetPlayers());
    if (!anchor) return null;
    const graph = roads.current();
    const index = graph.crossingAround(anchor.position, minimum, maximum) ?? graph.crossingAround(anchor.position, 0, maximum * 2);
    return index === null ? graph.roadPoint() : graph.roadside(index);
}

function clearPresentation() {
    markersAll("event", []);
    for (const player of World.getPlayers()) {
        compass(player, "event", 40, null);
        card(player, "event", null);
    }
}

function endEvent(message, tone = "info") {
    if (!current) return;
    const ended = current;
    current = null;
    nextEventAt = Date.now() + EVENT_EVERY_MS;
    clearPresentation();
    if (ended.kind === "hotcar") {
        const vehicle = vehicleById(ended.vehicleId);
        if (vehicle) {
            vehicle.setRadarMarker(false);
            vehicle.state.remove("fr.hot");
            // An abandoned contract car leaves once nobody sits in it.
            setTimeout(() => {
                const car = vehicleById(ended.vehicleId);
                if (car && car.getOccupants().every((occupant) => occupant === null)) car.destroy();
            }, 60_000);
        }
    }
    if (message) toastAll(message, {title: ended.title, tone});
}

function startSupplyDrop() {
    const spot = spotNear(300, 900);
    if (!spot) return false;
    const position = {x: spot.position.x, y: spot.position.y + 0.4, z: spot.position.z};
    current = {kind: "supply", title: "Supply drop", position, expiresAt: Date.now() + DROP_LIFETIME_MS,
        generation: World.getMissionGeneration()};
    markersAll("event", [{id: "drop", position, label: "SUPPLY DROP", color: COLORS.gold, beacon: true, model: "bedna00.i3d", primary: true}]);
    for (const player of World.getPlayers()) compass(player, "event", 40, {position});
    toastAll("A crate of guns fell off a truck. First one to it keeps the lot. Follow the compass.", {title: "Supply drop", tone: "gold", seconds: 8});
    feed("event", [text("Supply drop", COLORS.gold), text(" landed somewhere in the city")]);
    return true;
}

function startHotCar() {
    const spot = spotNear(400, 1000);
    if (!spot) return false;
    const choice = pick(HOT_CARS);
    const dropOffs = maps.places();
    const dropOff = pick(dropOffs.filter((entry) => flatDistance(entry.position, spot.position) > 800)) ?? pick(dropOffs);
    const graph = roads.current();
    const delivery = graph.crossings[graph.nearestCrossing(dropOff.position)];
    const heading = Math.atan2(spot.direction.x, spot.direction.z);
    const vehicle = Vehicle.spawn(choice.model, {x: spot.position.x, y: spot.position.y + 0.6, z: spot.position.z}, heading);
    if (!vehicle) return false;
    vehicle.state.set("fr.fullTank", true);
    vehicle.state.set("fr.hot", true);
    vehicle.setRadarMarker(true, 0xd0574a);
    current = {kind: "hotcar", title: "Hot car", vehicleId: vehicle.id, carName: choice.name, dropOff: dropOff.name,
        delivery: {x: delivery.x, y: delivery.y, z: delivery.z}, driverId: null, wanted: new Set(),
        expiresAt: Date.now() + HOT_CAR_LIFETIME_MS, generation: World.getMissionGeneration()};
    markersAll("event", [
        {id: "car", vehicleId: vehicle.id, label: `HOT CAR · ${choice.name.toUpperCase()}`, color: COLORS.red, beacon: true, primary: true},
        {id: "delivery", position: current.delivery, label: `DELIVER TO ${dropOff.name.toUpperCase()}`, color: COLORS.green, beacon: true},
    ]);
    for (const player of World.getPlayers()) compass(player, "event", 40, {vehicleId: vehicle.id});
    toastAll(`A ${choice.name} needs to reach ${dropOff.name}, no questions asked. ${formatMoney(HOT_CAR_PRIZE)} for the driver.`,
        {title: "Hot car", tone: "gold", seconds: 9});
    feed("event", [text("Hot car", COLORS.red), text(` · a ${choice.name} is waiting for a driver`)]);
    return true;
}

function startEvent(kind) {
    if (current || !inFreeRide() || !World.isReady() || !streetPlayers().length) return false;
    const started = (kind ?? pick(["supply", "hotcar"])) === "supply" ? startSupplyDrop() : startHotCar();
    nextEventAt = Date.now() + EVENT_EVERY_MS;
    return started;
}

function tickSupply(now) {
    for (const player of streetPlayers()) {
        if (distance(player.position, current.position) > DROP_REACH) continue;
        const {position} = current;
        DROP_LOOT.forEach((item, index) => {
            const angle = index / DROP_LOOT.length * Math.PI * 2;
            Pickup.create(item.weaponId, {x: position.x + Math.cos(angle) * 1.4, y: position.y, z: position.z + Math.sin(angle) * 1.4},
                angle, item.loaded, item.reserve, 120_000);
        });
        player.giveMoney(DROP_PRIZE);
        profiles.bump(player, "eventsWon");
        profiles.addXp(player, DROP_XP, "Supply drop");
        feed("event", [name(player, COLORS.gold), text(" cracked the supply drop · "), text(formatMoney(DROP_PRIZE), COLORS.gold)]);
        toast(player, `The crate is yours, plus ${formatMoney(DROP_PRIZE)} under the straw. Grab the guns.`, {title: "Supply drop", tone: "good"});
        endEvent(null);
        return;
    }
    if (now >= current.expiresAt) endEvent("Someone else got to the supply drop first. It's gone.");
}

function tickHotCar(now) {
    const vehicle = vehicleById(current.vehicleId);
    if (!vehicle || vehicle.terminalState !== 0) {
        endEvent("The hot car was wrecked. The buyer walked away.", "bad");
        return;
    }
    const driver = vehicle.getDriver();
    if ((driver?.id ?? null) !== current.driverId) {
        current.driverId = driver?.id ?? null;
        for (const player of World.getPlayers()) {
            compass(player, "event", 40, driver && player.id === driver.id ? {position: current.delivery} : {vehicleId: current.vehicleId});
        }
        if (driver) {
            feed("event", [name(driver, COLORS.red), text(` took the ${current.carName}`)]);
            toast(driver, `Get it to ${current.dropOff} in one piece. The police already know.`, {title: "Hot car", tone: "bad"});
            // The simulation host runs the police; without it there is no chase.
            if (!current.wanted.has(driver.id) && Police.setWanted(driver, 2)) current.wanted.add(driver.id);
        }
    }
    if (driver && flatDistance(vehicle.position, current.delivery) <= DELIVERY_RADIUS && speedOf(vehicle) < 4) {
        driver.giveMoney(HOT_CAR_PRIZE);
        profiles.bump(driver, "eventsWon");
        profiles.addXp(driver, HOT_CAR_XP, "Hot car delivered");
        for (const passenger of vehicle.getOccupants()) {
            if (!passenger || passenger.id === driver.id) continue;
            passenger.giveMoney(HOT_CAR_PASSENGER_PRIZE);
            profiles.addXp(passenger, HOT_CAR_XP / 3, "Rode along");
        }
        feed("event", [name(driver, COLORS.gold), text(` delivered the ${current.carName} · `), text(formatMoney(HOT_CAR_PRIZE), COLORS.gold)]);
        endEvent(null);
        return;
    }
    if (now >= current.expiresAt) endEvent("The buyer stopped waiting for the hot car.");
}

function sendCards(now) {
    if (now - lastCardAt < CARD_EVERY_MS) return;
    lastCardAt = now;
    const vehicle = current.kind === "hotcar" ? vehicleById(current.vehicleId) : null;
    const target = current.kind === "supply" ? current.position : vehicle?.position;
    for (const player of World.getPlayers()) {
        if (!player.spawned) continue;
        const driving = vehicle && vehicle.getDriver()?.id === player.id;
        const goal = driving ? current.delivery : target;
        card(player, "event", {
            title: current.title,
            subtitle: current.kind === "supply" ? "First to the crate keeps it"
                : driving ? `Deliver to ${current.dropOff}` : current.driverId ? "Someone has the car. Stop them or ride along" : "Find the car and take the wheel",
            rows: [["Distance", goal ? `${Math.round(distance(player.position, goal))} m` : "-"], ["Ends in", formatMinutes(current.expiresAt - now)],
                ["Prize", formatMoney(current.kind === "supply" ? DROP_PRIZE : HOT_CAR_PRIZE)]],
            progress: Math.max(0, (current.expiresAt - now) / (current.kind === "supply" ? DROP_LIFETIME_MS : HOT_CAR_LIFETIME_MS)),
            tone: current.kind === "supply" ? "gold" : "bad",
        });
    }
}

function tick() {
    const now = Date.now();
    if (!current) {
        if (nextEventAt && now >= nextEventAt) startEvent();
        return;
    }
    if (current.generation !== World.getMissionGeneration()) {
        current = null;
        return;
    }
    if (current.kind === "supply") tickSupply(now);
    else tickHotCar(now);
    if (current) sendCards(now);
}

// Late arrivals see the event already in progress.
function catchUp(player) {
    if (!current) return;
    const vehicle = current.kind === "hotcar" ? vehicleById(current.vehicleId) : null;
    const items = current.kind === "supply"
        ? [{id: "drop", position: current.position, label: "SUPPLY DROP", color: COLORS.gold, beacon: true, model: "bedna00.i3d", primary: true}]
        : [{id: "car", vehicleId: current.vehicleId, label: `HOT CAR · ${current.carName.toUpperCase()}`, color: COLORS.red, beacon: true, primary: true},
            {id: "delivery", position: current.delivery, label: `DELIVER TO ${current.dropOff.toUpperCase()}`, color: COLORS.green, beacon: true}];
    notify.markers(player, "event", items);
    compass(player, "event", 40, current.kind === "supply" ? {position: current.position} : vehicle ? {vehicleId: vehicle.id} : null);
}

util.onStart(() => {
    nextEventAt = Date.now() + FIRST_EVENT_MS;
    tickTimer = setInterval(tick, TICK_MS);
});
util.onStop(() => {
    clearInterval(tickTimer);
    endEvent(null);
});
Events.on("missionChange", () => {
    current = null;
    nextEventAt = Date.now() + FIRST_EVENT_MS;
});
Events.on("playerSpawn", catchUp);
Events.on("vehicleDestroy", (vehicle) => {
    if (current?.kind === "hotcar" && current.vehicleId === vehicle.id) endEvent("The hot car is gone.", "bad");
});
Events.on("consoleCommand", (command, args) => {
    if (command !== "fr" || args[0] !== "event") return;
    if (current) endEvent("City Hall called off the event.");
    console.log(startEvent(args[1] === "hotcar" ? "hotcar" : args[1] === "supply" ? "supply" : undefined)
        ? `[freeride] Started ${current.kind}` : "[freeride] No event could start (needs a player on the street)");
});

commands.register({name: "event", usage: "/event", help: "Where the current city event is.", category: "City"}, (player) => {
    if (!current) {
        toast(player, `No event right now. The next one comes in about ${formatMinutes(Math.max(0, nextEventAt - Date.now()))}.`);
        return;
    }
    catchUp(player);
    toast(player, current.kind === "supply" ? "A supply drop is out there. Your compass points at it."
        : `A hot ${current.carName} is waiting. Your compass points at it.`, {title: current.title, tone: "gold"});
});

menu.provide("event", () => current ? {kind: current.kind, title: current.title,
    detail: current.kind === "supply" ? "First to the crate keeps it" : `${current.carName} to ${current.dropOff}`,
    secondsLeft: Math.max(0, Math.ceil((current.expiresAt - Date.now()) / 1000))}
    : {kind: null, nextInSeconds: Math.max(0, Math.ceil((nextEventAt - Date.now()) / 1000))});
