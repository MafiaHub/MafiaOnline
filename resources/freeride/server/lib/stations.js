// Job stations: where a job is joined and where its vehicles wait. A station
// has a sign-on point on foot (the retail mission arrow spinning over the
// spot, with a label: stand under it and press G to go on or off duty) and parking
// spots its vehicles return to. Both show on the city map. Anyone may ride
// in a station's vehicle, but only a player on that job drives it. A wreck,
// or a vehicle left empty away from its spot, is replaced at the spot.
const jobkey = require("./jobkey.js");
const {toast, compass, markers, markersAll} = require("./notify.js");
const {MAIN_WORLD, flatDistance, distance, playable, playerById, vehicleById, onStop, inFreeRide} = require("./util.js");

const SIGN_ON_RADIUS = 2.2;
const TICK_MS = 250;
const UPKEEP_MS = 5000;
const RESPAWN_MS = 20_000;
const ABANDON_MS = 3 * 60_000;
const ABANDON_REACH = 30;
const CREW_NEARBY = 80;
const SPOT_CLEAR = 4;
const INTRO_EVERY_MS = 30_000;
const COMPASS_PRIORITY = 40;

const STEAL_UNDO_MS = 3000;
const FLEET_POSITION_STEP = 10;

const stations = [];
// playerId -> {station, onDuty}: the sign-on point the player stands on and
// the duty state the prompt was written for.
const standing = new Map();
const introducedAt = new Map();
// playerId -> station id whose sign-on point the compass shows them.
const guiding = new Map();
// vehicleId -> {playerId, at}: who last left a station vehicle's wheel, so a
// refused steal can put them back.
const lastDrivers = new Map();
let lastPoints = "";

function streetPlayers() {
    return World.getPlayers().filter((player) => playable(player) && player.virtualWorld === MAIN_WORLD);
}

function occupied(vehicle) {
    return vehicle.getOccupants().some((occupant) => occupant !== null);
}

function spotClear(spot) {
    return !World.getVehicles().some((vehicle) => flatDistance(vehicle.position, spot.position) < SPOT_CLEAR);
}

// definition: {id, title, crew, mapLabel, color, radar, model, vehicleLabel, pitch,
// place() -> {signOn, spots: [{position, heading}]} | null for this map,
// available() -> boolean, onDuty(player), start(player), stop(player),
// onDrive?(player, vehicle), onLeave?(player, vehicle),
// mayDrive?(player, vehicle) -> true lets someone off duty take this one's wheel}
function define(definition) {
    const station = {
        ...definition,
        slots: [],
        fleetShownTo: new Set(),
        lastFleet: "",
        here() {
            return inFreeRide() && station.available() ? station.place() : null;
        },
        vehicles() {
            return station.slots.map((slot) => vehicleById(slot.vehicleId)).filter(Boolean);
        },
        isVehicle(vehicle) {
            return Boolean(vehicle) && station.slots.some((slot) => slot.vehicleId === vehicle.id);
        },
        nearestVehicle(position) {
            return station.vehicles().map((vehicle) => ({vehicle, gap: distance(vehicle.position, position)}))
                .sort((a, b) => a.gap - b.gap)[0] ?? null;
        },
        // Points the player at the sign-on point; asked again, it stops.
        // False when it stopped or there is no point here.
        guide(player) {
            const place = station.here();
            if (!place || guiding.get(player.id) === station.id) {
                station.stopGuiding(player);
                return false;
            }
            if (guiding.has(player.id)) compass(player, `station:${guiding.get(player.id)}`, COMPASS_PRIORITY, null);
            guiding.set(player.id, station.id);
            compass(player, `station:${station.id}`, COMPASS_PRIORITY, {position: place.signOn});
            return true;
        },
        stopGuiding(player) {
            if (guiding.get(player.id) === station.id) guiding.delete(player.id);
            compass(player, `station:${station.id}`, COMPASS_PRIORITY, null);
        },
        // Re-sends this job's vehicles to whoever is on duty now.
        refresh() {
            presentFleet(station);
        },
    };
    stations.push(station);
    return station;
}

function presentPoints(force = false) {
    const items = [];
    for (const station of stations) {
        const place = station.here();
        if (!place) continue;
        items.push({id: station.id, position: place.signOn, label: station.title.toUpperCase(), color: station.color,
            model: "sipka.i3d", range: 250, near: 60, hint: "STAND UNDER THE ARROW · PRESS G",
            map: {icon: "square", label: station.mapLabel, size: 6}});
    }
    const payload = JSON.stringify(items);
    if (!force && payload === lastPoints) return;
    lastPoints = payload;
    markersAll("stations", items);
}

// Where a vehicle is, to the nearest few metres: the client shows this when
// the vehicle is not streamed to it, and the coarse step keeps it from
// resending for every metre driven.
function roughPosition(vehicle) {
    const {x, y, z} = vehicle.position;
    const step = (value) => Math.round(value / FLEET_POSITION_STEP) * FLEET_POSITION_STEP;
    return {x: step(x), y, z: step(z)};
}

// Players on the job see where its vehicles are, on the map and up close.
function presentFleet(station) {
    const items = station.vehicles().map((vehicle, index) => ({id: `v${index}`, vehicleId: vehicle.id, position: roughPosition(vehicle),
        label: station.vehicleLabel, color: station.color, range: 40, map: {icon: "triangle", size: 4}}));
    const payload = JSON.stringify(items);
    const crew = new Set(World.getPlayers().filter((player) => station.onDuty(player)).map((player) => player.id));
    for (const id of station.fleetShownTo) {
        if (crew.has(id)) continue;
        const player = playerById(id);
        if (player) markers(player, `fleet:${station.id}`, []);
    }
    for (const id of crew) {
        if (station.fleetShownTo.has(id) && payload === station.lastFleet) continue;
        markers(playerById(id), `fleet:${station.id}`, items);
    }
    station.fleetShownTo = crew;
    station.lastFleet = payload;
}

function spawnAt(station, slot, spot) {
    const vehicle = Vehicle.spawn(station.model, spot.position, spot.heading);
    if (!vehicle) return;
    vehicle.setEngine(false);
    vehicle.state.set("fr.fullTank", true);
    vehicle.state.set("fr.station", station.id);
    vehicle.setRadarMarker(true, station.radar);
    slot.vehicleId = vehicle.id;
    slot.missingSince = null;
    slot.awaySince = null;
    slot.wreckedAt = null;
}

// Takes the slot's vehicle away. An occupied one stays unless `evict`, which
// puts everyone out first: a station that goes away leaves no stray vehicle
// anyone could drive.
function retire(slot, evict = false) {
    const vehicle = vehicleById(slot.vehicleId);
    if (vehicle && evict) for (const occupant of vehicle.getOccupants()) occupant?.removeFromVehicle();
    if (vehicle && (evict || !occupied(vehicle))) vehicle.destroy();
    slot.vehicleId = null;
    slot.awaySince = null;
    slot.wreckedAt = null;
}

function upkeep() {
    if (!World.isReady()) return;
    const now = Date.now();
    const anyone = streetPlayers().length > 0;
    for (const station of stations) {
        const place = station.here();
        if (!place) {
            for (const slot of station.slots) retire(slot, true);
            station.slots = [];
            presentFleet(station);
            continue;
        }
        while (station.slots.length < place.spots.length) station.slots.push({vehicleId: null, missingSince: 0, awaySince: null, wreckedAt: null});
        place.spots.forEach((spot, index) => {
            const slot = station.slots[index];
            const vehicle = vehicleById(slot.vehicleId);
            if (vehicle && slot.wreckedAt !== null && now - slot.wreckedAt >= RESPAWN_MS && !occupied(vehicle)) {
                retire(slot);
                slot.missingSince = now - RESPAWN_MS;
                return;
            }
            if (!vehicle) {
                slot.vehicleId = null;
                slot.missingSince ??= now;
                // The first vehicles are there at once, a replacement after a while.
                if (anyone && now - slot.missingSince >= RESPAWN_MS && spotClear(spot)) spawnAt(station, slot, spot);
                return;
            }
            const away = flatDistance(vehicle.position, spot.position) > ABANDON_REACH;
            const crewNearby = World.getPlayers().some((player) => station.onDuty(player) && distance(player.position, vehicle.position) < CREW_NEARBY);
            if (!away || occupied(vehicle) || crewNearby) {
                slot.awaySince = null;
                return;
            }
            slot.awaySince ??= now;
            if (now - slot.awaySince >= ABANDON_MS) {
                // Towed back: the replacement waits at the spot right away.
                retire(slot);
                slot.missingSince = now - RESPAWN_MS;
            }
        });
        presentFleet(station);
    }
    presentPoints();
}

function stationAt(player) {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD || player.getVehicle()) return null;
    for (const station of stations) {
        const place = station.here();
        if (place && flatDistance(player.position, place.signOn) <= SIGN_ON_RADIUS && Math.abs(player.position.y - place.signOn.y) < 3) return station;
    }
    return null;
}

function tick() {
    const now = Date.now();
    for (const player of World.getPlayers()) {
        const station = stationAt(player);
        const previous = standing.get(player.id) ?? null;
        const onDuty = station ? station.onDuty(player) : false;
        // The prompt follows duty changed elsewhere too (/fire, an arrest).
        if (station === previous?.station && onDuty === previous?.onDuty) continue;
        const arrived = station !== previous?.station;
        if (station) standing.set(player.id, {station, onDuty});
        else standing.delete(player.id);
        if (!station) {
            jobkey.prompt(player, null, null, "station");
            continue;
        }
        jobkey.prompt(player, onDuty ? `[G] CLOCK OFF · ${station.title.toUpperCase()}` : `[G] SIGN ON · ${station.title.toUpperCase()}`, null, "station");
        station.stopGuiding(player);
        if (arrived && !onDuty && now - (introducedAt.get(player.id) ?? 0) >= INTRO_EVERY_MS) {
            introducedAt.set(player.id, now);
            toast(player, station.pitch, {title: station.title, tone: "gold", seconds: 8});
        }
    }
}

// Standing on a sign-on point, G toggles duty there before any job uses it.
jobkey.register({
    press(player) {
        const station = stationAt(player);
        if (!station) return false;
        if (station.onDuty(player)) station.stop(player);
        else station.start(player);
        station.refresh();
        return true;
    },
});

// Taking the wheel, by getting in or by climbing across from another seat.
// A refused steal also puts the driver it threw out back at the wheel.
function takeWheel(vehicle, player, stolen = false) {
    const station = stations.find((candidate) => candidate.isVehicle(vehicle));
    if (!station) return;
    if (!station.onDuty(player) && !station.mayDrive?.(player, vehicle)) {
        player.removeFromVehicle();
        const last = lastDrivers.get(vehicle.id);
        const victim = stolen && last && Date.now() - last.at < STEAL_UNDO_MS ? playerById(last.playerId) : null;
        if (victim && victim.alive && !victim.getVehicle()) victim.putInVehicle(vehicle, 0);
        if (guiding.get(player.id) !== station.id) station.guide(player);
        toast(player, `Only ${station.crew} on duty drive this. Sign on under the arrow at the ${station.mapLabel.toLowerCase()} first: ` +
            "it is on your compass and on the city map (TAB).", {title: station.title, tone: "bad", seconds: 6});
        return;
    }
    station.onDrive?.(player, vehicle);
}

function leaveWheel(vehicle, player) {
    const station = stations.find((candidate) => candidate.isVehicle(vehicle));
    if (!station) return;
    lastDrivers.set(vehicle.id, {playerId: player.id, at: Date.now()});
    if (station.onDuty(player)) station.onLeave?.(player, vehicle);
}

Events.on("vehiclePlayerEntered", (vehicle, player, {seat, stolen}) => {
    if (seat === 0) takeWheel(vehicle, player, stolen);
});

Events.on("vehiclePlayerSeatChanged", (vehicle, player, {seat, fromSeat}) => {
    if (fromSeat === 0) leaveWheel(vehicle, player);
    if (seat === 0) takeWheel(vehicle, player);
});

Events.on("vehiclePlayerExited", (vehicle, player, {seat}) => {
    if (seat === 0) leaveWheel(vehicle, player);
});

// state 1 burnt out, 2 lost: either way a new one is sent later.
Events.on("vehicleTerminal", (vehicle) => {
    for (const station of stations) {
        const slot = station.slots.find((candidate) => candidate.vehicleId === vehicle.id);
        if (slot) slot.wreckedAt ??= Date.now();
    }
});

Events.on("playerSpawn", (player) => {
    if (lastPoints) markers(player, "stations", JSON.parse(lastPoints));
});

Events.on("playerDisconnect", (player) => {
    standing.delete(player.id);
    introducedAt.delete(player.id);
    guiding.delete(player.id);
});

Events.on("vehicleDestroy", (vehicle) => lastDrivers.delete(vehicle.id));

// The mission's cars go with it; the new city gets its own. Nobody stands
// on a point there yet, and no compass points at the old one.
Events.on("missionChange", () => {
    for (const station of stations) {
        for (const slot of station.slots) retire(slot);
        station.slots = [];
        station.fleetShownTo.clear();
        station.lastFleet = "";
    }
    for (const id of standing.keys()) {
        const player = playerById(id);
        if (player) jobkey.prompt(player, null, null, "station");
    }
    for (const [id, stationId] of guiding) {
        const player = playerById(id);
        if (player) compass(player, `station:${stationId}`, COMPASS_PRIORITY, null);
    }
    standing.clear();
    guiding.clear();
    lastDrivers.clear();
    lastPoints = "";
});

const ticker = setInterval(tick, TICK_MS);
const upkeeper = setInterval(upkeep, UPKEEP_MS);
onStop(() => {
    clearInterval(ticker);
    clearInterval(upkeeper);
    for (const station of stations) for (const slot of station.slots) retire(slot, true);
});

module.exports = {define, SIGN_ON_RADIUS};
