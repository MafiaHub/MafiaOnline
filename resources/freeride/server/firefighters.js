// The fire brigade. A player signs on at the fire station (the stock
// models/9hasic.4ds the city scene places in the Works Quarter, garage doors
// on the street: stand under the arrow on the apron and press G), puts on the
// stock fireman's clothes (Hasic01) and takes one of the stock fire trucks
// parked in front of the doors (lib/stations.js keeps them there). Fires
// break out across the city on the road graph: a house front, a burning car,
// a crash with people trapped in it. Firefighters put them out with the hose
// (hold G within reach of any fire truck, or from a truck's roof cannon while
// at its wheel): the native water jet arcs onto the fire and every hose on it
// takes the flames down, so a crew is faster than one. Trapped civilians are
// led out with G. Everyone sees the fires, the crew on duty on the city map
// too; the pay for one is split by how much water each firefighter put on it.
//
// With a crew on duty bigger calls come in too: a filling station alight,
// whose pumps go up if the flames are not knocked down in time; a fire in a
// bar, where the crew goes in through the door, hoses the room and leads the
// barman and the drinkers out to the street (the medics take them from
// there); and a house set alight on purpose, its arsonist running off down
// the street until a firefighter or a police officer on duty grabs him.
// A pile-up the ambulance answers (ambulance.js) may have a wreck burning.
const roads = require("./lib/roads.js");
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const jobkey = require("./lib/jobkey.js");
const stations = require("./lib/stations.js");
const barRoom = require("./lib/bar_room.js");
const interiors = require("./interiors.js");
const bars = require("./bars.js");
const policeDuty = require("./police_duty.js");
const {toast, toastAll, card, compass, markers, crewMarkers, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, flatDistance, playable, playerById, vehicleById, formatMoney, formatMinutes, headingOf, pick, onStart, onStop, inFreeRide} =
    require("./lib/util.js");

const UNIFORM = "Hasic01.i3d";
const TRUCK = "fire00.i3d";
// The station on the city maps: 9hasic.4ds sits unrotated at
// (-1158.59, -5.92, 306.54) in both FREERIDE and Freeride Extended cache.bin;
// its garage doors (@VRAT02) span x -1171.4..-1142.7 at z 296.5 facing the
// street whose road graph line runs along z 284.4. Two trucks stand on the
// apron in front of the doors, noses to the street, at the apron's height
// (the building's base, -5.92): a parked car nobody drives does not settle
// onto its wheels, and a car's origin is at ground level. The sign-on arrow is
// on the apron beside them.
const CITY_STATION = {signOn: {x: -1147.5, y: -5.9, z: 292.0}, spots: [
    {position: {x: -1158.0, y: -5.85, z: 289.5}, heading: Math.PI},
    {position: {x: -1166.5, y: -5.85, z: 289.5}, heading: Math.PI},
]};
const STATIONS = {freeride: CITY_STATION, freeride_extended: CITY_STATION, freeridenoc: CITY_STATION};
const HOSE_TO_TRUCK = 35;
const HOSE_REACH = 24;
const CANNON_REACH = 32;
const RESCUE_REACH = 3;
const SAFE_DISTANCE = 12;
const TICK_MS = 250;
const CARD_MS = 1000;
const FIRST_FIRE_MS = 90_000;
const FIRE_EVERY_MS = 4 * 60_000;
const QUIET_FIRE_EVERY_MS = 11 * 60_000;
const MAX_INCIDENTS = 2;
const INCIDENT_LIFETIME_MS = 9 * 60_000;
const CAR_BLOWS_AFTER_MS = 4 * 60_000;
const SMOKE_MS = 25_000;
// The big calls come to about $500-700 for one firefighter: the filling
// station $380 + $40 + $160 = $580 saved; the bar $260 + 2 x $40 + 3 x $50
// + $160 = $650 with everyone out.
const PAY = {house: 420, car: 260, crash: 320, pump: 380, bar: 260, pileup: 260};
// Each further fire of a call (the pumps, the rooms of a bar).
const SPOT_PAY = 40;
// For the whole crew, split half by water, half evenly: the pumps saved,
// everyone out of the bar.
const BONUS = {pump: 160, bar: 160};
const RESCUE_PAY = 120;
// Each one led out of a burning bar: three of them, and the bonus besides.
const BAR_RESCUE_PAY = 50;
// A smoke victim out of a bar carries this much more on the medic's fare.
const BAR_MEDIC_BONUS = 40;
const XP = 70;
const RESCUE_XP = 40;
const CIVILIANS = ["Delnik01.i3d", "Delnik02.i3d", "Czena01.i3d", "Czena02.i3d", "Homeless01.i3d"];
const WRECKS = ["fordtfor00.i3d", "chev00.i3d", "forafo00.i3d", "foraca00.i3d"];
// Stock clips (anims\ in a4.dta) the retail panicking walkers use
// (C_human anim_info 337, 331): lying on the ground, crouched.
const LYING = "x panika leh stativ.i3d";
const CROUCHED = "x panika drep stativ.i3d";
// Retail voice lines (Npc.say): 66-69 panic, 75-78 giving up, 85-91 pain.
const PANIC_LINES = [66, 67, 68, 69];
const GIVE_UP_LINES = [75, 76, 77, 78];
const PANIC_EVERY_MS = 9000;

// The stock Free Ride scenes' three filling stations, the same in FREERIDE
// (a1.dta) and freeride_extended (the mod's scene2.bin): the pump
// ("_pumpa_pumpaN", models/xpumpa 2.4ds) and the nearest road graph point
// (data/roads_<map>.js), 9 to 14 m off. The burning car stands on the
// forecourt between them, 40% of the way from the pump to the road. The
// first is the Night Pump of server/shops.js.
const PUMPS = [
    {pump: {x: -109.22, y: 8.52, z: -136.51}, road: {x: -100.79, y: 8.49, z: -146.09}},
    {pump: {x: -1381.2, y: -6.81, z: -61.73}, road: {x: -1381.22, y: -6.86, z: -52.56}},
    {pump: {x: -1784.15, y: -4.85, z: -210.74}, road: {x: -1784.01, y: -4.63, z: -197.07}},
];
// The pumps go up this long after the call, or later for a crew far away;
// unless every flame there is down to this.
const PUMP_FUSE_MS = 150_000;
const PUMP_SAFE_INTENSITY = 0.3;

// Fires in a bar (server/bars.js, its door in lib/maps.js). Morello's bar on
// Lost Heaven Extended (models/morellobar.4ds): flames out of the shop
// windows east of the door arrow on the south front (z 451.5), and two in
// its room (floor y 17.3, x -415.7..-406.0, z 451.6..461.2): between the
// stools and the way in, and behind the counter's east end. Stock Lost
// Heaven's back-room bar (lib/bar_room.js, room-local coordinates): flames
// out of Salieri's west shop window beside his door, one in the aisle in
// front of the counter and one at the far end. The people caught inside are
// the bar's own barman and drinkers, at their own places ([barman,
// ...drinkers] indices); led to the exit arrow they come out on the
// pavement between the door and the road (the road graph line at z 440.3
// for Morello's, z -8.1 for Salieri's).
const BAR_FIRES = {
    morello: {interior: "b:morello", front: {x: -408.0, y: 17.45, z: 450.6}, people: [0, 3, 4],
        rooms: () => [{x: -411.0, y: 17.35, z: 456.6, radius: 1.4}, {x: -407.2, y: 17.35, z: 460.0, radius: 1.2}],
        street: [{x: -414.5, y: 17.6, z: 446.5}, {x: -413.0, y: 17.6, z: 446.0}, {x: -411.5, y: 17.6, z: 446.5}]},
    backroom: {interior: "b:salieri-bar", front: {x: -1779.6, y: -5.5, z: 4.2}, people: [0, 1, 4],
        rooms: () => [{...barRoom.toWorld({x: 1.5, y: 0.02, z: -1.0}), radius: 1.4}, {...barRoom.toWorld({x: -6.2, y: 0.02, z: 1.6}), radius: 1.2}],
        street: [{x: -1777.8, y: -5.3, z: 1.0}, {x: -1776.3, y: -5.3, z: 0.6}, {x: -1774.8, y: -5.3, z: 1.0}]},
};
// A civilian led this close to the exit arrow inside is out.
const EXIT_REACH = 3.5;

// The arsonist: a hood (the stock Gangster01, one of Salieri's men in
// bars.js) who runs from a house he set alight.
const ARSONIST_MODEL = "Gangster01.i3d";
const ARSONIST_VOICE = 13;
const ARSONIST_CHANCE = 0.4;
const ARSONIST_MS = 100_000;
const ARSONIST_PAY = 150;
const ARSONIST_XP = 50;
const CATCH_REACH = 3;
const ARSON_ALERT_RANGE = 1500;

const crews = new Map();
const incidents = new Map();
let nextIncident = 1;
let nextFireAt = 0;
let timer = null;
let lastCardAt = 0;
let ambulance = null;
// playerId -> the fire markers last sent (lib/notify.js crewMarkers).
const sentMarkers = new Map();
// Civilians walking home after a rescue, removed a little later.
const strays = new Set();
// Officers on duty shown the grab prompt, by player id.
const arsonPrompts = new Map();

function available() {
    return typeof Fire !== "undefined" && typeof WaterJet !== "undefined";
}

const station = stations.define({
    id: "fire",
    title: "Fire brigade",
    crew: "firefighters",
    mapLabel: "Fire station",
    color: COLORS.red,
    radar: 0xd0574a,
    model: TRUCK,
    vehicleLabel: "FIRE TRUCK",
    pitch: "Put out fires around the city with the hose and lead people out of them. $260 to $420 a fire, split by the water each " +
        "firefighter puts on it, $120 for each person you lead out; a filling station or a bar with people inside pays about $600 in all, with a crew bonus. Press G here to sign on.",
    place: () => STATIONS[World.getMission()] ?? null,
    available,
    onDuty: (player) => crews.has(player.id),
    start: (player) => startDuty(player),
    stop: (player) => endDuty(player, "Off duty. The trucks stay at the station."),
    onDrive: (player, truck) => takeWheel(player, truck),
    onLeave: (player) => leaveWheel(player),
});

function streetPlayers() {
    return World.getPlayers().filter((player) => playable(player) && player.virtualWorld === MAIN_WORLD);
}

function place(position) {
    const nearest = maps.places().map((entry) => ({entry, gap: flatDistance(entry.position, position)})).sort((a, b) => a.gap - b.gap)[0];
    return nearest && nearest.gap < 700 ? `near ${nearest.entry.name}` : "in the city";
}

// A spot beside a road, `side` metres off the lane and facing along it.
function roadside(origin, minimum, maximum, side) {
    const graph = roads.current();
    let spot = null;
    for (let attempt = 0; attempt < 30 && !spot; attempt++) {
        const candidate = graph.roadPoint();
        const gap = origin ? flatDistance(candidate.position, origin) : minimum;
        if (gap >= minimum && gap <= maximum) spot = candidate;
    }
    spot ??= graph.roadPoint();
    const flip = Math.random() < 0.5 ? -1 : 1;
    const {x, y, z} = spot.position;
    return {
        position: {x: x + spot.direction.z * side * flip, y, z: z - spot.direction.x * side * flip},
        heading: Math.atan2(spot.direction.x, spot.direction.z),
    };
}

// Every fire of a call: the main one and the further ones (the pumps, the
// rooms of a bar, the fuel store once the pumps went up).
function firesOf(incident) {
    return [incident.fire, ...incident.extra].filter(Boolean);
}

function burning(incident) {
    return firesOf(incident).some((fire) => fire.burning);
}

// What the call is, in a few words: "House fire", "Fire at Morello's Bar".
function titleOf(incident) {
    if (incident.kind === "bar") return `Fire at ${incident.barName}`;
    if (incident.kind === "pump") return "Filling station fire";
    if (incident.kind === "pileup") return "Burning wreck";
    return incident.label.split(" · ")[0].toLowerCase().replace(/^./, (c) => c.toUpperCase());
}

function intensityOf(incident) {
    return firesOf(incident).reduce((highest, fire) => Math.max(highest, fire.burning ? fire.intensity : 0), 0);
}

// The bar burning on this map, if any: its fire layout and its interior.
function barVenue() {
    const map = maps.current();
    for (const bar of bars.bars()) {
        const layout = BAR_FIRES[bar.id];
        const interior = layout && map?.interiors?.find((entry) => entry.id === layout.interior);
        if (!interior || (bar.built && !barRoom.isBuilt())) continue;
        return {bar, layout, interior};
    }
    return null;
}

// The bar on fire whose room the player is in.
function burningInterior(player) {
    const interior = interiors.insideOf(player);
    if (!interior) return null;
    return [...incidents.values()].find((incident) => incident.interior?.id === interior.id) ?? null;
}

// The fire truck nearest the player, for the hose and the card. Inside a
// burning bar the hose runs in through its door from a truck outside.
function nearestTruck(player) {
    const indoors = burningInterior(player);
    return station.nearestVehicle(indoors ? indoors.interior.outside.position : player.position);
}

// At a truck's wheel the firefighter works its roof cannon; the water is theirs.
function takeWheel(player, truck) {
    const crew = crews.get(player.id);
    if (!crew || crew.cannonTruckId === truck.id) return;
    crew.cannon?.destroy();
    crew.cannon = WaterJet.create(truck, {pressure: 24, player});
    crew.cannonTruckId = crew.cannon ? truck.id : null;
}

function leaveWheel(player) {
    const crew = crews.get(player.id);
    if (!crew) return;
    crew.cannon?.destroy();
    crew.cannon = null;
    crew.cannonTruckId = null;
}

function presentIncidents() {
    const items = [];
    for (const incident of incidents.values()) {
        if (!incident.fire) continue;
        const alight = burning(incident);
        items.push({id: `fire${incident.id}`, position: incident.position, label: alight ? `${incident.label} · ${Math.round(intensityOf(incident) * 10) * 10}%` : "FIRE OUT",
            color: alight ? COLORS.red : COLORS.muted, beacon: alight, range: 1200, near: 120,
            map: alight ? {icon: "diamond", label: incident.label.split(" · ")[0], flash: true} : false});
        for (const civilian of incident.civilians) {
            if (civilian.state === "trapped" && civilian.npc) {
                items.push({id: `trapped${incident.id}_${civilian.index}`, position: civilian.npc.position, label: "TRAPPED", color: COLORS.gold,
                    range: incident.interior ? 40 : 150, map: false});
            }
        }
        const arsonist = incident.arsonist;
        if (arsonist?.state === "fleeing" && arsonist.npc) {
            const {x, y, z} = arsonist.npc.position;
            items.push({id: `arson${incident.id}`, position: {x: Math.round(x), y: Math.round(y), z: Math.round(z)}, label: "ARSONIST", color: COLORS.red,
                range: 400, near: 60, map: {icon: "triangle", label: "Arsonist"}});
        }
    }
    // Everyone sees the flames; only the crew on duty gets them on the map.
    crewMarkers("fires", sentMarkers, items, (player) => crews.has(player.id));
}

// The retail voices of men (26-28) and women (29-31), one per person.
function voiceFor(model) {
    return (/^czena/i.test(model) ? 29 : 26) + Math.floor(Math.random() * 3);
}

function spawnCivilian(incident, position, clip, index, {model = pick(CIVILIANS), heading = Math.random() * Math.PI * 2, world} = {}) {
    if (typeof Npc === "undefined") return;
    const voice = voiceFor(model);
    const npc = Npc.spawn(model, position, heading, {behaviour: "neutral", health: 100, voice, ...(world === undefined ? {} : {virtualWorld: world})});
    if (!npc) return;
    incident.civilians.push({index, npc, clip, model, voice, state: "spawning", leaderId: null, nextLineAt: Date.now() + 3000 + Math.random() * PANIC_EVERY_MS});
}

function newIncident(kind, position, label, fields = {}) {
    return {id: nextIncident++, kind, position, startedAt: Date.now(), vehicleId: null, fire: null, extra: [], civilians: [], water: new Map(),
        generation: World.getMissionGeneration(), label, where: place(position), ...fields};
}

// options (pileup, from ambulance.js): position and vehicleId of a wreck
// already on the road, which the call leaves standing when it ends.
function startIncident(kind, origin, options = {}) {
    if (!available() || !inFreeRide() || !World.isReady() || incidents.size >= MAX_INCIDENTS) return null;
    if (kind === "pump") return startPumpFire(origin);
    if (kind === "bar") return startBarFire();
    if (kind === "pileup") return startPileupFire(options);
    const crash = kind === "crash";
    const house = kind === "house";
    const spot = roadside(origin, 250, 1100, house ? 9 : 3.6);
    const incident = newIncident(kind, spot.position, house ? "HOUSE FIRE" : crash ? "CRASH · PEOPLE TRAPPED" : "CAR FIRE");
    const id = incident.id;
    if (!house) {
        const wreck = Vehicle.spawn(pick(WRECKS), {x: spot.position.x, y: spot.position.y + 0.6, z: spot.position.z},
            spot.heading + (crash ? (Math.random() - 0.5) * 1.2 : 0));
        if (!wreck) return null;
        wreck.setEngine(false);
        wreck.state.set("fr.wreck", true);
        incident.vehicleId = wreck.id;
    }
    const fire = Fire.create({x: spot.position.x, y: spot.position.y + (house ? 0 : 0.4), z: spot.position.z}, house
        ? {radius: 4.5, height: 5, toughness: 4, growth: 0.012, damage: 18}
        : {radius: 2.6, height: 1, toughness: 2, growth: 0.01, damage: 15, attachTo: vehicleById(incident.vehicleId) ?? undefined});
    if (!fire) {
        vehicleById(incident.vehicleId)?.destroy();
        return null;
    }
    incident.fire = fire;
    incidents.set(id, incident);
    // Who is caught in it: the crash's passengers on the road, a resident
    // crouched at the house door. Only with a crew on duty to get them out.
    if (crews.size && crash) {
        for (let index = 0; index < 1 + Math.floor(Math.random() * 2); index++) {
            const angle = spot.heading + Math.PI / 2 + (index ? Math.PI : 0);
            spawnCivilian(incident, {x: spot.position.x + Math.sin(angle) * 2.2, y: spot.position.y + 0.2, z: spot.position.z + Math.cos(angle) * 2.2}, LYING, index);
        }
    } else if (crews.size && house && Math.random() < 0.6) {
        spawnCivilian(incident, {x: spot.position.x, y: spot.position.y + 0.2, z: spot.position.z}, CROUCHED, 0);
    }
    if (crews.size && house && typeof Npc !== "undefined" && Math.random() < ARSONIST_CHANCE) spawnArsonist(incident, spot);
    const crewCount = crews.size;
    if (incident.arsonist) {
        incident.label = "HOUSE FIRE · ARSON";
        toastAll(`Someone set a house alight ${incident.where}, and he is running from it. Put the fire out; firefighters and police on duty ` +
            "can grab him: G next to him.", {title: "Arson", tone: "bad", seconds: 9});
    } else {
        toastAll(`${house ? "A house is on fire" : crash ? "A car crashed and caught fire with people inside" : "A car is burning"} ${incident.where}.` +
            (crewCount ? "" : " Nobody is on duty at the fire station."), {title: "Fire", tone: "bad", seconds: 7});
    }
    feed("fire", [text("Fire", COLORS.red), text(` · ${incident.label.toLowerCase()} ${incident.where}`)]);
    presentIncidents();
    return incident;
}

// ---- A filling station alight ----------------------------------------------

function startPumpFire(origin) {
    if (!crews.size || !maps.current()?.shops) return null;
    if ([...incidents.values()].some((incident) => incident.kind === "pump")) return null;
    const choices = PUMPS.filter((entry) => !origin || flatDistance(entry.pump, origin) >= 150);
    const {pump, road} = pick(choices.length ? choices : PUMPS);
    const heading = Math.atan2(road.x - pump.x, road.z - pump.z) + Math.PI / 2;
    const forecourt = {x: pump.x + (road.x - pump.x) * 0.4, y: pump.y, z: pump.z + (road.z - pump.z) * 0.4};
    const car = Vehicle.spawn(pick(WRECKS), {x: forecourt.x, y: forecourt.y + 0.6, z: forecourt.z}, heading);
    if (!car) return null;
    car.setEngine(false);
    car.state.set("fr.wreck", true);
    const fire = Fire.create({x: forecourt.x, y: forecourt.y + 0.4, z: forecourt.z},
        {radius: 2.6, height: 1, toughness: 2.5, growth: 0.012, damage: 15, attachTo: car});
    const pumpFire = Fire.create(pump, {radius: 2.2, height: 3, toughness: 3, growth: 0.015, damage: 18});
    if (!fire || !pumpFire) {
        fire?.destroy();
        pumpFire?.destroy();
        car.destroy();
        return null;
    }
    const incident = newIncident("pump", forecourt, "FILLING STATION FIRE", {vehicleId: car.id, fire, extra: [pumpFire], pump});
    // Long enough for the nearest crew to drive there and get the hose out.
    const nearest = Math.min(...[...crews.keys()].map(playerById).filter(Boolean).map((player) => flatDistance(player.position, forecourt)));
    incident.fuseAt = Date.now() + Math.max(PUMP_FUSE_MS, (Number.isFinite(nearest) ? nearest / 14 : 0) * 1000 + 90_000);
    incidents.set(incident.id, incident);
    toastAll(`A car caught fire at the pumps of the filling station ${incident.where}. Knock the flames down before the pumps go up!`,
        {title: "Fire", tone: "bad", seconds: 8});
    feed("fire", [text("Fire", COLORS.red), text(` · the filling station ${incident.where} is burning`)]);
    presentIncidents();
    return incident;
}

// Time is up at the pumps: what still burns there takes the fuel with it.
function pumpsGoUp(incident) {
    incident.blown = true;
    vehicleById(incident.vehicleId)?.explode();
    const store = Fire.create(incident.pump, {radius: 5, height: 6, toughness: 5, growth: 0.02, damage: 25});
    if (store) incident.extra.push(store);
    incident.label = "FILLING STATION FIRE · PUMPS GONE UP";
    for (const player of streetPlayers()) {
        if (crews.has(player.id) || flatDistance(player.position, incident.position) < 300) {
            toast(player, `The pumps ${incident.where} went up. Put out what is left.`, {title: "Fire", tone: "bad", seconds: 6});
        }
    }
    feed("fire", [text("Fire", COLORS.red), text(` · the pumps ${incident.where} went up`)]);
    presentIncidents();
}

// ---- A bar on fire, people inside ------------------------------------------

function startBarFire() {
    const venue = barVenue();
    if (!crews.size || !venue || typeof Npc === "undefined") return null;
    if ([...incidents.values()].some((incident) => incident.kind === "bar")) return null;
    const {bar, layout, interior} = venue;
    const front = Fire.create(layout.front, {radius: 2.0, height: 4, toughness: 3, growth: 0.012, damage: 18});
    if (!front) return null;
    const world = bar.world;
    const inside = layout.rooms().map(({x, y, z, radius}) => Fire.create({x, y, z}, {radius, height: 1.2, toughness: 1.5, growth: 0.01, damage: 12,
        ...(world === MAIN_WORLD ? {} : {virtualWorld: world})})).filter(Boolean);
    const incident = newIncident("bar", {...interior.outside.position}, `FIRE AT ${bar.name.toUpperCase()}`,
        {fire: front, extra: inside, interior, barId: bar.id, barName: bar.name, world, street: layout.street});
    incidents.set(incident.id, incident);
    // The staff and drinkers who were serving go; the ones caught in it stay.
    bars.setClosed(bar.id, true);
    const staff = [bar.bartender, ...bar.patrons];
    layout.people.forEach((slot, index) => {
        const spec = staff[slot];
        if (spec) spawnCivilian(incident, spec.position, CROUCHED, index, {model: spec.model, heading: spec.heading, world});
    });
    for (const player of World.getPlayers()) {
        if (bars.barOf(player)?.id === bar.id) toast(player, "Fire! The bar is burning. Get out through the door.", {title: bar.name, tone: "bad", seconds: 6});
    }
    toastAll(`${bar.name} is on fire ${incident.where}, people inside. Firefighters: hose the front, go in through the door and lead them ` +
        "out with G. Medics: they will need you on the pavement.", {title: "Fire", tone: "bad", seconds: 10});
    feed("fire", [text("Fire", COLORS.red), text(` · ${bar.name} is burning with people inside`)]);
    presentIncidents();
    return incident;
}

// Led to the exit arrow, a civilian comes out onto the pavement: the one
// inside goes and the same person stands outside, coughing.
function bringOut(incident, civilian) {
    civilian.npc?.remove();
    civilian.npc = null;
    rescued(incident, civilian, false);
    const spot = incident.street[civilian.index % incident.street.length];
    const npc = Npc.spawn(civilian.model, spot, Math.PI, {behaviour: "neutral", health: 100, voice: civilian.voice});
    if (!npc) return;
    civilian.outside = npc;
    civilian.state = "outside";
}

// ---- A burning wreck of the ambulance's pile-up ----------------------------

function startPileupFire({position, vehicleId}) {
    const wreck = vehicleById(vehicleId);
    if (!wreck || !position) return null;
    const fire = Fire.create({x: position.x, y: position.y + 0.4, z: position.z}, {radius: 2.6, height: 1, toughness: 2, growth: 0.01, damage: 15, attachTo: wreck});
    if (!fire) return null;
    const incident = newIncident("pileup", {...position}, "PILE-UP · WRECK ON FIRE", {fire, vehicleId, keepVehicle: true});
    incidents.set(incident.id, incident);
    for (const id of crews.keys()) {
        const player = playerById(id);
        if (player) toast(player, `A pile-up ${incident.where}: a wreck is burning beside the injured. Put it out before it reaches them.`,
            {title: "Fire", tone: "bad", seconds: 7});
    }
    presentIncidents();
    return incident;
}

// ---- The arsonist ----------------------------------------------------------

function spawnArsonist(incident, spot) {
    const heading = spot.heading;
    const position = {x: spot.position.x + Math.sin(heading) * 6, y: spot.position.y + 0.2, z: spot.position.z + Math.cos(heading) * 6};
    const npc = Npc.spawn(ARSONIST_MODEL, position, heading, {behaviour: "neutral", health: 100, voice: ARSONIST_VOICE});
    if (!npc) return;
    // alerted: the officers told about him, whose compass points at him.
    incident.arsonist = {npc, state: "fleeing", ready: false, task: 0, until: Date.now() + ARSONIST_MS, alerted: new Set()};
    for (const player of streetPlayers()) {
        if (!policeDuty.onDuty(player) || flatDistance(player.position, position) > ARSON_ALERT_RANGE) continue;
        incident.arsonist.alerted.add(player.id);
        toast(player, `An arsonist is running from a house fire ${incident.where}. Your compass shows him: grab him with G, or knock him down.`,
            {title: "Police radio", tone: "gold", seconds: 8});
    }
}

// Away down the street, at a run; again when he gets there.
function flee(incident) {
    const arsonist = incident.arsonist;
    if (!arsonist?.npc || arsonist.state !== "fleeing" || !arsonist.ready) return;
    const away = roadside(arsonist.npc.position, 120, 260, 4.5);
    arsonist.task = arsonist.npc.walkTo(away.position, {run: true, radius: 3});
}

function arsonistNear(player) {
    for (const incident of incidents.values()) {
        const arsonist = incident.arsonist;
        if (arsonist?.state === "fleeing" && arsonist.npc && distance(arsonist.npc.position, player.position) <= CATCH_REACH) return incident;
    }
    return null;
}

function canCatch(player) {
    return crews.has(player.id) || policeDuty.onDuty(player);
}

function endArson(incident, state) {
    const arsonist = incident.arsonist;
    if (!arsonist || arsonist.state !== "fleeing") return;
    arsonist.state = state;
    for (const id of arsonist.alerted) {
        const player = playerById(id);
        if (player) compass(player, "arson", 46, null);
    }
    arsonist.alerted.clear();
}

// paid: a firefighter or an officer on duty caught him; anyone else only
// stops him.
function catchArsonist(incident, player, paid = true) {
    const arsonist = incident.arsonist;
    endArson(incident, "caught");
    const npc = arsonist.npc;
    arsonist.npc = null;
    if (npc) {
        npc.playAnimation?.(CROUCHED, {loop: true});
        npc.say?.(pick(GIVE_UP_LINES));
        // The police take him from here.
        strays.add(npc);
        setTimeout(() => {
            strays.delete(npc);
            npc.remove();
        }, 15_000);
    }
    if (paid) {
        player.giveMoney(ARSONIST_PAY);
        const crew = crews.get(player.id);
        if (crew) crew.earned += ARSONIST_PAY;
        profiles.addXp(player, ARSONIST_XP, "Caught an arsonist");
        toast(player, `Got him. ${formatMoney(ARSONIST_PAY)} from the city; the police take him from here.`, {title: "Arson", tone: "good"});
    } else {
        toast(player, "You stopped the arsonist. The police take him from here; the city's reward is for the brigade and officers on duty.",
            {title: "Arson", tone: "gold"});
    }
    feed("fire", [name(player, COLORS.red), text(` caught the arsonist ${incident.where}`)]);
    presentIncidents();
}

function arsonistGone(incident, reason) {
    const arsonist = incident.arsonist;
    if (!arsonist || arsonist.state !== "fleeing") return;
    endArson(incident, reason);
    const npc = arsonist.npc;
    arsonist.npc = null;
    if (reason === "escaped") {
        npc?.remove();
        feed("fire", [text("Fire", COLORS.muted), text(` · the arsonist ${incident.where} got away`)]);
    } else if (npc) {
        feed("fire", [text("Fire", COLORS.muted), text(` · the arsonist ${incident.where} is dead`)]);
        strays.add(npc);
        setTimeout(() => {
            strays.delete(npc);
            npc.remove();
        }, 30_000);
    }
    presentIncidents();
}

function endIncident(incident, message, smokeMs = SMOKE_MS) {
    incidents.delete(incident.id);
    for (const civilian of incident.civilians) {
        civilian.npc?.remove();
        civilian.outside?.remove();
    }
    if (incident.arsonist) {
        endArson(incident, "escaped");
        incident.arsonist.npc?.remove();
        incident.arsonist.npc = null;
    }
    if (incident.barId) bars.setClosed(incident.barId, false);
    for (const crew of crews.values()) if (crew.helping === incident.id) crew.helping = null;
    const fires = firesOf(incident);
    // A pile-up's wrecks are the ambulance's to clear.
    const wreckId = incident.keepVehicle ? null : incident.vehicleId;
    // The smoke stays a while, then the wreck is towed away with it.
    const clear = () => {
        for (const fire of fires) fire.destroy();
        const wreck = vehicleById(wreckId);
        if (wreck && wreck.getOccupants().every((occupant) => occupant === null)) wreck.destroy();
    };
    if (smokeMs > 0) setTimeout(clear, smokeMs);
    else clear();
    if (message) feed("fire", [text("Fire", COLORS.muted), text(` · ${message}`)]);
    presentIncidents();
}

function incidentOfFire(fire) {
    for (const incident of incidents.values()) if (firesOf(incident).some((entry) => entry.id === fire.id)) return incident;
    return null;
}

// The point a hose aims at: the fire's edge nearest the nozzle, inside its
// area, so the water lands in the flames.
function aimPoint(fire, nozzle) {
    const dx = nozzle.x - fire.position.x;
    const dz = nozzle.z - fire.position.z;
    const gap = Math.hypot(dx, dz) || 1;
    const inset = Math.min(gap, fire.radius * 0.5);
    return {x: fire.position.x + dx / gap * inset, y: fire.position.y + 0.6 + fire.height * 0.3, z: fire.position.z + dz / gap * inset};
}

function facingTo(player, point) {
    const heading = headingOf(player.rotation);
    const dx = point.x - player.position.x;
    const dz = point.z - player.position.z;
    const gap = Math.hypot(dx, dz) || 1;
    return (Math.sin(heading) * dx + Math.cos(heading) * dz) / gap;
}

// Where water from this firefighter can go now: the cannon from a fire
// truck's driver seat, the hose on foot within reach of any fire truck.
function hoseFor(player, crew) {
    const vehicle = player.getVehicle();
    if (vehicle) {
        return crew.cannon && crew.cannonTruckId === vehicle.id && player.getSeat() === 0
            ? {jet: crew.cannon, reach: CANNON_REACH, from: vehicle.position} : null;
    }
    const truck = nearestTruck(player);
    if (!truck || truck.gap > HOSE_TO_TRUCK) return null;
    return {jet: crew.hose, reach: HOSE_REACH, from: player.position};
}

function spray(player, crew) {
    const hose = hoseFor(player, crew);
    if (!hose) {
        crew.hose?.stop();
        crew.cannon?.stop();
        return;
    }
    (hose.jet === crew.hose ? crew.cannon : crew.hose)?.stop();
    const onFoot = hose.jet === crew.hose;
    const targets = Fire.getInRange(hose.from, hose.reach + 15).filter((fire) => fire.burning)
        .map((fire) => ({fire, point: aimPoint(fire, hose.from)}))
        .filter((entry) => flatDistance(entry.point, hose.from) <= hose.reach)
        .map((entry) => ({...entry, score: flatDistance(entry.point, hose.from) - (onFoot ? facingTo(player, entry.point) * 10 : 0)}))
        .sort((a, b) => a.score - b.score);
    if (targets.length) {
        hose.jet.aimAt(targets[0].point);
        return;
    }
    // Nothing in reach: the water goes where the nozzle points.
    const heading = headingOf(onFoot ? player.rotation : player.getVehicle().rotation);
    hose.jet.aimAt({x: hose.from.x + Math.sin(heading) * 12, y: hose.from.y, z: hose.from.z + Math.cos(heading) * 12});
}

function rescueNear(player) {
    for (const incident of incidents.values()) {
        for (const civilian of incident.civilians) {
            if (civilian.state === "trapped" && civilian.npc && distance(civilian.npc.position, player.position) <= RESCUE_REACH) return {incident, civilian};
        }
    }
    return null;
}

function leadOut(player, incident, civilian) {
    civilian.state = "following";
    civilian.leaderId = player.id;
    civilian.npc.stopAnimation?.();
    civilian.npc.follow(player, 1.6);
    toast(player, incident.interior ? "They are on their feet. Lead them to the exit arrow; they come out with you."
        : "They are on their feet. Lead them well clear of the fire.", {title: "Rescue", tone: "gold"});
    presentIncidents();
}

// Pays the one who led them out; `handOff` passes them on to a medic or
// sends them home now (a bar's civilians first come out onto the pavement).
function rescued(incident, civilian, handOff = true) {
    const player = playerById(civilian.leaderId);
    civilian.state = "safe";
    if (player) incident.rescuers = new Set([...(incident.rescuers ?? []), player.id]);
    if (player) {
        const pay = incident.kind === "bar" ? BAR_RESCUE_PAY : RESCUE_PAY;
        player.giveMoney(pay);
        const crew = crews.get(player.id);
        if (crew) crew.earned += pay;
        profiles.bump(player, "rescues");
        profiles.addXp(player, RESCUE_XP, "Led someone out of a fire");
        toast(player, `Safe. ${formatMoney(pay)} from the city.`, {title: "Rescue", tone: "good"});
        feed("fire", [name(player, COLORS.red), text(" pulled someone out of a fire")]);
    }
    if (!handOff) return;
    const npc = civilian.npc;
    civilian.npc = null;
    passOn(incident, npc);
}

// A medic on duty takes them to hospital; otherwise they go home. Out of a
// bar, the medic's call carries a share of the big call's bonus.
function passOn(incident, npc) {
    if (!npc) return;
    const label = incident.kind === "bar" ? `Smoke victim from ${incident.barName}` : `Smoke victim ${incident.where}`;
    if (!ambulance?.takePatient(npc, incident.position, label, {bonus: incident.kind === "bar" ? BAR_MEDIC_BONUS : 0})) {
        // Out of a bar, along the pavement rather than back into the building.
        npc.walkTo({x: npc.position.x + 20, y: npc.position.y, z: npc.position.z + (incident.kind === "bar" ? 0 : 20)});
        strays.add(npc);
        setTimeout(() => {
            strays.delete(npc);
            npc.remove();
        }, 20_000);
    }
}

function endDuty(player, message) {
    const crew = crews.get(player.id);
    if (!crew) return;
    crews.delete(player.id);
    jobkey.release(player);
    jobkey.prompt(player, null);
    crew.hose?.destroy();
    crew.cannon?.destroy();
    life.end(player, "fire");
    profiles.setPublic(player, "fr.job", null);
    compass(player, "fire", 44, null);
    card(player, "fire", null);
    if (World.getPlayers().some((other) => other.id === player.id)) {
        // Off duty nobody drives a fire truck: the player gets out first.
        if (player.getSeat() === 0 && station.isVehicle(player.getVehicle())) player.removeFromVehicle();
        player.setModel(crew.model);
        // Not into a cell: the jail took the player off the job and their guns.
        if (player.alive && !life.activity(player)) life.equip(player, crew.loadout.weapons, crew.loadout.selected);
    }
    station.refresh();
    if (message) toast(player, `${message} You earned ${formatMoney(crew.earned)} this shift.`, {title: "Fire brigade"});
}

function startDuty(player) {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) return;
    if (!available()) {
        toast(player, "The fire brigade needs a newer server.", {tone: "bad"});
        return;
    }
    const home = station.here();
    if (!home) {
        toast(player, "There is no fire station out here.", {tone: "bad"});
        return;
    }
    if (player.getVehicle() || flatDistance(player.position, home.signOn) > stations.SIGN_ON_RADIUS + 1) {
        // Asked again, the compass stops showing the way.
        if (!station.guide(player)) {
            toast(player, "The compass no longer shows the way.", {title: "Fire brigade", seconds: 3});
            return;
        }
        toast(player, "Sign on at the fire station in the Works Quarter: follow the compass to the arrow by the garage doors and press G. " +
            "Hold TAB to see it on the map.", {title: "Fire brigade", tone: "gold", seconds: 8});
        return;
    }
    if (!life.begin(player, "fire", "Fire duty")) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    const hose = WaterJet.create(player);
    if (!hose) {
        life.end(player, "fire");
        toast(player, "The station has no hose free. Try again shortly.", {tone: "bad"});
        return;
    }
    const crew = {model: player.model, loadout: life.loadoutOf(player), earned: 0, fires: 0, hose, cannon: null, cannonTruckId: null, helping: null};
    crews.set(player.id, crew);
    player.setModel(UNIFORM);
    life.giveKit(player, {weapons: [], selected: 0});
    profiles.setPublic(player, "fr.job", "fire");
    feed("fire", [name(player, COLORS.red), text(" joined the fire brigade")]);
    toast(player, "You are on duty. Get in one of the fire trucks in front of the station. Fires show on your compass and on the map (TAB). " +
        "Near a fire, hold G to spray: on foot within 35 m of a truck, or the roof cannon from the driver's seat. " +
        "G next to a trapped person leads them out. Back under the arrow, G clocks you off.",
        {title: "Fire brigade", tone: "gold", seconds: 12});
    if (!incidents.size) nextFireAt = Math.min(nextFireAt, Date.now() + 30_000);
}

// People still to get out: trapped, being led, or just come out of a bar.
function waiting(incident) {
    return incident.civilians.some((civilian) => ["trapped", "spawning", "following", "outside"].includes(civilian.state));
}

function active(incident) {
    return burning(incident) || incident.civilians.some((civilian) => civilian.state === "trapped") || incident.arsonist?.state === "fleeing";
}

function nearestIncident(position) {
    return [...incidents.values()].filter(active)
        .map((incident) => ({incident, gap: flatDistance(incident.position, position)})).sort((a, b) => a.gap - b.gap)[0] ?? null;
}

// Where the compass points for this call: the fire, or the arsonist once it
// is out, or inside a burning bar the nearest one still waiting there.
function targetOf(incident, player) {
    if (incident.arsonist?.state === "fleeing" && incident.arsonist.npc && !burning(incident)) return incident.arsonist.npc.position;
    if (incident.interior && burningInterior(player) === incident) {
        const inside = incident.civilians.filter((civilian) => civilian.state === "trapped" && civilian.npc)
            .sort((a, b) => distance(a.npc.position, player.position) - distance(b.npc.position, player.position))[0];
        return inside?.npc.position ?? incident.interior.inside.position;
    }
    return incident.position;
}

// The call is over with everyone out: the big ones pay the crew a bonus.
function complete(incident) {
    const bonus = BONUS[incident.kind];
    if (!bonus) return;
    if (incident.kind === "pump" && incident.blown) return;
    if (incident.kind === "bar" && incident.civilians.some((civilian) => civilian.state !== "safe")) return;
    const total = [...incident.water.values()].reduce((sum, amount) => sum + amount, 0);
    const shares = new Map([...incident.water].map(([id, amount]) => [id, total > 0 ? amount / total : 0]));
    // Everyone who led someone out of the bar has a part in it too.
    for (const id of incident.rescuers ?? []) if (!shares.has(id)) shares.set(id, 0);
    const people = [...shares].map(([id, share]) => ({player: playerById(id), share})).filter((entry) => entry.player);
    if (!people.length) return;
    const even = 1 / people.length;
    for (const {player, share} of people) {
        // Half by water, half evenly: the one who went in counts as much.
        const cut = Math.max(25, Math.round(bonus * (share + even) / 2));
        player.giveMoney(cut);
        const crew = crews.get(player.id);
        if (crew) crew.earned += cut;
        profiles.addXp(player, 40, incident.kind === "pump" ? "Saved the pumps" : "Got everyone out");
        toast(player, `${incident.kind === "pump" ? "The pumps are saved" : "Everyone got out alive"}. Bonus: ${formatMoney(cut)}.`, {title: "Fire brigade", tone: "good"});
    }
    feed("fire", [...people.flatMap(({player}, index) => [index ? text(", ") : text(""), name(player, COLORS.red)]),
        text(incident.kind === "pump" ? ` saved the filling station ${incident.where}` : ` got everyone out of ${incident.barName}`)]);
}

function catchHint(player) {
    return canCatch(player) && !player.getVehicle() && arsonistNear(player) ? "[G] GRAB HIM" : null;
}

function tick() {
    const now = Date.now();
    if (!inFreeRide() || !World.isReady()) return;
    // A new fire now and then; more often with the brigade on duty. Only on
    // maps with a fire station to call. A crew on duty also gets the bigger
    // calls: the filling station and the bar.
    if (available() && station.here() && now >= nextFireAt && streetPlayers().length) {
        nextFireAt = now + (crews.size ? FIRE_EVERY_MS : QUIET_FIRE_EVERY_MS);
        const anchor = pick(crews.size ? [...crews.keys()].map(playerById).filter(Boolean) : streetPlayers());
        const kind = pick(crews.size ? ["house", "house", "car", "crash", "pump", "bar"] : ["house", "house", "car", "crash"]);
        if (!startIncident(kind, anchor?.position ?? null) && (kind === "pump" || kind === "bar")) startIncident("house", anchor?.position ?? null);
    }
    for (const incident of [...incidents.values()]) {
        if (incident.generation !== World.getMissionGeneration()) {
            incidents.delete(incident.id);
            continue;
        }
        for (const civilian of incident.civilians) {
            // Trapped civilians lie down once the host made them.
            if (civilian.state === "spawning" && civilian.npc && civilian.ready) {
                civilian.state = "trapped";
                civilian.npc.playAnimation?.(civilian.clip, {loop: true});
            } else if (civilian.state === "following" && civilian.npc) {
                const leader = playerById(civilian.leaderId);
                // Out of a bar through its exit arrow; a leader who went
                // out without them leaves them behind.
                const behind = incident.interior && leader && interiors.insideOf(leader)?.id !== incident.interior.id;
                const exit = incident.interior?.inside.position;
                if (exit && flatDistance(civilian.npc.position, exit) <= EXIT_REACH && Math.abs(civilian.npc.position.y - exit.y) < 3) {
                    bringOut(incident, civilian);
                } else if (!leader || !leader.alive || behind) {
                    civilian.state = "trapped";
                    civilian.npc.stop?.();
                    civilian.npc.playAnimation?.(CROUCHED, {loop: true});
                    if (behind) toast(leader, "You left them behind inside. Go back in and bring them to the exit arrow.", {title: "Rescue", tone: "bad"});
                } else if (!incident.interior && flatDistance(civilian.npc.position, incident.position) >= SAFE_DISTANCE) {
                    rescued(incident, civilian);
                }
            } else if (civilian.state === "outside" && civilian.outside && civilian.outsideReady) {
                const npc = civilian.outside;
                civilian.outside = null;
                civilian.state = "safe";
                passOn(incident, npc);
            }
            // Cries for help while they wait.
            if (civilian.state === "trapped" && civilian.npc && now >= civilian.nextLineAt) {
                civilian.nextLineAt = now + PANIC_EVERY_MS * (0.7 + Math.random() * 0.6);
                civilian.npc.say?.(pick(PANIC_LINES));
            }
        }
        const arsonist = incident.arsonist;
        if (arsonist?.state === "fleeing") {
            if (now >= arsonist.until || !arsonist.npc) arsonistGone(incident, "escaped");
            else for (const id of arsonist.alerted) {
                const officer = playerById(id);
                if (officer) compass(officer, "arson", 46, {position: arsonist.npc.position});
            }
        }
        if (incident.fire && !burning(incident) && !waiting(incident) && incident.arsonist?.state !== "fleeing") {
            complete(incident);
            endIncident(incident, null);
            continue;
        }
        // The pumps: safe once every flame there is down low, else they go up.
        if (incident.kind === "pump" && incident.fuseAt) {
            const left = incident.fuseAt - now;
            if (firesOf(incident).every((fire) => !fire.burning || fire.intensity <= PUMP_SAFE_INTENSITY)) {
                incident.fuseAt = null;
                for (const id of crews.keys()) {
                    const player = playerById(id);
                    if (player) toast(player, `The pumps ${incident.where} are safe. Finish the fire off.`, {title: "Fire brigade", tone: "good", seconds: 4});
                }
            } else if (left <= 0) {
                incident.fuseAt = null;
                pumpsGoUp(incident);
            } else if (left <= 30_000 && !incident.warned) {
                incident.warned = true;
                for (const id of crews.keys()) {
                    const player = playerById(id);
                    if (player) toast(player, `Thirty seconds before the pumps ${incident.where} go up!`, {title: "Fire brigade", tone: "bad", seconds: 4});
                }
            }
        }
        const age = now - incident.startedAt;
        if (incident.vehicleId && incident.fire?.burning && age >= CAR_BLOWS_AFTER_MS && !incident.blown && incident.kind !== "pump" && !incident.keepVehicle) {
            incident.blown = true;
            vehicleById(incident.vehicleId)?.explode();
        }
        if (age >= INCIDENT_LIFETIME_MS) {
            for (const fire of firesOf(incident)) fire.extinguish();
            endIncident(incident, incident.kind === "bar" ? `the fire at ${incident.barName} burned itself out` : `the fire ${incident.where} burned itself out`);
        }
    }
    // Officers on duty grab an arsonist with G too.
    for (const player of World.getPlayers()) {
        if (crews.has(player.id) || !policeDuty.onDuty(player)) continue;
        const hint = playable(player) ? catchHint(player) : null;
        if (hint !== (arsonPrompts.get(player.id) ?? null)) {
            arsonPrompts.set(player.id, hint);
            jobkey.prompt(player, hint);
        }
    }
    for (const [id, crew] of crews) {
        const player = playerById(id);
        if (!player) {
            crews.delete(id);
            continue;
        }
        // Another module took the player off the job: an arrest and the jail.
        if (life.activity(player)?.kind !== "fire") {
            endDuty(player, "Your shift is over.");
            continue;
        }
        if (!playable(player)) {
            if (!player.alive) {
                crew.hose?.stop();
                crew.cannon?.stop();
            }
            continue;
        }
        if (jobkey.isHeld(player) && crew.spraying) spray(player, crew);
        const nearest = nearestIncident(player.position);
        compass(player, "fire", 44, nearest ? {position: targetOf(nearest.incident, player)} : null);
        const rescue = rescueNear(player);
        const hose = hoseFor(player, crew);
        const near = nearest && hose && (nearest.gap < hose.reach + 10 || burningInterior(player) === nearest.incident);
        const hint = catchHint(player) ?? (rescue ? "[G] LEAD THEM OUT" : crew.spraying ? "SPRAYING · RELEASE G TO STOP"
            : near && burning(nearest.incident) ? "[G] HOLD TO SPRAY" : null);
        if (hint !== crew.hint) {
            crew.hint = hint;
            jobkey.prompt(player, hint);
        }
    }
    if (now - lastCardAt >= CARD_MS) {
        lastCardAt = now;
        presentIncidents();
        for (const [id, crew] of crews) {
            const player = playerById(id);
            if (!player) continue;
            const nearest = nearestIncident(player.position);
            const truck = player.getVehicle() && station.isVehicle(player.getVehicle()) ? null : nearestTruck(player);
            const rows = [];
            let subtitle = truck && truck.gap > HOSE_TO_TRUCK ? "Get in a fire truck" : "All quiet. Wait at the station.";
            if (nearest) {
                const incident = nearest.incident;
                const fires = firesOf(incident);
                rows.push([titleOf(incident),
                    `${Math.round(nearest.gap)} m · ${Math.round(intensityOf(incident) * 100)}%`]);
                if (fires.length > 1) rows.push(["Burning", `${fires.filter((fire) => fire.burning).length} of ${fires.length}`]);
                const trapped = incident.civilians.filter((civilian) => civilian.state === "trapped").length;
                if (trapped) rows.push([incident.interior ? "Inside" : "Trapped", `${trapped}`]);
                if (incident.fuseAt) rows.push(["Pumps go up in", formatMinutes(Math.max(0, incident.fuseAt - now))]);
                const arsonist = incident.arsonist;
                if (arsonist?.state === "fleeing" && arsonist.npc) rows.push(["Arsonist", `${Math.round(distance(arsonist.npc.position, player.position))} m · running`]);
                subtitle = incident.interior ? (burningInterior(player) === incident ? "Hose the room, lead them to the exit arrow"
                    : `People inside ${incident.barName}: in through the door`)
                    : incident.fuseAt ? "Knock the flames down before the pumps go up"
                    : !burning(incident) && arsonist?.state === "fleeing" ? "Catch the arsonist: G next to him"
                    : `A fire ${incident.where}`;
            }
            if (truck) rows.push(["Nearest truck", `${Math.round(truck.gap)} m`]);
            rows.push(["This shift", `${crew.fires} fire${crew.fires === 1 ? "" : "s"} · ${formatMoney(crew.earned)}`]);
            card(player, "fire", {title: "Fire brigade", subtitle, rows, tone: "bad"});
        }
    }
}

// Grabbing an arsonist: firefighters and police officers on duty.
jobkey.register({
    press(player) {
        if (!playable(player) || player.getVehicle() || !canCatch(player)) return false;
        const incident = arsonistNear(player);
        if (!incident) return false;
        catchArsonist(incident, player);
        return true;
    },
});

jobkey.register({
    press(player) {
        const crew = crews.get(player.id);
        if (!crew || !playable(player)) return false;
        const rescue = rescueNear(player);
        if (rescue && !player.getVehicle()) {
            leadOut(player, rescue.incident, rescue.civilian);
            return true;
        }
        // Nothing of the brigade's here: the key goes to whatever else is
        // (the rum still, a side job). The roof cannon at a truck's wheel
        // always answers it.
        const hose = hoseFor(player, crew);
        const nearest = nearestIncident(player.position);
        const fireNear = Boolean(nearest) && burning(nearest.incident) &&
            (nearest.gap < (hose?.reach ?? HOSE_REACH) + 10 || burningInterior(player) === nearest.incident);
        if (!hose) {
            if (!fireNear) return false;
            toast(player, "Too far from a fire truck: the hose reaches 35 m from one.", {tone: "bad", seconds: 3});
            return true;
        }
        if (hose.jet === crew.hose && !fireNear) return false;
        crew.spraying = true;
        spray(player, crew);
        return true;
    },
    release(player) {
        const crew = crews.get(player.id);
        if (!crew) return;
        crew.spraying = false;
        crew.hose?.stop();
        crew.cannon?.stop();
    },
});

Events.on("npcSpawn", (npc) => {
    for (const incident of incidents.values()) {
        const civilian = incident.civilians.find((entry) => entry.npc && entry.npc.id === npc.id);
        if (civilian) civilian.ready = true;
        const out = incident.civilians.find((entry) => entry.outside && entry.outside.id === npc.id);
        if (out) out.outsideReady = true;
        if (incident.arsonist?.npc && incident.arsonist.npc.id === npc.id) {
            incident.arsonist.ready = true;
            flee(incident);
        }
    }
});

// The arsonist keeps running while he is loose.
Events.on("npcTaskComplete", (npc, info) => {
    for (const incident of incidents.values()) {
        const arsonist = incident.arsonist;
        if (arsonist?.npc && arsonist.npc.id === npc.id && info.id === arsonist.task && info.result !== "cancelled") flee(incident);
    }
});

// Hit by a player, or knocked over by a player's car, he gives up. The
// city pays only a firefighter or an officer on duty for him.
Events.on("npcDamage", (npc, info) => {
    if (!info || info.healthAfter <= 0) return;
    const attacker = info.attacker instanceof Vehicle ? info.attacker.getDriver?.() : info.attacker;
    if (!(attacker instanceof Player)) return;
    for (const incident of incidents.values()) {
        const arsonist = incident.arsonist;
        if (arsonist?.state === "fleeing" && arsonist.npc && arsonist.npc.id === npc.id) catchArsonist(incident, attacker, canCatch(attacker));
    }
});

Events.on("npcDeath", (npc) => {
    for (const incident of incidents.values()) {
        const civilian = incident.civilians.find((entry) => entry.npc && entry.npc.id === npc.id);
        if (civilian) civilian.state = "dead";
        if (incident.arsonist?.npc && incident.arsonist.npc.id === npc.id) arsonistGone(incident, "dead");
    }
});

// A civilian the host could not make, or lost: nobody is left to lead out.
Events.on("npcRetire", (npc) => {
    for (const incident of incidents.values()) {
        const civilian = incident.civilians.find((entry) => entry.npc && entry.npc.id === npc.id);
        if (civilian) {
            civilian.npc = null;
            if (civilian.state !== "dead") civilian.state = "gone";
        }
        // Out on the pavement already: they went on their own.
        const out = incident.civilians.find((entry) => entry.outside && entry.outside.id === npc.id);
        if (out) {
            out.outside = null;
            out.state = "safe";
        }
        if (incident.arsonist?.npc && incident.arsonist.npc.id === npc.id) arsonistGone(incident, "escaped");
    }
    for (const stray of strays) if (stray.id === npc.id) strays.delete(stray);
});

Events.on("fireExtinguished", (fire, info) => {
    const incident = incidentOfFire(fire);
    if (!incident) return;
    if (info.reason !== "doused") return;
    // The main fire pays the call; each further one (a pump, a room) a little.
    const main = incident.fire?.id === fire.id;
    const pay = main ? PAY[incident.kind] ?? 300 : SPOT_PAY;
    const last = !burning(incident);
    const crewNames = [];
    for (const {player, amount, share} of info.contributors) {
        if (!player || share <= 0) continue;
        incident.water.set(player.id, (incident.water.get(player.id) ?? 0) + amount);
        const cut = Math.max(main ? 25 : 10, Math.round(pay * share));
        player.giveMoney(cut);
        const crew = crews.get(player.id);
        if (crew) {
            crew.earned += cut;
            if (main) crew.fires++;
        }
        if (main) profiles.bump(player, "firesOut");
        profiles.addXp(player, main ? Math.round(20 + XP * share) : 10, "Put out a fire");
        toast(player, last ? `Fire out. Your share: ${formatMoney(cut)} (${Math.round(share * 100)}% of the water).`
            : `One fire down, ${firesOf(incident).filter((entry) => entry.burning).length} still burning. ${formatMoney(cut)}.`, {title: "Fire brigade", tone: "good"});
        crewNames.push(player);
    }
    if (crewNames.length && last) {
        feed("fire", [...crewNames.flatMap((player, index) => [index ? text(", ") : text(""), name(player, COLORS.red)]),
            text(incident.kind === "bar" ? ` put out the fire at ${incident.barName}` : ` put out the ${titleOf(incident).toLowerCase()} ${incident.where}`)]);
    }
    presentIncidents();
});

// Signing on happens under the station's arrow; the command shows the way there
// and clocks off from anywhere.
commands.register({name: "fire", aliases: ["firefighter", "fireman"], usage: "/fire", help: "Show the way to the fire station, or clock off.", category: "City"}, (player) => {
    if (crews.has(player.id)) endDuty(player, "Off duty.");
    else startDuty(player);
});

commands.register({name: "fires", usage: "/fires", help: "Where the city is burning.", category: "City"}, (player) => {
    const list = [...incidents.values()].filter(burning);
    if (!list.length) {
        toast(player, "No fires right now.", {title: "Fire brigade"});
        return;
    }
    const nearest = nearestIncident(player.position);
    if (nearest) compass(player, "fire", 44, {position: nearest.incident.position});
    toast(player, list.map((incident) => `${incident.label} ${incident.where}`).join(" · "), {title: "Fires", tone: "bad"});
});

menu.provide("fire", (player) => ({onDuty: crews.has(player.id), crew: crews.size, available: Boolean(station.here()),
    fires: [...incidents.values()].filter(burning).length, earned: crews.get(player.id)?.earned ?? 0}));

onStart(() => {
    nextFireAt = Date.now() + FIRST_FIRE_MS;
    timer = setInterval(tick, TICK_MS);
});
onStop(() => {
    clearInterval(timer);
    for (const id of [...crews.keys()]) {
        const player = playerById(id);
        if (player) endDuty(player, null);
    }
    for (const incident of [...incidents.values()]) endIncident(incident, null, 0);
    for (const stray of strays) stray.remove();
    strays.clear();
    for (const [id, hint] of arsonPrompts) {
        const player = playerById(id);
        if (player && hint) jobkey.prompt(player, null);
    }
    arsonPrompts.clear();
});
Events.on("playerSpawn", (player) => {
    // The fires burning before this player came go out on the next tick.
    sentMarkers.delete(player.id);
});
Events.on("playerDisconnect", (player) => {
    arsonPrompts.delete(player.id);
    sentMarkers.delete(player.id);
    const crew = crews.get(player.id);
    if (!crew) return;
    crews.delete(player.id);
    crew.hose?.destroy();
    crew.cannon?.destroy();
});
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) endDuty(player, "Your shift ended with the city reloading.");
    incidents.clear();
    strays.clear();
    arsonPrompts.clear();
    sentMarkers.clear();
    nextFireAt = Date.now() + FIRST_FIRE_MS;
});

module.exports = {
    onDuty: (player) => crews.has(player.id),
    crewSize: () => crews.size,
    WRECKS,
    startIncident,
    incidents: () => [...incidents.values()],
    // The next fire breaks out in ms (dev tools, tests).
    schedule(ms) { nextFireAt = Date.now() + ms; },
    // ambulance.js offers to take smoke victims.
    setAmbulance(handler) { ambulance = handler; },
};
