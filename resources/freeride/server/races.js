// Street races. Anyone opens a lobby on a route; everyone in the city is
// invited, drives to the start line and is put on a starting grid. The server
// places the cars, runs the countdown, checks every checkpoint against the
// car's replicated position and pays the podium. Built-in routes follow the
// stock road graph; players can record their own with /race create.
const fs = require("node:fs");
const path = require("node:path");
const roads = require("./lib/roads.js");
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const notify = require("./lib/notify.js");
const util = require("./lib/util.js");

const {toast, card, compass, markers, feed, name, text, COLORS} = notify;
const {distance, flatDistance, playable, formatMoney, formatDuration, formatMinutes, emit} = util;

const FILE = path.join(profiles.DATA_DIR, "races.json");
const LOBBY_MS = 45_000;
const COUNTDOWN_MS = 4_000;
const TICK_MS = 200;
const STATE_EVERY_MS = 500;
const START_REACH = 150;
const CHECKPOINT_RADIUS = 16;
const CHECKPOINT_HEIGHT = 9;
const OUT_OF_CAR_MS = 15_000;
const FINISH_GRACE_MS = 90_000;
const RACE_LIMIT_MS = 15 * 60_000;
const MAX_RACES = 3;
const MAX_RACERS = 12;
const RECORD_BONUS = 200;
const SOLO_PRIZE = 120;
const SOLO_COOLDOWN_MS = 5 * 60_000;
const MIN_CUSTOM_LENGTH = 600;
const CREATE_COOLDOWN_MS = 5 * 60_000;
const MIN_CUSTOM_POINTS = 3;
const MAX_CUSTOM_POINTS = 40;
const MIN_CUSTOM_SPACING = 40;
const MAX_CUSTOM_ROUTES = 60;
const MAX_ROUTES_PER_AUTHOR = 5;
// Circuit races put every entrant on the grid in the same car, wherever they
// were, and bring them back to that spot afterwards.
const CIRCUIT_CAR = "speedster00.i3d";
const CIRCUIT_RETURN_MS = 10_000;

// Each map lists its built-in routes in lib/maps.js as landmark waypoints;
// the street line between them is the map's road graph's shortest path.
const routesByMap = new Map();
let customRoutes = [];
let records = {};
const races = new Map();
const recordings = new Map();
const lastSoloPrize = new Map();
const lastCreated = new Map();
let nextRaceId = 1;
let tickTimer = null;
let saveTimer = null;

function slug(value) {
    return value.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
}

// A race of `laps` laps on the map's circuit, for the route book and the
// championship.
function circuitRoute(map, laps, name, description, id = `b:${map.id}:circuit-${laps}`) {
    const {lap, grid, heading} = map.circuit;
    let length = 0;
    for (let index = 0; index < lap.length; index++) length += flatDistance(lap[index], lap[(index + 1) % lap.length]);
    const slots = grid.map((point) => ({position: {x: point.x, y: point.y + 0.6, z: point.z}, heading}));
    return {id, name, description, builtIn: true, author: null, circuit: true, laps,
        checkpoints: Array.from({length: laps}, () => lap).flat(), grid: slots, start: slots[0].position, length: length * laps};
}

function builtIn(map) {
    if (routesByMap.has(map.id)) return routesByMap.get(map.id);
    const graph = roads.forMap(map);
    const built = [];
    if (map.circuit) {
        built.push(circuitRoute(map, 1, "Autodrom Sprint", "One lap of the official circuit, in identical cars."));
        built.push(circuitRoute(map, 3, "Autodrom Grand Prix", "Three laps of the official circuit, in identical cars."));
    }
    for (const spec of map.routes) {
        const line = graph.route(spec.via.map((key) => maps.landmark(key).position));
        if (!line) {
            console.error(`[freeride] Race route ${spec.name} has no street path on ${map.id}`);
            continue;
        }
        built.push({id: `b:${map.id}:${spec.id}`, name: spec.name, description: spec.description, builtIn: true, author: null,
            checkpoints: roads.checkpoints(line), grid: roads.gridSlots(line, MAX_RACERS), start: line[0], length: roads.lineLength(line)});
    }
    routesByMap.set(map.id, built);
    return built;
}

// A recorded route starts where its author stood. The grid lines up behind
// that spot on the road the author drove in on, facing the way the car faced.
function customRoute(stored) {
    const start = stored.start.position;
    const dx = Math.sin(stored.start.heading);
    const dz = Math.cos(stored.start.heading);
    const grid = [];
    for (let slot = 0; slot < MAX_RACERS; slot++) {
        const row = Math.floor(slot / 2);
        const side = slot % 2 === 0 ? -2.3 : 2.3;
        grid.push({position: {x: start.x - dx * row * 8 + dz * side, y: start.y + 0.6, z: start.z - dz * row * 8 - dx * side},
            heading: stored.start.heading});
    }
    let length = 0;
    let previous = start;
    for (const point of stored.checkpoints) {
        length += flatDistance(previous, point);
        previous = point;
    }
    return {id: `c:${stored.id}`, name: stored.name, description: `Recorded by ${stored.author}.`, builtIn: false,
        author: stored.author, checkpoints: stored.checkpoints, grid, start, length};
}

// The routes of the loaded map; routes recorded before maps existed belong to
// stock Lost Heaven.
function allRoutes() {
    const map = maps.current();
    if (!map) return [];
    return [...builtIn(map), ...customRoutes.filter((route) => (route.mission ?? "freeride") === map.id).map(customRoute)];
}

function load() {
    try {
        const stored = JSON.parse(fs.readFileSync(FILE, "utf8"));
        customRoutes = Array.isArray(stored.routes) ? stored.routes : [];
        records = stored.records && typeof stored.records === "object" ? stored.records : {};
    } catch (error) {
        if (error.code !== "ENOENT") console.error(`[freeride] Could not read ${FILE}: ${error.message}`);
    }
}

function save() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => {
        try {
            fs.mkdirSync(profiles.DATA_DIR, {recursive: true});
            fs.writeFileSync(`${FILE}.tmp`, JSON.stringify({version: 1, routes: customRoutes, records}, null, 1));
            fs.renameSync(`${FILE}.tmp`, FILE);
        } catch (error) {
            console.error(`[freeride] Could not save races: ${error.message}`);
        }
    }, 1000);
}

function findRoute(query) {
    const list = allRoutes();
    const text = String(query ?? "").trim().toLowerCase();
    if (!text) return null;
    if (/^\d+$/.test(text)) return list[Number(text) - 1] ?? null;
    return list.find((route) => route.id.toLowerCase() === text || route.name.toLowerCase() === text)
        ?? list.find((route) => slug(route.name).startsWith(slug(text))) ?? null;
}

// A finished racer stays in the standings until the race is paid out but is
// free to do something else; only an unfinished entry counts as racing.
function raceOf(player) {
    for (const race of races.values()) if (race.racers.get(player.id)?.finishedMs === null) return race;
    return null;
}

function openLobby() {
    return [...races.values()].find((race) => race.phase === "lobby") ?? null;
}

function racerList(race) {
    return [...race.racers.values()];
}

function lengthText(route) {
    return `${(route.length / 1000).toFixed(1)} km`;
}

function lobbyCard(race, racer) {
    const circuit = race.route.circuit;
    const atStart = circuit || (racer.player.getSeat() === 0 && distance(racer.player.getVehicle().position, race.route.start) <= START_REACH);
    card(racer.player, "race", {
        title: race.route.name, subtitle: circuit ? `Circuit · ${race.route.laps} lap${race.route.laps === 1 ? "" : "s"} · ${lengthText(race.route)}`
            : `Street race · ${lengthText(race.route)} · ${race.route.checkpoints.length} checkpoints`,
        rows: [["Starts in", formatMinutes(race.lobbyEndsAt - Date.now())], ["Racers", String(race.racers.size)],
            ["You", circuit ? "You will be put on the grid" : atStart ? "At the start line" : racer.player.getSeat() === 0 ? "Drive to the start" : "Get a car (/car)"]],
        progress: Math.max(0, (race.lobbyEndsAt - Date.now()) / race.lobbyMs), tone: atStart ? "good" : "gold",
    });
}

// Back to where the player was before the circuit took them, once they are
// out of the race; the race car goes.
function sendHome(racer) {
    if (!racer.returnTo) return;
    const {player, returnTo, raceCarId} = racer;
    racer.returnTo = null;
    setTimeout(() => {
        const car = util.vehicleById(raceCarId);
        const current = life.activity(player);
        if (World.getPlayers().some((other) => other.id === player.id) && player.alive && !raceOf(player) && !current) {
            if (player.getVehicle()) player.removeFromVehicle();
            life.teleport(player, returnTo.position, returnTo.heading);
        }
        if (car && car.getOccupants().every((occupant) => occupant === null)) car.destroy();
    }, CIRCUIT_RETURN_MS);
}

function routeMarkers(race, racer) {
    const cps = race.route.checkpoints;
    const items = [];
    for (let index = racer.cp; index < Math.min(cps.length, racer.cp + 2); index++) {
        const last = index === cps.length - 1;
        items.push({id: `cp${index}`, position: cps[index], label: last ? "FINISH" : `CHECKPOINT ${index + 1}/${cps.length}`,
            color: index === racer.cp ? (last ? COLORS.green : COLORS.gold) : COLORS.muted, beacon: true, primary: index === racer.cp});
    }
    markers(racer.player, "race", items);
    compass(racer.player, "race", 100, cps[racer.cp] ? {position: cps[racer.cp]} : null);
}

function leaveRace(race, racer, message) {
    race.racers.delete(racer.player.id);
    sendHome(racer);
    life.end(racer.player, "race-lobby");
    life.end(racer.player, "race");
    card(racer.player, "race", null);
    markers(racer.player, "race", []);
    compass(racer.player, "race", 100, null);
    emit(racer.player, "fr:race:state", {active: false});
    if (message) toast(racer.player, message, {tone: "bad"});
}

function closeRace(race) {
    for (const racer of racerList(race)) leaveRace(race, racer, null);
    races.delete(race.id);
}

function join(race, player) {
    if (race.racers.has(player.id)) return true;
    if (race.racers.size >= MAX_RACERS) {
        toast(player, "That race is full.", {tone: "bad"});
        return false;
    }
    if (!life.begin(player, "race-lobby", `Race lobby · ${race.route.name}`)) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return false;
    }
    const racer = {player, cp: 0, finishedMs: null, dnf: false, outOfCarSince: null, slot: null, returnTo: null, raceCarId: null};
    race.racers.set(player.id, racer);
    if (race.route.circuit) {
        toast(player, `You're in. A race car will be waiting for you on the grid in ${formatMinutes(race.lobbyEndsAt - Date.now())}.`, {title: race.route.name, tone: "gold"});
    } else {
        compass(player, "race", 100, {position: race.route.start});
        markers(player, "race", [{id: "start", position: race.route.start, label: "START LINE", color: COLORS.gold, beacon: true, primary: true}]);
        toast(player, `You're in. Be at the start line in a car within ${formatMinutes(race.lobbyEndsAt - Date.now())}.`, {title: race.route.name, tone: "gold"});
    }
    for (const other of racerList(race)) if (other.player.id !== player.id) toast(other.player, `${player.nickname} joined the race.`);
    return true;
}

// A player opens a lobby, or with host null a scripted event such as a
// championship round does: options.title names it, options.lobbyMs sets the
// wait and options.onConclude(finishers, entrants) receives the result.
function openRace(host, route, options = {}) {
    const refuse = (message) => {
        if (host) toast(host, message, {tone: "bad"});
        else console.warn(`[freeride] ${message}`);
        return null;
    };
    if (host && !playable(host)) return null;
    if (host && raceOf(host)) return refuse("You are already in a race. /race leave first.");
    if (openLobby()) return refuse("A race lobby is already open. Join it with /race join.");
    if (races.size >= MAX_RACES) return refuse("The streets are busy with races. Try again in a minute.");
    const lobbyMs = options.lobbyMs ?? LOBBY_MS;
    const race = {id: nextRaceId++, route, host: host?.nickname ?? options.title ?? "City Hall", generation: World.getMissionGeneration(), phase: "lobby",
        racers: new Map(), lobbyMs, lobbyEndsAt: Date.now() + lobbyMs, startedAt: 0, firstFinishAt: 0, lastStateAt: 0, entrants: 0,
        onConclude: options.onConclude ?? null};
    races.set(race.id, race);
    if (host && !join(race, host)) {
        races.delete(race.id);
        return null;
    }
    for (const player of World.getPlayers()) {
        if (player.id === host?.id || !player.spawned) continue;
        toast(player, `${race.host} opened ${route.circuit ? "a circuit race" : "a street race"}: ${route.name}, ${lengthText(route)}. Type /race join or use F2.`,
            {title: "Race invitation", tone: "gold", seconds: 9});
    }
    feed("race", [host ? name(host, COLORS.gold) : text(race.host, COLORS.gold), text(" opened a race on "), text(route.name, COLORS.white)]);
    return race;
}

// Puts a circuit entrant in a fresh race car on their grid slot.
function seatOnGrid(racer, slot) {
    const {player} = racer;
    racer.returnTo = {position: util.plain(player.position), heading: util.headingOf(player.rotation)};
    if (player.getVehicle()) player.removeFromVehicle();
    if (!life.teleport(player, slot.position, slot.heading)) return false;
    const car = Vehicle.spawn(CIRCUIT_CAR, slot.position, slot.heading, player);
    if (!car) return false;
    racer.raceCarId = car.id;
    car.state.set("fr.fullTank", true);
    car.state.set("fr.owner", player.nickname);
    return player.putInVehicle(car, 0);
}

function startCountdown(race) {
    const ready = [];
    for (const racer of racerList(race)) {
        if (race.route.circuit) {
            // Moving to the grid starts a new native life; nobody shakes off the police that way.
            if (!playable(racer.player) || racer.player.virtualWorld !== util.MAIN_WORLD || (Police.getRecord(racer.player)?.level ?? 0) > 0) {
                leaveRace(race, racer, "Only players on the street with a clean record can be put on the grid.");
                continue;
            }
            ready.push(racer);
            continue;
        }
        const car = racer.player.getSeat() === 0 ? racer.player.getVehicle() : null;
        if (!playable(racer.player) || !car || car.terminalState !== 0 || distance(car.position, race.route.start) > START_REACH) {
            leaveRace(race, racer, "You were not at the start line in a car. The race went without you.");
            continue;
        }
        ready.push(racer);
    }
    if (!ready.length) {
        closeRace(race);
        return;
    }
    race.phase = "countdown";
    race.entrants = ready.length;
    race.startedAt = Date.now() + COUNTDOWN_MS;
    // The server owns the grid: each car is put on its slot, repaired, filled
    // up and held there until the lights go green. Grid places are drawn by
    // lot so joining first is no advantage.
    const order = ready.map((racer) => ({racer, lot: Math.random()})).sort((a, b) => a.lot - b.lot).map((entry) => entry.racer);
    order.forEach((racer, index) => {
        life.end(racer.player, "race-lobby");
        life.begin(racer.player, "race", `${race.route.circuit ? "Circuit race" : "Street race"} · ${race.route.name}`);
        racer.slot = race.route.grid[index];
        if (race.route.circuit && !seatOnGrid(racer, racer.slot)) {
            leaveRace(race, racer, "Your race car could not be brought to the grid.");
            return;
        }
        const car = racer.player.getVehicle();
        car.setTransform(racer.slot.position, racer.slot.heading, {x: 0, y: 0, z: 0});
        car.repair();
        car.state.set("fr.fullTank", true);
        if (car.dynamicsValid && car.fuelTankCapacity > 0) car.setFuel(car.fuelTankCapacity);
        card(racer.player, "race", {title: race.route.name, subtitle: `Grid slot ${index + 1} of ${ready.length}`, rows: [["Status", "Get ready"]], tone: "gold"});
        emit(racer.player, "fr:race:countdown", {seconds: Math.round(COUNTDOWN_MS / 1000) - 1});
        routeMarkers(race, racer);
    });
}

function placeOrder(race) {
    const cps = race.route.checkpoints;
    return racerList(race).sort((a, b) => {
        if (a.finishedMs !== null || b.finishedMs !== null) return (a.finishedMs ?? Infinity) - (b.finishedMs ?? Infinity);
        if (a.cp !== b.cp) return b.cp - a.cp;
        const carA = a.player.getVehicle();
        const carB = b.player.getVehicle();
        const nextA = carA && cps[a.cp] ? distance(carA.position, cps[a.cp]) : Infinity;
        const nextB = carB && cps[b.cp] ? distance(carB.position, cps[b.cp]) : Infinity;
        return nextA - nextB;
    });
}

function sendState(race) {
    const order = placeOrder(race);
    const elapsed = Date.now() - race.startedAt;
    order.forEach((racer, index) => {
        if (racer.dnf) return;
        emit(racer.player, "fr:race:state", {
            active: true, route: race.route.name, checkpoint: Math.min(racer.cp + 1, race.route.checkpoints.length),
            total: race.route.checkpoints.length, place: index + 1, racers: order.length,
            elapsedMs: racer.finishedMs ?? elapsed, finished: racer.finishedMs !== null,
            record: records[race.route.id] ?? null,
            standings: order.slice(0, 6).map((other, place) => ({place: place + 1, name: other.player.nickname,
                you: other.player.id === racer.player.id, finishedMs: other.finishedMs})),
        });
    });
}

function finishRacer(race, racer) {
    racer.finishedMs = Date.now() - race.startedAt;
    if (!race.firstFinishAt) race.firstFinishAt = Date.now();
    const place = racerList(race).filter((other) => other.finishedMs !== null).length;
    markers(racer.player, "race", []);
    compass(racer.player, "race", 100, null);
    life.end(racer.player, "race");
    notify.announce(racer.player, place === 1 && race.entrants > 1 ? "WINNER!" : `FINISHED · ${ordinal(place)}`, 3, "beep50.wav");
    toast(racer.player, `${ordinal(place)} place in ${formatDuration(racer.finishedMs)}.`, {title: race.route.name, tone: "gold"});
}

function ordinal(place) {
    const tens = place % 100;
    if (tens >= 11 && tens <= 13) return `${place}th`;
    return `${place}${["th", "st", "nd", "rd"][place % 10] ?? "th"}`;
}

function concludeRace(race) {
    const finishers = racerList(race).filter((racer) => racer.finishedMs !== null).sort((a, b) => a.finishedMs - b.finishedMs);
    const pool = 100 + 120 * race.entrants;
    const shares = race.entrants > 1 ? [0.6, 0.25, 0.15] : [];
    finishers.forEach((racer, index) => {
        // A solo run pays only on a built-in route, and once per cooldown.
        const solo = race.entrants === 1;
        const soloPaid = solo && race.route.builtIn && Date.now() - (lastSoloPrize.get(racer.player.id) ?? 0) >= SOLO_COOLDOWN_MS;
        if (soloPaid) lastSoloPrize.set(racer.player.id, Date.now());
        const prize = solo ? (soloPaid ? SOLO_PRIZE : 0) : Math.round(pool * (shares[index] ?? 0));
        if (prize > 0) racer.player.giveMoney(prize);
        profiles.bump(racer.player, "racesRun");
        if (index === 0 && race.entrants > 1) profiles.bump(racer.player, "racesWon");
        profiles.addXp(racer.player, 30 + 25 * Math.max(0, race.entrants - index - 1) + (index === 0 && race.entrants > 1 ? 60 : 0), "Street race");
        const record = records[race.route.id];
        if (!record || racer.finishedMs < record.ms) {
            // Records count on any run; only one set against rivals pays.
            records[race.route.id] = {ms: racer.finishedMs, name: racer.player.nickname, at: Date.now()};
            if (!solo) racer.player.giveMoney(RECORD_BONUS);
            profiles.bump(racer.player, "raceRecords");
            feed("record", [name(racer.player, COLORS.gold), text(" set the record on "), text(race.route.name, COLORS.white),
                text(` · ${formatDuration(racer.finishedMs)}`, COLORS.gold)]);
            toast(racer.player, solo ? "New route record!" : `New route record! +${formatMoney(RECORD_BONUS)}`, {tone: "gold"});
            save();
        }
        card(racer.player, "race", {title: `${race.route.name} · results`, subtitle: `${ordinal(index + 1)} of ${race.entrants}`,
            rows: finishers.slice(0, 5).map((other, place) => [`${place + 1}. ${other.player.nickname}`, formatDuration(other.finishedMs)])
                .concat(prize > 0 ? [["Prize", formatMoney(prize)]] : []), tone: "gold", seconds: 15});
    });
    try {
        race.onConclude?.(finishers.map((racer) => ({player: racer.player, ms: racer.finishedMs})), race.entrants);
    } catch (error) {
        console.error(`[freeride] Race result handler failed: ${error?.stack ?? error}`);
    }
    if (finishers.length && race.entrants > 1) {
        feed("race", [name(finishers[0].player, COLORS.gold), text(` won ${race.route.name} against ${race.entrants - 1} `),
            text(race.entrants === 2 ? "rival" : "rivals"), text(` · ${formatDuration(finishers[0].finishedMs)}`, COLORS.white)]);
    }
    for (const racer of racerList(race)) {
        if (racer.finishedMs === null) leaveRace(race, racer, "The race is over. You did not finish.");
        else {
            race.racers.delete(racer.player.id);
            sendHome(racer);
            emit(racer.player, "fr:race:state", {active: false});
        }
    }
    races.delete(race.id);
}

function tickLobby(race, now) {
    for (const racer of racerList(race)) {
        if (!playable(racer.player)) continue;
        if (now - race.lastStateAt >= 1000) lobbyCard(race, racer);
    }
    if (now - race.lastStateAt >= 1000) race.lastStateAt = now;
    if (now >= race.lobbyEndsAt) startCountdown(race);
}

function tickCountdown(race, now) {
    for (const racer of racerList(race)) {
        const car = racer.player.getVehicle();
        if (car && racer.slot && distance(car.position, racer.slot.position) > 1.5) {
            car.setTransform(racer.slot.position, racer.slot.heading, {x: 0, y: 0, z: 0});
        }
    }
    if (now >= race.startedAt) {
        race.phase = "running";
        for (const racer of racerList(race)) card(racer.player, "race", null);
        sendState(race);
    }
}

function tickRunning(race, now) {
    const cps = race.route.checkpoints;
    for (const racer of racerList(race)) {
        if (racer.finishedMs !== null) continue;
        if (!playable(racer.player)) {
            leaveRace(race, racer, "You dropped out of the race.");
            continue;
        }
        const car = racer.player.getSeat() === 0 ? racer.player.getVehicle() : null;
        if (!car || car.terminalState !== 0) {
            racer.outOfCarSince ??= now;
            if (now - racer.outOfCarSince > OUT_OF_CAR_MS) leaveRace(race, racer, "Disqualified: you left the driver's seat.");
            continue;
        }
        racer.outOfCarSince = null;
        const target = cps[racer.cp];
        if (flatDistance(car.position, target) <= CHECKPOINT_RADIUS && Math.abs(car.position.y - target.y) <= CHECKPOINT_HEIGHT) {
            racer.cp++;
            emit(racer.player, "fr:race:checkpoint", {index: racer.cp, total: cps.length});
            if (racer.cp >= cps.length) finishRacer(race, racer);
            else routeMarkers(race, racer);
            race.lastStateAt = 0;
        }
    }
    if (now - race.lastStateAt >= STATE_EVERY_MS) {
        race.lastStateAt = now;
        sendState(race);
    }
    const remaining = racerList(race).filter((racer) => racer.finishedMs === null);
    if (!race.racers.size || !remaining.length || (race.firstFinishAt && now - race.firstFinishAt > FINISH_GRACE_MS) ||
        now - race.startedAt > RACE_LIMIT_MS) concludeRace(race);
}

function tick() {
    const now = Date.now();
    for (const race of [...races.values()]) {
        if (race.generation !== World.getMissionGeneration()) {
            closeRace(race);
            continue;
        }
        for (const racer of racerList(race)) {
            if (!World.getPlayers().some((player) => player.id === racer.player.id)) race.racers.delete(racer.player.id);
        }
        if (race.phase === "lobby") tickLobby(race, now);
        else if (race.phase === "countdown") tickCountdown(race, now);
        else tickRunning(race, now);
        // An empty lobby waits for its timer: a scripted one starts with nobody in it.
        if (races.has(race.id) && !race.racers.size && race.phase !== "lobby") races.delete(race.id);
    }
    for (const [id, recording] of recordings) {
        const player = util.playerById(id);
        if (!player) recordings.delete(id);
        else if (now - recording.markersAt > 2000) {
            recording.markersAt = now;
            recordingMarkers(player, recording);
        }
    }
}

function recordingMarkers(player, recording) {
    markers(player, "race-edit", [
        {id: "start", position: recording.start.position, label: `START · ${recording.name}`, color: COLORS.gold, beacon: true},
        ...recording.checkpoints.map((point, index) => ({id: `cp${index}`, position: point, label: `${index + 1}`, color: COLORS.blue, beacon: true})),
    ]);
}

function driverCar(player) {
    const car = player.getSeat() === 0 ? player.getVehicle() : null;
    if (!car) toast(player, "Record routes from the driver's seat.", {tone: "bad"});
    return car;
}

function routeSummary(route, index) {
    const record = records[route.id];
    return `${index + 1}. ${route.name} · ${lengthText(route)}${record ? ` · record ${formatDuration(record.ms)} by ${record.name}` : ""}`;
}

const subcommands = {
    start(player, args) {
        const route = args.length ? findRoute(args.join(" ")) : util.pick(allRoutes().filter((entry) => entry.builtIn));
        if (!route) {
            toast(player, "No such route. /race list shows them.", {tone: "bad"});
            return;
        }
        openRace(player, route);
    },
    join(player, args) {
        const race = args.length ? races.get(Number(args[0])) : openLobby();
        if (!race || race.phase !== "lobby") {
            toast(player, "No race is taking entries. Open one with /race start.", {tone: "bad"});
            return;
        }
        if (playable(player)) join(race, player);
    },
    leave(player) {
        const race = raceOf(player);
        if (!race) return;
        leaveRace(race, race.racers.get(player.id), "You left the race.");
    },
    list(player) {
        allRoutes().slice(0, 12).forEach((route, index) => player.sendMessage(routeSummary(route, index), 0xd9cdb4));
        player.sendMessage("Open a lobby with /race start <number or name>.", 0xe8c57d);
    },
    create(player, args) {
        const routeName = args.join(" ").trim();
        if (!/^[A-Za-z0-9 '.-]{3,28}$/.test(routeName)) {
            toast(player, "Name the route with 3 to 28 letters, digits or spaces.", {tone: "bad"});
            return;
        }
        // Recorded route IDs are shared by every map, so names must be too.
        if (allRoutes().some((route) => slug(route.name) === slug(routeName)) || customRoutes.some((route) => route.id === slug(routeName))) {
            toast(player, "A route with that name exists.", {tone: "bad"});
            return;
        }
        const car = driverCar(player);
        if (!car || !playable(player)) return;
        const wait = (lastCreated.get(player.id) ?? 0) + CREATE_COOLDOWN_MS - Date.now();
        if (wait > 0) {
            toast(player, `You can record another route in ${formatMinutes(wait)}.`, {tone: "bad"});
            return;
        }
        recordings.set(player.id, {name: routeName, start: {position: util.plain(car.position), heading: util.headingOf(car.rotation)},
            checkpoints: [], markersAt: 0});
        toast(player, "Recording. Drive the route and type /race cp at each checkpoint, /race save when done.", {title: routeName, tone: "gold", seconds: 8});
    },
    cp(player) {
        const recording = recordings.get(player.id);
        const car = recording && driverCar(player);
        if (!car) return;
        const last = recording.checkpoints[recording.checkpoints.length - 1] ?? recording.start.position;
        if (flatDistance(last, car.position) < MIN_CUSTOM_SPACING) {
            toast(player, `Checkpoints need ${MIN_CUSTOM_SPACING} m between them.`, {tone: "bad"});
            return;
        }
        if (recording.checkpoints.length >= MAX_CUSTOM_POINTS) {
            toast(player, "That is as many checkpoints as a route can have. /race save it.", {tone: "bad"});
            return;
        }
        recording.checkpoints.push(util.plain(car.position));
        recording.markersAt = 0;
        toast(player, `Checkpoint ${recording.checkpoints.length} placed.`, {tone: "good", seconds: 2});
    },
    undo(player) {
        const recording = recordings.get(player.id);
        if (recording?.checkpoints.pop()) recording.markersAt = 0;
    },
    save(player) {
        const recording = recordings.get(player.id);
        if (!recording) return;
        if (recording.checkpoints.length < MIN_CUSTOM_POINTS) {
            toast(player, `A route needs at least ${MIN_CUSTOM_POINTS} checkpoints.`, {tone: "bad"});
            return;
        }
        let length = 0;
        let previous = recording.start.position;
        for (const point of recording.checkpoints) {
            length += flatDistance(previous, point);
            previous = point;
        }
        if (length < MIN_CUSTOM_LENGTH) {
            toast(player, `A route needs at least ${MIN_CUSTOM_LENGTH} m between its checkpoints; this one is ${Math.round(length)} m.`, {tone: "bad"});
            return;
        }
        const mine = customRoutes.filter((route) => route.author === player.nickname);
        if (mine.length >= MAX_ROUTES_PER_AUTHOR || customRoutes.length >= MAX_CUSTOM_ROUTES) {
            toast(player, "The route book is full. Delete one of yours with /race delete <name>.", {tone: "bad"});
            return;
        }
        customRoutes.push({id: slug(recording.name), name: recording.name, author: player.nickname, start: recording.start,
            checkpoints: recording.checkpoints, mission: maps.current()?.id ?? "freeride", created: Date.now()});
        recordings.delete(player.id);
        lastCreated.set(player.id, Date.now());
        markers(player, "race-edit", []);
        save();
        toast(player, `${recording.name} is in the route book. Open it with /race start ${recording.name}.`, {tone: "good"});
        feed("race", [name(player, COLORS.gold), text(" published a new route: "), text(recording.name, COLORS.white)]);
    },
    discard(player) {
        if (recordings.delete(player.id)) markers(player, "race-edit", []);
    },
    delete(player, args) {
        const route = findRoute(args.join(" "));
        const index = route && !route.builtIn ? customRoutes.findIndex((entry) => `c:${entry.id}` === route.id) : -1;
        if (index < 0 || customRoutes[index].author !== player.nickname) {
            toast(player, "You can delete only routes you recorded.", {tone: "bad"});
            return;
        }
        customRoutes.splice(index, 1);
        delete records[route.id];
        save();
        toast(player, `${route.name} removed from the route book.`);
    },
};

commands.register({name: "race", aliases: ["races"], usage: "/race [start|join|leave|list|create|cp|undo|save|discard|delete]",
    help: "Open, join or record street races.", category: "Races"}, (player, args) => {
    const action = (args[0] ?? "").toLowerCase();
    if (subcommands[action]) {
        subcommands[action](player, args.slice(1));
        return;
    }
    if (!action) {
        if (openLobby() && !raceOf(player)) subcommands.join(player, []);
        else emit(player, "fr:menu:open", {tab: "races"});
        return;
    }
    subcommands.start(player, args);
});

menu.provide("races", (player) => ({
    routes: allRoutes().map((route, index) => ({index: index + 1, id: route.id, name: route.name, description: route.description,
        length: Math.round(route.length), checkpoints: route.checkpoints.length, builtIn: route.builtIn,
        mine: route.author === player.nickname, record: records[route.id] ?? null})),
    races: [...races.values()].map((race) => ({id: race.id, route: race.route.name, host: race.host, phase: race.phase,
        racers: racerList(race).map((racer) => racer.player.nickname), secondsLeft: Math.max(0, Math.ceil((race.lobbyEndsAt - Date.now()) / 1000)),
        joined: race.racers.has(player.id)})),
    recording: recordings.has(player.id) ? {name: recordings.get(player.id).name, checkpoints: recordings.get(player.id).checkpoints.length} : null,
}));

util.onStart(() => {
    load();
    tickTimer = setInterval(tick, TICK_MS);
});
util.onStop(() => {
    clearInterval(tickTimer);
    for (const race of [...races.values()]) closeRace(race);
    clearTimeout(saveTimer);
});
Events.on("missionChange", () => {
    for (const race of [...races.values()]) closeRace(race);
    for (const player of World.getPlayers()) markers(player, "race-edit", []);
    recordings.clear();
});
Events.on("playerDeath", (player) => {
    const race = raceOf(player);
    if (race) leaveRace(race, race.racers.get(player.id), "You were taken out of the race.");
});
Events.on("playerDisconnect", (player) => {
    const race = raceOf(player);
    if (race) race.racers.delete(player.id);
    recordings.delete(player.id);
    lastSoloPrize.delete(player.id);
    lastCreated.delete(player.id);
});

module.exports = {openRace, circuitRoute, raceOf};
