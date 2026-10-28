// The ambulance. A player goes on duty at Lost Heaven Hospital (the stock
// models/9medic.4ds, which both city scenes' cache.bin place at
// (-759.22, 11.56, 778.99): its entrance steps run down to z 758 facing the
// street whose road graph line runs along z 729.1) and takes the stock
// ambulance. Calls come in from the street: someone collapsed, someone
// badly hurt, a smoke victim the fire brigade carried out, and players who
// went down while a medic is on duty: they wait for one before the usual
// respawn (lib/life.js holds it). The medic drives there, holds G beside
// the patient to treat them and either leaves them on their feet or takes
// them to the hospital, riding in the ambulance, before the time runs out
// for a tip.
const roads = require("./lib/roads.js");
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const jobkey = require("./lib/jobkey.js");
const firefighters = require("./firefighters.js");
const barRoom = require("./lib/bar_room.js");
const prison = require("./lib/prison.js");
const {toast, card, compass, markers, markersAll, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, flatDistance, playable, playerById, vehicleById, speedOf, formatMoney, formatMinutes, headingOf, pick, onStart, onStop, inFreeRide} =
    require("./lib/util.js");

const AMBULANCE = "ambulance00.i3d";
const CITY_HOSPITAL = {
    name: "Lost Heaven Hospital",
    duty: {x: -759.4, y: 11.6, z: 755.5},
    ambulance: {position: {x: -752.0, y: 11.9, z: 733.0}, heading: Math.PI / 2},
    bay: {x: -759.4, y: 11.5, z: 735.0},
    // The landing in front of the doors (3DVERE4 at z 763.5, floor y 13.2).
    door: {x: -759.4, y: 13.3, z: 762.0},
};
const HOSPITALS = {freeride: CITY_HOSPITAL, freeride_extended: CITY_HOSPITAL, freeridenoc: CITY_HOSPITAL};
const DUTY_REACH = 40;
const TREAT_REACH = 3;
const TREAT_MS = 5000;
const REVIVE_MS = 4000;
const BAY_RADIUS = 22;
const BAY_SPEED = 3;
const TICK_MS = 250;
const CARD_MS = 1000;
const CALL_EVERY_MS = 3 * 60_000;
const FIRST_CALL_MS = 45_000;
const MAX_CALLS = 3;
const CALL_LIFETIME_MS = 8 * 60_000;
const DOWN_MS = 60_000;
const BOARD_TIMEOUT_MS = 15_000;
const PICKUP_REACH = 20;
const PAY = {minor: 140, serious: 220, revive: 200};
// A player brought back pays the medic once in this long: no farming one
// friend who keeps going down.
const REVIVE_PAY_EVERY_MS = 10 * 60_000;
const PER_METRE = 0.25;
const TIP = 0.4;
const XP = 50;
const CIVILIANS = ["Delnik01.i3d", "Delnik02.i3d", "Czena01.i3d", "Czena02.i3d", "Homeless01.i3d"];
// Stock clips (a4.dta anims\): a panicking walker lying on the ground
// (C_human anim_info 337) and crouched (331).
const LYING = "x panika leh stativ.i3d";
const CROUCHED = "x panika drep stativ.i3d";

// Activities (lib/life.js) whose players still wait for a medic.
const JOBS = new Set(["fire", "medic", "taxi", "duty", "courier", "bootleg"]);

const medics = new Map();
const calls = new Map();
let nextCall = 1;
let nextCallAt = 0;
let timer = null;
let lastCardAt = 0;
let lastMarkers = "";
let lastItems = [];
// Patients who walk off after treatment or into the hospital, removed a
// little later; and the exit tasks after which they walk in.
const strays = new Set();
const arrivals = new Map();
// Player record -> when their revive last paid.
const revivePaid = new Map();

function hospital() {
    return HOSPITALS[World.getMission()] ?? null;
}

function ambulanceOf(medic) {
    return vehicleById(medic.ambulanceId);
}

function place(position) {
    const nearest = maps.places().map((entry) => ({entry, gap: flatDistance(entry.position, position)})).sort((a, b) => a.gap - b.gap)[0];
    return nearest && nearest.gap < 700 ? `near ${nearest.entry.name}` : "in the city";
}

function presentCalls() {
    const items = [...calls.values()].filter((call) => call.state === "waiting").map((call) => ({id: `call${call.id}`, position: call.position,
        label: call.kind === "downed" ? `MAN DOWN · ${call.label}` : call.kind === "serious" ? "BADLY HURT" : "INJURED", color: COLORS.green, beacon: true, range: 900}));
    const payload = JSON.stringify(items);
    if (payload === lastMarkers) return;
    lastMarkers = payload;
    lastItems = items;
    markersAll("medic", items);
}

function openCall(kind, position, label, npc = null, playerId = null, killerId = null) {
    const call = {id: nextCall++, kind, position: {...position}, label, npc, playerId, killerId, state: "waiting", openedAt: Date.now(),
        medicId: null, progress: 0, generation: World.getMissionGeneration(), where: place(position)};
    calls.set(call.id, call);
    for (const [id] of medics) {
        const medic = playerById(id);
        if (medic) toast(medic, `${kind === "downed" ? `${label} is down` : kind === "serious" ? "Someone is badly hurt" : "Someone collapsed"} ${call.where}.`,
            {title: "Ambulance call", tone: "gold", seconds: 6});
    }
    feed("medic", [text("Ambulance", COLORS.green), text(` · ${kind === "downed" ? `${label} needs a medic` : "a call came in"} ${call.where}`)]);
    presentCalls();
    return call;
}

function closeCall(call, removeNpc = true) {
    calls.delete(call.id);
    if (removeNpc && call.npc) {
        const npc = call.npc;
        call.npc = null;
        npc.remove();
    }
    if (call.kind === "downed" && call.state === "waiting") {
        const player = playerById(call.playerId);
        if (player) life.holdRespawn(player, null);
    }
    presentCalls();
}

// A street call: someone lying on the pavement by a road.
function streetCall(origin) {
    if (typeof Npc === "undefined" || calls.size >= MAX_CALLS) return null;
    const graph = roads.current();
    let spot = null;
    for (let attempt = 0; attempt < 30 && !spot; attempt++) {
        const candidate = graph.roadPoint();
        const gap = origin ? flatDistance(candidate.position, origin) : 400;
        if (gap >= 200 && gap <= 1000) spot = candidate;
    }
    spot ??= graph.roadPoint();
    const side = Math.random() < 0.5 ? -4.5 : 4.5;
    const position = {x: spot.position.x + spot.direction.z * side, y: spot.position.y + 0.2, z: spot.position.z - spot.direction.x * side};
    const npc = Npc.spawn(pick(CIVILIANS), position, Math.random() * Math.PI * 2, {behaviour: "neutral", health: 100});
    if (!npc) return null;
    return openCall(Math.random() < 0.5 ? "minor" : "serious", position, "a civilian", npc);
}

// Walks off and is gone a little later.
function walkAway(npc, position, ms) {
    if (position) npc.walkTo(position);
    strays.add(npc);
    setTimeout(() => {
        strays.delete(npc);
        for (const [task, arrival] of arrivals) if (arrival.npc === npc) arrivals.delete(task);
        npc.remove();
    }, ms);
}

function payMedic(player, medic, amount, reason) {
    player.giveMoney(amount);
    medic.earned += amount;
    medic.patients++;
    profiles.bump(player, "patients");
    profiles.addXp(player, XP, reason);
}

function finishTreatment(player, medic, call) {
    call.medicId = player.id;
    if (call.kind === "downed") {
        const victim = playerById(call.playerId);
        calls.delete(call.id);
        presentCalls();
        if (!victim || victim.alive || !life.revive(victim, call.position, 0, 60)) {
            toast(player, "Too late for them.", {title: "Ambulance", tone: "bad"});
            return;
        }
        const key = profiles.keyOf(victim) ?? `#${victim.id}`;
        const now = Date.now();
        for (const [entry, at] of revivePaid) if (now - at >= REVIVE_PAY_EVERY_MS) revivePaid.delete(entry);
        // No pay for patching up your own victim or the same patient again.
        if (call.killerId === player.id || revivePaid.has(key)) {
            medic.patients++;
            toast(player, `${victim.nickname} is back on their feet. The city pays for that once in a while, not now.`, {title: "Ambulance"});
        } else {
            revivePaid.set(key, now);
            payMedic(player, medic, PAY.revive, "Brought someone back");
            toast(player, `${victim.nickname} is back on their feet. ${formatMoney(PAY.revive)}.`, {title: "Ambulance", tone: "good"});
        }
        toast(victim, `${player.nickname} patched you up.`, {title: "Ambulance", tone: "good"});
        feed("medic", [name(player, COLORS.green), text(" brought "), name(victim), text(" back")]);
        return;
    }
    const npc = call.npc;
    npc?.stopAnimation?.();
    const quick = Date.now() - call.openedAt < 4 * 60_000;
    if (call.kind === "minor") {
        const pay = PAY.minor + (quick ? Math.round(PAY.minor * TIP) : 0);
        payMedic(player, medic, pay, "Treated a patient");
        toast(player, `Patched up${quick ? " and thanked for the quick arrival" : ""}. ${formatMoney(pay)}.`, {title: "Ambulance", tone: "good"});
        call.npc = null;
        closeCall(call, false);
        if (npc) walkAway(npc, {x: npc.position.x + 15, y: npc.position.y, z: npc.position.z + 15}, 20_000);
        return;
    }
    // Badly hurt: to the hospital, in the ambulance.
    const ambulance = ambulanceOf(medic);
    const home = hospital();
    if (!ambulance || !home) {
        toast(player, "You have no ambulance to take them in.", {tone: "bad"});
        return;
    }
    // They get in once the medic is back at the wheel: a passenger of a car
    // a player drives rides along (NpcService's player-driven seat rule),
    // while an empty car would go to the simulation host.
    call.state = npc ? "pickup" : "aboard";
    medic.patient = call.id;
    const straight = flatDistance(call.position, home.bay);
    call.deadline = Date.now() + (straight / 11 + 90) * 1000;
    call.from = {...call.position};
    presentCalls();
    toast(player, `Stabilised. Bring the ambulance up and stop beside them, then get them to ${home.name}; ` +
        `${formatMinutes(call.deadline - Date.now())} for a tip.`, {title: "Ambulance", tone: "gold"});
}

// The patient boards a still ambulance its medic drives, in a free back or
// passenger seat.
function board(player, medic, call) {
    const ambulance = ambulanceOf(medic);
    const npc = call.npc;
    if (!npc) {
        stretcher(call);
        return;
    }
    if (!ambulance || ambulance.getDriver()?.id !== player.id || speedOf(ambulance) > 1 || distance(ambulance.position, npc.position) > PICKUP_REACH) return;
    const seat = [1, 2, 3].find((index) => index < (ambulance.seatCount ?? 4) && !ambulance.getOccupants()[index]) ?? 1;
    call.task = npc.enterVehicle(ambulance, seat);
    if (!call.task) {
        stretcher(call);
        return;
    }
    call.state = "boarding";
    call.boardSince = Date.now();
}

// Carried in on the stretcher: the patient rides out of sight.
function stretcher(call) {
    call.state = "aboard";
    if (call.npc) {
        const npc = call.npc;
        call.npc = null;
        npc.remove();
    }
}

function deliver(player, medic, call) {
    const home = hospital();
    const ambulance = ambulanceOf(medic);
    medic.patient = null;
    calls.delete(call.id);
    const inTime = Date.now() <= call.deadline;
    const fare = PAY.serious + Math.round(flatDistance(call.from, home.bay) * PER_METRE);
    const pay = fare + (inTime ? Math.round(fare * TIP) : 0);
    payMedic(player, medic, pay, "Brought a patient in");
    toast(player, inTime ? `Delivered in time. ${formatMoney(fare)} and a ${formatMoney(pay - fare)} tip.` : `Delivered, but slowly. ${formatMoney(pay)}.`,
        {title: "Ambulance", tone: "good"});
    feed("medic", [name(player, COLORS.green), text(` brought a patient to ${home.name}`)]);
    // The patient walks in through the doors, once out of the car (a walk
    // given earlier would cancel the getting out).
    let npc = call.npc;
    if (npc) {
        const exit = npc.exitVehicle();
        if (exit) arrivals.set(exit, {npc, door: home.door});
        walkAway(npc, exit ? null : home.door, 25_000);
    } else if (typeof Npc !== "undefined" && ambulance) {
        const {x, y, z} = ambulance.position;
        npc = Npc.spawn(pick(CIVILIANS), {x: x + 1.5, y: y + 0.2, z: z + 1.5}, 0, {behaviour: "neutral"});
        if (npc) {
            setTimeout(() => npc.walkTo(home.door), 600);
            walkAway(npc, null, 25_000);
        }
    }
    presentCalls();
}

function endDuty(player, message) {
    const medic = medics.get(player.id);
    if (!medic) return;
    medics.delete(player.id);
    jobkey.release(player);
    jobkey.prompt(player, null);
    life.end(player, "medic");
    profiles.setPublic(player, "fr.job", null);
    compass(player, "medic", 43, null);
    card(player, "medic", null);
    const call = calls.get(medic.patient);
    if (call) closeCall(call);
    for (const open of calls.values()) if (open.medicId === player.id && open.state === "waiting") open.medicId = null;
    // Downed players wait only while someone can come.
    for (const open of [...calls.values()]) {
        if (open.kind === "downed" && ![...medics.keys()].some((id) => id !== open.playerId)) closeCall(open);
    }
    if (ambulanceOf(medic)) setTimeout(() => {
        const current = vehicleById(medic.ambulanceId);
        if (current && current.getOccupants().every((occupant) => occupant === null)) current.destroy();
    }, 3000);
    if (message) toast(player, `${message} ${medic.patients} patient${medic.patients === 1 ? "" : "s"}, ${formatMoney(medic.earned)}.`, {title: "Ambulance"});
}

function startDuty(player) {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) return;
    const home = hospital();
    if (!home) {
        toast(player, "There is no hospital out here.", {tone: "bad"});
        return;
    }
    if (distance(player.position, home.duty) > DUTY_REACH) {
        compass(player, "medic", 43, {position: home.duty});
        toast(player, `Report to ${home.name}. Follow the compass, then /medic again.`, {title: "Ambulance", tone: "gold"});
        return;
    }
    if (!life.begin(player, "medic", "Ambulance duty")) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    if (player.getVehicle()) player.removeFromVehicle();
    const ambulance = Vehicle.spawn(AMBULANCE, home.ambulance.position, home.ambulance.heading, player);
    if (!ambulance) {
        life.end(player, "medic");
        toast(player, "Every ambulance is out. Try again shortly.", {tone: "bad"});
        return;
    }
    ambulance.state.set("fr.fullTank", true);
    ambulance.state.set("fr.owner", player.nickname);
    ambulance.setRadarMarker(true, 0x8fc27a);
    medics.set(player.id, {ambulanceId: ambulance.id, earned: 0, patients: 0, patient: null, treating: null, hint: null});
    profiles.setPublic(player, "fr.job", "medic");
    player.putInVehicle(ambulance, 0);
    feed("medic", [name(player, COLORS.green), text(" went on ambulance duty")]);
    toast(player, "On duty. Calls show on your compass and map. Hold G beside a patient to treat them; " +
        "the badly hurt ride with you to the hospital, quickly for a tip. Players who go down now wait a minute for you. /medic to clock off.",
        {title: "Ambulance", tone: "gold", seconds: 10});
    if (!calls.size) nextCallAt = Math.min(nextCallAt, Date.now() + 20_000);
}

function callNear(player) {
    for (const call of calls.values()) {
        if (call.state !== "waiting") continue;
        const target = call.kind === "downed" ? playerById(call.playerId)?.position ?? call.position : call.npc?.position ?? call.position;
        if (distance(target, player.position) <= TREAT_REACH) return call;
    }
    return null;
}

function nearestCall(position) {
    return [...calls.values()].filter((call) => call.state === "waiting")
        .map((call) => ({call, gap: flatDistance(call.position, position)})).sort((a, b) => a.gap - b.gap)[0] ?? null;
}

function tick() {
    const now = Date.now();
    if (!inFreeRide() || !World.isReady()) return;
    if (medics.size && now >= nextCallAt && hospital()) {
        nextCallAt = now + CALL_EVERY_MS;
        const anchor = pick([...medics.keys()].map(playerById).filter(Boolean));
        streetCall(anchor?.position ?? null);
    }
    for (const call of [...calls.values()]) {
        if (call.generation !== World.getMissionGeneration()) {
            calls.delete(call.id);
            continue;
        }
        if (call.state === "waiting" && call.npc && call.ready && !call.lying) {
            call.lying = true;
            call.npc.playAnimation?.(call.clip ?? (call.kind === "minor" ? CROUCHED : LYING), {loop: true});
        }
        if (call.kind === "downed") {
            const victim = playerById(call.playerId);
            if (!victim || victim.alive || now - call.openedAt > DOWN_MS) {
                if (victim && !victim.alive) toast(victim, "No medic made it in time.", {title: "Ambulance"});
                closeCall(call);
            }
            continue;
        }
        if (call.state === "boarding" && now - call.boardSince > BOARD_TIMEOUT_MS) stretcher(call);
        if (call.state === "waiting" && now - call.openedAt > CALL_LIFETIME_MS) closeCall(call);
    }
    for (const [id, medic] of medics) {
        const player = playerById(id);
        if (!player) {
            medics.delete(id);
            continue;
        }
        // Another module took the player off the job: an arrest and the jail.
        if (life.activity(player)?.kind !== "medic") {
            endDuty(player, "Your shift is over.");
            continue;
        }
        if (!playable(player)) continue;
        const call = calls.get(medic.patient);
        const home = hospital();
        const ambulance = ambulanceOf(medic);
        if (call && call.state === "pickup") {
            compass(player, "medic", 43, {position: call.npc?.position ?? call.position});
            board(player, medic, call);
        } else if (call && (call.state === "aboard" || call.state === "boarding")) {
            compass(player, "medic", 43, {position: home.bay});
            if (!ambulance) {
                toast(player, "The ambulance is gone, and the patient with it.", {tone: "bad"});
                medic.patient = null;
                closeCall(call);
            } else if (call.state === "aboard" && player.getVehicle()?.id === ambulance.id && flatDistance(ambulance.position, home.bay) <= BAY_RADIUS &&
                       speedOf(ambulance) < BAY_SPEED) {
                deliver(player, medic, call);
            }
        } else {
            const nearest = nearestCall(player.position);
            compass(player, "medic", 43, nearest ? {position: nearest.call.position} : null);
        }
        // Treating takes a few seconds of G beside the patient.
        if (medic.treating) {
            const treating = calls.get(medic.treating.callId);
            const reach = treating && callNear(player)?.id === treating.id;
            if (!treating || !reach || !jobkey.isHeld(player) || player.getVehicle()) {
                medic.treating = null;
                jobkey.prompt(player, null);
                medic.hint = null;
            } else {
                const needed = treating.kind === "downed" ? REVIVE_MS : TREAT_MS;
                const progress = Math.min(1, (now - medic.treating.startedAt) / needed);
                jobkey.prompt(player, treating.kind === "downed" ? "REVIVING…" : "TREATING…", progress);
                medic.hint = "busy";
                if (progress >= 1) {
                    medic.treating = null;
                    jobkey.prompt(player, null);
                    medic.hint = null;
                    finishTreatment(player, medic, treating);
                }
            }
            continue;
        }
        const near = !player.getVehicle() && callNear(player);
        const hint = near ? (near.kind === "downed" ? "[G] HOLD TO REVIVE" : "[G] HOLD TO TREAT") : null;
        if (hint !== medic.hint) {
            medic.hint = hint;
            jobkey.prompt(player, hint);
        }
    }
    if (now - lastCardAt >= CARD_MS) {
        lastCardAt = now;
        presentCalls();
        for (const [id, medic] of medics) {
            const player = playerById(id);
            if (!player) continue;
            const call = calls.get(medic.patient);
            const nearest = nearestCall(player.position);
            const rows = [];
            if (call) {
                rows.push(["Patient", call.state === "pickup" ? "waiting for the ambulance" : call.state === "boarding" ? "getting in" : "aboard"]);
                if (call.deadline) rows.push(["Tip window", call.deadline > now ? formatMinutes(call.deadline - now) : "missed"]);
                rows.push(["Hospital", `${Math.round(flatDistance(player.position, hospital().bay))} m`]);
            } else if (nearest) {
                rows.push([nearest.call.kind === "downed" ? "Man down" : nearest.call.kind === "serious" ? "Badly hurt" : "Collapsed", `${Math.round(nearest.gap)} m`]);
            }
            rows.push(["Open calls", `${[...calls.values()].filter((entry) => entry.state === "waiting").length}`]);
            rows.push(["This shift", `${medic.patients} · ${formatMoney(medic.earned)}`]);
            card(player, "medic", {title: "Ambulance", subtitle: call ? "Get the patient to the hospital" : nearest ? `A call ${nearest.call.where}` : "No calls. Wait by the hospital.",
                rows, tone: "good"});
        }
    }
}

jobkey.register({
    press(player) {
        // A downed player gives up waiting.
        if (!player.alive) {
            const call = [...calls.values()].find((entry) => entry.kind === "downed" && entry.playerId === player.id);
            if (!call) return false;
            closeCall(call);
            toast(player, "You stop waiting for a medic.", {title: "Ambulance"});
            return true;
        }
        const medic = medics.get(player.id);
        if (!medic || !playable(player) || player.getVehicle()) return false;
        const call = callNear(player);
        if (!call) return false;
        medic.treating = {callId: call.id, startedAt: Date.now()};
        return true;
    },
    release(player) {
        const medic = medics.get(player.id);
        if (medic) medic.treating = null;
    },
});

Events.on("npcSpawn", (npc) => {
    for (const call of calls.values()) if (call.npc && call.npc.id === npc.id) call.ready = true;
});

Events.on("npcTaskComplete", (npc, info) => {
    const arrival = arrivals.get(info.id);
    if (arrival && arrival.npc.id === npc.id) {
        arrivals.delete(info.id);
        npc.walkTo(arrival.door);
        return;
    }
    for (const call of calls.values()) {
        if (!call.npc || call.npc.id !== npc.id || call.state !== "boarding" || info.id !== call.task) continue;
        if (info.success) call.state = "aboard";
        else stretcher(call);
    }
});

Events.on("npcDeath", (npc) => {
    for (const call of [...calls.values()]) {
        if (!call.npc || call.npc.id !== npc.id) continue;
        const medic = medics.get(call.medicId);
        if (medic) medic.patient = null;
        feed("medic", [text("Ambulance", COLORS.muted), text(` · a patient ${call.where} did not make it`)]);
        closeCall(call, false);
    }
});

// A patient the host could not make or lost: a street call is off, one
// already in the medic's hands rides in on the stretcher.
Events.on("npcRetire", (npc) => {
    for (const call of [...calls.values()]) {
        if (!call.npc || call.npc.id !== npc.id) continue;
        call.npc = null;
        if (call.state === "waiting") closeCall(call, false);
        else call.state = "aboard";
    }
    for (const stray of strays) if (stray.id === npc.id) strays.delete(stray);
});

// A player who goes down while a medic is on duty waits for one.
Events.on("playerDeath", (player, killer, info) => {
    // Not in a duel, race or other contest; a job is fine. Not after /kill,
    // which asks for the respawn, and not for the only medic.
    const busy = life.activity(player);
    if (!hospital() || !inFreeRide() || player.virtualWorld !== MAIN_WORLD || (busy && !JOBS.has(busy.kind)) || info?.cause === "script") return;
    if (![...medics.keys()].some((id) => id !== player.id)) return;
    // Rooms built apart under the fields (the back-room bar, the jail): the
    // interior has already put the player back in the street world, but no
    // ambulance reaches them there.
    if (barRoom.inRoom(player.position, 1) || prison.inHall(player.position, 2)) return;
    if ([...calls.values()].some((call) => call.kind === "downed" && call.playerId === player.id)) return;
    life.holdRespawn(player, Date.now() + DOWN_MS);
    openCall("downed", player.position, player.nickname, null, player.id, killer instanceof Player ? killer.id : null);
    toast(player, "A medic has been called. Hold on for a minute, or press G to wake up at the hospital.", {title: "Ambulance", tone: "gold", seconds: 8});
});

// Smoke victims the fire brigade led out (firefighters.js).
firefighters.setAmbulance({
    takePatient(npc, position, label) {
        if (!medics.size || !hospital() || calls.size >= MAX_CALLS + 2) return false;
        npc.stop?.();
        const call = openCall("serious", npc.position ?? position, label, npc);
        // Coughing on the kerb, not lying in the road.
        call.clip = CROUCHED;
        call.ready = true;
        return true;
    },
});

commands.register({name: "medic", aliases: ["ambulance", "paramedic"], usage: "/medic", help: "Go on or off ambulance duty.", category: "City"}, (player) => {
    if (medics.has(player.id)) endDuty(player, "Off duty. Ambulance back at the hospital.");
    else startDuty(player);
});

menu.provide("medic", (player) => ({onDuty: medics.has(player.id), medics: medics.size, available: Boolean(hospital()),
    calls: [...calls.values()].filter((call) => call.state === "waiting").length, earned: medics.get(player.id)?.earned ?? 0}));

function hospitalMarker(player) {
    const home = hospital();
    const items = home ? [{id: "medic", position: home.duty, label: "AMBULANCE · /medic", color: COLORS.green, range: 120}] : [];
    if (player) markers(player, "medicstation", items);
    else markersAll("medicstation", items);
}

onStart(() => {
    nextCallAt = Date.now() + FIRST_CALL_MS;
    timer = setInterval(tick, TICK_MS);
    hospitalMarker(null);
});
onStop(() => {
    clearInterval(timer);
    for (const id of [...medics.keys()]) {
        const player = playerById(id);
        if (player) endDuty(player, null);
    }
    for (const call of [...calls.values()]) closeCall(call);
    for (const npc of strays) npc.remove();
    strays.clear();
    arrivals.clear();
});
Events.on("playerSpawn", (player) => {
    hospitalMarker(player);
    // The calls open before this player came: markersAll sends only changes.
    if (lastItems.length) markers(player, "medic", lastItems);
});
Events.on("playerDisconnect", (player) => {
    for (const call of [...calls.values()]) if (call.kind === "downed" && call.playerId === player.id) calls.delete(call.id);
    const medic = medics.get(player.id);
    if (!medic) return;
    medics.delete(player.id);
    const call = calls.get(medic.patient);
    if (call) closeCall(call);
    for (const open of [...calls.values()]) {
        if (open.kind === "downed" && ![...medics.keys()].some((id) => id !== open.playerId)) closeCall(open);
    }
    const ambulance = ambulanceOf(medic);
    if (ambulance && ambulance.getOccupants().every((occupant) => occupant === null)) ambulance.destroy();
});
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) endDuty(player, "Your shift ended with the city reloading.");
    calls.clear();
    strays.clear();
    arrivals.clear();
    lastMarkers = "";
    lastItems = [];
    nextCallAt = Date.now() + FIRST_CALL_MS;
    hospitalMarker(null);
});

module.exports = {onDuty: (player) => medics.has(player.id), calls: () => [...calls.values()], streetCall,
    // The next street call comes in ms (dev tools, tests).
    schedule(ms) { nextCallAt = Date.now() + ms; }};
