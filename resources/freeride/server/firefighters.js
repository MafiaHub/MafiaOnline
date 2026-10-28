// The fire brigade. A player goes on duty at the fire station (the stock
// models/9hasic.4ds the city scene places in the Works Quarter, garage doors
// on the street) in the stock fireman's clothes (Hasic01) with the stock fire
// truck. Fires break out across the city on the road graph: a house front,
// a burning car, a crash with people trapped in it. Firefighters put them out
// with the hose (hold G within reach of their truck, or from the truck's
// roof cannon while at its wheel): the native water jet arcs onto the fire
// and every hose on it takes the flames down, so a crew is faster than one.
// Trapped civilians are led out with G. Everyone sees the fires; the pay for
// one is split by how much water each firefighter put on it.
const roads = require("./lib/roads.js");
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const jobkey = require("./lib/jobkey.js");
const {toast, toastAll, card, compass, markers, markersAll, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, flatDistance, playable, playerById, vehicleById, formatMoney, headingOf, pick, onStart, onStop, inFreeRide} =
    require("./lib/util.js");

const UNIFORM = "Hasic01.i3d";
const TRUCK = "fire00.i3d";
// The station on the city maps: 9hasic.4ds sits unrotated at
// (-1158.59, -5.92, 306.54) in both FREERIDE and Freeride Extended cache.bin;
// its garage doors (@VRAT02) span x -1171.4..-1142.7 at z 296.5 facing the
// street whose road graph line runs along z 284.4.
const CITY_STATION = {name: "the fire station", position: {x: -1150.5, y: -5.9, z: 293.0},
    truck: {position: {x: -1158.0, y: -5.4, z: 289.5}, heading: Math.PI}};
const STATIONS = {freeride: CITY_STATION, freeride_extended: CITY_STATION, freeridenoc: CITY_STATION};
const STATION_REACH = 40;
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
const PAY = {house: 420, car: 260, crash: 320};
const RESCUE_PAY = 120;
const XP = 70;
const RESCUE_XP = 40;
const CIVILIANS = ["Delnik01.i3d", "Delnik02.i3d", "Czena01.i3d", "Czena02.i3d", "Homeless01.i3d"];
const WRECKS = ["fordtfor00.i3d", "chev00.i3d", "forafo00.i3d", "foraca00.i3d"];
// Stock clips (anims\ in a4.dta) the retail panicking walkers use
// (C_human anim_info 337, 331): lying on the ground, crouched.
const LYING = "x panika leh stativ.i3d";
const CROUCHED = "x panika drep stativ.i3d";

const crews = new Map();
const incidents = new Map();
let nextIncident = 1;
let nextFireAt = 0;
let timer = null;
let lastCardAt = 0;
let ambulance = null;
let lastMarkers = "";
let lastItems = [];
// Civilians walking home after a rescue, removed a little later.
const strays = new Set();

function available() {
    return typeof Fire !== "undefined" && typeof WaterJet !== "undefined";
}

function station() {
    return STATIONS[World.getMission()] ?? null;
}

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

function truckOf(crew) {
    return vehicleById(crew.truckId);
}

function presentIncidents() {
    const items = [];
    for (const incident of incidents.values()) {
        const fire = incident.fire;
        if (!fire) continue;
        const burning = fire.burning;
        items.push({id: `fire${incident.id}`, position: incident.position, label: burning ? `${incident.label} · ${Math.round(fire.intensity * 10) * 10}%` : "FIRE OUT",
            color: burning ? COLORS.red : COLORS.muted, beacon: burning, range: 1200});
        for (const civilian of incident.civilians) {
            if (civilian.state === "trapped" && civilian.npc) {
                items.push({id: `trapped${incident.id}_${civilian.index}`, position: civilian.npc.position, label: "TRAPPED", color: COLORS.gold, range: 150});
            }
        }
    }
    // Sent again only when something on it changed.
    const payload = JSON.stringify(items);
    if (payload === lastMarkers) return;
    lastMarkers = payload;
    lastItems = items;
    markersAll("fires", items);
}

function spawnCivilian(incident, position, clip, index) {
    if (typeof Npc === "undefined") return;
    const npc = Npc.spawn(pick(CIVILIANS), position, Math.random() * Math.PI * 2, {behaviour: "neutral", health: 100});
    if (!npc) return;
    incident.civilians.push({index, npc, clip, state: "spawning", leaderId: null});
}

function startIncident(kind, origin) {
    if (!available() || !inFreeRide() || !World.isReady() || incidents.size >= MAX_INCIDENTS) return null;
    const crash = kind === "crash";
    const house = kind === "house";
    const spot = roadside(origin, 250, 1100, house ? 9 : 3.6);
    const id = nextIncident++;
    const incident = {id, kind, position: spot.position, startedAt: Date.now(), vehicleId: null, fire: null, civilians: [],
        generation: World.getMissionGeneration(), label: house ? "HOUSE FIRE" : crash ? "CRASH · PEOPLE TRAPPED" : "CAR FIRE", where: place(spot.position)};
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
    const crewCount = crews.size;
    toastAll(`${incident.label === "HOUSE FIRE" ? "A house is on fire" : crash ? "A car crashed and caught fire with people inside" : "A car is burning"} ${incident.where}.` +
        (crewCount ? "" : " The fire station has nobody on duty: /fire"), {title: "Fire", tone: "bad", seconds: 7});
    feed("fire", [text("Fire", COLORS.red), text(` · ${incident.label.toLowerCase()} ${incident.where}`)]);
    presentIncidents();
    return incident;
}

function endIncident(incident, message, smokeMs = SMOKE_MS) {
    incidents.delete(incident.id);
    for (const civilian of incident.civilians) civilian.npc?.remove();
    for (const crew of crews.values()) if (crew.helping === incident.id) crew.helping = null;
    const fire = incident.fire;
    const wreckId = incident.vehicleId;
    // The smoke stays a while, then the wreck is towed away with it.
    const clear = () => {
        fire?.destroy();
        const wreck = vehicleById(wreckId);
        if (wreck && wreck.getOccupants().every((occupant) => occupant === null)) wreck.destroy();
    };
    if (smokeMs > 0) setTimeout(clear, smokeMs);
    else clear();
    if (message) feed("fire", [text("Fire", COLORS.muted), text(` · ${message}`)]);
    presentIncidents();
}

function incidentOfFire(fire) {
    for (const incident of incidents.values()) if (incident.fire && incident.fire.id === fire.id) return incident;
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

// Where water from this firefighter can go now: the cannon from the truck's
// driver seat, the hose on foot within reach of the truck.
function hoseFor(player, crew) {
    const truck = truckOf(crew);
    if (!truck) return null;
    if (player.getVehicle()?.id === truck.id) return player.getSeat() === 0 ? {jet: crew.cannon, reach: CANNON_REACH, from: truck.position} : null;
    if (player.getVehicle()) return null;
    if (distance(player.position, truck.position) > HOSE_TO_TRUCK) return null;
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
    const heading = headingOf(onFoot ? player.rotation : truckOf(crew).rotation);
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
    toast(player, "They are on their feet. Lead them well clear of the fire.", {title: "Rescue", tone: "gold"});
    presentIncidents();
}

function rescued(incident, civilian) {
    const player = playerById(civilian.leaderId);
    civilian.state = "safe";
    if (player) {
        player.giveMoney(RESCUE_PAY);
        const crew = crews.get(player.id);
        if (crew) crew.earned += RESCUE_PAY;
        profiles.bump(player, "rescues");
        profiles.addXp(player, RESCUE_XP, "Led someone out of a fire");
        toast(player, `Safe. ${formatMoney(RESCUE_PAY)} from the city.`, {title: "Rescue", tone: "good"});
        feed("fire", [name(player, COLORS.red), text(" pulled someone out of a fire")]);
    }
    // A medic on duty takes them to hospital; otherwise they go home.
    const npc = civilian.npc;
    civilian.npc = null;
    if (!ambulance?.takePatient(npc, incident.position, `Smoke victim ${incident.where}`)) {
        npc.walkTo({x: npc.position.x + 20, y: npc.position.y, z: npc.position.z + 20});
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
        player.setModel(crew.model);
        // Not into a cell: the jail took the player off the job and their guns.
        if (player.alive && !life.activity(player)) life.equip(player, crew.loadout.weapons, crew.loadout.selected);
    }
    const truck = truckOf(crew);
    if (truck) setTimeout(() => {
        const current = vehicleById(crew.truckId);
        if (current && current.getOccupants().every((occupant) => occupant === null)) current.destroy();
    }, 3000);
    if (message) toast(player, `${message} You earned ${formatMoney(crew.earned)} this shift.`, {title: "Fire brigade"});
}

function startDuty(player) {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) return;
    if (!available()) {
        toast(player, "The fire brigade needs a newer server.", {tone: "bad"});
        return;
    }
    const home = station();
    if (!home) {
        toast(player, "There is no fire station out here.", {tone: "bad"});
        return;
    }
    if (distance(player.position, home.position) > STATION_REACH) {
        compass(player, "fire", 44, {position: home.position});
        toast(player, "Report to the fire station in the Works Quarter. Follow the compass, then /fire again.", {title: "Fire brigade", tone: "gold"});
        return;
    }
    if (!life.begin(player, "fire", "Fire duty")) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    if (player.getVehicle()) player.removeFromVehicle();
    const truck = Vehicle.spawn(TRUCK, home.truck.position, home.truck.heading, player);
    if (!truck) {
        life.end(player, "fire");
        toast(player, "The truck is out on another call. Try again shortly.", {tone: "bad"});
        return;
    }
    const hose = WaterJet.create(player);
    const cannon = WaterJet.create(truck, {pressure: 24, player});
    if (!hose || !cannon) {
        hose?.destroy();
        cannon?.destroy();
        truck.destroy();
        life.end(player, "fire");
        toast(player, "The station has no hose free. Try again shortly.", {tone: "bad"});
        return;
    }
    truck.state.set("fr.fullTank", true);
    truck.state.set("fr.owner", player.nickname);
    truck.setRadarMarker(true, 0xd0574a);
    const crew = {model: player.model, loadout: life.loadoutOf(player), truckId: truck.id, earned: 0, fires: 0,
        hose, cannon, helping: null};
    crews.set(player.id, crew);
    player.setModel(UNIFORM);
    life.giveKit(player, {weapons: [], selected: 0});
    profiles.setPublic(player, "fr.job", "fire");
    player.putInVehicle(truck, 0);
    feed("fire", [name(player, COLORS.red), text(" joined the fire brigade")]);
    toast(player, "On duty. Fires show on your compass and map. Park near one and hold G to spray: on foot within 35 m of your truck, " +
        "or from the roof cannon at the wheel. G next to a trapped civilian leads them out. /fire to clock off.",
        {title: "Fire brigade", tone: "gold", seconds: 10});
    if (!incidents.size) nextFireAt = Math.min(nextFireAt, Date.now() + 30_000);
}

function nearestIncident(position) {
    return [...incidents.values()].filter((incident) => incident.fire?.burning || incident.civilians.some((civilian) => civilian.state === "trapped"))
        .map((incident) => ({incident, gap: flatDistance(incident.position, position)})).sort((a, b) => a.gap - b.gap)[0] ?? null;
}

function tick() {
    const now = Date.now();
    if (!inFreeRide() || !World.isReady()) return;
    // A new fire now and then; more often with the brigade on duty. Only on
    // maps with a fire station to call.
    if (available() && station() && now >= nextFireAt && streetPlayers().length) {
        nextFireAt = now + (crews.size ? FIRE_EVERY_MS : QUIET_FIRE_EVERY_MS);
        const anchor = pick(crews.size ? [...crews.keys()].map(playerById).filter(Boolean) : streetPlayers());
        startIncident(pick(["house", "house", "car", "crash"]), anchor?.position ?? null);
    }
    for (const incident of [...incidents.values()]) {
        if (incident.generation !== World.getMissionGeneration()) {
            incidents.delete(incident.id);
            continue;
        }
        const fire = incident.fire;
        // Trapped civilians lie down once the host made them.
        for (const civilian of incident.civilians) {
            if (civilian.state === "spawning" && civilian.npc && civilian.ready) {
                civilian.state = "trapped";
                civilian.npc.playAnimation?.(civilian.clip, {loop: true});
            } else if (civilian.state === "following" && civilian.npc) {
                const leader = playerById(civilian.leaderId);
                if (!leader || !leader.alive) {
                    civilian.state = "trapped";
                    civilian.npc.stop?.();
                    civilian.npc.playAnimation?.(CROUCHED, {loop: true});
                } else if (flatDistance(civilian.npc.position, incident.position) >= SAFE_DISTANCE) {
                    rescued(incident, civilian);
                }
            }
        }
        const trapped = incident.civilians.some((civilian) => civilian.state === "trapped" || civilian.state === "spawning" || civilian.state === "following");
        if (fire && !fire.burning && !trapped) {
            endIncident(incident, null);
            continue;
        }
        const age = now - incident.startedAt;
        if (incident.vehicleId && fire?.burning && age >= CAR_BLOWS_AFTER_MS && !incident.blown) {
            incident.blown = true;
            vehicleById(incident.vehicleId)?.explode();
        }
        if (age >= INCIDENT_LIFETIME_MS) {
            fire?.extinguish();
            endIncident(incident, `the fire ${incident.where} burned itself out`);
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
        compass(player, "fire", 44, nearest ? {position: nearest.incident.position} : null);
        const rescue = rescueNear(player);
        const hose = hoseFor(player, crew);
        const hint = rescue ? "[G] LEAD THEM OUT" : crew.spraying ? "SPRAYING · RELEASE G TO STOP"
            : hose && nearest && nearest.gap < hose.reach + 10 ? "[G] HOLD TO SPRAY" : null;
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
            const truck = truckOf(crew);
            const rows = [];
            if (nearest) {
                rows.push([nearest.incident.label.split(" · ")[0].toLowerCase().replace(/^./, (c) => c.toUpperCase()),
                    `${Math.round(nearest.gap)} m · ${Math.round((nearest.incident.fire?.intensity ?? 0) * 100)}%`]);
                const waiting = nearest.incident.civilians.filter((civilian) => civilian.state === "trapped").length;
                if (waiting) rows.push(["Trapped", `${waiting}`]);
            }
            rows.push(["Truck", truck ? `${Math.round(distance(player.position, truck.position))} m` : "gone"]);
            rows.push(["This shift", `${crew.fires} fire${crew.fires === 1 ? "" : "s"} · ${formatMoney(crew.earned)}`]);
            card(player, "fire", {title: "Fire brigade", subtitle: nearest ? `A fire ${nearest.incident.where}` : "All quiet. Wait at the station.", rows, tone: "bad"});
        }
    }
}

jobkey.register({
    press(player) {
        const crew = crews.get(player.id);
        if (!crew || !playable(player)) return false;
        const rescue = rescueNear(player);
        if (rescue && !player.getVehicle()) {
            leadOut(player, rescue.incident, rescue.civilian);
            return true;
        }
        if (!hoseFor(player, crew)) {
            toast(player, "Too far from your truck: the hose reaches 35 m.", {tone: "bad", seconds: 3});
            return true;
        }
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
    }
});

Events.on("npcDeath", (npc) => {
    for (const incident of incidents.values()) {
        const civilian = incident.civilians.find((entry) => entry.npc && entry.npc.id === npc.id);
        if (civilian) civilian.state = "dead";
    }
});

// A civilian the host could not make, or lost: nobody is left to lead out.
Events.on("npcRetire", (npc) => {
    for (const incident of incidents.values()) {
        const civilian = incident.civilians.find((entry) => entry.npc && entry.npc.id === npc.id);
        if (!civilian) continue;
        civilian.npc = null;
        if (civilian.state !== "dead") civilian.state = "gone";
    }
    for (const stray of strays) if (stray.id === npc.id) strays.delete(stray);
});

Events.on("fireExtinguished", (fire, info) => {
    const incident = incidentOfFire(fire);
    if (!incident) return;
    if (info.reason !== "doused") return;
    const pay = PAY[incident.kind] ?? 300;
    const crewNames = [];
    for (const {player, share} of info.contributors) {
        if (!player || share <= 0) continue;
        const cut = Math.max(25, Math.round(pay * share));
        player.giveMoney(cut);
        const crew = crews.get(player.id);
        if (crew) {
            crew.earned += cut;
            crew.fires++;
        }
        profiles.bump(player, "firesOut");
        profiles.addXp(player, Math.round(20 + XP * share), "Put out a fire");
        toast(player, `Fire out. Your share: ${formatMoney(cut)} (${Math.round(share * 100)}% of the water).`, {title: "Fire brigade", tone: "good"});
        crewNames.push(player);
    }
    if (crewNames.length) {
        feed("fire", [...crewNames.flatMap((player, index) => [index ? text(", ") : text(""), name(player, COLORS.red)]),
            text(` put out the ${incident.label.split(" · ")[0].toLowerCase()} ${incident.where}`)]);
    }
    presentIncidents();
});

commands.register({name: "fire", aliases: ["firefighter", "fireman"], usage: "/fire", help: "Go on or off duty with the fire brigade.", category: "City"}, (player) => {
    if (crews.has(player.id)) endDuty(player, "Off duty. Truck back in the station.");
    else startDuty(player);
});

commands.register({name: "fires", usage: "/fires", help: "Where the city is burning.", category: "City"}, (player) => {
    const list = [...incidents.values()].filter((incident) => incident.fire?.burning);
    if (!list.length) {
        toast(player, "No fires right now.", {title: "Fire brigade"});
        return;
    }
    const nearest = nearestIncident(player.position);
    if (nearest) compass(player, "fire", 44, {position: nearest.incident.position});
    toast(player, list.map((incident) => `${incident.label} ${incident.where}`).join(" · "), {title: "Fires", tone: "bad"});
});

menu.provide("fire", (player) => ({onDuty: crews.has(player.id), crew: crews.size, available: Boolean(station()) && available(),
    fires: [...incidents.values()].filter((incident) => incident.fire?.burning).length, earned: crews.get(player.id)?.earned ?? 0}));

function stationMarkers() {
    const home = station();
    markersAll("firestation", home && available() ? [{id: "station", position: home.position, label: "FIRE STATION · /fire", color: COLORS.red, range: 120}] : []);
}

onStart(() => {
    nextFireAt = Date.now() + FIRST_FIRE_MS;
    timer = setInterval(tick, TICK_MS);
    stationMarkers();
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
});
Events.on("playerSpawn", (player) => {
    const home = station();
    if (home && available()) markers(player, "firestation", [{id: "station", position: home.position, label: "FIRE STATION · /fire", color: COLORS.red, range: 120}]);
    // The fires burning before this player came: markersAll sends only changes.
    if (lastItems.length) markers(player, "fires", lastItems);
});
Events.on("playerDisconnect", (player) => {
    const crew = crews.get(player.id);
    if (!crew) return;
    crews.delete(player.id);
    crew.hose?.destroy();
    crew.cannon?.destroy();
    const truck = truckOf(crew);
    if (truck && truck.getOccupants().every((occupant) => occupant === null)) truck.destroy();
});
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) endDuty(player, "Your shift ended with the city reloading.");
    incidents.clear();
    strays.clear();
    lastMarkers = "";
    lastItems = [];
    nextFireAt = Date.now() + FIRST_FIRE_MS;
    stationMarkers();
});

module.exports = {
    onDuty: (player) => crews.has(player.id),
    startIncident,
    incidents: () => [...incidents.values()],
    // The next fire breaks out in ms (dev tools, tests).
    schedule(ms) { nextFireAt = Date.now() + ms; },
    // ambulance.js offers to take smoke victims.
    setAmbulance(handler) { ambulance = handler; },
};
