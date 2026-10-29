// Four places with a reason to visit: Pete sells weapons, the pump's
// attendants sell fuel, the pharmacy patches wounds, and Salieri's back window
// starts a courier run.
// Positions were read from the stock a1.dta mission scene2.bin frames:
//   MISE19-MESTO: "1dvere u peteho" (Pete's doorway)
//   FREERIDE and FREERIDENOC: "_pumpa2", "lekarna", "Salieri_save".
// Both selected Free Ride scenes contain the same latter three positions.
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, card, compass} = require("./lib/notify.js");
const maps = require("./lib/maps.js");
const {distance, emit, onClient, onStart, onStop, formatMoney, formatMinutes} = require("./lib/util.js");

// The city maps have the shops; the countryside has none.
function shopsOpen() {
    return Boolean(maps.current()?.shops);
}

const WEAPON = Object.freeze({BAT: 4, COLT: 6, THOMPSON: 10, SHOTGUN: 11, GRENADE: 15});
const START_FUEL_FRACTION = 0.12;
// The retail attendants (Pumper) fuel the car at this price per litre.
const FUEL_PRICE_PER_LITRE = 3;
const SHOP_REACH = 3.5;
const PURCHASE_COOLDOWN_MS = 450;
const COURIER_TIME_MS = 8 * 60_000;
const COURIER_REWARD = 180;
const COURIER_XP = 60;
const LOW_FUEL_RETRY_MS = 500;
const LOW_FUEL_RETRY_COUNT = 30;

const SHOPS = [
    {id: "pete", name: "Yellow Pete's", tagline: "No questions after dark", position: {x: 61.13562, y: 6.12056, z: 113.52177},
        // Box24 is the shop door hinge in both Free Ride scenes. Pete faces
        // away from the hinge toward players approaching the doorway.
        entranceFrame: "Box24", entrance: {x: 60.58458, y: 6.09017, z: 113.82933},
        model: "ProdZ1.i3d", animation: "BorecStat.i3d", offers: [
            {id: "colt", name: "Colt .45", price: 90, note: "Six in the chamber, eighteen spare"},
            {id: "shells", name: "Shotgun & shells", price: 210, note: "For when a quiet word will not do"},
            {id: "thompson", name: "Thompson & drums", price: 320, note: "A loud answer to a bad question"},
            {id: "grenades", name: "Three grenades", price: 95, note: "Keep them out of the glovebox"},
        ]},
    // The stock attendants serve the cars here; no scripted one stands in.
    {id: "pump", name: "The Night Pump", tagline: "A full tank buys a little more time", position: {x: -110.6361, y: 8.4794, z: -135.5433},
        model: null, animation: null, offers: [
            {id: "petrol", name: "A full tank", price: null,
                note: `$${FUEL_PRICE_PER_LITRE} per litre · stop at a pump, engine off, and ask the attendant`},
        ]},
    // The "lekarna" frame floats 1.5 m over the hospital's front landing,
    // whose floor is at y 13.2 (the door 3DVERE4 at z 763.5): the clerk
    // stands on the landing below it.
    {id: "pharmacy", name: "The Dispensary", tagline: "The city keeps its own hours", position: {x: -761.3176, y: 13.25, z: 762.4796},
        model: "recep.i3d", animation: null, offers: [
            {id: "bandage", name: "Bandages & tincture", price: 65, note: "Restore up to 45 health"},
        ]},
    {id: "salieri", name: "Salieri's Back Window", tagline: "Coffee, pie, and sealed envelopes", position: {x: -1774.5226, y: -4.5073, z: 5.2466},
        model: "Barman01.i3d", animation: null, offers: [
            {id: "coffee", name: "Coffee & pie", price: 18, note: "Restore up to 18 health"},
            {id: "courier", name: "A sealed envelope", price: 0, note: `Take it to Pete in eight minutes · earn $${COURIER_REWARD}`},
        ]},
];

const lastPurchase = new Map();
const courier = new Map();
const lowFuelPending = new Map();
let fuelTimer = null;
let courierTimer = null;

// On Lost Heaven Extended Salieri's front door opens into his bar (bars.js),
// where the clerk would stand in the doorway; the window keeps serving
// without him.
function shownShops() {
    return maps.current()?.id === "freeride_extended" ? SHOPS.map((shop) => shop.id === "salieri" ? {...shop, model: null} : shop) : SHOPS;
}

function shopState(player) {
    if (!shopsOpen()) return;
    emit(player, "fr:shops:state", {generation: World.getMissionGeneration(), shops: shownShops(), courier: courier.has(player.id)});
}

function chargeAndApply(player, cost, apply) {
    if (!player.trySpendMoney(cost)) {
        toast(player, `You need ${formatMoney(cost)}; you have ${formatMoney(player.money)}.`, {tone: "bad"});
        return false;
    }
    if (apply()) return true;
    player.giveMoney(cost);
    toast(player, "The deal fell through; your money was returned.", {tone: "bad"});
    return false;
}

function buyWeapon(player, offer) {
    if (offer.id === "colt") return player.hasWeapon(WEAPON.COLT)
        ? player.giveAmmo(WEAPON.COLT, 18) : player.giveWeapon(WEAPON.COLT, 6, 18, true);
    if (offer.id === "shells") return player.hasWeapon(WEAPON.SHOTGUN)
        ? player.giveAmmo(WEAPON.SHOTGUN, 24) : player.giveWeapon(WEAPON.SHOTGUN, 8, 24, true);
    if (offer.id === "thompson") return player.hasWeapon(WEAPON.THOMPSON)
        ? player.giveAmmo(WEAPON.THOMPSON, 100) : player.giveWeapon(WEAPON.THOMPSON, 50, 100, true);
    // A throwable's count is its loaded value; addItem adds to it.
    if (offer.id === "grenades") return player.hasWeapon(WEAPON.GRENADE)
        ? player.addItem(WEAPON.GRENADE, 3) : player.giveWeapon(WEAPON.GRENADE, 3, 0, true);
    return false;
}

// The attendant fuels the car himself (Pumper, server-charged); the counter
// only tells how.
function buyFuel(player) {
    toast(player, "Drive up to a pump, switch the engine off and press the exit key: the attendant comes to you.");
}

const PUMPER_REFUSALS = {
    disabled: "The pumps are closed.",
    unavailable: "Nobody is minding the pumps right now.",
    busy: "The attendant is busy with another car. Wait your turn.",
    full: "Your tank is full already.",
};

function buyHealth(player, offer) {
    const healing = offer.id === "bandage" ? 45 : 18;
    const health = Math.min(100, player.health + healing);
    if (health <= player.health) {
        toast(player, "You are in good shape already.");
        return;
    }
    if (chargeAndApply(player, offer.price, () => player.setHealth(health))) {
        toast(player, `${offer.name}: health ${Math.round(player.health)}/100.`, {tone: "good"});
    }
}

function courierCard(player) {
    const job = courier.get(player.id);
    card(player, "courier", job ? {
        title: "Courier run", subtitle: "Deliver the envelope to Yellow Pete",
        rows: [["Pay", formatMoney(COURIER_REWARD)], ["Time left", formatMinutes(job.startedAt + COURIER_TIME_MS - Date.now())]],
        progress: Math.max(0, 1 - (Date.now() - job.startedAt) / COURIER_TIME_MS), tone: "gold",
    } : null);
}

function endCourier(player, message, tone = "bad") {
    if (!courier.delete(player.id)) return;
    life.end(player, "courier");
    courierCard(player);
    compass(player, "courier", 60, null);
    shopState(player);
    if (message) toast(player, message, {tone});
}

function startCourier(player) {
    if (courier.has(player.id)) {
        toast(player, "You already have an envelope to deliver.");
        return;
    }
    if (!life.begin(player, "courier", "Courier run")) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    courier.set(player.id, {startedAt: Date.now()});
    toast(player, `Take the sealed envelope to Yellow Pete. Eight minutes, ${formatMoney(COURIER_REWARD)}.`, {title: "Courier run", tone: "gold"});
    compass(player, "courier", 60, {position: SHOPS[0].position});
    courierCard(player);
    shopState(player);
}

function buy(player, request) {
    if (!shopsOpen() || !World.isReady() || !player.spawned || !player.alive ||
        player.missionGeneration !== World.getMissionGeneration() ||
        request?.generation !== World.getMissionGeneration()) return;
    const shop = SHOPS.find((entry) => entry.id === request.shopId);
    const offer = shop?.offers.find((entry) => entry.id === request.offerId);
    if (!shop || !offer || distance(player.position, shop.position) > SHOP_REACH) return;
    const now = Date.now();
    if (now - (lastPurchase.get(player.id) ?? 0) < PURCHASE_COOLDOWN_MS) return;
    lastPurchase.set(player.id, now);

    if (offer.id === "petrol") return buyFuel(player);
    if (offer.id === "courier") return startCourier(player);
    if (offer.id === "bandage" || offer.id === "coffee") return buyHealth(player, offer);
    if (chargeAndApply(player, offer.price, () => buyWeapon(player, offer))) {
        toast(player, `${offer.name} for ${formatMoney(offer.price)}. Pete counts the bills twice.`, {tone: "good"});
    }
}

function deliver(player) {
    const job = courier.get(player.id);
    if (!job || !shopsOpen() || !player.spawned || !player.alive ||
        distance(player.position, SHOPS[0].position) > SHOP_REACH) return;
    if (Date.now() - job.startedAt > COURIER_TIME_MS) {
        endCourier(player, "The envelope arrived too late. Nobody at Pete's will take it.");
        return;
    }
    player.giveMoney(COURIER_REWARD);
    profiles.bump(player, "deliveries");
    profiles.addXp(player, COURIER_XP, "Courier run");
    endCourier(player, `Pete takes the envelope without a word. ${formatMoney(COURIER_REWARD)} earned.`, "good");
}

function tickCouriers() {
    for (const player of World.getPlayers()) {
        const job = courier.get(player.id);
        if (!job) continue;
        if (Date.now() - job.startedAt > COURIER_TIME_MS) endCourier(player, "The courier run expired. Salieri will not be pleased.");
        else courierCard(player);
    }
}

onStart(() => {
    Pumper.setPrice(FUEL_PRICE_PER_LITRE);
    fuelTimer = setInterval(() => {
        for (const [id, entry] of lowFuelPending) {
            const vehicle = World.getVehicles().find((candidate) => candidate.id === id);
            if (!vehicle || vehicle.state.get("fr.fullTank") || entry.generation !== World.getMissionGeneration() ||
                ++entry.retries > LOW_FUEL_RETRY_COUNT) {
                lowFuelPending.delete(id);
                continue;
            }
            if (!vehicle.dynamicsValid || vehicle.fuelTankCapacity <= 0) continue;
            const startingFuel = Math.min(vehicle.fuel, vehicle.fuelTankCapacity * START_FUEL_FRACTION);
            if (vehicle.setFuel(startingFuel)) lowFuelPending.delete(id);
        }
    }, LOW_FUEL_RETRY_MS);
    courierTimer = setInterval(tickCouriers, 5000);
});
onStop(() => {
    clearInterval(fuelTimer);
    clearInterval(courierTimer);
    lowFuelPending.clear();
    courier.clear();
    lastPurchase.clear();
});

Events.on("missionChange", () => {
    lowFuelPending.clear();
    for (const player of World.getPlayers()) endCourier(player, null);
});
// Personal cars start nearly dry so the pump has a use; race and event cars
// mark themselves full with the "fr.fullTank" state key.
Events.on("vehicleSpawn", (vehicle) => {
    if (shopsOpen() && !vehicle.traffic) lowFuelPending.set(vehicle.id, {generation: World.getMissionGeneration(), retries: 0});
});
Events.on("vehicleDestroy", (vehicle) => lowFuelPending.delete(vehicle.id));
Events.on("vehicleRefuel", (vehicle, player, info) => {
    if (player && info.litres > 0) {
        toast(player, `The attendant put in ${info.litres.toFixed(1)} litres for ${formatMoney(info.cost)}.`, {tone: "good"});
    }
    else if (player) toast(player, "The attendant pumped, but you had nothing to pay with.", {tone: "bad"});
});
Events.on("pumperRefused", (player, vehicle, info) => {
    toast(player, info.reason === "money"
        ? `A full tank costs ${formatMoney(info.cost)}; you have ${formatMoney(player.money)}.`
        : PUMPER_REFUSALS[info.reason] ?? "The attendant waves you off.", {tone: "bad"});
});
Events.on("playerSpawn", shopState);
Events.on("playerRespawn", shopState);
Events.on("playerDeath", (player) => endCourier(player, "You dropped the envelope when you went down."));
Events.on("playerDisconnect", (player) => {
    lastPurchase.delete(player.id);
    courier.delete(player.id);
});
onClient("fr:shop:buy", buy);
onClient("fr:shop:deliver", deliver);

commands.register({name: "courier", usage: "/courier [cancel]", help: "Check or abandon your courier run.", category: "City"}, (player, args) => {
    const job = courier.get(player.id);
    if (args[0]?.toLowerCase() === "cancel") {
        if (job) endCourier(player, "You burned the envelope. No pay today.");
        return;
    }
    toast(player, job ? `Deliver to Yellow Pete: ${formatMinutes(job.startedAt + COURIER_TIME_MS - Date.now())} left.`
        : "Salieri's back window has courier work. Press N there to pick the envelope, B to take it.");
});

menu.provide("places", () => SHOPS.map((shop) => ({id: shop.id, name: shop.name, tagline: shop.tagline,
    offers: shop.offers.map((offer) => offer.name)})));

module.exports = {SHOPS};
