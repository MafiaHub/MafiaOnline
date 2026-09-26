// Four places with a reason to visit: Pete sells weapons, the pump sells fuel,
// the pharmacy patches wounds, and Salieri's back window starts a courier run.
// Positions were read from the stock a1.dta mission scene2.bin frames:
//   MISE19-MESTO: "1dvere u peteho" (Pete's doorway)
//   FREERIDE and FREERIDENOC: "_pumpa2", "lekarna", "Salieri_save".
// Both selected Free Ride scenes contain the same latter three positions.
const SHOP_MISSIONS = new Set(["freeride", "freeridenoc"]);
const WEAPON = Object.freeze({BAT: 4, COLT: 6, THOMPSON: 10, SHOTGUN: 11, GRENADE: 15});
const STARTING_CASH = 300;
const START_FUEL_FRACTION = 0.12;
const SHOP_REACH = 3.5;
const CAR_REACH = 9;
const PURCHASE_COOLDOWN_MS = 450;
const COURIER_TIME_MS = 8 * 60_000;
const COURIER_REWARD = 180;
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
    {id: "pump", name: "The Night Pump", tagline: "A full tank buys a little more time", position: {x: -110.6361, y: 8.4794, z: -135.5433},
        model: "pumpar01.i3d", animation: "pumpar.i3d", offers: [
            {id: "petrol", name: "Up to 25 litres", price: null, note: "$3 per litre · your car must be nearby"},
        ]},
    {id: "pharmacy", name: "The Dispensary", tagline: "The city keeps its own hours", position: {x: -761.3176, y: 14.813, z: 762.4796},
        model: "recep.i3d", animation: null, offers: [
            {id: "bandage", name: "Bandages & tincture", price: 65, note: "Restore up to 45 health"},
        ]},
    {id: "salieri", name: "Salieri's Back Window", tagline: "Coffee, pie, and sealed envelopes", position: {x: -1774.5226, y: -4.5073, z: 5.2466},
        model: "Barman01.i3d", animation: null, offers: [
            {id: "coffee", name: "Coffee & pie", price: 18, note: "Restore up to 18 health"},
            {id: "courier", name: "A sealed envelope", price: 0, note: `Take it to Pete in eight minutes · earn $${COURIER_REWARD}`},
        ]},
];

const funded = new Set();
const lastVehicle = new Map();
const lastPurchase = new Map();
const courier = new Map();
const lowFuelPending = new Map();
let fuelTimer = null;

function inFreeRide() { return SHOP_MISSIONS.has(World.getMission()); }
function distance(a, b) { return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z); }

function shopState(player) {
    if (!inFreeRide()) return;
    player.emit("sample:shops:state", JSON.stringify({
        generation: World.getMissionGeneration(),
        shops: SHOPS,
        courier: courier.has(player.id),
        money: player.money,
    }));
}

function chargeAndApply(player, cost, apply) {
    if (!player.trySpendMoney(cost)) {
        player.sendMessage(`You need $${cost}; you have $${player.money}.`);
        return false;
    }
    if (apply()) return true;
    player.giveMoney(cost);
    player.sendMessage("The transaction could not be completed; your money was returned.");
    return false;
}

function buyWeapon(player, offer) {
    if (offer.id === "colt") return player.hasWeapon(WEAPON.COLT)
        ? player.giveAmmo(WEAPON.COLT, 18) : player.giveWeapon(WEAPON.COLT, 6, 18, true);
    if (offer.id === "shells") return player.hasWeapon(WEAPON.SHOTGUN)
        ? player.giveAmmo(WEAPON.SHOTGUN, 24) : player.giveWeapon(WEAPON.SHOTGUN, 8, 24, true);
    if (offer.id === "thompson") return player.hasWeapon(WEAPON.THOMPSON)
        ? player.giveAmmo(WEAPON.THOMPSON, 100) : player.giveWeapon(WEAPON.THOMPSON, 50, 100, true);
    if (offer.id === "grenades") return player.hasWeapon(WEAPON.GRENADE)
        ? player.giveAmmo(WEAPON.GRENADE, 3) : player.giveWeapon(WEAPON.GRENADE, 3, 0, true);
    return false;
}

function buyFuel(player) {
    const vehicle = player.getVehicle() ?? lastVehicle.get(player.id);
    if (!vehicle || !vehicle.model || distance(vehicle.position, SHOPS[1].position) > CAR_REACH) {
        player.sendMessage("Bring your car close to the pump, then come back to the attendant.");
        return;
    }
    if (!vehicle.dynamicsValid || vehicle.fuelTankCapacity <= 0) {
        player.sendMessage("The attendant is waiting for your fuel gauge to settle.");
        return;
    }
    const litres = Math.min(25, Math.max(0, vehicle.fuelTankCapacity - vehicle.fuel));
    if (litres < 0.5) {
        player.sendMessage("Your tank is full enough already.");
        return;
    }
    const price = Math.ceil(litres * 3);
    if (chargeAndApply(player, price, () => vehicle.setFuel(Math.min(vehicle.fuelTankCapacity, vehicle.fuel + litres)))) {
        player.sendMessage(`The attendant pumped ${litres.toFixed(1)} litres for $${price}.`);
    }
}

function buyHealth(player, offer) {
    const healing = offer.id === "bandage" ? 45 : 18;
    const health = Math.min(100, player.health + healing);
    if (health <= player.health) {
        player.sendMessage("You are in good shape already.");
        return;
    }
    if (chargeAndApply(player, offer.price, () => player.setHealth(health))) {
        player.sendMessage(`${offer.name}: health ${Math.round(player.health)}/100, balance $${player.money}.`);
    }
}

function buy(player, request) {
    if (!inFreeRide() || !World.isReady() || !player.spawned || !player.alive ||
        player.missionGeneration !== World.getMissionGeneration() ||
        request?.generation !== World.getMissionGeneration()) return;
    const shop = SHOPS.find((entry) => entry.id === request.shopId);
    const offer = shop?.offers.find((entry) => entry.id === request.offerId);
    if (!shop || !offer || distance(player.position, shop.position) > SHOP_REACH) return;
    const now = Date.now();
    if (now - (lastPurchase.get(player.id) ?? 0) < PURCHASE_COOLDOWN_MS) return;
    lastPurchase.set(player.id, now);

    if (offer.id === "petrol") return buyFuel(player);
    if (offer.id === "courier") {
        if (courier.has(player.id)) {
            player.sendMessage("You already have an envelope to deliver.");
            return;
        }
        courier.set(player.id, {startedAt: now});
        player.sendMessage(`Take the sealed envelope to Yellow Pete. You have eight minutes; the fee is $${COURIER_REWARD}.`);
        shopState(player);
        return;
    }
    if (offer.id === "bandage" || offer.id === "coffee") return buyHealth(player, offer);
    if (chargeAndApply(player, offer.price, () => buyWeapon(player, offer))) {
        player.sendMessage(`${offer.name} bought for $${offer.price}. Pete counts the bills twice.`);
    }
}

function deliver(player) {
    const job = courier.get(player.id);
    if (!job || !inFreeRide() || !player.spawned || !player.alive ||
        distance(player.position, SHOPS[0].position) > SHOP_REACH) return;
    courier.delete(player.id);
    if (Date.now() - job.startedAt > COURIER_TIME_MS) {
        player.sendMessage("The envelope arrived too late. Nobody at Pete's will take it.");
    }
    else {
        player.giveMoney(COURIER_REWARD);
        player.sendMessage(`Pete takes the envelope without a word. Courier fee: $${COURIER_REWARD}.`);
    }
    shopState(player);
}

Events.on("resourceStart", (name) => {
    if (name !== "mafia1online-sample") return;
    fuelTimer = setInterval(() => {
        for (const [id, entry] of lowFuelPending) {
            const vehicle = World.getVehicles().find((candidate) => candidate.id === id);
            if (!vehicle || entry.generation !== World.getMissionGeneration() || ++entry.retries > LOW_FUEL_RETRY_COUNT) {
                lowFuelPending.delete(id);
                continue;
            }
            if (!vehicle.dynamicsValid || vehicle.fuelTankCapacity <= 0) continue;
            const startingFuel = Math.min(vehicle.fuel, vehicle.fuelTankCapacity * START_FUEL_FRACTION);
            if (vehicle.setFuel(startingFuel)) lowFuelPending.delete(id);
        }
    }, LOW_FUEL_RETRY_MS);
});
Events.on("resourceStop", (name) => {
    if (name !== "mafia1online-sample") return;
    if (fuelTimer !== null) clearInterval(fuelTimer);
    fuelTimer = null;
    lowFuelPending.clear();
    courier.clear();
    funded.clear();
    lastVehicle.clear();
    lastPurchase.clear();
});
Events.on("missionChange", () => lowFuelPending.clear());
Events.on("vehicleSpawn", (vehicle) => {
    if (inFreeRide()) lowFuelPending.set(vehicle.id, {generation: World.getMissionGeneration(), retries: 0});
});
Events.on("vehicleDestroy", (vehicle) => lowFuelPending.delete(vehicle.id));
Events.on("vehiclePlayerEntered", (vehicle, player) => lastVehicle.set(player.id, vehicle));
Events.on("vehiclePlayerExited", (vehicle, player) => lastVehicle.set(player.id, vehicle));
Events.on("playerSpawn", (player) => {
    if (!inFreeRide()) return;
    if (!funded.has(player.id)) {
        funded.add(player.id);
        if (player.money === 0) player.setMoney(STARTING_CASH);
        player.sendMessage(`You have $${player.money}. Pete, the pump and the dispensary take cash.`);
    }
    shopState(player);
});
Events.on("playerRespawn", shopState);
Events.on("playerDisconnect", (player) => {
    funded.delete(player.id);
    lastVehicle.delete(player.id);
    lastPurchase.delete(player.id);
    courier.delete(player.id);
});
Events.onClient("sample:shop:buy", buy);
Events.onClient("sample:shop:deliver", deliver);
Events.on("playerCommand", (player, command) => {
    if (command === "deliver") deliver(player);
});
