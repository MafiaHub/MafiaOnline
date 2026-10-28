// Calling for backup. An officer on duty rings the precinct from one of the
// city's telephone booths (data/phone_booths.js) or over the radio of their
// squad car. A wanted player near the caller gets the retail police's own
// reinforcements: a police car with two officers from the police pool, sent
// the way a rising wanted level sends them (Police.sendBackup). Anything else
// (a bounty target, hostile gangsters, a quiet street) or a refused call gets
// a scripted squad: a squad car spawned out of sight on a road, whose two
// officers board it, drive to the scene and fight the suspect or cover the
// caller. Every unit stands down when the suspect is caught or dead, the
// caller clocks off, or its time runs out; a squad drives off before it is
// removed. police_duty.js configures this with its duty state.
const booths = require("./data/phone_booths.js");
const maps = require("./lib/maps.js");
const roads = require("./lib/roads.js");
const commands = require("./lib/commands.js");
const {toast, compass, markers, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, flatDistance, playable, playerById, vehicleById, emit, onClient, onStart, onStop} = require("./lib/util.js");

// A booth is in reach this close to its frame, on the same level.
const BOOTH_REACH = 2.8;
const BOOTH_LEVEL = 2.5;
const BOOTH_MARKER_RANGE = 160;
const COOLDOWN_MS = 90_000;
// Scripted squads on the map at once, server wide; each is a car and two
// scripted NPCs.
const MAX_SQUADS = 2;
const SUSPECT_RANGE = 300;
const GANGSTER_RANGE = 70;
// How long the host may take to answer Police.sendBackup.
const NATIVE_REPLY_MS = 5000;
// The officers of a native car join the NPCs within this long of the answer,
// this close to the suspect.
const NATIVE_UNITS_MS = 10_000;
const NATIVE_UNITS_RANGE = 260;
// Police.setWanted reaches the record through the simulation host.
const WANTED_GRACE_MS = 5000;
const SQUAD_MODEL = "poli00.i3d";
const OFFICER_MODELS = ["pol01.i3d", "pol02.i3d"];
const OFFICER_KITS = [{weapon: 11, ammo: 8, reserve: 24}, {weapon: 6, ammo: 8, reserve: 32}];
const OFFICER_HEALTH = 160;
const SPAWN_MIN = 110;
const SPAWN_MAX = 220;
const ARRIVE_DISTANCE = 30;
const REROUTE_DISTANCE = 60;
const BOARD_TIMEOUT_MS = 20_000;
const DRIVE_TIMEOUT_MS = 75_000;
// A call with no suspect covers the caller this long.
const ESCORT_MS = 90_000;
const CALL_LIFETIME_MS = 180_000;
const LEAVE_TIMEOUT_MS = 40_000;
const LEAVE_DISTANCE = 350;
const ALERT_RANGE = 800;
const ALERT_MS = 60_000;
const TICK_MS = 1000;
const CALL_COLOR = "#6fa8ff";

let duty = {onDuty: () => false, carOf: () => null, suspects: () => []};
const calls = new Map();
const cooldowns = new Map();
const officersWithBooths = new Set();
let nextCall = 1;
let timer = null;

function boothList() {
    const map = maps.current();
    return (map ? booths[map.id] ?? [] : []).map(([frame, x, y, z]) => ({frame, position: {x, y, z}}));
}

function boothAt(position) {
    let best = null;
    for (const booth of boothList()) {
        const gap = flatDistance(booth.position, position);
        if (gap <= BOOTH_REACH && Math.abs(booth.position.y - position.y) <= BOOTH_LEVEL && (!best || gap < best.gap)) best = {...booth, gap};
    }
    return best;
}

function nearestBooth(position) {
    let best = null;
    for (const booth of boothList()) {
        const gap = distance(booth.position, position);
        if (!best || gap < best.gap) best = {...booth, gap};
    }
    return best;
}

function sendPolice(player, data) {
    emit(player, "fr:police", data);
}

function sound(player, wave, volume = 0.8) {
    sendPolice(player, {action: "sound", wave, volume});
}

function onDutyPlayers() {
    return World.getPlayers().filter((player) => duty.onDuty(player));
}

// Booth markers and the call prompt, for officers on duty only.
function showBooths(player) {
    const list = boothList();
    officersWithBooths.add(player.id);
    markers(player, "booths", list.map((booth) => ({id: booth.frame, position: booth.position, label: "CALL BOX",
        color: CALL_COLOR, ring: 1.2, range: BOOTH_MARKER_RANGE})));
    sendPolice(player, {action: "booths", reach: BOOTH_REACH, booths: list.map((booth) => booth.position)});
}

function hideBooths(player) {
    if (!officersWithBooths.delete(player.id)) return;
    markers(player, "booths", []);
    sendPolice(player, {action: "booths", booths: []});
}

// The suspect a call goes after: the nearest wanted player or bounty
// target, else the nearest hostile gangster.
function pickSuspect(caller) {
    const players = duty.suspects()
        .map((entry) => ({type: "player", id: entry.player.id, level: entry.level, name: entry.player.nickname,
            distance: distance(caller.position, entry.player.position)}))
        .filter((entry) => entry.distance <= SUSPECT_RANGE)
        .sort((a, b) => a.distance - b.distance);
    if (players[0]) return players[0];
    const gangster = Npc.getInRange(caller.position, GANGSTER_RANGE)
        .filter((npc) => npc.kind === "gangster" && npc.alive)
        .sort((a, b) => distance(caller.position, a.position) - distance(caller.position, b.position))[0];
    return gangster ? {type: "npc", id: gangster.id, level: 0, name: "armed gangsters"} : null;
}

function suspectEntity(call) {
    if (!call.suspect) return null;
    if (call.suspect.type === "npc") return Npc.getAll().find((npc) => npc.id === call.suspect.id && npc.alive) ?? null;
    const player = playerById(call.suspect.id);
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) return null;
    // An attacker the squad just made wanted counts before the host's record
    // shows the level.
    if (Date.now() < (call.suspect.graceUntil ?? 0)) return player;
    return duty.suspects().some((entry) => entry.player.id === player.id) ? player : null;
}

function unitsOf(call) {
    return call.units.map((id) => Npc.getAll().find((npc) => npc.id === id)).filter((npc) => npc && npc.alive);
}

// The caller and every officer on duty near the scene hear of it.
function alert(call, caller, where) {
    for (const officer of onDutyPlayers()) {
        if (officer.id === caller.id || distance(officer.position, where) > ALERT_RANGE) continue;
        toast(officer, `${caller.nickname} called for backup${call.suspect ? ` against ${call.suspect.name}` : ""}. The scene is on your map.`,
            {title: "Police radio", tone: "info", seconds: 7});
        markers(officer, `backup:${call.id}`, [{id: "scene", position: where, label: "OFFICER NEEDS ASSISTANCE", color: CALL_COLOR, beacon: true, primary: true}]);
        call.alerted.add(officer.id);
        sound(officer, "12_radio.wav", 0.6);
    }
    call.alertUntil = Date.now() + ALERT_MS;
}

function call(caller) {
    if (!duty.onDuty(caller)) {
        toast(caller, "Only officers on duty can call the precinct. /duty to sign on.", {tone: "bad"});
        return false;
    }
    if (!playable(caller) || caller.virtualWorld !== MAIN_WORLD) return false;
    const car = duty.carOf(caller);
    const seated = car && caller.getVehicle()?.id === car.id;
    const booth = seated ? null : boothAt(caller.position);
    if (!seated && !booth) {
        const near = nearestBooth(caller.position);
        toast(caller, near ? `No telephone here. The nearest call box is ${Math.round(near.gap)} m away; sit in your squad car to use its radio.`
            : "No telephone on this map. Sit in your squad car to use its radio.", {tone: "bad"});
        return false;
    }
    if ([...calls.values()].some((entry) => entry.callerId === caller.id && entry.stage !== "leaving")) {
        toast(caller, "Your backup is already on its way.", {tone: "bad"});
        return false;
    }
    const wait = (cooldowns.get(caller.id) ?? 0) - Date.now();
    if (wait > 0) {
        toast(caller, `The precinct has nobody free yet. Call again in ${Math.ceil(wait / 1000)} s.`, {tone: "bad"});
        return false;
    }
    const suspect = pickSuspect(caller);
    const entry = {id: nextCall++, callerId: caller.id, via: seated ? "radio" : "booth", suspect, stage: "calling", units: [],
        carId: null, startedAt: Date.now(), alerted: new Set(), alertUntil: 0, arrived: false, request: 0, replyBy: 0, unitsUntil: 0,
        tasks: new Map(), stageAt: Date.now(), goal: null, escortUntil: suspect ? 0 : Infinity};
    calls.set(entry.id, entry);
    cooldowns.set(caller.id, Date.now() + COOLDOWN_MS);
    if (entry.via === "booth") {
        sound(caller, "06a_telzvednuti.wav");
        setTimeout(() => sound(caller, "15b_telefonvytaceni.wav", 0.7), 600);
    }
    else sound(caller, "12_radio.wav");
    const target = suspectEntity(entry);
    if (suspect?.type === "player" && suspect.level > 0 && Police.isEnabled()) {
        entry.request = Police.sendBackup(target);
        entry.replyBy = Date.now() + NATIVE_REPLY_MS;
    }
    if (entry.request) entry.stage = "dispatching";
    else dispatchSquad(entry, caller);
    if (entry.stage === "done") return false;
    alert(entry, caller, target?.position ?? caller.position);
    feed("police", [text("Officer ", COLORS.blue), name(caller, COLORS.blue), text(" called for backup")]);
    return true;
}

// The squad's way in: a road crossing out of sight of the scene.
function squadStart(where) {
    const graph = roads.current();
    if (!graph) return null;
    const index = graph.crossingAround(where, SPAWN_MIN, SPAWN_MAX) ?? graph.crossingAround(where, 60, 500);
    if (index === null) return null;
    const {position, direction} = graph.roadside(index);
    return {position: {x: position.x, y: position.y + 0.4, z: position.z}, heading: Math.atan2(direction.x, direction.z)};
}

function dispatchSquad(entry, caller) {
    const target = suspectEntity(entry);
    const scene = target?.position ?? caller.position;
    const squads = [...calls.values()].filter((other) => other.carId !== null).length;
    const start = squads < MAX_SQUADS ? squadStart(scene) : null;
    const car = start ? Vehicle.spawn(SQUAD_MODEL, start.position, start.heading) : null;
    if (!car) {
        toast(caller, squads >= MAX_SQUADS ? "Every squad car is out on a call. Hold on." : "Dispatch could not raise a car.",
            {title: "Precinct", tone: "bad"});
        cooldowns.set(caller.id, Date.now() + COOLDOWN_MS / 3);
        end(entry, null);
        return;
    }
    car.state.set("fr.fullTank", true);
    car.state.set("fr.backup", true);
    car.setRadarMarker(true, 0x6fa8ff);
    entry.carId = car.id;
    entry.stage = "spawning";
    entry.stageAt = Date.now();
    OFFICER_MODELS.forEach((model, index) => {
        const side = index === 0 ? -2.2 : 2.2;
        const position = {x: start.position.x + Math.cos(start.heading) * side, y: start.position.y, z: start.position.z - Math.sin(start.heading) * side};
        const officer = Npc.spawn(model, position, start.heading, {behaviour: "neutral", health: OFFICER_HEALTH, ...OFFICER_KITS[index]});
        if (officer) entry.units.push(officer.id);
    });
    if (!entry.units.length) {
        toast(caller, "Dispatch could not raise a car.", {title: "Precinct", tone: "bad"});
        end(entry, null);
        return;
    }
    toast(caller, entry.suspect ? `A squad car is on its way to ${entry.suspect.name}. Follow it on your compass.`
        : "A squad car is on its way to you. Follow it on your compass.", {title: "Precinct", tone: "gold", seconds: 7});
}

// The caller's compass follows the arriving unit until it is on scene.
function pointCompass(entry, caller, target) {
    const key = JSON.stringify(target);
    if (entry.compassKey === key) return;
    entry.compassKey = key;
    compass(caller, "backup", 50, target);
}

function giveTask(entry, npc, method, ...args) {
    const id = npc[method](...args);
    if (id) entry.tasks.set(npc.id, {id, method});
    return id;
}

function board(entry) {
    const car = vehicleById(entry.carId);
    entry.stage = "boarding";
    entry.stageAt = Date.now();
    entry.seated = new Set();
    unitsOf(entry).forEach((npc, index) => {
        if (!car || !giveTask(entry, npc, "enterVehicle", car, index, {run: true})) entry.seated.add(npc.id);
    });
    if (entry.seated.size >= unitsOf(entry).length) drive(entry);
}

function drive(entry) {
    const [driver] = unitsOf(entry);
    const target = suspectEntity(entry) ?? playerById(entry.callerId);
    entry.stage = "driving";
    entry.stageAt = Date.now();
    entry.goal = target ? {...target.position} : null;
    if (!driver || !driver.getVehicle() || !target || !giveTask(entry, driver, "driveTo", target.position)) arrive(entry);
}

// On scene: out of the car, then the fight or the cover.
function arrive(entry) {
    entry.stage = "exiting";
    entry.stageAt = Date.now();
    entry.exited = new Set();
    for (const npc of unitsOf(entry)) {
        if (!npc.getVehicle() || !giveTask(entry, npc, "exitVehicle")) entry.exited.add(npc.id);
    }
    if (entry.exited.size >= unitsOf(entry).length) engage(entry);
}

function engage(entry) {
    entry.stage = "engaging";
    entry.stageAt = Date.now();
    const caller = playerById(entry.callerId);
    const target = suspectEntity(entry);
    for (const npc of unitsOf(entry)) {
        if (target) giveTask(entry, npc, "attack", target);
        else if (caller) giveTask(entry, npc, "follow", caller, 3);
    }
    if (!entry.arrived) onScene(entry);
}

function onScene(entry) {
    entry.arrived = true;
    if (!entry.suspect) entry.escortUntil = Date.now() + ESCORT_MS;
    const caller = playerById(entry.callerId);
    if (!caller) return;
    sound(caller, "poli_fife1.wav");
    toast(caller, "Backup on scene.", {title: "Precinct", tone: "good"});
    compass(caller, "backup", 50, null);
}

// Back in the car and away, then gone.
function standDown(entry, reason) {
    if (entry.stage === "leaving" || entry.stage === "done") return;
    const caller = playerById(entry.callerId);
    if (caller && reason) toast(caller, reason, {title: "Precinct", tone: "info"});
    if (caller) {
        compass(caller, "backup", 50, null);
        sound(caller, entry.via === "booth" ? "06a_telpolozeni.wav" : "12_radio.wav", 0.6);
    }
    clearAlerts(entry);
    if (entry.carId === null) {
        end(entry, null);
        return;
    }
    entry.stage = "leaving";
    entry.stageAt = Date.now();
    const car = vehicleById(entry.carId);
    if (!car) return;
    unitsOf(entry).forEach((npc, index) => {
        if (npc.getVehicle()?.id === car.id) {
            if (index === 0) leaveIn(entry, npc);
        }
        else giveTask(entry, npc, "enterVehicle", car, index, {run: false});
    });
}

function leaveIn(entry, driver) {
    const graph = roads.current();
    const index = graph?.crossingAround(driver.position, LEAVE_DISTANCE, LEAVE_DISTANCE + 400);
    if (index === null || index === undefined) return;
    giveTask(entry, driver, "driveTo", graph.crossings[index]);
}

function clearAlerts(entry) {
    for (const id of entry.alerted) {
        const officer = playerById(id);
        if (officer) markers(officer, `backup:${entry.id}`, []);
    }
    entry.alerted.clear();
}

function end(entry, reason) {
    if (entry.stage === "done") return;
    entry.stage = "done";
    calls.delete(entry.id);
    clearAlerts(entry);
    const caller = playerById(entry.callerId);
    if (caller) {
        compass(caller, "backup", 50, null);
        if (reason) toast(caller, reason, {title: "Precinct", tone: "info"});
    }
    if (entry.carId === null) return;
    for (const id of entry.units) Npc.getAll().find((npc) => npc.id === id)?.remove();
    const car = vehicleById(entry.carId);
    if (car && car.getOccupants().every((occupant) => occupant === null || !(occupant instanceof Player))) car.destroy();
}

function nearAnyPlayer(position, range) {
    return World.getPlayers().some((player) => player.spawned && distance(player.position, position) < range);
}

function tick() {
    const now = Date.now();
    for (const entry of [...calls.values()]) {
        const caller = playerById(entry.callerId);
        const target = suspectEntity(entry);
        const units = unitsOf(entry);
        if (entry.alertUntil && now > entry.alertUntil) {
            clearAlerts(entry);
            entry.alertUntil = 0;
        }
        if (entry.stage === "leaving") {
            const car = vehicleById(entry.carId);
            const where = units[0]?.position ?? car?.position;
            if (!where || now - entry.stageAt > LEAVE_TIMEOUT_MS || !nearAnyPlayer(where, 120)) end(entry, null);
            continue;
        }
        if (now - entry.startedAt > CALL_LIFETIME_MS) {
            standDown(entry, "Your backup was called back to the precinct.");
            continue;
        }
        if (!caller || !duty.onDuty(caller)) {
            standDown(entry, null);
            continue;
        }
        if (entry.suspect && !target) {
            standDown(entry, `${entry.suspect.name} is no longer a threat. Units back to patrol.`);
            continue;
        }
        if (!entry.suspect && now > entry.escortUntil) {
            standDown(entry, "Quiet streets. Your backup goes back to patrol.");
            continue;
        }
        if (entry.stage === "dispatching") {
            if (now > entry.replyBy) {
                entry.request = 0;
                dispatchSquad(entry, caller);
            }
            continue;
        }
        if (entry.stage === "native") {
            if (now > entry.unitsUntil && !units.length) {
                standDown(entry, "The police car is off the case.");
                continue;
            }
            const lead = units.find((npc) => npc.getVehicle()) ?? units[0];
            if (lead && !entry.arrived && target && distance(lead.position, target.position) < ARRIVE_DISTANCE * 1.5) onScene(entry);
            else if (lead && !entry.arrived) pointCompass(entry, caller, lead.getVehicle() ? {vehicleId: lead.getVehicle().id} : {position: lead.position});
            continue;
        }
        if (entry.carId !== null && !units.length && entry.stage !== "spawning") {
            feed("police", [text("Officers down answering "), name(caller, COLORS.blue), text("'s call", COLORS.muted)]);
            end(entry, "Your backup is down.");
            continue;
        }
        const car = vehicleById(entry.carId);
        if (entry.stage === "spawning" && now - entry.stageAt > BOARD_TIMEOUT_MS) {
            end(entry, "Dispatch could not raise a car.");
            continue;
        }
        if (entry.stage === "boarding" && now - entry.stageAt > BOARD_TIMEOUT_MS) drive(entry);
        else if (entry.stage === "driving") {
            const goal = target ?? caller;
            if (now - entry.stageAt > DRIVE_TIMEOUT_MS || (car && distance(car.position, goal.position) < ARRIVE_DISTANCE)) arrive(entry);
            else if (entry.goal && distance(entry.goal, goal.position) > REROUTE_DISTANCE && units[0]?.getVehicle()) {
                // The suspect moved on: the driver heads for where they are now.
                entry.goal = {...goal.position};
                giveTask(entry, units[0], "driveTo", goal.position);
            }
        }
        else if (entry.stage === "exiting" && now - entry.stageAt > BOARD_TIMEOUT_MS) engage(entry);
        if (!entry.arrived && car) pointCompass(entry, caller, {vehicleId: car.id});
    }
}

Events.on("npcSpawn", (npc) => {
    for (const entry of calls.values()) {
        if (entry.stage === "spawning" && entry.units.includes(npc.id)) {
            if (!entry.ready) entry.ready = new Set();
            entry.ready.add(npc.id);
            if (entry.ready.size >= entry.units.length) board(entry);
            return;
        }
        // A native car's officers are the police NPCs that turn up near
        // the suspect right after the host sent it.
        if (entry.stage === "native" && npc.kind === "police" && Date.now() <= entry.unitsUntil && entry.units.length < 2) {
            const target = suspectEntity(entry);
            if (!target || distance(npc.position, target.position) > NATIVE_UNITS_RANGE) continue;
            entry.units.push(npc.id);
            return;
        }
    }
});

Events.on("npcTaskComplete", (npc, info) => {
    for (const entry of calls.values()) {
        const task = entry.tasks.get(npc.id);
        if (!task || task.id !== info.id) continue;
        entry.tasks.delete(npc.id);
        if (entry.stage === "boarding" && info.task === "enterVehicle") {
            entry.seated.add(npc.id);
            if (entry.seated.size >= unitsOf(entry).length) drive(entry);
        }
        else if (entry.stage === "driving" && info.task === "driveTo") arrive(entry);
        else if (entry.stage === "exiting" && info.task === "exitVehicle") {
            entry.exited.add(npc.id);
            if (entry.exited.size >= unitsOf(entry).length) engage(entry);
        }
        else if (entry.stage === "engaging" && info.task === "attack") {
            // The target is dead, or out of reach: the next tick decides.
            const target = suspectEntity(entry);
            const caller = playerById(entry.callerId);
            if (target) giveTask(entry, npc, "attack", target);
            else if (caller) giveTask(entry, npc, "follow", caller, 3);
        }
        else if (entry.stage === "leaving" && info.task === "enterVehicle" && info.success && unitsOf(entry)[0]?.id === npc.id) leaveIn(entry, npc);
        else if (entry.stage === "leaving" && info.task === "driveTo") end(entry, null);
        return;
    }
});

Events.on("policeBackup", (suspect, info) => {
    const entry = [...calls.values()].find((candidate) => candidate.stage === "dispatching" && candidate.request === info.id);
    if (!entry) return;
    const caller = playerById(entry.callerId);
    if (!caller) {
        end(entry, null);
        return;
    }
    if (!info.sent) {
        dispatchSquad(entry, caller);
        return;
    }
    entry.stage = "native";
    entry.unitsUntil = Date.now() + NATIVE_UNITS_MS;
    toast(caller, `The precinct sent a police car after ${suspect.nickname}. Follow it on your compass.`, {title: "Precinct", tone: "gold", seconds: 7});
});

// Hurting one of the squad's officers is an assault on the police: the
// attacker is wanted and the squad turns on them. Officers on duty are the
// squad's colleagues; their stray shots are forgiven.
Events.on("npcDamage", (npc, info) => {
    const attacker = info?.attacker;
    if (!(attacker instanceof Player) || duty.onDuty(attacker)) return;
    const entry = [...calls.values()].find((candidate) => candidate.carId !== null && candidate.units.includes(npc.id));
    if (!entry || entry.stage === "leaving") return;
    const level = Police.getRecord(attacker)?.level ?? 0;
    // With the police off the attacker is not wanted, and the squad keeps its call.
    if (level < 2 && !Police.setWanted(attacker, 2)) return;
    if (!entry.suspect || entry.suspect.type !== "player" || entry.suspect.id !== attacker.id) {
        entry.suspect = {type: "player", id: attacker.id, level: 2, name: attacker.nickname, graceUntil: Date.now() + WANTED_GRACE_MS};
        entry.escortUntil = 0;
        if (entry.stage === "engaging") for (const unit of unitsOf(entry)) giveTask(entry, unit, "attack", attacker);
    }
});

Events.on("npcDeath", (npc, killer) => {
    const entry = [...calls.values()].find((candidate) => candidate.carId !== null && candidate.units.includes(npc.id));
    if (entry && killer instanceof Player && !duty.onDuty(killer)) Police.setWanted(killer, 3);
});

// A suspect the squad takes down is settled like one an officer takes down.
Events.on("playerDeath", (victim, killer) => {
    if (!killer || killer instanceof Player) return;
    for (const entry of calls.values()) {
        if (entry.suspect?.type === "player" && entry.suspect.id === victim.id && entry.units.includes(killer.id)) {
            Police.clear(victim);
            const caller = playerById(entry.callerId);
            if (caller) toast(caller, `Your backup took ${victim.nickname} down.`, {title: "Precinct", tone: "good"});
            feed("police", [text("Police backup took down "), name(victim)]);
            return;
        }
    }
});

Events.on("npcRetire", (npc) => {
    for (const entry of calls.values()) {
        if (!entry.units.includes(npc.id)) continue;
        entry.units = entry.units.filter((id) => id !== npc.id);
        if (entry.stage === "spawning" && !entry.units.length) end(entry, "Dispatch could not raise a car.");
        // The other officer is already in: the squad goes without this one.
        else if (entry.stage === "spawning" && entry.units.every((id) => entry.ready?.has(id))) board(entry);
        return;
    }
});

commands.register({name: "backup", aliases: ["callbackup", "911"], usage: "/backup",
    help: "On police duty: call for backup from a telephone booth or your squad car's radio.", category: "City"}, (player) => call(player));
onClient("fr:backup:call", (player) => call(player));

function reset() {
    for (const entry of [...calls.values()]) end(entry, null);
    cooldowns.clear();
}

onStart(() => { timer = setInterval(tick, TICK_MS); });
onStop(() => {
    clearInterval(timer);
    reset();
});
Events.on("playerDisconnect", (player) => {
    officersWithBooths.delete(player.id);
    cooldowns.delete(player.id);
});
Events.on("missionChange", () => {
    // The mission took every car and NPC with it.
    for (const entry of [...calls.values()]) {
        entry.carId = null;
        end(entry, null);
    }
    officersWithBooths.clear();
});

// One line for the duty card while a call runs.
function status(player) {
    const entry = [...calls.values()].find((candidate) => candidate.callerId === player.id);
    if (entry) {
        const label = {calling: "Calling", dispatching: "Calling", native: "Police car en route", spawning: "Squad car en route",
            boarding: "Squad car en route", driving: "Squad car en route", exiting: "On scene", engaging: "On scene", leaving: "Back to patrol"};
        return label[entry.stage] ?? null;
    }
    const wait = (cooldowns.get(player.id) ?? 0) - Date.now();
    return wait > 0 ? `Free again in ${Math.ceil(wait / 1000)} s` : "Call box or car radio: /backup";
}

module.exports = {
    configure(provider) { duty = provider; },
    status,
    start: showBooths,
    stop(player) {
        hideBooths(player);
        compass(player, "backup", 50, null);
        for (const entry of [...calls.values()]) if (entry.callerId === player.id) standDown(entry, null);
    },
    call,
};
