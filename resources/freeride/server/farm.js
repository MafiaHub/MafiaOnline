// The tobacco farm, in the countryside. A player signs on under the arrow at
// the lowland barn (lib/stations.js; two farm trucks wait beside it, which
// only farmhands drive), plants seedlings on the plots of the field beside
// the barn, waters them with a farm truck's hose at each stage, picks off
// hornworms, picks the ripe leaves and carries them to the barn to cure. The
// cured tobacco goes on a farm truck at the loading bay and is sold in the
// village, or exported at the container docks for far more; near the docks
// the whole city learns of the load, and whoever drives that truck in is
// paid. A loaded farm truck may be driven by anyone.
//
// Where it is. Each map has its own site; both have seven rows of eight
// plots, so a plant keeps its plot (row and place in the row) from map to
// map, and a plant saved at the old Lost Heaven Extended field grows on in
// the same plot of the new one. Ground heights are sampled from each map's
// ground mesh; every spot below was checked clear of the scene's objects.
//
// The Countryside (FREEKRAJINA): the stock models/4stodola.4ds its scene2.bin
// places at (-2295.25, -3.62, 711.61), rotation (0.447, 0, 0.894, 0), at the
// end of the lowland road: road graph crossing (-2306.2, -5.9, 668.8) is a
// dead end 32 m from the barn, with fences (@PLplot) only on the far side of
// the road. Applied as LS3D does (the conjugate, see lib/maps.js) its lean-to
// side, local -x, faces (0.6, -0.8), toward the field: open grass east of the
// barn. Heights from the FREEKRAJINA scene.4ds ground mesh.
//
// Lost Heaven Extended: the same barn is no farm there. The mod's scene.4ds
// adds "Loft14", a 3 m plank wall (0LKULNA1.BMP) from (-2172.8, 496.4) to
// (-2349.1, 781.2) with 16 collision faces and two plates in its tree.klz,
// between that road and the barn: the trucks and the loading bay were on the
// road side, the arrow and the field behind the wall. The farm is the mod's
// farmstead in the north of the countryside instead: scene2.bin's "prejimka"
// (models/prejimka.4ds, its rebuild of the stock FREEKRAJINA "prejimka*"
// frames) furnished with straw, sacks, carts and tractor "traktor1". The way
// in is its bridge ("2obj80"/"Object08", deck y 19.8, 6 m between the rails)
// over the creek, 22 m north of the road graph line at (-840, 18.6, 3316),
// on the main network (4.4 km by road to the village co-op, 5.8 km to the
// docks), then the lane north (Loft05/Object25, fenced on the east by
// Object31/28/26) into the yard. The barn ("&& stodola1-4", floor y 19.86)
// opens to the west between its posts at (-849.6, 3491) and (-851.8, 3497.8);
// the arrow is in front of that door. West of it lies the fenced paddock
// (Object14/Cylinder134 north, Object13/Object11 west, Object32 south, with
// collision kolize5/6/8) whose east side is open between Object09 (ends at
// (-863.2, 3505.7)) and Object07 (ends at (-858.8, 3491.5)): the field fills
// it on flat grass (y 19.45 to 19.79), the trucks wait beside it facing the
// gap and the loading bay is in the gap, clear of the crates there
// ("anchr" boxes x -864.7 to -862.8, z 3496.6 to 3499.7).
//
// Buyers. The village buyer is the main street crossing in front of the
// village store (models/4build76.4ds at (-1488.4, 14.4, 714.4) in
// FREEKRAJINA, (-1489.8, 14.5, 714.6) in the mod's cache.bin); the road graph
// crossing (-1484.1, 14.6, 725.1) is on both maps. The docks are the mod's
// container docks crossing (lib/maps.js).
//
// Plants live on the server's farm clock: it runs while a map with the farm
// is loaded, so a crop waits through a map without it or a restart, but not
// through its owner leaving. Plants, leaves, curing batches and cured
// tobacco are kept in the database by player record; a load on a truck goes
// back to its owner's barn when the map changes or the resource stops.
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const jobkey = require("./lib/jobkey.js");
const stations = require("./lib/stations.js");
const store = require("./lib/store.js");
const {toast, toastAll, card, compass, markers, markersAll, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, flatDistance, playable, playerById, vehicleById, speedOf, formatMoney, formatMinutes, onStart, onStop, inFreeRide} =
    require("./lib/util.js");

const LEAF = "#a7b85a";
const TRUCK = "trucka00.i3d";
// models/s_ker.4ds: the countryside's own bush (75 in FREEKRAJINA), 1.7 m
// across and 1.33 m tall from its base at the origin; scaled per stage.
const PLANT_MODEL = "s_ker.i3d";
// models/baliksena.4ds: the round hay bale the countryside's fields carry.
const BALE_MODEL = "baliksena.i3d";
const STORE = "farm";

// The plots of a field: seven rows dz apart, eight plots dx apart in each,
// with the ground height of each plot.
function plots(x0, dx, z0, dz, ground) {
    return ground.flatMap((row, r) => row.map((y, c) => ({id: r * row.length + c, row: r + 1, x: x0 + c * dx, y, z: z0 + r * dz})));
}
const PLOT_COUNT = 56;

const LOWLANDS = {
    place: "the barn in the lowlands",
    signOn: {x: -2286.7, y: -5.0, z: 700.2},
    // On the flat apron between the road and the barn, noses north-west
    // along the road; ground -4.68 and -4.53 under both ends.
    spots: [
        {position: {x: -2314.5, y: -4.63, z: 716.0}, heading: -0.675},
        {position: {x: -2311.2, y: -4.48, z: 718.6}, heading: -0.675},
    ],
    // In front of the barn's road side, 7 m out.
    loading: {x: -2305.0, y: -4.84, z: 702.0},
    plots: plots(-2276, 3, 699, 5, [
        [-5.52, -5.64, -5.75, -5.87, -5.98, -6.09, -6.21, -6.32],
        [-5.10, -5.22, -5.33, -5.45, -5.56, -5.68, -5.79, -5.90],
        [-4.68, -4.80, -4.91, -5.03, -5.14, -5.26, -5.37, -5.49],
        [-4.26, -4.38, -4.49, -4.61, -4.72, -4.84, -4.95, -5.07],
        [-3.85, -3.96, -4.07, -4.19, -4.30, -4.42, -4.53, -4.65],
        [-3.50, -3.54, -3.66, -3.77, -3.88, -4.00, -4.11, -4.23],
        [-3.34, -3.28, -3.24, -3.35, -3.47, -3.58, -3.69, -3.81],
    ]),
    field: {minX: -2277.5, maxX: -2253.5, minZ: 697, maxZ: 731, center: {x: -2265.5, y: -4.6, z: 714}},
    // Hay bales at the field's corners mark it out.
    bales: [
        {x: -2279.5, y: -5.87, z: 695.0}, {x: -2252.5, y: -6.9, z: 695.0},
        {x: -2279.5, y: -3.45, z: 733.5}, {x: -2252.5, y: -3.68, z: 733.5},
    ],
};

// Heights from the prejimka model's own ground patches (Object21, Object23,
// Object27, Object30), which lie over the mod's countryside mesh scene_fk.
const FARMSTEAD = {
    place: "the barn of the farmstead north-west of the hills, over the creek bridge",
    // Outside the barn's west door, 3.5 m from the barrels at its north post.
    signOn: {x: -854.0, y: 19.88, z: 3494.5},
    // In the paddock between the field and the crates, noses north towards
    // the gap; ground 19.51/19.57 and 19.67/19.71 under the ends.
    spots: [
        {position: {x: -871.0, y: 19.54, z: 3490.0}, heading: 0},
        {position: {x: -866.5, y: 19.69, z: 3490.0}, heading: 0},
    ],
    // In the paddock's gap, between the barn and the field.
    loading: {x: -859.5, y: 20.12, z: 3499.0},
    plots: plots(-897.5, 3, 3478, 5, [
        [19.45, 19.49, 19.57, 19.60, 19.66, 19.73, 19.79, 19.76],
        [19.46, 19.46, 19.46, 19.48, 19.51, 19.54, 19.56, 19.58],
        [19.48, 19.48, 19.48, 19.47, 19.49, 19.51, 19.52, 19.50],
        [19.49, 19.48, 19.48, 19.48, 19.48, 19.49, 19.50, 19.51],
        [19.48, 19.48, 19.47, 19.47, 19.48, 19.49, 19.50, 19.50],
        [19.48, 19.47, 19.47, 19.47, 19.48, 19.49, 19.50, 19.53],
        [19.47, 19.47, 19.47, 19.47, 19.49, 19.50, 19.51, 19.57],
    ]),
    field: {minX: -899, maxX: -875, minZ: 3476, maxZ: 3510, center: {x: -887, y: 19.48, z: 3493}},
    // Bales at the field's corners, each 1.5 m or more clear of the paddock
    // fences and the stable (Object119, south of the field).
    bales: [
        {x: -898.5, y: 19.57, z: 3473.0}, {x: -873.0, y: 19.85, z: 3475.0},
        {x: -901.5, y: 19.47, z: 3508.0}, {x: -873.5, y: 19.68, z: 3510.0},
    ],
};

const COOP = {id: "coop", name: "the village co-op", label: "TOBACCO BUYER", mapLabel: "Tobacco buyer", position: {x: -1484.1, y: 14.6, z: 725.1}, rate: 1};
const DOCKS = {id: "docks", name: "the container docks", label: "TOBACCO EXPORT", mapLabel: "Tobacco export", position: maps.landmark("docks").position, rate: 1.7, risky: true};
// The farmstead is 4.4 km from the co-op and 5.8 km from the docks by road,
// where the old Lost Heaven Extended barn was 1.3 and 1.7 km away. The
// longer drive pays a little more, but a full load stays near the other
// jobs' biggest calls for its 15-20 minutes: about $970 at the co-op and
// $1,320 at the docks, whose run is the risky one.
const FARMS = {
    freekrajina: {site: LOWLANDS, buyers: [COOP]},
    freeride_extended: {site: FARMSTEAD, buyers: [{...COOP, rate: 1.1}, {...DOCKS, rate: 1.5}]},
};
for (const {site} of Object.values(FARMS)) {
    if (site.plots.length !== PLOT_COUNT) throw new Error("farm: every site needs the same plots");
}

// Growing time of each stage with water: seedling, young, leafy; then ripe.
const STAGE_MS = [70_000, 90_000, 90_000];
const RIPE = STAGE_MS.length;
// How each stage looks (scale of the bush); a plant short of water or eaten
// by hornworms droops.
const LOOKS = [{x: 0.22, y: 0.22}, {x: 0.45, y: 0.5}, {x: 0.7, y: 0.85}, {x: 0.85, y: 1.25}];
const DROOP = 0.7;
// A dry plant keeps its quality this long, then loses it.
const DRY_GRACE_MS = 60_000;
const WILT_PER_MS = 15 / 60_000;
const PEST_PER_MS = 20 / 60_000;
const PEST_CHANCE = 0.3;
const RIPE_KEEP_MS = 15 * 60_000;
const ROT_PER_MS = 10 / 60_000;
const CURE_MS = 150_000;
const MAX_PLANTS = 8;
const SEEDLING_PRICE = 10;
const BASE_PRICE = 110;
const NEIGHBOUR_TIP = 5;
const WATER_MS = 2500;
const PICK_MS = 2000;
const HARVEST_MS = 2000;
const HOSE_TO_TRUCK = 30;
const HOSE_REACH = 9;
const HOSE_PRESSURE = 11;
const TOUCH = 2.2;
const PLANT_REACH = 3;
const DROP_RADIUS = 7;
const LOAD_RADIUS = 9;
const LOAD_MS = 6000;
const STOP_RADIUS = 16;
const RUMOUR_RADIUS = 900;
const LABEL_RANGE = 25;
const COMPASS_PRIORITY = 45;
const TICK_MS = 250;
const CARD_MS = 1000;
const SAVE_MS = 15_000;
const MAX_STEP_MS = 2000;
const NOTICE_EVERY_MS = 45_000;
const XP = {plant: 2, water: 3, pick: 4, harvest: 6, sell: 10};

const farmers = new Map();
// plot id -> plant
const field = new Map();
// player record key -> {name, leaves, curing, cured}
const stocks = new Map();
// vehicle id -> the tobacco on it
const cargo = new Map();
// player id -> a load going onto their truck
const loading = new Map();
// plot id -> {object, look, retryAt}
const objects = new Map();
let bales = [];
let objectsGeneration = -1;
// player id -> what they were last shown
const shown = new Map();
const notices = new Map();
let lastCargoMarkers = "";
let cargoItems = [];
let lastTick = 0;
let lastCardAt = 0;
let lastSaveAt = 0;
let dirty = false;
let wasRaining = false;
let timer = null;

function available() {
    return typeof MapObject !== "undefined" && typeof WaterJet !== "undefined";
}

function farm() {
    return inFreeRide() && available() ? FARMS[World.getMission()] ?? null : null;
}

function buyers() {
    return farm()?.buyers ?? [];
}

// The current map's farm site; only asked while a map with the farm plays.
function site() {
    return farm()?.site ?? null;
}

const station = stations.define({
    id: "farm",
    title: "Tobacco farm",
    crew: "farmhands",
    mapLabel: "Tobacco farm",
    color: LEAF,
    radar: 0xa7b85a,
    model: TRUCK,
    vehicleLabel: "FARM TRUCK",
    pitch: "Grow tobacco: plant seedlings in the field beside the barn, water them with a farm truck's hose as they grow, pick the leaves " +
        "and cure them here, then drive the tobacco to a buyer. Press G here to sign on.",
    place: () => (site() ? {signOn: site().signOn, spots: site().spots} : null),
    available,
    onDuty: (player) => farmers.has(player.id),
    start: (player) => startDuty(player),
    stop: (player) => endDuty(player, "Off duty. Your plants keep growing."),
    // A loaded truck can be taken by anyone: that is the risk of the run.
    mayDrive: (_player, vehicle) => cargo.has(vehicle.id),
});

/* Records */

function stockOf(key, ownerName = null) {
    let stock = stocks.get(key);
    if (!stock) {
        stock = {name: ownerName ?? "", leaves: {n: 0, q: 0}, curing: [], cured: {n: 0, q: 0}};
        stocks.set(key, stock);
    }
    if (ownerName) stock.name = ownerName;
    return stock;
}

function load() {
    const saved = store.load(STORE) ?? {};
    for (const plant of saved.plants ?? []) {
        // Plots are kept by index, the same row and place on every site.
        if (Number.isInteger(plant.plot) && plant.plot >= 0 && plant.plot < PLOT_COUNT && typeof plant.ownerKey === "string") field.set(plant.plot, plant);
    }
    for (const [key, stock] of Object.entries(saved.stocks ?? {})) {
        stocks.set(key, {name: stock.name ?? "", leaves: stock.leaves ?? {n: 0, q: 0}, curing: stock.curing ?? [], cured: stock.cured ?? {n: 0, q: 0}});
    }
}

function persist() {
    const kept = [...stocks].filter(([, stock]) => stock.leaves.n || stock.curing.length || stock.cured.n);
    store.save(STORE, {plants: [...field.values()], stocks: Object.fromEntries(kept)});
    dirty = false;
    lastSaveAt = Date.now();
}

function crewTag(player) {
    return player.state.get("fr.crew")?.tag ?? null;
}

function sameCrew(player, tag) {
    return Boolean(tag) && crewTag(player) === tag;
}

function ownerOnline(key) {
    return World.getPlayers().find((player) => profiles.keyOf(player) === key) ?? null;
}

// A toast to a plant's owner, at most once in a while for each kind of news.
function notice(key, kind, message, tone = "gold") {
    const player = ownerOnline(key);
    if (!player) return;
    const id = `${key}:${kind}`;
    if (Date.now() - (notices.get(id) ?? 0) < NOTICE_EVERY_MS) return;
    notices.set(id, Date.now());
    toast(player, message, {title: "Tobacco farm", tone});
}

function plural(count, word) {
    return `${count} ${word}${count === 1 ? "" : "s"}`;
}

function quality(entry) {
    return entry.n ? Math.round(entry.q / entry.n) : 0;
}

/* The field */

function inField(position) {
    const area = site().field;
    return position.x >= area.minX && position.x <= area.maxX && position.z >= area.minZ && position.z <= area.maxZ &&
        Math.abs(position.y - area.center.y) < 6;
}

function plotPosition(plot) {
    const spot = site().plots[plot];
    return {x: spot.x, y: spot.y, z: spot.z};
}

function mine(player, plant) {
    return profiles.keyOf(player) === plant.ownerKey || sameCrew(player, plant.ownerCrew);
}

function ownPlants(key) {
    return [...field.values()].filter((plant) => plant.ownerKey === key);
}

function plantsNear(position, reach) {
    return [...field.values()].map((plant) => ({plant, gap: flatDistance(plotPosition(plant.plot), position)}))
        .filter((entry) => entry.gap <= reach).sort((a, b) => a.gap - b.gap);
}

// The plot the player stands on, when it is free.
function freePlot(position) {
    const nearest = site().plots.map((spot) => ({spot, gap: flatDistance(spot, position)})).sort((a, b) => a.gap - b.gap)[0];
    return nearest && nearest.gap <= PLANT_REACH && !field.has(nearest.spot.id) ? nearest.spot : null;
}

function rollPest(stage) {
    return stage > 0 && Math.random() < PEST_CHANCE ? STAGE_MS[stage] * (0.2 + Math.random() * 0.6) : null;
}

function wilting(plant) {
    return plant.pest || (plant.dry && plant.dryMs > DRY_GRACE_MS);
}

function isRaining() {
    const weather = World.getWeather?.();
    return weather?.weather === "rain" && (weather.intensity ?? 50) >= 20;
}

function water(plant) {
    plant.dry = false;
    plant.dryMs = 0;
    dirty = true;
}

function remove(plant) {
    field.delete(plant.plot);
    const record = objects.get(plant.plot);
    record?.object?.destroy();
    objects.delete(plant.plot);
    dirty = true;
}

function grow(dt) {
    const raining = isRaining();
    if (raining && !wasRaining && [...field.values()].some((plant) => plant.dry)) {
        for (const id of farmers.keys()) {
            const player = playerById(id);
            if (player) toast(player, "Rain! It waters the whole field for you.", {title: "Tobacco farm"});
        }
    }
    wasRaining = raining;
    for (const plant of [...field.values()]) {
        if (plant.stage >= RIPE) {
            plant.ripeMs += dt;
            if (plant.ripeMs > RIPE_KEEP_MS) plant.quality -= ROT_PER_MS * dt;
        } else {
            if (plant.dry && raining) water(plant);
            if (plant.dry) {
                plant.dryMs += dt;
                if (plant.dryMs > DRY_GRACE_MS) plant.quality -= WILT_PER_MS * dt;
            }
            if (plant.pest) plant.quality -= PEST_PER_MS * dt;
            if (!plant.dry && !plant.pest) {
                plant.grown += dt;
                if (plant.pestAt !== null && plant.grown >= plant.pestAt) {
                    plant.pest = true;
                    plant.pestAt = null;
                    notice(plant.ownerKey, "pest", "Hornworms are eating one of your plants! Hold G at it to pick them off.", "bad");
                }
                if (plant.grown >= STAGE_MS[plant.stage]) {
                    plant.stage++;
                    plant.grown = 0;
                    if (plant.stage >= RIPE) {
                        plant.ripeMs = 0;
                        notice(plant.ownerKey, "ripe", "Your tobacco is ripe. Hold G at it to pick the leaves before they spoil.", "good");
                    } else {
                        plant.dry = true;
                        plant.dryMs = 0;
                        plant.pestAt = rollPest(plant.stage);
                    }
                }
            }
        }
        if (plant.quality <= 0) {
            remove(plant);
            notice(plant.ownerKey, "withered", "One of your tobacco plants withered.", "bad");
        }
    }
    if (field.size) dirty = true;
}

function lookOf(plant) {
    const look = LOOKS[Math.min(plant.stage, RIPE)];
    return wilting(plant) ? {x: look.x, y: look.y * DROOP, z: look.x} : {x: look.x, y: look.y, z: look.x};
}

// The plants as map objects every client sees (no collision: you walk
// through the rows), remade when a plant changes its look.
function syncObjects(now) {
    if (!World.isReady()) return;
    const generation = World.getMissionGeneration();
    if (generation !== objectsGeneration) {
        // The mission change removed them.
        objects.clear();
        bales = [];
        objectsGeneration = generation;
    }
    if (!bales.length) {
        bales = site().bales.map((spot, index) => MapObject.create(BALE_MODEL, spot, {x: 0, y: index * 1.3, z: 0}, {virtualWorld: MAIN_WORLD}))
            .filter(Boolean);
    }
    for (const [plot, record] of objects) {
        if (field.has(plot)) continue;
        record.object?.destroy();
        objects.delete(plot);
    }
    for (const plant of field.values()) {
        const scale = lookOf(plant);
        const look = `${scale.x}:${scale.y}`;
        const record = objects.get(plant.plot);
        if (record && (record.look === look || now < record.retryAt)) continue;
        record?.object?.destroy();
        const spot = site().plots[plant.plot];
        const object = MapObject.create(PLANT_MODEL, {x: spot.x, y: spot.y - 0.05, z: spot.z}, {x: 0, y: plant.yaw, z: 0},
            {virtualWorld: MAIN_WORLD, collision: false, scale});
        objects.set(plant.plot, {object, look: object ? look : null, retryAt: now + 2000});
    }
}

function clearObjects() {
    for (const record of objects.values()) record.object?.destroy();
    objects.clear();
    for (const bale of bales) bale.destroy();
    bales = [];
}

/* The barn */

function cure(dt) {
    for (const [key, stock] of stocks) {
        if (!stock.curing.length) continue;
        let done = null;
        for (const batch of stock.curing) {
            batch.left -= dt;
            if (batch.left > 0) continue;
            stock.cured.n += batch.n;
            stock.cured.q += batch.q;
            done = (done ?? 0) + batch.n;
        }
        stock.curing = stock.curing.filter((batch) => batch.left > 0);
        dirty = true;
        if (done) {
            const player = ownerOnline(key);
            if (player) {
                toast(player, `${plural(done, "bundle")} of tobacco cured in the barn. Stop a farm truck at the loading bay beside the barn to load ` +
                    `${stock.cured.n === 1 ? "it" : "them"}.`, {title: "Tobacco farm", tone: "good", seconds: 7});
            }
        }
    }
}

function hang(player, stock) {
    stock.curing.push({n: stock.leaves.n, q: stock.leaves.q, left: CURE_MS});
    toast(player, `${plural(stock.leaves.n, "bundle")} of leaves hung up in the barn to cure. Ready in ${formatMinutes(CURE_MS)}; ` +
        "it cures while you work.", {title: "Tobacco farm", tone: "good"});
    stock.leaves = {n: 0, q: 0};
    dirty = true;
}

/* Cargo */

function priceOf(entry, buyer) {
    return Math.round(BASE_PRICE * buyer.rate * (0.35 + 0.65 * quality(entry) / 100));
}

function roughPosition(vehicle) {
    if (!vehicle) return null;
    const {x, y, z} = vehicle.position;
    return {x: Math.round(x / 10) * 10, y, z: Math.round(z / 10) * 10};
}

function presentCargo(force = false) {
    // The rough position shows the truck on the map of everyone it is not
    // streamed to; the city-wide rumour is the point of the flashing blip.
    const items = [...cargo].filter(([, entry]) => entry.rumoured).map(([vehicleId]) => ({id: `c${vehicleId}`, vehicleId,
        position: roughPosition(vehicleById(vehicleId)), label: "TOBACCO", color: COLORS.red, range: 300, near: 80,
        map: {icon: "triangle", label: "Tobacco truck", size: 6, flash: true}})).filter((item) => item.position);
    const payload = JSON.stringify(items);
    if (!force && payload === lastCargoMarkers) return;
    lastCargoMarkers = payload;
    cargoItems = items;
    markersAll("farm:cargo", items);
}

function returnCargo() {
    for (const entry of cargo.values()) {
        const stock = stockOf(entry.ownerKey, entry.ownerName);
        stock.cured.n += entry.n;
        stock.cured.q += entry.q;
    }
    cargo.clear();
    dirty = true;
}

function sell(driver, vehicle, entry, buyer) {
    cargo.delete(vehicle.id);
    dirty = true;
    const each = priceOf(entry, buyer);
    const total = each * entry.n;
    driver.giveMoney(total);
    profiles.bump(driver, "tobacco", entry.n);
    profiles.addXp(driver, XP.sell * entry.n, "Sold tobacco");
    const farmer = farmers.get(driver.id);
    if (farmer) farmer.earned += total;
    const own = profiles.keyOf(driver) === entry.ownerKey || sameCrew(driver, entry.ownerCrew);
    if (own) {
        toast(driver, `Sold at ${buyer.name}: ${formatMoney(each)} a bundle, ${formatMoney(total)} for ${entry.n} (quality ${quality(entry)}%).`, {title: "Tobacco farm", tone: "good", seconds: 7});
        feed("farm", [name(driver, LEAF), text(` sold ${plural(entry.n, "bundle")} of tobacco at ${buyer.name} · `), text(formatMoney(total), COLORS.gold)]);
    } else {
        toast(driver, `${entry.ownerName}'s tobacco fetches ${formatMoney(total)}. They will not be pleased.`, {title: "Tobacco", tone: "good", seconds: 7});
        feed("farm", [name(driver, COLORS.red), text(` sold ${entry.ownerName}'s tobacco at ${buyer.name} · `), text(formatMoney(total), COLORS.gold)]);
        const owner = ownerOnline(entry.ownerKey);
        if (owner) toast(owner, `${driver.nickname} took your truck and sold your tobacco at ${buyer.name}.`, {title: "Tobacco farm", tone: "bad", seconds: 8});
    }
    presentCargo();
}

function tickCargo() {
    let changed = false;
    for (const [vehicleId, entry] of [...cargo]) {
        const vehicle = vehicleById(vehicleId);
        if (!vehicle) {
            // Towed back from where it was left (lib/stations.js): the load
            // goes back into the barn.
            cargo.delete(vehicleId);
            const stock = stockOf(entry.ownerKey, entry.ownerName);
            stock.cured.n += entry.n;
            stock.cured.q += entry.q;
            dirty = changed = true;
            notice(entry.ownerKey, "towed", "Your loaded truck was left standing, so it was towed back and unloaded at the barn.");
            continue;
        }
        if (vehicle.terminalState !== 0) {
            cargo.delete(vehicleId);
            dirty = changed = true;
            notice(entry.ownerKey, "lost", "The farm truck carrying your tobacco is wrecked. The load is lost.", "bad");
            feed("farm", [text("Tobacco", LEAF), text(` · ${entry.ownerName}'s load went up with its truck`)]);
            continue;
        }
        if (!entry.rumoured && buyers().some((buyer) => buyer.risky && flatDistance(vehicle.position, buyer.position) <= RUMOUR_RADIUS)) {
            entry.rumoured = changed = true;
            const driver = vehicle.getDriver();
            toastAll(`A farm truck loaded with tobacco is heading for the docks. Whoever drives it in gets paid for the load.`,
                {title: "Tobacco", tone: "gold", seconds: 7});
            feed("farm", [text("Tobacco", LEAF), text(" · a loaded farm truck is heading for the docks"),
                ...(driver ? [text(" · "), name(driver, COLORS.white)] : [])]);
        }
        const driver = vehicle.getDriver();
        if (!driver || !playable(driver) || speedOf(vehicle) > 2) continue;
        const buyer = buyers().find((candidate) => flatDistance(vehicle.position, candidate.position) <= STOP_RADIUS);
        if (buyer) sell(driver, vehicle, entry, buyer);
    }
    // Unchanged items are not resent; a rumoured truck's rough position moves.
    presentCargo(changed);
}

// Leaves are hung up by walking (or driving) to the barn's arrow; cured
// tobacco goes onto a farm truck stopped at the loading bay.
function tickBarn(now) {
    for (const player of World.getPlayers()) {
        const key = profiles.keyOf(player);
        const stock = key ? stocks.get(key) : null;
        if (!stock || !playable(player) || player.virtualWorld !== MAIN_WORLD) {
            loading.delete(player.id);
            continue;
        }
        const {signOn, loading: bay} = site();
        if (stock.leaves.n && flatDistance(player.position, signOn) <= DROP_RADIUS && Math.abs(player.position.y - signOn.y) < 4) hang(player, stock);
        const vehicle = player.getSeat() === 0 ? player.getVehicle() : null;
        const pending = loading.get(player.id);
        const atBay = Boolean(vehicle) && stock.cured.n > 0 && flatDistance(vehicle.position, bay) <= LOAD_RADIUS && speedOf(vehicle) < 1.5;
        if (!atBay) {
            if (pending) {
                loading.delete(player.id);
                jobkey.prompt(player, null);
                if (!pending.refused && vehicle && stock.cured.n > 0) toast(player, "You pulled away before the tobacco was loaded.", {title: "Tobacco farm", tone: "bad"});
            }
            continue;
        }
        if (!station.isVehicle(vehicle)) {
            if (!pending?.refused) {
                loading.set(player.id, {vehicleId: vehicle.id, refused: true});
                toast(player, "The bales go on a farm truck: bring one of the farm's two trucks.", {title: "Tobacco farm", tone: "bad"});
            }
            continue;
        }
        const aboard = cargo.get(vehicle.id);
        if (aboard && aboard.ownerKey !== key) {
            if (!pending?.refused) {
                loading.set(player.id, {vehicleId: vehicle.id, refused: true});
                toast(player, `This truck already carries ${aboard.ownerName}'s tobacco.`, {title: "Tobacco farm", tone: "bad"});
            }
            continue;
        }
        if (!pending || pending.refused || pending.vehicleId !== vehicle.id) {
            loading.set(player.id, {vehicleId: vehicle.id, startedAt: now});
            continue;
        }
        const progress = Math.min(1, (now - pending.startedAt) / LOAD_MS);
        jobkey.prompt(player, "LOADING THE TOBACCO… HOLD STILL", progress);
        if (progress < 1) continue;
        loading.delete(player.id);
        jobkey.prompt(player, null);
        const entry = aboard ?? {ownerKey: key, ownerName: player.nickname, ownerCrew: crewTag(player), n: 0, q: 0, rumoured: false};
        entry.n += stock.cured.n;
        entry.q += stock.cured.q;
        cargo.set(vehicle.id, entry);
        stock.cured = {n: 0, q: 0};
        dirty = true;
        const offers = buyers().map((buyer) => `${formatMoney(priceOf(entry, buyer))} a bundle at ${buyer.name}`).join(", ");
        toast(player, `${plural(entry.n, "bundle")} loaded: ${offers}.` +
            (buyers().some((buyer) => buyer.risky) ? " Near the docks the whole city will know, and anyone can take the truck." : ""),
            {title: "Tobacco farm", tone: "gold", seconds: 10});
    }
}

/* Duty */

function stopAction(player, farmer) {
    farmer.action = null;
    farmer.jet?.stop();
    farmer.hint = undefined;
    jobkey.prompt(player, null);
}

function endDuty(player, message) {
    const farmer = farmers.get(player.id);
    if (!farmer) return;
    farmers.delete(player.id);
    jobkey.release(player);
    jobkey.prompt(player, null);
    farmer.jet?.destroy();
    life.end(player, "farm");
    profiles.setPublic(player, "fr.job", null);
    if (World.getPlayers().some((other) => other.id === player.id)) {
        // Off duty nobody drives an empty farm truck: the player gets out first.
        const vehicle = player.getVehicle();
        if (player.getSeat() === 0 && station.isVehicle(vehicle) && !cargo.has(vehicle.id)) player.removeFromVehicle();
        present(player, true);
    }
    station.refresh();
    if (message) toast(player, `${message} You earned ${formatMoney(farmer.earned)} this shift.`, {title: "Tobacco farm"});
}

function startDuty(player) {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) return;
    if (!farm()) {
        toast(player, "The tobacco farm is in the countryside: play Lost Heaven Extended or The Countryside (/map).", {tone: "bad"});
        return;
    }
    if (player.getVehicle() || flatDistance(player.position, site().signOn) > stations.SIGN_ON_RADIUS + 1) {
        // Asked again, the compass stops showing the way.
        if (!station.guide(player)) {
            toast(player, "The compass no longer shows the way.", {title: "Tobacco farm", seconds: 3});
            return;
        }
        toast(player, `Sign on at ${site().place}: follow the compass to the arrow and press G. ` +
            "Hold TAB to see it on the map.", {title: "Tobacco farm", tone: "gold", seconds: 8});
        return;
    }
    if (!profiles.keyOf(player)) return;
    if (!life.begin(player, "farm", "Farm work")) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    farmers.set(player.id, {earned: 0, jet: null, action: null, hint: undefined});
    profiles.setPublic(player, "fr.job", "farm");
    feed("farm", [name(player, LEAF), text(" went to work on the tobacco farm")]);
    toast(player, `You are on the farm. Walk into the field beside the barn and press G to plant a seedling (${formatMoney(SEEDLING_PRICE)}, ` +
        `up to ${MAX_PLANTS}). They need water at every stage: drive a farm truck up to the field and hold G by the dry ones. ` +
        "Hold G at a ripe plant to pick it, carry the leaves to this arrow to cure, then load a farm truck at the loading bay and sell. " +
        "Back under the arrow, G clocks you off.", {title: "Tobacco farm", tone: "gold", seconds: 14});
    present(player, true);
}

function truckNear(player) {
    const truck = station.nearestVehicle(player.position);
    return truck && truck.gap <= HOSE_TO_TRUCK ? truck : null;
}

function dryInReach(player) {
    return plantsNear(player.position, HOSE_REACH).find((entry) => entry.plant.dry)?.plant ?? null;
}

// What G does where the player stands, strongest first.
function chooseAction(player) {
    const close = plantsNear(player.position, TOUCH);
    const ripe = close.find((entry) => entry.plant.stage >= RIPE)?.plant;
    if (ripe) return {kind: "harvest", plant: ripe};
    const pest = close.find((entry) => entry.plant.pest)?.plant;
    if (pest) return {kind: "pick", plant: pest};
    // Standing on an empty plot plants it; standing by a planted one waters.
    const plot = inField(player.position) ? freePlot(player.position) : null;
    if (plot && ownPlants(profiles.keyOf(player)).length < MAX_PLANTS) return {kind: "plant", plot};
    const dry = dryInReach(player);
    if (dry) return {kind: "water", plant: dry};
    return null;
}

function hintFor(player) {
    if (player.getVehicle()) return null;
    const action = chooseAction(player);
    if (!action) return null;
    switch (action.kind) {
        case "harvest": return mine(player, action.plant) ? "[G] HOLD TO PICK THE LEAVES" : `${action.plant.ownerName.toUpperCase()}'S TOBACCO`;
        case "pick": return "[G] HOLD TO PICK OFF THE HORNWORMS";
        case "water": return truckNear(player) ? "[G] HOLD TO WATER" : `DRY · BRING A FARM TRUCK WITHIN ${HOSE_TO_TRUCK} M`;
        default: return `[G] PLANT A SEEDLING · ROW ${action.plot.row} · ${formatMoney(SEEDLING_PRICE)}`;
    }
}

function plantSeedling(player, plot) {
    const key = profiles.keyOf(player);
    if (!key) return;
    if (!player.trySpendMoney(SEEDLING_PRICE)) {
        toast(player, `A seedling costs ${formatMoney(SEEDLING_PRICE)}.`, {title: "Tobacco farm", tone: "bad", seconds: 3});
        return;
    }
    field.set(plot.id, {plot: plot.id, ownerKey: key, ownerName: player.nickname, ownerCrew: crewTag(player), stage: 0, grown: 0,
        dry: true, dryMs: 0, pest: false, pestAt: null, ripeMs: 0, quality: 100, yaw: Math.random() * Math.PI * 2});
    dirty = true;
    profiles.addXp(player, XP.plant, "Planted tobacco");
    const count = ownPlants(key).length;
    if (count === MAX_PLANTS) {
        toast(player, `That is ${MAX_PLANTS} plants, as many as one farmhand can tend.`, {title: "Tobacco farm", seconds: 5});
    } else if (count === 1) {
        toast(player, "Planted. A seedling grows only once watered: bring a farm truck within 30 m and hold G by it.", {title: "Tobacco farm", seconds: 7});
    }
}

function finishAction(player, farmer, action, plant) {
    if (action.kind === "water") {
        water(plant);
        profiles.addXp(player, XP.water, "Watered tobacco");
        // Crewmates' plants count as one's own: no tip between them.
        if (!mine(player, plant)) {
            player.giveMoney(NEIGHBOUR_TIP);
            farmer.earned += NEIGHBOUR_TIP;
        }
        return;
    }
    if (action.kind === "pick") {
        plant.pest = false;
        dirty = true;
        profiles.addXp(player, XP.pick, "Picked off hornworms");
        return;
    }
    const key = profiles.keyOf(player);
    const stock = stockOf(key, player.nickname);
    const grade = Math.max(1, Math.round(plant.quality));
    stock.leaves.n++;
    stock.leaves.q += grade;
    remove(plant);
    profiles.addXp(player, XP.harvest, "Picked tobacco");
    if (stock.leaves.n === 1) {
        toast(player, `Leaves picked (quality ${grade}%). Carry them to the barn's arrow: they cure there.`, {title: "Tobacco farm", seconds: 6});
    }
}

function progressAction(player, farmer, dt) {
    const action = farmer.action;
    if (!jobkey.isHeld(player) || player.getVehicle()) {
        stopAction(player, farmer);
        return;
    }
    if (action.kind === "water") {
        if (!truckNear(player)) {
            stopAction(player, farmer);
            toast(player, `The hose runs from a farm truck's tank and reaches ${HOSE_TO_TRUCK} m from it.`, {title: "Tobacco farm", tone: "bad", seconds: 4});
            return;
        }
        let plant = field.get(action.plot);
        if (!plant || !plant.dry || flatDistance(plotPosition(plant.plot), player.position) > HOSE_REACH) {
            plant = dryInReach(player);
            action.plot = plant?.plot ?? null;
            action.progress = 0;
        }
        if (!plant) {
            farmer.jet?.stop();
            jobkey.prompt(player, "NOTHING DRY IN REACH · RELEASE G");
            return;
        }
        const spot = plotPosition(plant.plot);
        farmer.jet.aimAt({x: spot.x, y: spot.y + 0.3, z: spot.z});
        action.progress += dt;
        jobkey.prompt(player, "WATERING…", Math.min(1, action.progress / WATER_MS));
        if (action.progress >= WATER_MS) {
            finishAction(player, farmer, action, plant);
            action.plot = null;
            action.progress = 0;
        }
        return;
    }
    const plant = field.get(action.plot);
    const valid = plant && flatDistance(plotPosition(plant.plot), player.position) <= TOUCH + 0.8 &&
        (action.kind === "harvest" ? plant.stage >= RIPE : plant.pest);
    if (!valid) {
        stopAction(player, farmer);
        return;
    }
    action.progress += dt;
    const needed = action.kind === "harvest" ? HARVEST_MS : PICK_MS;
    jobkey.prompt(player, action.kind === "harvest" ? "PICKING THE LEAVES…" : "PICKING OFF THE HORNWORMS…", Math.min(1, action.progress / needed));
    if (action.progress >= needed) {
        finishAction(player, farmer, action, plant);
        stopAction(player, farmer);
    }
}

function tickFarmers(dt) {
    for (const [id, farmer] of farmers) {
        const player = playerById(id);
        if (!player) {
            farmers.delete(id);
            continue;
        }
        // Another module took the player off the job: an arrest and the jail.
        if (life.activity(player)?.kind !== "farm") {
            endDuty(player, "Your shift is over.");
            continue;
        }
        if (!playable(player)) {
            if (farmer.action) stopAction(player, farmer);
            continue;
        }
        if (farmer.action) {
            progressAction(player, farmer, dt);
            continue;
        }
        if (loading.has(player.id)) continue;
        const hint = hintFor(player);
        if (hint !== farmer.hint) {
            farmer.hint = hint;
            jobkey.prompt(player, hint);
        }
    }
}

jobkey.register({
    press(player) {
        const farmer = farmers.get(player.id);
        if (!farmer || !playable(player) || player.getVehicle() || !farm()) return false;
        const action = chooseAction(player);
        if (!action) return false;
        if (action.kind === "plant") {
            plantSeedling(player, action.plot);
            farmer.hint = undefined;
            return true;
        }
        if (action.kind === "harvest" && !mine(player, action.plant)) {
            toast(player, `This is ${action.plant.ownerName}'s plant. Water it or pick off its hornworms if you like; the leaves are theirs.`,
                {title: "Tobacco farm", tone: "bad", seconds: 4});
            return true;
        }
        if (action.kind === "water") {
            if (!truckNear(player)) {
                toast(player, `The hose runs from a farm truck: park one within ${HOSE_TO_TRUCK} m of the plants.`, {title: "Tobacco farm", tone: "bad", seconds: 4});
                return true;
            }
            farmer.jet ??= WaterJet.create(player, {pressure: HOSE_PRESSURE});
            if (!farmer.jet) {
                toast(player, "No hose free right now. Try again shortly.", {tone: "bad", seconds: 3});
                return true;
            }
        }
        farmer.action = {kind: action.kind, plot: action.plant.plot, progress: 0};
        return true;
    },
    release(player) {
        const farmer = farmers.get(player.id);
        if (farmer?.action) stopAction(player, farmer);
    },
});

/* What each player sees */

function nextStep(player, key, stock, plants) {
    const ripe = plants.filter((plant) => plant.stage >= RIPE).length;
    const pests = plants.filter((plant) => plant.pest).length;
    const dry = plants.filter((plant) => plant.dry).length;
    if (ripe) return "Hold G at the ripe plants to pick the leaves";
    if (pests) return "Hornworms! Hold G at the plant to pick them off";
    if (dry) return truckNear(player) ? "Hold G near the dry plants to water them" : "Bring a farm truck to the field: its hose waters the plants";
    if (stock?.leaves.n) return "Carry the leaves to the barn's arrow to cure them";
    if (stock?.cured.n) return "Stop a farm truck at the loading bay by the barn";
    if (plants.length < MAX_PLANTS) return "Walk into the field and press G to plant";
    if (stock?.curing.length) return "Curing in the barn";
    return "Growing. Keep them watered";
}

function farmRows(player, farmer, key, stock, plants) {
    const rows = [];
    if (plants.length) {
        const ripe = plants.filter((plant) => plant.stage >= RIPE).length;
        const dry = plants.filter((plant) => plant.dry).length;
        const pests = plants.filter((plant) => plant.pest).length;
        const status = [dry ? `${dry} dry` : null, pests ? `${pests} hornworms` : null, ripe ? `${ripe} ripe` : null].filter(Boolean).join(" · ");
        rows.push(["Your plants", `${plants.length}/${MAX_PLANTS}${status ? ` · ${status}` : ""}`]);
        const growing = plants.filter((plant) => plant.stage < RIPE && !plant.dry && !plant.pest);
        if (growing.length) rows.push(["Next stage", formatMinutes(Math.min(...growing.map((plant) => STAGE_MS[plant.stage] - plant.grown)))]);
        rows.push(["Quality", `${Math.round(plants.reduce((sum, plant) => sum + plant.quality, 0) / plants.length)}%`]);
    }
    if (stock?.leaves.n) rows.push(["Leaves picked", `${stock.leaves.n} · ${quality(stock.leaves)}%`]);
    if (stock?.curing.length) {
        rows.push(["Curing", `${stock.curing.reduce((sum, batch) => sum + batch.n, 0)} · ${formatMinutes(Math.min(...stock.curing.map((batch) => batch.left)))}`]);
    }
    if (stock?.cured.n) rows.push(["Cured", `${stock.cured.n} ready to load`]);
    if (isRaining() && plants.length) rows.push(["Weather", "rain is watering the field"]);
    rows.push(["This shift", formatMoney(farmer.earned)]);
    return rows;
}

function markerItems(player, farmer, stock, entry) {
    const items = [];
    if (entry) {
        for (const buyer of buyers()) {
            items.push({id: buyer.id, position: buyer.position, label: `${buyer.label} · ${formatMoney(priceOf(entry, buyer) * entry.n)}`,
                color: buyer.risky ? COLORS.red : LEAF, primary: true, map: {icon: "diamond", label: buyer.mapLabel, edge: true}});
        }
        return items;
    }
    if (stock?.cured.n) {
        items.push({id: "load", position: site().loading, label: "LOADING BAY", hint: "STOP A FARM TRUCK HERE", color: LEAF, range: 300, near: 60,
            map: {icon: "dot", label: "Loading bay", size: 5}});
    }
    if (!farmer) return items;
    items.push({id: "field", position: site().field.center, label: "TOBACCO FIELD", color: LEAF, range: 300, near: 45,
        map: {icon: "square", label: "Tobacco field", size: 5}});
    // Only plants losing quality are labelled; the rest show by their size
    // and the card counts them.
    for (const {plant} of plantsNear(player.position, LABEL_RANGE)) {
        if (!wilting(plant)) continue;
        const spot = plotPosition(plant.plot);
        items.push({id: `p${plant.plot}`, position: {x: spot.x, y: spot.y + 0.2, z: spot.z}, label: plant.pest ? "HORNWORMS" : "WILTING",
            color: plant.pest ? COLORS.red : COLORS.blue, range: LABEL_RANGE, near: 14, map: false});
    }
    return items;
}

function compassTarget(player, farmer, stock, entry, plants) {
    if (entry) {
        const nearest = buyers().map((buyer) => ({buyer, gap: flatDistance(buyer.position, player.position)})).sort((a, b) => a.gap - b.gap)[0];
        return nearest ? nearest.buyer.position : null;
    }
    if (!farmer) return null;
    const {signOn, loading: bay, field: area} = site();
    if (stock?.leaves.n && !plants.some((plant) => plant.stage >= RIPE)) return signOn;
    if (stock?.cured.n && !plants.some((plant) => plant.stage >= RIPE || plant.dry || plant.pest)) return bay;
    if (flatDistance(player.position, area.center) > 45 && (!plants.length || plants.some((plant) => plant.dry || plant.pest || plant.stage >= RIPE))) {
        return area.center;
    }
    return null;
}

// The card, compass and markers of one player; only changes are sent.
function present(player, force = false) {
    const farmer = farmers.get(player.id) ?? null;
    const key = profiles.keyOf(player);
    const stock = key ? stocks.get(key) ?? null : null;
    const vehicle = player.getSeat() === 0 ? player.getVehicle() : null;
    const entry = vehicle && farm() ? cargo.get(vehicle.id) ?? null : null;
    const active = Boolean(farm()) && player.virtualWorld === MAIN_WORLD;
    const plants = key ? ownPlants(key) : [];
    const last = shown.get(player.id) ?? {markers: null, compass: null, card: false};
    if (force) {
        last.markers = null;
        last.compass = null;
    }
    const items = active ? markerItems(player, farmer, stock, entry) : [];
    const payload = JSON.stringify(items);
    if (payload !== last.markers) {
        last.markers = payload;
        markers(player, "farm", items);
    }
    const target = active ? compassTarget(player, farmer, stock, entry, plants) : null;
    const aim = JSON.stringify(target);
    if (aim !== last.compass) {
        last.compass = aim;
        compass(player, "farm", COMPASS_PRIORITY, target ? {position: target} : null);
    }
    if (active && entry) {
        const own = key === entry.ownerKey || sameCrew(player, entry.ownerCrew);
        const rows = [["Load", `${plural(entry.n, "bundle")} · quality ${quality(entry)}%`]];
        for (const buyer of buyers()) {
            rows.push([buyer.risky ? "Docks (risky)" : "Village co-op",
                `${Math.round(flatDistance(buyer.position, vehicle.position))} m · ${formatMoney(priceOf(entry, buyer) * entry.n)}`]);
        }
        if (entry.rumoured) rows.push(["Watch out", "the city knows about this load"]);
        card(player, "farm", {title: "Tobacco", subtitle: own ? "Drive the load to a buyer and stop there" : `${entry.ownerName}'s load: sell it before they catch you`,
            rows, tone: "gold"});
        last.card = true;
    } else if (active && farmer) {
        card(player, "farm", {title: "Tobacco farm", subtitle: nextStep(player, key, stock, plants), rows: farmRows(player, farmer, key, stock, plants), tone: "good"});
        last.card = true;
    } else if (last.card || force) {
        card(player, "farm", null);
        last.card = false;
    }
    shown.set(player.id, last);
}

/* The clock */

function tick() {
    const now = Date.now();
    const dt = Math.min(MAX_STEP_MS, Math.max(0, now - lastTick));
    lastTick = now;
    if (!farm() || !World.isReady()) return;
    grow(dt);
    cure(dt);
    syncObjects(now);
    tickFarmers(dt);
    tickBarn(now);
    tickCargo();
    if (now - lastCardAt >= CARD_MS) {
        lastCardAt = now;
        for (const player of World.getPlayers()) present(player);
    }
    if (dirty && now - lastSaveAt >= SAVE_MS) persist();
}

// Signing on happens under the barn's arrow; the command shows the way there
// and clocks off from anywhere.
commands.register({name: "farm", aliases: ["tobacco", "farmer"], usage: "/farm", help: "Show the way to the tobacco farm, or clock off.", category: "City"}, (player) => {
    if (farmers.has(player.id)) endDuty(player, "Off duty. Your plants keep growing.");
    else startDuty(player);
});

menu.provide("farm", (player) => {
    const key = profiles.keyOf(player);
    const plants = key ? ownPlants(key) : [];
    const stock = key ? stocks.get(key) : null;
    return {available: Boolean(farm()), onDuty: farmers.has(player.id), farmers: farmers.size, plants: plants.length,
        ripe: plants.filter((plant) => plant.stage >= RIPE).length, curing: stock?.curing.reduce((sum, batch) => sum + batch.n, 0) ?? 0,
        cured: stock?.cured.n ?? 0, export: buyers().some((buyer) => buyer.risky), earned: farmers.get(player.id)?.earned ?? 0,
        place: site()?.place ?? null};
});

load();

onStart(() => {
    lastTick = Date.now();
    timer = setInterval(tick, TICK_MS);
});
onStop(() => {
    clearInterval(timer);
    for (const id of [...farmers.keys()]) {
        const player = playerById(id);
        if (player) endDuty(player, null);
    }
    returnCargo();
    persist();
    clearObjects();
});
Events.on("playerSpawn", (player) => {
    shown.delete(player.id);
    if (cargoItems.length) markers(player, "farm:cargo", cargoItems);
});
Events.on("playerDisconnect", (player) => {
    const farmer = farmers.get(player.id);
    farmer?.jet?.destroy();
    farmers.delete(player.id);
    loading.delete(player.id);
    shown.delete(player.id);
});
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) {
        endDuty(player, "Your shift ended with the map change. Your plants wait for you.");
        // Someone driving a load had its card and compass, not a shift.
        card(player, "farm", null);
        compass(player, "farm", COMPASS_PRIORITY, null);
    }
    returnCargo();
    loading.clear();
    shown.clear();
    // The mission's objects went with it.
    objects.clear();
    bales = [];
    lastCargoMarkers = "";
    cargoItems = [];
    persist();
});

module.exports = {onDuty: (player) => farmers.has(player.id)};
